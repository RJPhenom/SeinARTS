/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.cpp
 * @brief   The shipped lateral-steer boids avoidance model. See header + the file
 *          docstring on USeinAvoidance for the contract. This body is the SpringRTS/
 *          BAR-distilled model lifted verbatim from the former inline
 *          FSeinAvoidanceSystem::Tick (now a thin delegator) — behaviour is
 *          byte-identical; only its home moved (inline ISeinSystem → pluggable
 *          USeinAvoidance subclass) and the write target changed from the flat
 *          `AvoidanceSteer` field to `AvoidanceOutput.SteerDir`.
 */

#include "Movement/SeinAvoidanceDefault.h"

#include "Core/SeinParallel.h"
#include "Math/MathLib.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Movement/SeinMovement.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

void USeinAvoidanceDefault::ComputeAvoidance(USeinWorldSubsystem& World)
{
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

	// --- Tunables: model-shape constants shared by ALL movers, authored in plugin
	//     settings (Movement|Avoidance). Per-unit dials (strength/weight) live on
	//     FSeinMovementComponent. Defaults equal the former inline values, so motion
	//     is unchanged until tuned. Read once per tick (CDO fetch is cheap). ---
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint LookaheadSeconds    = Settings->AvoidanceLookaheadSeconds;
	const FFixedPoint MovingSpeedFloor    = Settings->AvoidanceMovingSpeedFloor;
	const FFixedPoint FalloffRadii        = Settings->AvoidanceFalloffRadii;
	const FFixedPoint SmoothKeep          = Settings->AvoidanceSmoothKeep;
	const FFixedPoint HeadOnBase          = Settings->AvoidanceHeadOnBase;
	const FFixedPoint ArrivalReleaseRadii = Settings->AvoidanceArrivalReleaseRadii;
	const FFixedPoint MaxSteerMagnitude   = Settings->AvoidanceMaxSteerMagnitude;

	// Hoist component-storage lookups out of the per-entity / per-neighbour
	// loop: GetComponent<T>() is a hashmap lookup by UScriptStruct* per call;
	// resolving each storage once turns every access into an O(1) indexed get.
	ISeinComponentStorage* MoveStorage    = World.GetComponentStorageRaw(FSeinMovementComponent::StaticStruct());
	ISeinComponentStorage* NavStorage     = World.GetComponentStorageRaw(FSeinNavigationComponent::StaticStruct());
	ISeinComponentStorage* ExtentsStorage = World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
	// Group identity for cohesion: a unit's CurrentBrokerHandle (stamped when it
	// joins a command broker — a selection-order group or a squad). Members of
	// the SAME broker do NOT avoid each other, so a group converges and packs
	// instead of steering around itself; the hard collision floor still keeps
	// them from overlapping. Reusing the broker as the group means no separate
	// group-ID concept — it's the canonical "who was ordered together."
	ISeinComponentStorage* BrokerStorage  = World.GetComponentStorageRaw(FSeinBrokerMembershipData::StaticStruct());

	// Gather live handles into an indexable array (serial, cheap), then fan the
	// per-unit avoidance computation across worker threads. Each body reads the
	// immutable start-of-tick snapshot (broadphase + neighbour transforms /
	// velocities, all frozen at PreTick) and writes ONLY its own AvoidanceOutput —
	// the determinism contract this file's header already guarantees IS the
	// SeinParallelFor body contract (immutable reads + disjoint per-self writes).
	// `Sein.Sim.Parallel 0` forces this serial; the result is bit-identical.
	TArray<FSeinEntityHandle> LiveHandles;
	LiveHandles.Reserve(World.GetEntityPool().GetActiveCount());
	World.GetEntityPool().ForEachEntity([&LiveHandles](FSeinEntityHandle Handle, FSeinEntity&) { LiveHandles.Add(Handle); });

	SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
	{
		const FSeinEntityHandle SelfHandle = LiveHandles[Index];
		FSeinEntity* SelfEntityPtr = World.GetEntityPool().Get(SelfHandle);
		if (!SelfEntityPtr) return;
		FSeinEntity& SelfEntity = *SelfEntityPtr;

		// Per-body neighbour scratch — MUST be a local (one buffer per body
		// invocation) so concurrent QueryRadius calls never share it.
		TArray<FSeinEntityHandle> Neighbors;

		FSeinMovementComponent* Move = MoveStorage ? static_cast<FSeinMovementComponent*>(MoveStorage->GetComponentRaw(SelfHandle)) : nullptr;
		if (!Move) return;
		// Opted out → leave AvoidanceOutput untouched (stays its default zero steer), so the
		// unit's motion is bit-identical to a world with no avoidance.
		if (Move->AvoidanceStrength <= FFixedPoint::Zero) return;
		// No active move order → clear any lingering steer (so it can't go stale at rest, in the
		// state hash or the debug viz) and bail. Avoidance only runs while a unit is moving.
		if (!Move->bHasTarget) { Move->AvoidanceOutput.SteerDir = FFixedVector::ZeroVector; return; }

		// Heading from end-of-last-tick velocity (the same snapshot value for every
		// unit at PreTick). Stopped/too-slow → clear and bail.
		const FFixedVector Vel = Move->Velocity;
		const FFixedPoint Speed = Vel.Size();
		if (Speed <= MovingSpeedFloor) { Move->AvoidanceOutput.SteerDir = FFixedVector::ZeroVector; return; }
		const FFixedVector Heading(Vel.X / Speed, Vel.Y / Speed, FFixedPoint::Zero);
		const FFixedVector Right(Heading.Y, -Heading.X, FFixedPoint::Zero); // planar right of heading

		// Body radius from the movement/nav FOOTPRINT cascade — NOT collision extents.
		// Footprint pointers come from the hoisted storage; passed straight to the
		// no-lookup ResolveCollisionRadius overload (skips a per-self GetComponent).
		const FSeinNavigationComponent* SelfNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinExtentsComponent* SelfExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FFixedPoint SelfRadius = USeinMovement::ResolveCollisionRadius(SelfExt, SelfNav);
		if (SelfRadius <= FFixedPoint::Zero) { Move->AvoidanceOutput.SteerDir = FFixedVector::ZeroVector; return; }

		// Self's group (broker) handle — used to skip same-group neighbours below.
		// Invalid when the unit isn't in any broker (a lone, never-grouped unit).
		const FSeinBrokerMembershipData* SelfBroker = BrokerStorage
			? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinEntityHandle SelfBrokerHandle = SelfBroker ? SelfBroker->CurrentBrokerHandle : FSeinEntityHandle();
		// Self's per-order cohesion group (0 = none) — the cross-track group id stamped
		// at order time so co-selected units in DIFFERENT brokers (separate squads, or
		// squad-vs-loose) still cohere. See FSeinBrokerMembershipData::CohesionGroupId.
		const int64 SelfCohesionId = SelfBroker ? SelfBroker->CohesionGroupId : 0;

		const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();

		// Planar distance to goal — drives the past-goal gate AND the arrival fade.
		FFixedVector ToGoal = Move->TargetLocation - SelfPos;
		ToGoal.Z = FFixedPoint::Zero;
		const FFixedPoint GoalDistSq = ToGoal.SizeSquared();

		// ARRIVAL-RELEASE FADE: stop steering as the unit closes on its goal so
		// path-attraction + the floor own the endgame. Without this, a destination
		// inside/behind a standing cluster makes the unit orbit the perimeter forever.
		const FFixedPoint ReleaseRadius = SelfRadius * ArrivalReleaseRadii;
		if (GoalDistSq <= ReleaseRadius * ReleaseRadius) { Move->AvoidanceOutput.SteerDir = FFixedVector::ZeroVector; return; }

		// Speed-scaled perception radius (footprint-based; tuned independently of body
		// size, so "personal-space" reach needs no separate authored radius).
		const FFixedPoint Perception = SelfRadius * FFixedPoint::FromInt(2) + Speed * LookaheadSeconds;

		Neighbors.Reset();
		Hash.QueryRadius(SelfPos, Perception, Neighbors, SelfHandle);

		FFixedVector Accum = FFixedVector::ZeroVector;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;

			// UNIT-TO-UNIT ONLY. Avoidance is a cohesion tool, never a pathing tool: only other
			// MOVABLE units (entities with a movement component) contribute a steering force. A
			// neighbour with no movement component is static geometry — a wall, a building, a prop
			// — and nav blocking already routes units clear of those, so it is skipped entirely,
			// never avoided. (The spatial hash this queries holds ALL colliders, walls included,
			// which is why the filter must live here.) Fetched once and reused for head-on below.
			const FSeinMovementComponent* OtherMove = MoveStorage ? static_cast<const FSeinMovementComponent*>(MoveStorage->GetComponentRaw(OtherHandle)) : nullptr;
			if (!OtherMove) continue;

			// Neighbour's group (broker handle) — drives BOTH cohesion (skip a
			// same-group neighbour) AND group-vs-group passing (Phase D, below).
			const FSeinBrokerMembershipData* OtherBroker = BrokerStorage
				? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FSeinEntityHandle OtherBrokerHandle = OtherBroker ? OtherBroker->CurrentBrokerHandle : FSeinEntityHandle();

			// GROUP COHESION: a neighbour ordered together with us is NOT avoided —
			// the group converges and packs without steering around itself; the
			// collision floor keeps them non-overlapping. This is what kills the
			// in-group fan-out the closing-velocity gate alone can't (units
			// converging on one point ARE closing on each other, so they'd otherwise
			// dodge their own squad). "Ordered together" is EITHER the same immediate
			// broker OR the same per-order cohesion group — the latter so co-selected
			// units that dispatch into DIFFERENT brokers (separate squads, or
			// squad-vs-loose) also cohere, since broker membership alone is single-
			// level. Cross-group + ungrouped neighbours fall through.
			const bool bSameBroker = SelfBrokerHandle.IsValid() && OtherBrokerHandle == SelfBrokerHandle;
			const int64 OtherCohesionId = OtherBroker ? OtherBroker->CohesionGroupId : 0;
			const bool bSameCohesion = SelfCohesionId != 0 && SelfCohesionId == OtherCohesionId;
			if (bSameBroker || bSameCohesion) continue;

			// BULLDOZE IDLE NEIGHBOURS (the local-avoidance rule BAR uses; see the
			// orbit research). A STATIONARY neighbour gets NO steering dodge — you can't
			// orbit something that isn't moving, and steering around a static cluster is
			// exactly what curves a transiting unit into the "black hole" orbit. Idle
			// units are left to the collision floor (pushed aside, like a DENSE BLOB —
			// the case that already works), so the unit pushes straight through instead
			// of circling. Only MOVING neighbours get a dodge (do-si-do / Phase D), which
			// is correct for transient group-vs-group passing (the arc that works).
			// Squared compare avoids a per-neighbour sqrt; same MovingSpeedFloor the
			// self-speed gate above uses.
			if (OtherMove->Velocity.SizeSquared() <= MovingSpeedFloor * MovingSpeedFloor) continue;

			// WEIGHT-PRIORITY GATE. This unit only yields to a neighbour whose AvoidanceWeight
			// qualifies: equal-or-higher when bAvoidSameWeights, strictly higher otherwise. So a
			// heavier unit never dodges a lighter one (the lighter one dodges it), and with the
			// bool off, equal-weight peers stop avoiding each other and fall through to the
			// penetration floor — killing same-class mutual-avoidance orbits. Integer compare on
			// authored config → deterministic.
			const bool bQualifies = Move->bAvoidSameWeights
				? (OtherMove->AvoidanceWeight >= Move->AvoidanceWeight)
				: (OtherMove->AvoidanceWeight >  Move->AvoidanceWeight);
			if (!bQualifies) continue;

			FFixedVector ToOther = OtherEntity->Transform.GetLocation() - SelfPos;
			ToOther.Z = FFixedPoint::Zero;
			const FFixedPoint DistSq = ToOther.SizeSquared();
			if (DistSq <= FFixedPoint::Epsilon) continue;

			// Forward gate: ignore neighbours behind the heading.
			const FFixedPoint Ahead = ToOther.X * Heading.X + ToOther.Y * Heading.Y;
			if (Ahead <= FFixedPoint::Zero) continue;

			// CLOSING-VELOCITY GATE — group cohesion (the refinement this file's docstring names).
			// Only avoid a neighbour we are actually CLOSING distance with. Units moving in PARALLEL —
			// a cohesive group walking together, or a column flowing along a wall — have a ~zero
			// closing rate, so they SKIP here and stop shoving each other out of the cluster. Genuine
			// collision courses (crossing, head-on, an overtake) ARE closing and are still avoided.
			// ClosingDot = ToOther · (SelfVel − OtherVel) > 0 means the gap is shrinking this tick.
			// Snapshot velocities → deterministic, one-sided. (A stationary unit ahead still has a
			// positive closing dot because we move toward it, so units in your path are still avoided.)
			const FFixedVector RelVel(
				Vel.X - OtherMove->Velocity.X,
				Vel.Y - OtherMove->Velocity.Y,
				FFixedPoint::Zero);
			const FFixedPoint ClosingDot = ToOther.X * RelVel.X + ToOther.Y * RelVel.Y;
			if (ClosingDot <= FFixedPoint::Zero) continue;

			// Past-goal gate: ignore neighbours farther than the goal.
			if (GoalDistSq > FFixedPoint::Zero && DistSq >= GoalDistSq) continue;

			// Other body radius from the SAME footprint cascade (hoisted-storage pointers).
			const FSeinNavigationComponent* OtherNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FSeinExtentsComponent* OtherExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FFixedPoint OtherRadius = USeinMovement::ResolveCollisionRadius(OtherExt, OtherNav);
			if (OtherRadius <= FFixedPoint::Zero) continue;

			const FFixedPoint Dist = SeinMath::Sqrt(DistSq);
			const FFixedPoint FalloffRange = (SelfRadius + OtherRadius) * FalloffRadii;
			if (Dist >= FalloffRange) continue;
			const FFixedPoint Falloff = FFixedPoint::One - (Dist / FalloffRange);

			// Head-on weight: a neighbour moving AGAINST us weighs strong, one moving
			// WITH us weak; a stationary (or slow) neighbour gets the moderate base.
			// (OtherMove is guaranteed non-null — non-unit neighbours were skipped above.)
			FFixedPoint HeadOn = FFixedPoint::One + HeadOnBase;
			const FFixedVector OtherVel = OtherMove->Velocity;
			const FFixedPoint OtherSpeed = OtherVel.Size();
			if (OtherSpeed > MovingSpeedFloor)
			{
				// cos(angle) = Heading · OtherVel / |OtherVel|. Same dir → 1 (weak),
				// head-on → -1 (strong).
				const FFixedPoint CosA = (Heading.X * OtherVel.X + Heading.Y * OtherVel.Y) / OtherSpeed;
				HeadOn = (FFixedPoint::One - CosA) + HeadOnBase;
			}

			// Dodge AWAY from the neighbour's side. SideDot = how far the neighbour sits
			// to our right (world units). Inside a "dead-ahead" band the side is
			// undefined → break it DETERMINISTICALLY by handle, so two head-on units
			// pick OPPOSITE sides instead of marching into each other (do-si-do).
			const FFixedPoint SideDot = ToOther.X * Right.X + ToOther.Y * Right.Y;
			const FFixedPoint LateralBand = SelfRadius / FFixedPoint::FromInt(4);
			FFixedPoint TurnSign;
			if (SideDot > LateralBand)        { TurnSign = -FFixedPoint::One; } // neighbour on right → steer left
			else if (SideDot < -LateralBand)  { TurnSign =  FFixedPoint::One; } // neighbour on left  → steer right
			else { TurnSign = (SelfHandle.Index < OtherHandle.Index) ? FFixedPoint::One : -FFixedPoint::One; }

			// PHASE D — group-vs-group passing. When BOTH units belong to (different)
			// groups, OVERRIDE the per-unit do-si-do with a uniform "shift to my own
			// right" (+1 = +Right = heading-right). Every member of a group then steps
			// the SAME way, so two opposing blobs slide past each other like sidewalk
			// traffic instead of each unit independently picking a side and splaying
			// the group. Both groups shift to their respective rights, which (for a
			// head-on pass) are opposite world directions — so they clear each other.
			// Lone (ungrouped) neighbours keep the do-si-do above.
			// (Corridor-awareness — reduce the shift when the chosen side is
			// nav-blocked — is a deferred refinement; the hard-barrier push keeps
			// units from crossing the wall in the meantime.)
			if (SelfBrokerHandle.IsValid() && OtherBrokerHandle.IsValid())
			{
				TurnSign = FFixedPoint::One;
			}

			// Steer weight is PURE STEERING — NO mass term. HeadOn (encounter angle) × Falloff
			// (proximity, whose range ∝ combined footprint, so it is size-proportional) × TurnSign.
			// With no radius-derived mass, equal-AvoidanceStrength units of ANY size bend by the
			// same angle and react from proportionally-farther → they avoid proportionately the
			// same relative to their footprint. AvoidanceStrength (applied below) is the magnitude
			// knob; AvoidanceWeight (gated above) is the priority. Do NOT reintroduce a mass factor
			// here — mass physics belong to the collision floor, not this steering layer.
			const FFixedPoint W = HeadOn * Falloff * TurnSign;
			Accum.X += Right.X * W;
			Accum.Y += Right.Y * W;
		}

		// Clamp the accumulated lateral nudge (bounds an idle-crowd repulsor sum +
		// any fixed-point blow-up) BEFORE strength-scale + smoothing. The nudge bends
		// a UNIT direction downstream, so the cap is in that same unit space.
		const FFixedPoint AccumLen = Accum.Size();
		if (AccumLen > MaxSteerMagnitude && AccumLen > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale = MaxSteerMagnitude / AccumLen;
			Accum.X = Accum.X * Scale;
			Accum.Y = Accum.Y * Scale;
		}

		// Strength-scale, then temporally smooth against the previous steer (damps the
		// perception-boundary snap as neighbours enter/leave). Snap negligible results
		// to exactly zero so a unit clear of traffic returns to a true no-op.
		const FFixedVector Scaled(
			Accum.X * Move->AvoidanceStrength,
			Accum.Y * Move->AvoidanceStrength,
			FFixedPoint::Zero);
		FFixedVector Smoothed(
			Scaled.X * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceOutput.SteerDir.X * SmoothKeep,
			Scaled.Y * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceOutput.SteerDir.Y * SmoothKeep,
			FFixedPoint::Zero);
		if (Smoothed.SizeSquared() <= FFixedPoint::Epsilon) Smoothed = FFixedVector::ZeroVector;
		Move->AvoidanceOutput.SteerDir = Smoothed;
	});
}
