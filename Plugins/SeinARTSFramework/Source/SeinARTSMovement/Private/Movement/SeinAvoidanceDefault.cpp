/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.cpp
 * @brief   The shipped local-avoidance model — a lateral-steer + brake-to-yield kernel.
 *
 *          REBUILD NOTE (2026-07-03). This is the deliberate rebuild of the model gutted 2026-07-02.
 *          The post-mortem exonerated the original architecture: its two goal-relative mechanisms
 *          (the arrival-release fade and the past-goal gate) read FSeinMovementComponent::
 *          TargetLocation, which was NEVER WRITTEN at the time — both computed distance to the
 *          world origin, so the anti-orbit release was silently dead, and the orbit/pileup symptoms
 *          it should have prevented were blamed on the model (compounded by the since-fixed follower
 *          waypoint-advance bug, static-only nav floor, and async pathfinding). USeinMoveToAction
 *          now publishes TargetLocation every move tick, so those mechanisms are live here for the
 *          first time. New over the original: the SpeedScale producer (yield-by-braking — the
 *          output channel existed but never had a writer) and the goal mechanics actually working.
 *
 *          Determinism contract (the SeinParallelFor body contract): read ONLY the immutable
 *          start-of-tick snapshot (broadphase + neighbour transforms/velocities, frozen at PreTick);
 *          write ONLY each unit's own AvoidanceOutput; all fixed-point. `Sein.Sim.Parallel 0`
 *          forces serial and must be bit-identical. The hard collision floor owns no-overlap —
 *          this layer only shapes how crowds FLOW.
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

// Diagnostic channel for the avoidance model. Verbose = the grind dump (see the pinned-unit
// early-out in ComputeAvoidance): enable in PIE with `log LogSeinAvoidance Verbose`, reproduce,
// and the log shows each commanded-but-pinned unit's neighbourhood with per-gate skip counts.
DEFINE_LOG_CATEGORY_STATIC(LogSeinAvoidance, Log, All);

void USeinAvoidanceDefault::ComputeAvoidance(USeinWorldSubsystem& World)
{
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

	// --- Tunables: model-shape constants shared by ALL movers, authored in plugin settings
	//     (Navigation|Avoidance) and part of the settings determinism fingerprint. Per-unit
	//     dials (strength/weight/same-weights) live on FSeinMovementComponent. ---
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint LookaheadSeconds    = Settings->AvoidanceLookaheadSeconds;
	const FFixedPoint MovingSpeedFloor    = Settings->AvoidanceMovingSpeedFloor;
	const FFixedPoint FalloffRadii        = Settings->AvoidanceFalloffRadii;
	const FFixedPoint SmoothKeep          = Settings->AvoidanceSmoothKeep;
	const FFixedPoint HeadOnBase          = Settings->AvoidanceHeadOnBase;
	const FFixedPoint ArrivalReleaseRadii = Settings->AvoidanceArrivalReleaseRadii;
	const FFixedPoint MaxSteerMagnitude   = Settings->AvoidanceMaxSteerMagnitude;
	const FFixedPoint BrakeStrength       = Settings->AvoidanceBrakeStrength;
	const FFixedPoint CohesionHoldBack    = Settings->AvoidanceCohesionHoldBack;
	const FFixedPoint CohesionBoost       = Settings->AvoidanceCohesionCatchUpBoost;
	const FFixedPoint CohesionRangeRadii  = Settings->AvoidanceCohesionRangeRadii;
	// Cohesion off entirely when both sides are neutral — the aggregate pre-pass is skipped
	// and every unit's CohesionScale is exactly One (bit-exact no-op).
	const bool bCohesionEnabled = CohesionHoldBack > FFixedPoint::Zero || CohesionBoost > FFixedPoint::One;
	// The moving-speed floor expressed as a PER-TICK displacement, for the honest-motion
	// tests below (PrevTickLocation deltas are per-tick, not per-second).
	const int32 TickRate = GetDefault<USeinARTSCoreSettings>()->SimulationTickRate > 0
		? GetDefault<USeinARTSCoreSettings>()->SimulationTickRate : 30;
	const FFixedPoint FloorPerTick   = MovingSpeedFloor / FFixedPoint::FromInt(TickRate);
	const FFixedPoint FloorPerTickSq = FloorPerTick * FloorPerTick;

	// Hoist component-storage lookups out of the per-entity / per-neighbour loop:
	// GetComponent<T>() is a hashmap lookup by UScriptStruct* per call; resolving each
	// storage once turns every access into an O(1) indexed get.
	ISeinComponentStorage* MoveStorage    = World.GetComponentStorageRaw(FSeinMovementComponent::StaticStruct());
	ISeinComponentStorage* NavStorage     = World.GetComponentStorageRaw(FSeinNavigationComponent::StaticStruct());
	ISeinComponentStorage* ExtentsStorage = World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
	// Group identity — TWO-LAYER, matching the formation model: the immediate broker
	// (squad / loose-order group) and the per-order cohesion id spanning brokers of one
	// multi-element order. Same-group neighbours are never avoided (the group converges
	// and packs; the collision floor keeps bodies apart).
	ISeinComponentStorage* BrokerStorage  = World.GetComponentStorageRaw(FSeinBrokerMembershipData::StaticStruct());

	// Gather live handles (serial, cheap), then fan the per-unit computation across
	// worker threads under the body contract in the file docstring. The SAME serial
	// walk builds the per-formation cohesion aggregate: for every actively-moving
	// broker member, its planar remaining distance to its own goal, summed per broker.
	// Serial pool-order accumulation → deterministic; the parallel pass below only
	// READS the finished map (immutable snapshot), preserving the body contract.
	// Broker-scoped = the INNER formation layer (a squad, or a loose-order group).
	// Cross-broker cohesion for a multi-squad order (the outer CohesionGroupId layer —
	// squads keeping pace with squads) is a deliberate follow-up, not implemented here.
	struct FCohesionAggregate { int32 Count = 0; FFixedPoint SumDist; };
	TMap<FSeinEntityHandle, FCohesionAggregate> GroupAggregates;
	TArray<FSeinEntityHandle> LiveHandles;
	LiveHandles.Reserve(World.GetEntityPool().GetActiveCount());
	// Per-entity honest-motion samples over the previous tick, aligned index-for-index with
	// LiveHandles. Built serially here, read-only in the parallel pass below (immutable
	// snapshot — contract-safe). Two channels answering two different questions:
	//   ActualDispSq   — planar displacement² ("did this body move at all"); negative =
	//                    unknown (no movement component, or no sample yet).
	//   ActualProgress — planar headway toward the unit's CURRENT goal, signed ("did it
	//                    gain ground") — a churned unit extruded sideways at speed displaces
	//                    plenty while gaining nothing; ProgressUnknown sentinel = no sample /
	//                    no goal. Goal changes mid-order measure old-position-vs-new-goal,
	//                    which is still the honest "did it gain ground on where it is going".
	const FFixedPoint ProgressUnknown = FFixedPoint::FromInt(-1000000);
	TArray<FFixedPoint> ActualDispSq;
	TArray<FFixedPoint> ActualProgress;
	ActualDispSq.Reserve(World.GetEntityPool().GetActiveCount());
	ActualProgress.Reserve(World.GetEntityPool().GetActiveCount());
	World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
	{
		LiveHandles.Add(Handle);
		ActualDispSq.Add(FFixedPoint::FromInt(-1));
		ActualProgress.Add(ProgressUnknown);
		FSeinMovementComponent* Move = MoveStorage
			? static_cast<FSeinMovementComponent*>(MoveStorage->GetComponentRaw(Handle)) : nullptr;
		if (!Move) return;

		// ACTUAL-MOTION SAMPLE — every movement-carrying entity, every tick, idle or
		// ordered. Velocity cannot answer "did this body actually move": it is the unit's own
		// commanded step (post nav-floor, PRE body-collision — the resolver never writes it
		// back), so a body-blocked presser reads ~full speed while standing still. The
		// start-of-tick location against last PreTick's sample is the true world displacement
		// across the whole previous tick, collision included. Sample advances serially in
		// pool order — deterministic.
		const FFixedVector PosNow = Entity.Transform.GetLocation();
		const bool bHasSample = Move->PrevTickLocation.X != FFixedPoint::Zero
			|| Move->PrevTickLocation.Y != FFixedPoint::Zero
			|| Move->PrevTickLocation.Z != FFixedPoint::Zero;
		if (bHasSample)
		{
			FFixedVector ActualDelta = PosNow - Move->PrevTickLocation;
			ActualDelta.Z = FFixedPoint::Zero;
			ActualDispSq.Last() = ActualDelta.SizeSquared();
			if (Move->bHasTarget)
			{
				FFixedVector PrevToGoal = Move->TargetLocation - Move->PrevTickLocation;
				PrevToGoal.Z = FFixedPoint::Zero;
				FFixedVector NowToGoal = Move->TargetLocation - PosNow;
				NowToGoal.Z = FFixedPoint::Zero;
				ActualProgress.Last() = PrevToGoal.Size() - NowToGoal.Size();
			}
		}
		Move->PrevTickLocation = PosNow;

		if (!bCohesionEnabled) return;
		if (!Move->bHasTarget || Move->AvoidanceStrength <= FFixedPoint::Zero) return;
		const FSeinBrokerMembershipData* Broker = BrokerStorage
			? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(Handle)) : nullptr;
		if (!Broker || !Broker->CurrentBrokerHandle.IsValid()) return;
		// FREE-TO-MOVE members only, judged on ACTUAL displacement. A body-blocked member —
		// whether it reads pinned (commanded ~zero) or presser (commanding plenty, displacing
		// nothing; the population the movement trace exposed) — is EXCLUDED: counting it
		// inflates the group mean so mobile members hold back for units that cannot be helped
		// by waiting, exactly when the crowd needs to spread. Hold-back/catch-up respond to
		// spread among members that are genuinely moving. No sample yet (first tick after
		// spawn) → fall back to the commanded-velocity test.
		const FFixedPoint DispSq = ActualDispSq.Last();
		const bool bActuallyMoving = DispSq >= FFixedPoint::Zero
			? DispSq > FloorPerTickSq
			: Move->Velocity.SizeSquared() > MovingSpeedFloor * MovingSpeedFloor;
		if (!bActuallyMoving) return;
		FFixedVector ToGoal = Move->TargetLocation - Entity.Transform.GetLocation();
		ToGoal.Z = FFixedPoint::Zero;
		FCohesionAggregate& Agg = GroupAggregates.FindOrAdd(Broker->CurrentBrokerHandle);
		Agg.Count += 1;
		Agg.SumDist = Agg.SumDist + ToGoal.Size();
	});

	SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
	{
		const FSeinEntityHandle SelfHandle = LiveHandles[Index];
		FSeinEntity* SelfEntityPtr = World.GetEntityPool().Get(SelfHandle);
		if (!SelfEntityPtr) return;
		FSeinEntity& SelfEntity = *SelfEntityPtr;

		// Per-body neighbour scratch — MUST be a local (one buffer per body invocation)
		// so concurrent QueryRadius calls never share it.
		TArray<FSeinEntityHandle> Neighbors;

		FSeinMovementComponent* Move = MoveStorage ? static_cast<FSeinMovementComponent*>(MoveStorage->GetComponentRaw(SelfHandle)) : nullptr;
		if (!Move) return;
		// Opted out → leave AvoidanceOutput UNTOUCHED (its default zero steer / unit scale),
		// so the unit's motion is bit-identical to a world with no avoidance.
		if (Move->AvoidanceStrength <= FFixedPoint::Zero) return;

		// Full-release helper for the "not participating this tick" exits below: both output
		// channels return to their exact no-op values so nothing stale lingers in the state
		// hash, the debug viz, or a consumer that reads them next tick.
		const auto ClearOutput = [Move]()
		{
			Move->AvoidanceOutput.SteerDir = FFixedVector::ZeroVector;
			Move->AvoidanceOutput.SpeedScale = FFixedPoint::One;
		};

		// No active move order → release and bail. Avoidance only steers movers.
		if (!Move->bHasTarget) { ClearOutput(); return; }

		// Heading from end-of-last-tick velocity (the same snapshot value for every unit
		// at PreTick). Stopped/too-slow → release and bail.
		const FFixedVector Vel = Move->Velocity;
		const FFixedPoint Speed = Vel.Size();
		if (Speed <= MovingSpeedFloor)
		{
#if !UE_BUILD_SHIPPING
			// GRIND DIAGNOSTIC — pure observation, behaviour unchanged. A COMMANDED unit here
			// COMMANDED ~zero motion last tick (Velocity is the unit's own movement step —
			// the body-collision floor never writes it, so "pinned" is never collision-zeroed;
			// think path-budget wait, mode-policy zero, nav-floor hold). When the channel is
			// Verbose, dump this unit's neighbourhood every ~half second with per-gate skip
			// counts — replaying the live gate chain as if the unit were heading toward its
			// goal — so a PIE repro shows WHY no separation force is acting on it. Body-blocked
			// PRESSERS (commanding plenty, displacing nothing) never reach this early-out —
			// the movement trace ([EP]/[UNIT], `log LogSeinMoveTrace Verbose`) owns that
			// population. Enable: `log LogSeinAvoidance Verbose`.
			if (Move->bHasTarget
				&& UE_LOG_ACTIVE(LogSeinAvoidance, Verbose)
				&& (World.GetCurrentTick() % 15) == 0)
			{
				const FFixedVector DiagPos = SelfEntity.Transform.GetLocation();
				FFixedVector DiagToGoal = Move->TargetLocation - DiagPos;
				DiagToGoal.Z = FFixedPoint::Zero;
				const FFixedPoint DiagGoalDist = DiagToGoal.Size();
				FFixedVector DiagHeading = FFixedVector::ZeroVector;
				if (DiagGoalDist > FFixedPoint::Epsilon)
				{
					DiagHeading = FFixedVector(
						DiagToGoal.X / DiagGoalDist, DiagToGoal.Y / DiagGoalDist, FFixedPoint::Zero);
				}
				const FSeinNavigationComponent* DiagNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(SelfHandle)) : nullptr;
				const FSeinExtentsComponent* DiagExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle)) : nullptr;
				FFixedPoint DiagRadius = USeinMovement::ResolveCollisionRadius(DiagExt, DiagNav);
				if (DiagRadius <= FFixedPoint::Zero) { DiagRadius = FFixedPoint::FromInt(50); }
				const FSeinBrokerMembershipData* DiagBroker = BrokerStorage
					? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(SelfHandle)) : nullptr;
				const FSeinEntityHandle DiagBrokerHandle = DiagBroker ? DiagBroker->CurrentBrokerHandle : FSeinEntityHandle();
				const int64 DiagCohesionId = DiagBroker ? DiagBroker->CohesionGroupId : 0;

				Neighbors.Reset();
				Hash.QueryRadius(DiagPos, DiagRadius * FFixedPoint::FromInt(4), Neighbors, SelfHandle);
				int32 NKept = 0, NStatic = 0, NGroup = 0, NIdleTrue = 0, NIdlePinned = 0;
				int32 NWeight = 0, NBehind = 0, NNotClosing = 0, NPastGoal = 0, NFar = 0;
				FFixedPoint MinDistSq = FFixedPoint::FromInt(999999);
				for (const FSeinEntityHandle& OtherHandle : Neighbors)
				{
					const FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
					if (!OtherEntity) continue;
					const FSeinMovementComponent* OtherMove = MoveStorage ? static_cast<const FSeinMovementComponent*>(MoveStorage->GetComponentRaw(OtherHandle)) : nullptr;
					if (!OtherMove) { ++NStatic; continue; }
					FFixedVector ToOther = OtherEntity->Transform.GetLocation() - DiagPos;
					ToOther.Z = FFixedPoint::Zero;
					const FFixedPoint DistSq = ToOther.SizeSquared();
					if (DistSq < MinDistSq) { MinDistSq = DistSq; }
					const FSeinBrokerMembershipData* OtherBroker = BrokerStorage
						? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(OtherHandle)) : nullptr;
					const bool bSameBroker = DiagBrokerHandle.IsValid() && OtherBroker && OtherBroker->CurrentBrokerHandle == DiagBrokerHandle;
					const bool bSameCohesion = DiagCohesionId != 0 && OtherBroker && OtherBroker->CohesionGroupId == DiagCohesionId;
					if (bSameBroker || bSameCohesion) { ++NGroup; continue; }
					if (OtherMove->Velocity.SizeSquared() <= MovingSpeedFloor * MovingSpeedFloor)
					{
						// The deadlock's smoking gun: bulldoze-skipped neighbours that are
						// THEMSELVES commanded-but-pinned (idlePinned) vs true idlers (idleTrue).
						if (OtherMove->bHasTarget) { ++NIdlePinned; } else { ++NIdleTrue; }
						continue;
					}
					const bool bQualifiesDiag = Move->bAvoidSameWeights
						? (OtherMove->AvoidanceWeight >= Move->AvoidanceWeight)
						: (OtherMove->AvoidanceWeight >  Move->AvoidanceWeight);
					if (!bQualifiesDiag) { ++NWeight; continue; }
					const FFixedPoint Ahead = ToOther.X * DiagHeading.X + ToOther.Y * DiagHeading.Y;
					if (Ahead <= FFixedPoint::Zero) { ++NBehind; continue; }
					const FFixedVector RelVelDiag(
						Vel.X - OtherMove->Velocity.X, Vel.Y - OtherMove->Velocity.Y, FFixedPoint::Zero);
					if (ToOther.X * RelVelDiag.X + ToOther.Y * RelVelDiag.Y <= FFixedPoint::Zero) { ++NNotClosing; continue; }
					if (DiagGoalDist > FFixedPoint::Zero && DistSq >= DiagGoalDist * DiagGoalDist) { ++NPastGoal; continue; }
					const FSeinNavigationComponent* ONav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(OtherHandle)) : nullptr;
					const FSeinExtentsComponent* OExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle)) : nullptr;
					const FFixedPoint ORadius = USeinMovement::ResolveCollisionRadius(OExt, ONav);
					const FFixedPoint Range = (DiagRadius + ORadius) * FalloffRadii;
					if (DistSq >= Range * Range) { ++NFar; continue; }
					++NKept;
				}
				// steer/scale caveat: the pinned early-out CLEARS the output every pinned
				// tick, so on any dump after the first these print the reset values
				// (0 / 1.0), not what carried the unit into the pin — the movement
				// trace's [UNIT] lastLive fields hold that (log LogSeinMoveTrace Verbose).
				UE_LOG(LogSeinAvoidance, Verbose,
					TEXT("[GRIND] t=%d h=%d:%d grp=%d:%d coh=%lld goalDist=%.0f tgt=(%.0f,%.0f) nbrs=%d kept=%d skip{static=%d grp=%d idleTrue=%d idlePinned=%d wt=%d behind=%d notClosing=%d pastGoal=%d far=%d} minD=%.0f steer=%.3f scale=%.3f"),
					World.GetCurrentTick(), SelfHandle.Index, SelfHandle.Generation,
					DiagBrokerHandle.Index, DiagBrokerHandle.Generation, DiagCohesionId,
					DiagGoalDist.ToFloat(),
					Move->TargetLocation.X.ToFloat(), Move->TargetLocation.Y.ToFloat(),
					Neighbors.Num(), NKept,
					NStatic, NGroup, NIdleTrue, NIdlePinned, NWeight, NBehind, NNotClosing, NPastGoal, NFar,
					SeinMath::Sqrt(MinDistSq).ToFloat(),
					Move->AvoidanceOutput.SteerDir.Size().ToFloat(),
					Move->AvoidanceOutput.SpeedScale.ToFloat());
			}
#endif
			ClearOutput();
			return;
		}
		const FFixedVector Heading(Vel.X / Speed, Vel.Y / Speed, FFixedPoint::Zero);
		const FFixedVector Right(Heading.Y, -Heading.X, FFixedPoint::Zero); // planar right of heading

		// Body radius from the movement/nav FOOTPRINT cascade — NOT collision extents.
		// Hoisted-storage pointers feed the no-lookup ResolveCollisionRadius overload.
		const FSeinNavigationComponent* SelfNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinExtentsComponent* SelfExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FFixedPoint SelfRadius = USeinMovement::ResolveCollisionRadius(SelfExt, SelfNav);
		if (SelfRadius <= FFixedPoint::Zero) { ClearOutput(); return; }

		// Self's two-layer group identity (see the storage-hoist comment above).
		const FSeinBrokerMembershipData* SelfBroker = BrokerStorage
			? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinEntityHandle SelfBrokerHandle = SelfBroker ? SelfBroker->CurrentBrokerHandle : FSeinEntityHandle();
		const int64 SelfCohesionId = SelfBroker ? SelfBroker->CohesionGroupId : 0;

		const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();

		// Planar distance to goal — drives the past-goal gate AND the arrival fade.
		// TargetLocation is published by USeinMoveToAction every move tick (gated on
		// bHasTarget above), so this is the CURRENT order's resolved per-member goal.
		FFixedVector ToGoal = Move->TargetLocation - SelfPos;
		ToGoal.Z = FFixedPoint::Zero;
		const FFixedPoint GoalDistSq = ToGoal.SizeSquared();

		// ARRIVAL-RELEASE FADE: stop steering (and braking) as the unit closes on its goal
		// so path-attraction + the collision floor own the endgame. Without this, a
		// destination inside/behind a standing cluster makes the unit orbit the perimeter
		// forever. (This is the mechanism that was dead in the original model.)
		const FFixedPoint ReleaseRadius = SelfRadius * ArrivalReleaseRadii;
		if (GoalDistSq <= ReleaseRadius * ReleaseRadius) { ClearOutput(); return; }

		// Speed-scaled perception radius (footprint-based; "personal space" needs no
		// separate authored radius).
		const FFixedPoint Perception = SelfRadius * FFixedPoint::FromInt(2) + Speed * LookaheadSeconds;

		Neighbors.Reset();
		Hash.QueryRadius(SelfPos, Perception, Neighbors, SelfHandle);

		FFixedVector Accum = FFixedVector::ZeroVector;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;

			// UNIT-TO-UNIT ONLY. A neighbour with no movement component is static geometry
			// — a wall, a building, a prop — and nav blocking already routes units clear of
			// those. (The spatial hash holds ALL colliders, walls included, so the filter
			// must live here.) Fetched once and reused for head-on below.
			const FSeinMovementComponent* OtherMove = MoveStorage ? static_cast<const FSeinMovementComponent*>(MoveStorage->GetComponentRaw(OtherHandle)) : nullptr;
			if (!OtherMove) continue;

			// Neighbour's group — drives BOTH the cohesion skip and group-vs-group passing.
			const FSeinBrokerMembershipData* OtherBroker = BrokerStorage
				? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FSeinEntityHandle OtherBrokerHandle = OtherBroker ? OtherBroker->CurrentBrokerHandle : FSeinEntityHandle();

			// GROUP COHESION SKIP: a neighbour ordered together with us is never avoided —
			// the group converges and packs instead of steering around itself; the collision
			// floor keeps bodies apart. "Ordered together" = same immediate broker OR same
			// per-order cohesion group (the cross-broker layer, so separate squads and
			// squad-vs-loose in ONE order still cohere). This kills the in-group fan-out the
			// closing-velocity gate alone can't (units converging on one point ARE closing).
			const bool bSameBroker = SelfBrokerHandle.IsValid() && OtherBrokerHandle == SelfBrokerHandle;
			const int64 OtherCohesionId = OtherBroker ? OtherBroker->CohesionGroupId : 0;
			const bool bSameCohesion = SelfCohesionId != 0 && SelfCohesionId == OtherCohesionId;
			if (bSameBroker || bSameCohesion) continue;

			// BULLDOZE IDLE NEIGHBOURS (the anti-orbit rule). A STATIONARY neighbour gets NO
			// steering dodge — you can't orbit something that isn't moving, and steering
			// around a static cluster is exactly what curves a transiting unit into the
			// "black hole" orbit. Idle units are left to the collision floor (pushed aside),
			// so the unit pushes straight through instead of circling. Only MOVING
			// neighbours get a dodge. Squared compare avoids a per-neighbour sqrt.
			if (OtherMove->Velocity.SizeSquared() <= MovingSpeedFloor * MovingSpeedFloor) continue;

			// WEIGHT-PRIORITY GATE. This unit only yields to a neighbour whose
			// AvoidanceWeight qualifies: equal-or-higher when bAvoidSameWeights, strictly
			// higher otherwise (so a heavier class never dodges a lighter one, and with the
			// bool off, equal peers fall through to the collision floor — killing
			// same-class mutual-avoidance orbits). Integer compare → deterministic.
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

			// CLOSING-VELOCITY GATE. Only avoid a neighbour we are actually CLOSING with.
			// Parallel movers (a cohesive column, wall-following traffic) have ~zero closing
			// rate and skip here — they stop shoving each other out of the lane. Crossing /
			// head-on / overtake courses ARE closing and are avoided. Snapshot velocities →
			// deterministic, one-sided.
			const FFixedVector RelVel(
				Vel.X - OtherMove->Velocity.X,
				Vel.Y - OtherMove->Velocity.Y,
				FFixedPoint::Zero);
			const FFixedPoint ClosingDot = ToOther.X * RelVel.X + ToOther.Y * RelVel.Y;
			if (ClosingDot <= FFixedPoint::Zero) continue;

			// Past-goal gate: ignore neighbours farther than the goal — no dodging things
			// beyond where we will stop. (Live for the first time; see the file docstring.)
			if (GoalDistSq > FFixedPoint::Zero && DistSq >= GoalDistSq) continue;

			// Other body radius from the SAME footprint cascade.
			const FSeinNavigationComponent* OtherNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FSeinExtentsComponent* OtherExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle)) : nullptr;
			const FFixedPoint OtherRadius = USeinMovement::ResolveCollisionRadius(OtherExt, OtherNav);
			if (OtherRadius <= FFixedPoint::Zero) continue;

			const FFixedPoint Dist = SeinMath::Sqrt(DistSq);
			const FFixedPoint FalloffRange = (SelfRadius + OtherRadius) * FalloffRadii;
			if (Dist >= FalloffRange) continue;
			const FFixedPoint Falloff = FFixedPoint::One - (Dist / FalloffRange);

			// Head-on weight: a neighbour moving AGAINST us weighs strong, one moving WITH
			// us weak; a slow neighbour gets the moderate base.
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

			// Dodge AWAY from the neighbour's side. Inside a "dead-ahead" band the side is
			// undefined → break it DETERMINISTICALLY by handle index, so two head-on units
			// pick OPPOSITE sides instead of marching into each other (do-si-do).
			const FFixedPoint SideDot = ToOther.X * Right.X + ToOther.Y * Right.Y;
			const FFixedPoint LateralBand = SelfRadius / FFixedPoint::FromInt(4);
			FFixedPoint TurnSign;
			if (SideDot > LateralBand)        { TurnSign = -FFixedPoint::One; } // neighbour on right → steer left
			else if (SideDot < -LateralBand)  { TurnSign =  FFixedPoint::One; } // neighbour on left  → steer right
			else { TurnSign = (SelfHandle.Index < OtherHandle.Index) ? FFixedPoint::One : -FFixedPoint::One; }

			// GROUP-VS-GROUP SIDEWALK PASS: when BOTH units belong to (different) groups,
			// OVERRIDE the per-unit do-si-do with a uniform "shift to my own right" — every
			// member of a group steps the SAME way, so two opposing blobs slide past each
			// other like sidewalk traffic instead of each unit splaying independently.
			// (Corridor-awareness — damping the shift when that side is nav-blocked — is a
			// deferred refinement; the wall-tangent guard + hard barrier hold the line.)
			if (SelfBrokerHandle.IsValid() && OtherBrokerHandle.IsValid())
			{
				TurnSign = FFixedPoint::One;
			}

			// Steer weight is PURE STEERING — NO mass term. HeadOn (encounter angle) ×
			// Falloff (proximity, range ∝ combined footprint → size-proportional) ×
			// TurnSign. Do NOT reintroduce a mass factor here — mass physics belong to the
			// collision floor, not this steering layer. AvoidanceStrength (below) is the
			// magnitude knob; AvoidanceWeight (gated above) is the priority.
			const FFixedPoint W = HeadOn * Falloff * TurnSign;
			Accum.X += Right.X * W;
			Accum.Y += Right.Y * W;
		}

		// Clamp the accumulated lateral nudge (bounds a crowd repulsor sum + fixed-point
		// blow-up) BEFORE strength-scale + smoothing. The nudge bends a UNIT direction
		// downstream, so the cap is in that same unit space.
		FFixedPoint AccumLen = Accum.Size();
		if (AccumLen > MaxSteerMagnitude && AccumLen > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale = MaxSteerMagnitude / AccumLen;
			Accum.X = Accum.X * Scale;
			Accum.Y = Accum.Y * Scale;
			AccumLen = MaxSteerMagnitude;
		}

		// Strength-scale, then temporally smooth against the previous steer (damps the
		// perception-boundary snap as neighbours enter/leave). Snap negligible results to
		// exactly zero so a unit clear of traffic returns to a true no-op.
		const FFixedVector Scaled(
			Accum.X * Move->AvoidanceStrength,
			Accum.Y * Move->AvoidanceStrength,
			FFixedPoint::Zero);
		FFixedVector Smoothed(
			Scaled.X * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceOutput.SteerDir.X * SmoothKeep,
			Scaled.Y * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceOutput.SteerDir.Y * SmoothKeep,
			FFixedPoint::Zero);
		const bool bSteerCleared = Smoothed.SizeSquared() <= FFixedPoint::Epsilon;
		if (bSteerCleared) Smoothed = FFixedVector::ZeroVector;
		Move->AvoidanceOutput.SteerDir = Smoothed;

		// SPEED-SCALE PRODUCER — two composed terms, each independently dial-able to a
		// bit-exact One so PIE sessions can attribute feel per mechanism:
		//
		// 1. BRAKE (yield-by-slowing, layered on yield-by-turning). Steer saturation is
		//    the congestion proxy: the harder this unit is pushed sideways, the more it
		//    eases off cruise — a unit swerving at its cap through a dense weave slows
		//    into it instead of sliding through at full tilt. Linear ramp to
		//    (1 − BrakeStrength) at full saturation. BrakeStrength 0 = term pinned at One.
		FFixedPoint BrakeTerm = FFixedPoint::One;
		if (BrakeStrength > FFixedPoint::Zero && MaxSteerMagnitude > FFixedPoint::Zero && !bSteerCleared)
		{
			FFixedPoint YieldT = (AccumLen * Move->AvoidanceStrength) / MaxSteerMagnitude;
			if (YieldT > FFixedPoint::One) YieldT = FFixedPoint::One;
			BrakeTerm = FFixedPoint::One - BrakeStrength * YieldT;
		}

		// 2. FORMATION COHESION (broker-scoped — the inner formation layer). Compare this
		//    member's remaining distance to its group's mean (the serial pre-pass
		//    aggregate): AHEAD of the group → hold back toward (1 − HoldBack); BEHIND →
		//    catch-up boost toward CohesionBoost (> 1 — the widened SpeedScale contract's
		//    first producer). Deviation is normalized by the mean (floored at 4 footprints
		//    so an arriving group doesn't blow the ratio up) with a deadband so a
		//    steady formation doesn't oscillate around its own average. Solo units,
		//    single-member groups, and disabled dials all leave the term at exactly One.
		FFixedPoint CohesionTerm = FFixedPoint::One;
		if (bCohesionEnabled && SelfBrokerHandle.IsValid())
		{
			if (const FCohesionAggregate* Agg = GroupAggregates.Find(SelfBrokerHandle))
			{
				if (Agg->Count >= 2)
				{
					const FFixedPoint Mean = Agg->SumDist / FFixedPoint::FromInt(Agg->Count);
					const FFixedPoint SelfDist = SeinMath::Sqrt(GoalDistSq);
					// SPATIAL normalization — deviation measured in body-lengths (footprint ×
					// CohesionRangeRadii), NOT as a fraction of remaining trip. Normalizing by
					// the mean made cohesion invisible on long moves: a 200cm lag was 0.07 of a
					// 3000cm march (under the deadband) but 0.4 of a 500cm hop — the same
					// physical strung-out-ness must read the same at any order length.
					const FFixedPoint Norm = SelfRadius * CohesionRangeRadii;
					FFixedPoint DevT = (SelfDist - Mean) / Norm;                 // + = behind, − = ahead
					if (DevT >  FFixedPoint::One) DevT =  FFixedPoint::One;
					if (DevT < -FFixedPoint::One) DevT = -FFixedPoint::One;
					const FFixedPoint Deadband = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(20); // 0.15
					const FFixedPoint Span = FFixedPoint::One - Deadband;
					if (DevT > Deadband)
					{
						// CATCH-UP only for a member that is GAINING GROUND on its goal, judged
						// on ACTUAL progress — not commanded velocity (a body-blocked straggler
						// commands plenty while standing still) and not raw displacement either
						// (a churned unit extruded sideways at 100+ cm/s displaces plenty while
						// gaining nothing; the movement trace measured both populations drawing
						// boost into the jam). More throttle without headway is pure collision
						// load, so the boost waits until the unit genuinely closes on its goal.
						// No sample yet → boost (the pinned early-out above vouched for motion).
						const FFixedPoint SelfProgress = ActualProgress[Index];
						const bool bMakingHeadway = SelfProgress == ProgressUnknown
							|| SelfProgress > FloorPerTick;
						if (bMakingHeadway)
						{
							const FFixedPoint T = (DevT - Deadband) / Span;      // (0,1]
							CohesionTerm = FFixedPoint::One + (CohesionBoost - FFixedPoint::One) * T;
						}
					}
					else if (DevT < -Deadband)
					{
						const FFixedPoint T = (-DevT - Deadband) / Span;         // (0,1]
						CohesionTerm = FFixedPoint::One - CohesionHoldBack * T;
					}
				}
			}
		}

		// Compose multiplicatively, smooth like the steer (damps enter/leave snaps), snap
		// near-One back to EXACTLY One so a unit under neither term is a bit-exact no-op.
		const FFixedPoint TargetScale = BrakeTerm * CohesionTerm;
		FFixedPoint SmoothedScale =
			TargetScale * (FFixedPoint::One - SmoothKeep) + Move->AvoidanceOutput.SpeedScale * SmoothKeep;
		FFixedPoint OneDelta = FFixedPoint::One - SmoothedScale;
		if (OneDelta < FFixedPoint::Zero) OneDelta = -OneDelta;
		if (OneDelta <= FFixedPoint::Epsilon) SmoothedScale = FFixedPoint::One;
		if (SmoothedScale < FFixedPoint::Zero) SmoothedScale = FFixedPoint::Zero;

		// PHYSICAL FLOOR — a produced scale must never command a crawl at or below the
		// moving-speed floor. Brake × hold-back can compose arbitrarily small; below
		// MovingSpeedFloor/TopSpeed the unit reads as PINNED to every velocity-gated
		// consumer (the pinned early-out above, bulldoze-idle, the cohesion gather) and
		// self-stalls in a limit cycle: crawl → classified pinned → gates release →
		// re-accelerate → crawl. Floored at 2× so commanded speed sits decisively above
		// the classifier. Derived per-unit from TopSpeed — no dial: a floor that exists
		// only to stay above a fixed classifier threshold has exactly one correct value.
		if (SmoothedScale < FFixedPoint::One && Move->TopSpeed > FFixedPoint::Zero)
		{
			FFixedPoint MinScale = (MovingSpeedFloor * FFixedPoint::FromInt(2)) / Move->TopSpeed;
			if (MinScale > FFixedPoint::One) MinScale = FFixedPoint::One;
			if (SmoothedScale < MinScale) SmoothedScale = MinScale;
		}
		Move->AvoidanceOutput.SpeedScale = SmoothedScale;
	});
}
