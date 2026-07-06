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
#include "Components/SeinCommandBrokerData.h"   // blob-obstacle: neighbour broker Centroid/FormationRadius/flag
#include "Movement/SeinMovement.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

// Diagnostic channel for the avoidance model. Verbose = the grind dump (see the pinned-unit
// early-out in ComputeAvoidance): enable in PIE with `log LogSeinAvoidance Verbose`, reproduce,
// and the log shows each commanded-but-pinned unit's neighbourhood with per-gate skip counts.
DEFINE_LOG_CATEGORY_STATIC(LogSeinAvoidance, Log, All);

namespace
{
	/**
	 * Deterministic, antisymmetric "do-si-do": the WORLD-SPACE unit direction THIS unit should
	 * slide toward so it passes an oncoming/crossing partner cleanly instead of mirror-dancing.
	 *
	 * The trap the naive per-unit side-pick falls into: two opposed units each pick "the side the
	 * other is on relative to MY heading", and because their headings are opposed those picks
	 * RE-ALIGN to the same world direction — they march together, never passing. The fix is a
	 * SHARED world-frame axis: order the pair canonically (handle Index,Gen), take the perpendicular
	 * of the connecting line PosLo→PosHi (identical for both units, independent of either heading),
	 * and hand each role the OPPOSITE end of it. Lo → +perp, Hi → −perp ⇒ provably opposite world
	 * sides, computed from frozen snapshots with zero shared state (one-sided-write safe). The
	 * caller scales this by the usual HeadOn×Falloff magnitude and adds it to the world-space Accum.
	 */
	static FFixedVector ComputeDoSiDoSteer(
		const FSeinEntityHandle& Self, const FSeinEntityHandle& Other,
		const FFixedVector& SelfPos, const FFixedVector& OtherPos)
	{
		const bool bSelfIsLo = Self < Other;                       // canonical total order (Index, then Gen)
		const FFixedVector& PosLo = bSelfIsLo ? SelfPos : OtherPos;
		const FFixedVector& PosHi = bSelfIsLo ? OtherPos : SelfPos;
		const FFixedVector D(PosHi.X - PosLo.X, PosHi.Y - PosLo.Y, FFixedPoint::Zero); // shared connecting line
		FFixedVector Perp(-D.Y, D.X, FFixedPoint::Zero);           // world perpendicular — SAME for both units
		const FFixedPoint Len = Perp.Size();
		if (Len <= FFixedPoint::Epsilon)
		{
			// Coincident centres — no connecting line. Fall back to a fixed world axis split by
			// handle order (still shared + antisymmetric): Lo→+Y, Hi→−Y.
			return bSelfIsLo
				? FFixedVector(FFixedPoint::Zero,  FFixedPoint::One, FFixedPoint::Zero)
				: FFixedVector(FFixedPoint::Zero, -FFixedPoint::One, FFixedPoint::Zero);
		}
		Perp.X = Perp.X / Len;
		Perp.Y = Perp.Y / Len;
		// Lo pushes to the +perp end, Hi to the −perp end → opposite sides for head-on AND crossing.
		return bSelfIsLo ? Perp : FFixedVector(-Perp.X, -Perp.Y, FFixedPoint::Zero);
	}
}

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
	// Do-si-do (crossing slide-past) dials. Strength 0 = the crossing steer is off entirely
	// (pure group-skip + geometric sidewalk, the pre-do-si-do behaviour). CrossGoalDivergence is
	// the goal-separation-vs-body-separation ratio that marks a GENUINE crossing (paired with
	// opposed travel intent); larger = crossings recognised more rarely = more packing preserved.
	const FFixedPoint DoSiDoStrength      = Settings->AvoidanceDoSiDoStrength;
	const FFixedPoint DoSiDoCrossDiverge  = Settings->AvoidanceCrossingGoalDivergence;
	const FFixedPoint KconvSq             = DoSiDoCrossDiverge * DoSiDoCrossDiverge;
	const bool bDoSiDoEnabled             = DoSiDoStrength > FFixedPoint::Zero;
	// RESOLVE-THROUGH (mover-resolves-around-idlers). Strength 0 = the bulldoze-idle rule stands
	// bit-exact (a mover plows through parked units, collision shoves them). > 0 = a moving unit
	// steers around an idle neighbour whose AvoidanceWeight qualifies (heavier-or-equal), scaled by
	// this. Orbit-safe under the goal-relative bend cap (which guarantees forward progress).
	const FFixedPoint IdleResolveStrength = Settings->AvoidanceIdleResolveStrength;
	const bool bResolveThroughIdlers      = IdleResolveStrength > FFixedPoint::Zero;
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
	// Broker-LEVEL data (Centroid / FormationRadius / bAvoidAsCohesiveBody) for the blob-obstacle
	// scope: read per neighbour via its CurrentBrokerHandle. Prior-tick snapshot (broker maintenance
	// is PostTick), read-only in the parallel body → contract-safe.
	ISeinComponentStorage* BrokerDataStorage = World.GetComponentStorageRaw(FSeinCommandBrokerData::StaticStruct());

	// Gather live handles (serial, cheap), then fan the per-unit computation across
	// worker threads under the body contract in the file docstring. The SAME serial
	// walk builds the per-formation cohesion aggregate: for every actively-moving
	// broker member, its planar remaining distance to its own goal, summed per broker.
	// Serial pool-order accumulation → deterministic; the parallel pass below only
	// READS the finished map (immutable snapshot), preserving the body contract.
	// Broker-scoped = the INNER formation layer (a squad, or a loose-order group).
	// Cross-broker cohesion for a multi-squad order (the outer CohesionGroupId layer —
	// squads keeping pace with squads) is a deliberate follow-up, not implemented here.
	// Per-broker (INNER) aggregate. Also carries the broker's CohesionGroupId + the outer-pacing
	// flag, captured once from the first admitted member (all members of one broker share both), so
	// the OUTER aggregate-of-aggregates below can be built without a broker→members reverse walk.
	struct FCohesionAggregate
	{
		int32 Count = 0;
		FFixedPoint SumDist;
		int64 CohesionGroupId = 0;
		bool bPaceSquads = false;
		bool bStamped = false;
	};
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
		if (!Agg.bStamped)
		{
			// Capture the broker's order id + outer-pacing flag once (deterministic — every admitted
			// member of this broker shares both; the flag is a per-broker property).
			Agg.CohesionGroupId = Broker->CohesionGroupId;
			const FSeinCommandBrokerData* BD = BrokerDataStorage
				? static_cast<const FSeinCommandBrokerData*>(BrokerDataStorage->GetComponentRaw(Broker->CurrentBrokerHandle)) : nullptr;
			Agg.bPaceSquads = BD && BD->bPaceSquadsTogether;
			Agg.bStamped = true;
		}
		Agg.Count += 1;
		Agg.SumDist = Agg.SumDist + ToGoal.Size();
	});

	// OUTER cohesion aggregate-of-aggregates (squads pacing squads). Serial, after the per-broker
	// sums finalize and before the parallel pass. For each DISTINCT flagged broker in a multi-squad
	// order (keyed on the shared CohesionGroupId), accumulate its mean remaining distance with EQUAL
	// SQUAD WEIGHT (mean-of-broker-means, not member-weighted). Only flagged brokers enter, so the
	// setting OFF → empty map → OuterTerm==One everywhere → bit-exact inner-only. Commutative sums →
	// order-independent → deterministic despite TMap iteration order.
	struct FOuterCohesionAggregate { int32 DistinctBrokerCount = 0; FFixedPoint SumOfBrokerMeans; };
	TMap<int64, FOuterCohesionAggregate> OuterAggregates;
	if (bCohesionEnabled)
	{
		for (const TPair<FSeinEntityHandle, FCohesionAggregate>& Pair : GroupAggregates)
		{
			const FCohesionAggregate& A = Pair.Value;
			if (A.Count <= 0 || !A.bPaceSquads || A.CohesionGroupId == 0) continue;
			const FFixedPoint BrokerMean = A.SumDist / FFixedPoint::FromInt(A.Count);
			FOuterCohesionAggregate& O = OuterAggregates.FindOrAdd(A.CohesionGroupId);
			O.DistinctBrokerCount += 1;
			O.SumOfBrokerMeans = O.SumOfBrokerMeans + BrokerMean;
		}
	}

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

		// Self's own broker BLOB state — for blob-vs-blob: when BOTH self and the foreign obstacle
		// squad advertise "avoid me as a body", the two squads sidestep coherently keyed on their
		// broker centroids. When self is loose or non-blob, self routes around a foreign blob as an
		// individual (the simpler geometric pick in the blob branch below).
		const FSeinCommandBrokerData* SelfBrokerData = (SelfBrokerHandle.IsValid() && BrokerDataStorage)
			? static_cast<const FSeinCommandBrokerData*>(BrokerDataStorage->GetComponentRaw(SelfBrokerHandle)) : nullptr;
		const bool bSelfIsBlob = SelfBrokerData && SelfBrokerData->bAvoidAsCohesiveBody
			&& SelfBrokerData->FormationRadius > FFixedPoint::Zero;
		const FFixedVector SelfBrokerCentroid = SelfBrokerData ? SelfBrokerData->Centroid : FFixedVector::ZeroVector;

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

		// Body-local dedup for the blob scope: a blob-flagged foreign squad contributes ONE steer
		// (from its Centroid/FormationRadius), not one per member in range. Fresh per invocation.
		TSet<FSeinEntityHandle> VisitedBlobBrokers;

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

			// A blob already resolved this invocation: skip its remaining members entirely (one
			// steer per blob, from its Centroid — see the blob branch below). Placed before the
			// group-skip so it can't be reached for self's own broker (self is never in the set).
			if (OtherBrokerHandle.IsValid() && VisitedBlobBrokers.Contains(OtherBrokerHandle)) continue;

			// Neighbour goal snapshot (immutable PreTick) → the genuine-crossing predicate.
			const bool bOtherHasTarget = OtherMove->bHasTarget;
			// Blob-obstacle state of the neighbour's broker (never self's own broker).
			const FSeinCommandBrokerData* OtherBrokerData = (OtherBrokerHandle.IsValid() && BrokerDataStorage
				&& OtherBrokerHandle != SelfBrokerHandle)
				? static_cast<const FSeinCommandBrokerData*>(BrokerDataStorage->GetComponentRaw(OtherBrokerHandle)) : nullptr;
			const bool bOtherIsBlob = OtherBrokerData && OtherBrokerData->bAvoidAsCohesiveBody
				&& OtherBrokerData->FormationRadius > FFixedPoint::Zero;

			// GENUINE-CROSSING PREDICATE — distinguishes CONVERGING (goals collapsing to one region,
			// keep group-skip/packing) from CROSSING (opposed travel intent + goals far apart, so a
			// do-si-do is warranted). Reused by the intra-group carve-out (scope 1) AND the
			// cross-group passing (scope 2). Self's bHasTarget is guaranteed by the !bHasTarget
			// early-out at the top of this parallel body, so Move->TargetLocation is a live goal here.
			bool bGenuineCrossing = false;
			if (bDoSiDoEnabled && bOtherHasTarget)
			{
				FFixedVector OtherToGoal = OtherMove->TargetLocation - OtherEntity->Transform.GetLocation();
				OtherToGoal.Z = FFixedPoint::Zero;
				FFixedVector ToOtherEarly = OtherEntity->Transform.GetLocation() - SelfPos;
				ToOtherEarly.Z = FFixedPoint::Zero;
				const FFixedPoint BodySepSq = ToOtherEarly.SizeSquared();
				const FFixedVector RelVelEarly(Vel.X - OtherMove->Velocity.X, Vel.Y - OtherMove->Velocity.Y, FFixedPoint::Zero);
				const FFixedPoint ClosingEarly = ToOtherEarly.X * RelVelEarly.X + ToOtherEarly.Y * RelVelEarly.Y; // >0 closing
				const FFixedPoint IntentDot    = ToGoal.X * OtherToGoal.X + ToGoal.Y * OtherToGoal.Y;             // <0 opposed
				FFixedVector GoalSep = OtherMove->TargetLocation - Move->TargetLocation;
				GoalSep.Z = FFixedPoint::Zero;
				const FFixedPoint GoalSepSq = GoalSep.SizeSquared();
				bGenuineCrossing =
					   (ClosingEarly > FFixedPoint::Zero)          // closing
					&& (IntentDot   < FFixedPoint::Zero)           // opposed travel intent (sign-stable primary)
					&& (GoalSepSq   > KconvSq * BodySepSq);        // goals not collapsing to one region
			}

			// GROUP COHESION SKIP: a neighbour ordered together with us is never avoided —
			// the group converges and packs instead of steering around itself; the collision
			// floor keeps bodies apart. "Ordered together" = same immediate broker OR same
			// per-order cohesion group (the cross-broker layer, so separate squads and
			// squad-vs-loose in ONE order still cohere). This kills the in-group fan-out the
			// closing-velocity gate alone can't (units converging on one point ARE closing).
			const bool bSameBroker = SelfBrokerHandle.IsValid() && OtherBrokerHandle == SelfBrokerHandle;
			const int64 OtherCohesionId = OtherBroker ? OtherBroker->CohesionGroupId : 0;
			const bool bSameCohesion = SelfCohesionId != 0 && SelfCohesionId == OtherCohesionId;
			if (bSameBroker || bSameCohesion)
			{
				// CONVERGING mate → keep packing (group-skip unchanged). Only a GENUINE CROSSING
				// (two mates whose slots are on opposite sides — the re-seek-lockup case) unlocks a
				// do-si-do-ONLY slide-past; general mate-vs-mate separation stays OFF so tight
				// formations don't fan out. Anti-cross slot assignment (ReassignSlots) is the first
				// line that makes crossings rare; this is the safety net for the residual.
				if (!bGenuineCrossing) continue;
				// An idle mate is not a crossing partner (nothing to pass).
				if (OtherMove->Velocity.SizeSquared() <= MovingSpeedFloor * MovingSpeedFloor) continue;
				FFixedVector CToOther = OtherEntity->Transform.GetLocation() - SelfPos;
				CToOther.Z = FFixedPoint::Zero;
				const FFixedPoint CDistSq = CToOther.SizeSquared();
				if (CDistSq <= FFixedPoint::Epsilon) continue;
				const FSeinNavigationComponent* CNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(OtherHandle)) : nullptr;
				const FSeinExtentsComponent* CExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle)) : nullptr;
				const FFixedPoint CRadius = USeinMovement::ResolveCollisionRadius(CExt, CNav);
				if (CRadius <= FFixedPoint::Zero) continue;
				const FFixedPoint CDist = SeinMath::Sqrt(CDistSq);
				const FFixedPoint CRange = (SelfRadius + CRadius) * FalloffRadii;
				if (CDist >= CRange) continue;
				const FFixedPoint CFall = FFixedPoint::One - (CDist / CRange);
				const FFixedVector CSteer = ComputeDoSiDoSteer(
					SelfHandle, OtherHandle, SelfPos, OtherEntity->Transform.GetLocation());
				const FFixedPoint CMag = (FFixedPoint::One + HeadOnBase) * CFall * DoSiDoStrength;
				Accum.X += CSteer.X * CMag;
				Accum.Y += CSteer.Y * CMag;
				continue; // handled this mate as a crossing — no general separation
			}

			// BLOB OBSTACLE (scope 3, opt-in per squad). When the neighbour's broker advertises
			// "avoid me as one cohesive body", resolve ONCE against its Centroid + FormationRadius
			// instead of chasing the moving inter-member gap (which is what makes a transiting unit
			// orbit a squad). Deduped so a 50-member squad emits a single steer. Runs before the
			// bulldoze gate so a PARKED blob squad is still routed around (it advertised as a wall).
			if (bOtherIsBlob)
			{
				VisitedBlobBrokers.Add(OtherBrokerHandle);
				const FFixedVector BlobCentroid = OtherBrokerData->Centroid;
				const FFixedPoint BlobExtent    = OtherBrokerData->FormationRadius;
				FFixedVector ToBlob(BlobCentroid.X - SelfPos.X, BlobCentroid.Y - SelfPos.Y, FFixedPoint::Zero);
				const FFixedPoint BlobDistSq = ToBlob.SizeSquared();
				if (BlobDistSq <= FFixedPoint::Epsilon) continue;
				if (ToBlob.X * Heading.X + ToBlob.Y * Heading.Y <= FFixedPoint::Zero) continue;   // blob behind → ignore
				if (GoalDistSq > FFixedPoint::Zero && BlobDistSq >= GoalDistSq) continue;          // blob past our goal
				const FFixedPoint BlobDist  = SeinMath::Sqrt(BlobDistSq);
				const FFixedPoint BlobRange = (SelfRadius + BlobExtent) * FalloffRadii;
				if (BlobDist >= BlobRange) continue;
				const FFixedPoint BlobFall = FFixedPoint::One - (BlobDist / BlobRange);
				const FFixedPoint BlobMag  = (FFixedPoint::One + HeadOnBase) * BlobFall;
				if (bSelfIsBlob)
				{
					// BLOB-vs-BLOB: two flagged squads sidestep coherently — the shared axis is keyed
					// on the ordered BROKER pair via their centroids, so every member of squad X
					// steers the same world way and squad Y the opposite. (Blob avoidance is its own
					// opt-in; not gated on DoSiDoStrength — the helper is just the direction primitive.)
					const FFixedVector BSteer = ComputeDoSiDoSteer(
						SelfBrokerHandle, OtherBrokerHandle, SelfBrokerCentroid, BlobCentroid);
					Accum.X += BSteer.X * BlobMag;
					Accum.Y += BSteer.Y * BlobMag;
				}
				else
				{
					// UNIT-vs-BLOB: one-sided obstacle avoidance (the blob does not steer for me), so a
					// plain geometric side pick suffices — steer to the side of the blob my heading
					// favours, handle-tiebreak in the dead-ahead band (deterministic).
					const FFixedPoint SideDotB = ToBlob.X * Right.X + ToBlob.Y * Right.Y;
					const FFixedPoint BandB     = SelfRadius / FFixedPoint::FromInt(4);
					// Dead-ahead tiebreak: the operands are intentionally cross-namespace (a unit
					// handle vs a broker handle) — this is only a stable deterministic coin-flip for
					// the (unit, blob) pair, not a spatial relation; a one-sided obstacle has no
					// second observer to stay antisymmetric with.
					const FFixedPoint BlobTurn  = (SideDotB > BandB)  ? -FFixedPoint::One
						: (SideDotB < -BandB) ?  FFixedPoint::One
						: ((SelfHandle < OtherBrokerHandle) ? FFixedPoint::One : -FFixedPoint::One);
					Accum.X += Right.X * (BlobMag * BlobTurn);
					Accum.Y += Right.Y * (BlobMag * BlobTurn);
				}
				continue; // neighbour handled at the blob level
			}

			// BULLDOZE IDLE NEIGHBOURS — WEIGHT-GATED by RESOLVE-THROUGH. Historically a mover got
			// NO dodge around a STATIONARY neighbour: you can't orbit something that isn't moving,
			// and steering around a static cluster curves a transiting unit into a "black hole"
			// orbit — so idlers were left to the collision floor. That anti-orbit rationale is now
			// covered more robustly by the goal-relative bend cap (provable forward progress), so
			// when Idle Resolve is on a QUALIFYING idle neighbour falls through to the weight gate
			// and the geometric side-pick — the mover weaves around it. At strength 0 the
			// short-circuit is byte-identical to the old unconditional continue. Squared compare
			// avoids a per-neighbour sqrt.
			const bool bOtherIdle = OtherMove->Velocity.SizeSquared() <= MovingSpeedFloor * MovingSpeedFloor;
			if (bOtherIdle && !bResolveThroughIdlers) continue;

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

			// STEER DIRECTION. A GENUINE CROSSING (opposed intent + goals apart) resolves via the
			// antisymmetric world-frame do-si-do so the pair slides past on OPPOSITE sides — the
			// primitive that breaks the mirror-dance orbit for opposed pairs and squad-vs-squad
			// traffic. Everything else keeps the geometric side-pick (+ the group-vs-group sidewalk
			// shift for co-directional group lane traffic). The two branches are disjoint predicates.
			// Steer weight is PURE STEERING — NO mass term (mass physics belong to the collision
			// floor). AvoidanceStrength (below) is the magnitude knob; AvoidanceWeight is the priority.
			if (bGenuineCrossing)
			{
				const FFixedVector Steer = ComputeDoSiDoSteer(
					SelfHandle, OtherHandle, SelfPos, OtherEntity->Transform.GetLocation());
				const FFixedPoint W = HeadOn * Falloff * DoSiDoStrength;
				Accum.X += Steer.X * W;
				Accum.Y += Steer.Y * W;
			}
			else
			{
				// Dodge AWAY from the neighbour's side. Inside a "dead-ahead" band the side is
				// undefined → break it deterministically by handle index.
				const FFixedPoint SideDot = ToOther.X * Right.X + ToOther.Y * Right.Y;
				const FFixedPoint LateralBand = SelfRadius / FFixedPoint::FromInt(4);
				FFixedPoint TurnSign;
				if (SideDot > LateralBand)        { TurnSign = -FFixedPoint::One; } // neighbour on right → steer left
				else if (SideDot < -LateralBand)  { TurnSign =  FFixedPoint::One; } // neighbour on left  → steer right
				else { TurnSign = (SelfHandle.Index < OtherHandle.Index) ? FFixedPoint::One : -FFixedPoint::One; }

				// GROUP-VS-GROUP SIDEWALK PASS (non-crossing only): two groups in co-directional /
				// glancing traffic all shift to their own right so they slide past like sidewalk
				// lanes. Opposed crossings are handled by the do-si-do branch above, so this no
				// longer curves head-on pairs the same way (the old orbit cause).
				if (SelfBrokerHandle.IsValid() && OtherBrokerHandle.IsValid())
				{
					TurnSign = FFixedPoint::One;
				}

				// An IDLE neighbour only reaches here when Idle Resolve is on (else it was skipped
				// above); scale its contribution by the resolve strength so mover-resolve firmness
				// is dialable independently. A MOVING neighbour (bOtherIdle false) is unchanged.
				FFixedPoint W = HeadOn * Falloff * TurnSign;
				if (bOtherIdle) W = W * IdleResolveStrength;
				Accum.X += Right.X * W;
				Accum.Y += Right.Y * W;
			}
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
		FFixedPoint InnerTerm = FFixedPoint::One;
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
							InnerTerm = FFixedPoint::One + (CohesionBoost - FFixedPoint::One) * T;
						}
					}
					else if (DevT < -Deadband)
					{
						const FFixedPoint T = (-DevT - Deadband) / Span;         // (0,1]
						InnerTerm = FFixedPoint::One - CohesionHoldBack * T;
					}
				}
			}
		}

		// 3. OUTER COHESION (squads pacing squads). A member's SQUAD position vs the group-of-squads'
		//    mean progress (EQUAL squad weight = mean-of-broker-means), same ramp shape as inner.
		//    Engages ONLY when this member's cohesion group spans >= 2 distinct FLAGGED brokers (a
		//    multi-squad order); single-squad / loose / setting-off all leave OuterTerm == One (→
		//    inner-only, bit-exact). Self-broker must itself be flagged (symmetric: an opted-out squad
		//    is neither paced nor a pacer — inert under the global setting, correct if it ever goes
		//    per-squad). Outer catch-up is NOT progress-gated: a whole squad lagging is a spacing fact.
		FFixedPoint OuterTerm = FFixedPoint::One;
		if (bCohesionEnabled && SelfCohesionId != 0
			&& SelfBrokerData && SelfBrokerData->bPaceSquadsTogether)
		{
			if (const FOuterCohesionAggregate* OAgg = OuterAggregates.Find(SelfCohesionId))
			{
				if (OAgg->DistinctBrokerCount >= 2)
				{
					if (const FCohesionAggregate* SelfAgg = GroupAggregates.Find(SelfBrokerHandle))
					{
						if (SelfAgg->Count >= 1)
						{
							const FFixedPoint SelfBrokerMean = SelfAgg->SumDist / FFixedPoint::FromInt(SelfAgg->Count);
							const FFixedPoint GroupMean = OAgg->SumOfBrokerMeans / FFixedPoint::FromInt(OAgg->DistinctBrokerCount);
							const FFixedPoint Norm = SelfRadius * CohesionRangeRadii;
							FFixedPoint DevO = (SelfBrokerMean - GroupMean) / Norm;   // + = my squad behind, − = ahead
							if (DevO >  FFixedPoint::One) DevO =  FFixedPoint::One;
							if (DevO < -FFixedPoint::One) DevO = -FFixedPoint::One;
							const FFixedPoint Deadband = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(20); // 0.15
							const FFixedPoint Span = FFixedPoint::One - Deadband;
							if (DevO > Deadband)
							{
								const FFixedPoint T = (DevO - Deadband) / Span;
								OuterTerm = FFixedPoint::One + (CohesionBoost - FFixedPoint::One) * T;
							}
							else if (DevO < -Deadband)
							{
								const FFixedPoint T = (-DevO - Deadband) / Span;
								OuterTerm = FFixedPoint::One - CohesionHoldBack * T;
							}
						}
					}
				}
			}
		}

		// QUADRANT COMPOSE (RJ's model), two exact invariants: an inner-LEADER (ahead of its own
		// squad, InnerTerm < 1) NEVER speeds up (clamped ≤ 1); an inner-STRAGGLER (behind, InnerTerm
		// > 1) ALWAYS keeps its full inner catch-up and outer can only ADD, never cancel it (≥
		// InnerTerm — so case 3, squad-ahead, is ignored). NEUTRAL-inner takes the pure outer term.
		// Together: a squad's leaders can never outrun its own stragglers to chase macro pacing.
		FFixedPoint CohesionTerm;
		if (InnerTerm > FFixedPoint::One)
		{
			const FFixedPoint OuterAdd = (OuterTerm > FFixedPoint::One) ? OuterTerm : FFixedPoint::One;
			CohesionTerm = InnerTerm * OuterAdd;                                   // straggler: outer adds only
		}
		else if (InnerTerm < FFixedPoint::One)
		{
			const FFixedPoint Prod = InnerTerm * OuterTerm;
			CohesionTerm = (Prod < FFixedPoint::One) ? Prod : FFixedPoint::One;    // leader: never > 1
		}
		else
		{
			CohesionTerm = OuterTerm;                                              // neutral: pure outer
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
