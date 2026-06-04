/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceSystem.h
 * @brief   PreTick local unit-unit avoidance — the SOFT steering layer that sits
 *          ABOVE the hard penetration floor (FSeinCollisionResolutionSystem).
 *
 *          Per MOVING unit, reads neighbours from the collision spatial hash (a
 *          start-of-tick snapshot, since this runs BEFORE movement) and accumulates
 *          a purely-LATERAL steering nudge that bends the unit around others before
 *          they collide. The nudge is written to FSeinMovementComponent::AvoidanceSteer;
 *          the movement Tick consumes it via USeinMovement::ApplyAvoidanceSteer. The
 *          floor still guarantees no overlap — this just makes crowds FLOW (around
 *          moving AND standing units) instead of grind into the floor.
 *
 *          DETERMINISM — the whole reason this is a PreTick precompute, not inline
 *          in movement: movement runs through USeinLatentActionManager in INSERTION
 *          ORDER, reading neighbour transforms LIVE, so querying neighbours during
 *          movement would be order-dependent. Here, every unit reads the SAME
 *          start-of-tick snapshot (broadphase rebuilt at PreTick pri 5) and writes
 *          ONLY its own AvoidanceSteer. Neighbours arrive handle-sorted from
 *          QueryRadius; fixed-point add is exact, so the result is independent of
 *          iteration order. All math is fixed-point. Lockstep-safe.
 *
 *          BODY SIZE is the movement/nav FOOTPRINT (USeinMovement::ResolveCollisionRadius:
 *          Extents → NavComp.FallbackFootprintRadius → 0) — the same source pathing
 *          and nav-collision use, NOT the collision extents directly. Avoidance is a
 *          movement concern; it agrees with PATHING, honoring nav≠collision.
 *
 *          Model distilled from SpringRTS/Recoil's lateral-steer avoidance — turn-sign
 *          nudge, head-on weighting, distance falloff, temporal smoothing — plus a
 *          deterministic handle tie-break for the dead-ahead symmetry case. NOT ORCA.
 *
 *          Phase 1: soft steering only. Deferred (Phase 2+): closing-velocity
 *          weighting, frame-spread, side-lock hysteresis. NEVER here: mover-pushes-
 *          through-idle displacement (that needs a mass-respecting floor change).
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Movement/SeinMovement.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

/**
 * System: Avoidance (local unit-unit steering)
 * Phase: PreTick | Priority: 6  (after the CollisionBroadphase rebuild at pri 5, so
 *        neighbour reads are a consistent start-of-tick snapshot; before movement
 *        runs in AbilityExecution, which consumes the steer this same tick).
 */
class FSeinAvoidanceSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

		// --- Tunables (fixed-point constants → bit-identical cross-platform).
		//     First-guess values; the real tuning gate is a PIE feel test. ---
		const FFixedPoint LookaheadSeconds    = FFixedPoint::One / FFixedPoint::FromInt(2);   // 0.5s perception reach
		const FFixedPoint MovingSpeedFloor    = FFixedPoint::FromInt(10);                     // "is moving" threshold (uu/s)
		const FFixedPoint FalloffRadii        = FFixedPoint::FromInt(5);                      // influence out to 5× combined footprint
		const FFixedPoint SmoothKeep          = FFixedPoint::FromInt(7) / FFixedPoint::FromInt(10);  // 0.7 temporal keep
		const FFixedPoint HeadOnBase          = FFixedPoint::One / FFixedPoint::FromInt(10);  // 0.1 floor on the head-on weight
		const FFixedPoint ArrivalReleaseRadii = FFixedPoint::FromInt(3);                      // fade steer within 3× footprint of goal
		const FFixedPoint MaxSteerMagnitude   = FFixedPoint::FromInt(2);                      // clamp on the accumulated lateral nudge

		TArray<FSeinEntityHandle> Neighbors;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle SelfHandle, FSeinEntity& SelfEntity)
		{
			FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(SelfHandle);
			if (!Move) return;
			// Opted out → leave AvoidanceSteer untouched (stays its default zero), so the
			// unit's motion is bit-identical to a world with no avoidance.
			if (Move->AvoidanceStrength <= FFixedPoint::Zero) return;
			if (!Move->bHasTarget) return; // only steer while under a move order

			// Heading from end-of-last-tick velocity (the same snapshot value for every
			// unit at PreTick). Stopped/too-slow → clear and bail.
			const FFixedVector Vel = Move->Velocity;
			const FFixedPoint Speed = Vel.Size();
			if (Speed <= MovingSpeedFloor) { Move->AvoidanceSteer = FFixedVector::ZeroVector; return; }
			const FFixedVector Heading(Vel.X / Speed, Vel.Y / Speed, FFixedPoint::Zero);
			const FFixedVector Right(Heading.Y, -Heading.X, FFixedPoint::Zero); // planar right of heading

			// Body radius from the movement/nav FOOTPRINT cascade — NOT collision extents.
			const FSeinNavigationComponent* SelfNav = World.GetComponent<FSeinNavigationComponent>(SelfHandle);
			const FFixedPoint SelfRadius = USeinMovement::ResolveCollisionRadius(&World, SelfHandle, SelfNav);
			if (SelfRadius <= FFixedPoint::Zero) { Move->AvoidanceSteer = FFixedVector::ZeroVector; return; }
			const FFixedPoint SelfMass = SelfRadius * SelfRadius;

			const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();

			// Planar distance to goal — drives the past-goal gate AND the arrival fade.
			FFixedVector ToGoal = Move->TargetLocation - SelfPos;
			ToGoal.Z = FFixedPoint::Zero;
			const FFixedPoint GoalDistSq = ToGoal.SizeSquared();

			// ARRIVAL-RELEASE FADE: stop steering as the unit closes on its goal so
			// path-attraction + the floor own the endgame. Without this, a destination
			// inside/behind a standing cluster makes the unit orbit the perimeter forever.
			const FFixedPoint ReleaseRadius = SelfRadius * ArrivalReleaseRadii;
			if (GoalDistSq <= ReleaseRadius * ReleaseRadius) { Move->AvoidanceSteer = FFixedVector::ZeroVector; return; }

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
				FFixedVector ToOther = OtherEntity->Transform.GetLocation() - SelfPos;
				ToOther.Z = FFixedPoint::Zero;
				const FFixedPoint DistSq = ToOther.SizeSquared();
				if (DistSq <= FFixedPoint::Epsilon) continue;

				// Forward gate: ignore neighbours behind the heading.
				const FFixedPoint Ahead = ToOther.X * Heading.X + ToOther.Y * Heading.Y;
				if (Ahead <= FFixedPoint::Zero) continue;

				// Past-goal gate: ignore neighbours farther than the goal.
				if (GoalDistSq > FFixedPoint::Zero && DistSq >= GoalDistSq) continue;

				// Other body radius from the SAME footprint cascade.
				const FSeinNavigationComponent* OtherNav = World.GetComponent<FSeinNavigationComponent>(OtherHandle);
				const FFixedPoint OtherRadius = USeinMovement::ResolveCollisionRadius(&World, OtherHandle, OtherNav);
				if (OtherRadius <= FFixedPoint::Zero) continue;

				const FFixedPoint Dist = SeinMath::Sqrt(DistSq);
				const FFixedPoint FalloffRange = (SelfRadius + OtherRadius) * FalloffRadii;
				if (Dist >= FalloffRange) continue;
				const FFixedPoint Falloff = FFixedPoint::One - (Dist / FalloffRange);

				// Head-on weight: a neighbour moving AGAINST us weighs strong, one moving
				// WITH us weak; a stationary (or slow) neighbour gets the moderate base.
				FFixedPoint HeadOn = FFixedPoint::One + HeadOnBase;
				if (const FSeinMovementComponent* OtherMove = World.GetComponent<FSeinMovementComponent>(OtherHandle))
				{
					const FFixedVector OtherVel = OtherMove->Velocity;
					const FFixedPoint OtherSpeed = OtherVel.Size();
					if (OtherSpeed > MovingSpeedFloor)
					{
						// cos(angle) = Heading · OtherVel / |OtherVel|. Same dir → 1 (weak),
						// head-on → -1 (strong).
						const FFixedPoint CosA = (Heading.X * OtherVel.X + Heading.Y * OtherVel.Y) / OtherSpeed;
						HeadOn = (FFixedPoint::One - CosA) + HeadOnBase;
					}
				}

				const FFixedPoint OtherMass = OtherRadius * OtherRadius;
				const FFixedPoint MassSum = SelfMass + OtherMass;
				const FFixedPoint MassScale = (MassSum > FFixedPoint::Epsilon) ? (OtherMass / MassSum) : FFixedPoint::Half;

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

				const FFixedPoint W = HeadOn * Falloff * MassScale * TurnSign;
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
				Scaled.X * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceSteer.X * SmoothKeep,
				Scaled.Y * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceSteer.Y * SmoothKeep,
				FFixedPoint::Zero);
			if (Smoothed.SizeSquared() <= FFixedPoint::Epsilon) Smoothed = FFixedVector::ZeroVector;
			Move->AvoidanceSteer = Smoothed;
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 6; }
	virtual FName GetSystemName() const override { return TEXT("Avoidance"); }
};
