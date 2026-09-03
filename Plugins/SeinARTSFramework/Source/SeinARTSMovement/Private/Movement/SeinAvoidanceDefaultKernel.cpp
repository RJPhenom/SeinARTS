/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAvoidanceDefaultKernel.cpp
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       25 Aug 2026
 * @brief        Implements the shipped deterministic local-avoidance kernel.
 *
 *               The kernel reads the immutable start-of-tick broadphase and
 *               component snapshot, writes only each mover's own deferred
 *               avoidance state in parallel, then publishes real mutations
 *               serially in canonical handle order. The collision resolver
 *               remains responsible for hard no-overlap behavior.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Movement/SeinAvoidanceDefaultKernel.h"

#include "Movement/SeinAvoidanceDefault.h"

#include "Core/SeinParallel.h"
#include "Math/MathLib.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Components/SeinExtentsPayload.h"
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
	constexpr int32 GapCandidateCount = 9;
	constexpr int32 MaxIdleBlockers = 24;

	struct FCohesionAggregate
	{
		int32 Count = 0;
		FFixedPoint SumDist;
		int64 CohesionGroupId = 0;
		bool bPaceSquads = false;
		bool bStamped = false;
	};

	struct FOuterCohesionAggregate
	{
		int32 DistinctBrokerCount = 0;
		FFixedPoint SumOfBrokerMeans;
	};

	struct FDeferredMovementState
	{
		FFixedVector PrevTickLocation;
		FSeinAvoidanceOutput AvoidanceOutput;
	};

	struct FAvoidanceTickState
	{
		TMap<FSeinEntityHandle, FCohesionAggregate> GroupAggregates;
		TMap<int64, FOuterCohesionAggregate> OuterAggregates;
		TArray<FSeinEntityHandle> LiveHandles;
		TArray<FFixedPoint> ActualProgress;
		TArray<FDeferredMovementState> PreviousMovementState;
	};

	struct FAvoidanceOutputParameters
	{
		const TMap<FSeinEntityHandle, FCohesionAggregate>& GroupAggregates;
		const TMap<int64, FOuterCohesionAggregate>& OuterAggregates;
		const TArray<FFixedPoint>& ActualProgress;
		bool bCohesionEnabled = false;
		FFixedPoint ProgressUnknown;
		FFixedPoint FloorPerTick;
		FFixedPoint MovingSpeedFloor;
		FFixedPoint MaxSteerMagnitude;
		FFixedPoint SmoothKeep;
		FFixedPoint BrakeStrength;
		FFixedPoint CohesionHoldBack;
		FFixedPoint CohesionBoost;
		FFixedPoint CohesionRangeRadii;
	};

	struct FIdleBlockerSet
	{
		FFixedVector Directions[MaxIdleBlockers];
		FFixedPoint CosHalfAngles[MaxIdleBlockers];
		FFixedPoint Detours[MaxIdleBlockers];
		int32 Count = 0;
	};

	struct FAvoidanceNeighborParameters
	{
		USeinWorldSubsystem& World;
		const ISeinComponentStorage* MoveStorage = nullptr;
		const ISeinComponentStorage* NavStorage = nullptr;
		const ISeinComponentStorage* ExtentsStorage = nullptr;
		const ISeinComponentStorage* BrokerStorage = nullptr;
		const ISeinComponentStorage* BrokerDataStorage = nullptr;
		bool bDoSiDoEnabled = false;
		bool bResolveThroughIdlers = false;
		FFixedPoint MovingSpeedFloor;
		FFixedPoint FalloffRadii;
		FFixedPoint HeadOnBase;
		FFixedPoint DoSiDoStrength;
		FFixedPoint CrossingGoalDivergenceSquared;
	};

	struct FMoverAvoidanceSnapshot
	{
		FSeinEntityHandle Handle;
		FSeinEntityHandle BrokerHandle;
		int64 CohesionId = 0;
		FSeinMovementPayload& Movement;
		const FSeinCommandBrokerData* BrokerData = nullptr;
		bool bIsBlob = false;
		FFixedVector BrokerCentroid;
		FFixedVector Position;
		FFixedVector ToGoal;
		FFixedPoint GoalDistanceSquared;
		FFixedVector Heading;
		FFixedVector Right;
		FFixedVector Velocity;
		FFixedPoint Radius;
	};

	enum class ENeighborSkipReason : uint8
	{
		None,
		Idle,
		Weight,
		ZeroDist,
		Behind,
		NotClosing,
		PastGoal,
		ZeroRadius,
		Far,
	};

	struct FNeighborGateOutput
	{
		FFixedVector ToOther;
		FFixedPoint DistanceSquared;
		FFixedPoint Distance;
		FFixedPoint OtherRadius;
		FFixedPoint FalloffRange;
		FFixedPoint Falloff;
		bool bOtherIdle = false;
	};

	static ENeighborSkipReason ClassifyIndividualNeighbor(
		const FFixedVector& SelfPosition,
		const FFixedVector& SelfHeading,
		const FFixedVector& SelfVelocity,
		FFixedPoint SelfGoalDistanceSquared,
		FFixedPoint SelfRadius,
		int32 SelfAvoidanceWeight,
		bool bSelfAvoidSameWeights,
		bool bResolveThroughIdlers,
		FFixedPoint FalloffRadii,
		const ISeinComponentStorage* NavStorage,
		const ISeinComponentStorage* ExtentsStorage,
		FSeinEntityHandle OtherHandle,
		const FSeinEntity& OtherEntity,
		const FSeinMovementPayload& OtherMovement,
		FNeighborGateOutput& Out)
	{
		Out.bOtherIdle = !OtherMovement.bHasTarget;
		if (Out.bOtherIdle && !bResolveThroughIdlers)
		{
			return ENeighborSkipReason::Idle;
		}
		const bool bQualifies = bSelfAvoidSameWeights
			? OtherMovement.AvoidanceWeight >= SelfAvoidanceWeight
			: OtherMovement.AvoidanceWeight > SelfAvoidanceWeight;
		if (!bQualifies)
		{
			return ENeighborSkipReason::Weight;
		}
		Out.ToOther = OtherEntity.Transform.GetLocation() - SelfPosition;
		Out.ToOther.Z = FFixedPoint::Zero;
		Out.DistanceSquared = Out.ToOther.SizeSquared();
		if (Out.DistanceSquared <= FFixedPoint::Epsilon)
		{
			return ENeighborSkipReason::ZeroDist;
		}
		if (Out.ToOther.X * SelfHeading.X + Out.ToOther.Y * SelfHeading.Y
			<= FFixedPoint::Zero)
		{
			return ENeighborSkipReason::Behind;
		}
		const FFixedVector RelativeVelocity(
			SelfVelocity.X - OtherMovement.Velocity.X,
			SelfVelocity.Y - OtherMovement.Velocity.Y,
			FFixedPoint::Zero);
		if (Out.ToOther.X * RelativeVelocity.X
				+ Out.ToOther.Y * RelativeVelocity.Y
			<= FFixedPoint::Zero)
		{
			return ENeighborSkipReason::NotClosing;
		}
		if (SelfGoalDistanceSquared > FFixedPoint::Zero
			&& Out.DistanceSquared >= SelfGoalDistanceSquared)
		{
			return ENeighborSkipReason::PastGoal;
		}
		const FSeinNavigationPayload* OtherNavigation = NavStorage
			? static_cast<const FSeinNavigationPayload*>(
				NavStorage->GetComponentRaw(OtherHandle))
			: nullptr;
		const FSeinExtentsPayload* OtherExtents = ExtentsStorage
			? static_cast<const FSeinExtentsPayload*>(
				ExtentsStorage->GetComponentRaw(OtherHandle))
			: nullptr;
		Out.OtherRadius = USeinMovement::ResolveCollisionRadius(
			OtherExtents, OtherNavigation);
		if (Out.OtherRadius <= FFixedPoint::Zero)
		{
			return ENeighborSkipReason::ZeroRadius;
		}
		Out.Distance = SeinMath::Sqrt(Out.DistanceSquared);
		Out.FalloffRange = (SelfRadius + Out.OtherRadius) * FalloffRadii;
		if (Out.Distance >= Out.FalloffRange)
		{
			return ENeighborSkipReason::Far;
		}
		Out.Falloff = FFixedPoint::One - (Out.Distance / Out.FalloffRange);
		return ENeighborSkipReason::None;
	}

	struct FAvoidanceWorkerParameters
	{
		USeinWorldSubsystem& World;
		const FSeinCollisionSpatialHash& Hash;
		const TArray<FSeinEntityHandle>& LiveHandles;
		ISeinComponentStorage* MoveStorage = nullptr;
		const ISeinComponentStorage* ReadOnlyMoveStorage = nullptr;
		const ISeinComponentStorage* NavStorage = nullptr;
		const ISeinComponentStorage* ExtentsStorage = nullptr;
		const ISeinComponentStorage* BrokerStorage = nullptr;
		const ISeinComponentStorage* BrokerDataStorage = nullptr;
		const FAvoidanceNeighborParameters& NeighborParameters;
		const FAvoidanceOutputParameters& OutputParameters;
		const FFixedPoint* GapCos = nullptr;
		const FFixedPoint* GapSin = nullptr;
		bool bIdleDodgeEnabled = false;
		FFixedPoint MovingSpeedFloor;
		FFixedPoint FalloffRadii;
		FFixedPoint SmoothKeep;
		FFixedPoint ArrivalReleaseRadii;
		FFixedPoint ArrivalFadeInnerRadii;
		FFixedPoint MaxSteerMagnitude;
		FFixedPoint IdleResolveStrength;
		FFixedPoint IdleDodgeStrength;
		FFixedPoint BendCapCos;
		FFixedPoint LookaheadSeconds;
	};

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

	static void ClearAvoidanceOutput(FSeinMovementPayload& Movement)
	{
		Movement.AvoidanceOutput.SteerDir = FFixedVector::ZeroVector;
		Movement.AvoidanceOutput.SpeedScale = FFixedPoint::One;
	}

	static void ComputeIdleDodge(
		USeinWorldSubsystem& World,
		const FSeinCollisionSpatialHash& Hash,
		const ISeinComponentStorage* MoveStorage,
		const ISeinComponentStorage* NavStorage,
		const ISeinComponentStorage* ExtentsStorage,
		FSeinEntityHandle SelfHandle,
		const FSeinEntity& SelfEntity,
		FSeinMovementPayload& Move,
		bool bIdleDodgeEnabled,
		FFixedPoint MovingSpeedFloor,
		FFixedPoint FalloffRadii,
		FFixedPoint MaxSteerMagnitude,
		FFixedPoint IdleDodgeStrength,
		FFixedPoint SmoothKeep)
	{
		if (!bIdleDodgeEnabled)
		{
			ClearAvoidanceOutput(Move);
			return;
		}

		const FSeinNavigationPayload* SelfNavigation = NavStorage
			? static_cast<const FSeinNavigationPayload*>(
				NavStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FSeinExtentsPayload* SelfExtents = ExtentsStorage
			? static_cast<const FSeinExtentsPayload*>(
				ExtentsStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FFixedPoint SelfRadius = USeinMovement::ResolveCollisionRadius(
			SelfExtents, SelfNavigation);
		if (SelfRadius <= FFixedPoint::Zero)
		{
			ClearAvoidanceOutput(Move);
			return;
		}

		const FFixedVector SelfPosition = SelfEntity.Transform.GetLocation();
		const FFixedPoint Perception =
			SelfRadius * FFixedPoint::FromInt(2);
		TArray<FSeinEntityHandle> Neighbors;
		Hash.QueryRadius(SelfPosition, Perception, Neighbors, SelfHandle);

		FFixedVector DodgeAccum = FFixedVector::ZeroVector;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity =
				World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;
			const FSeinMovementPayload* OtherMove = MoveStorage
				? static_cast<const FSeinMovementPayload*>(
					MoveStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			if (!OtherMove || !OtherMove->bHasTarget) continue;
			const FFixedVector OtherVelocity = OtherMove->Velocity;
			if (OtherVelocity.SizeSquared()
				<= MovingSpeedFloor * MovingSpeedFloor)
			{
				continue;
			}
			const bool bQualifies = Move.bAvoidSameWeights
				? OtherMove->AvoidanceWeight >= Move.AvoidanceWeight
				: OtherMove->AvoidanceWeight > Move.AvoidanceWeight;
			if (!bQualifies) continue;

			FFixedVector ToSelf =
				SelfPosition - OtherEntity->Transform.GetLocation();
			ToSelf.Z = FFixedPoint::Zero;
			if (ToSelf.X * OtherVelocity.X + ToSelf.Y * OtherVelocity.Y
				<= FFixedPoint::Zero)
			{
				continue;
			}
			const FSeinNavigationPayload* OtherNavigation = NavStorage
				? static_cast<const FSeinNavigationPayload*>(
					NavStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const FSeinExtentsPayload* OtherExtents = ExtentsStorage
				? static_cast<const FSeinExtentsPayload*>(
					ExtentsStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const FFixedPoint OtherRadius =
				USeinMovement::ResolveCollisionRadius(
					OtherExtents, OtherNavigation);
			if (OtherRadius <= FFixedPoint::Zero) continue;
			const FFixedPoint DodgeRange =
				(SelfRadius + OtherRadius) * FalloffRadii;
			if (DodgeRange <= FFixedPoint::Epsilon) continue;
			const FFixedPoint Distance = SeinMath::Sqrt(ToSelf.SizeSquared());
			if (Distance >= DodgeRange) continue;
			const FFixedPoint Falloff =
				FFixedPoint::One - (Distance / DodgeRange);

			const FFixedPoint OtherSpeed = OtherVelocity.Size();
			const FFixedVector OtherHeading(
				OtherVelocity.X / OtherSpeed,
				OtherVelocity.Y / OtherSpeed,
				FFixedPoint::Zero);
			const FFixedVector MoverRight(
				OtherHeading.Y, -OtherHeading.X, FFixedPoint::Zero);
			const FFixedPoint SideDot =
				ToSelf.X * MoverRight.X + ToSelf.Y * MoverRight.Y;
			const FFixedPoint Band = SelfRadius / FFixedPoint::FromInt(4);
			const FFixedPoint TurnSign = SideDot > Band
				? FFixedPoint::One
				: SideDot < -Band
					? -FFixedPoint::One
					: SelfHandle.Index < OtherHandle.Index
						? FFixedPoint::One
						: -FFixedPoint::One;
			DodgeAccum.X += MoverRight.X * (Falloff * TurnSign);
			DodgeAccum.Y += MoverRight.Y * (Falloff * TurnSign);
		}

		if (DodgeAccum.SizeSquared() <= FFixedPoint::Epsilon)
		{
			ClearAvoidanceOutput(Move);
			return;
		}

		const FFixedPoint DodgeLength = DodgeAccum.Size();
		if (DodgeLength > MaxSteerMagnitude
			&& DodgeLength > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale = MaxSteerMagnitude / DodgeLength;
			DodgeAccum.X = DodgeAccum.X * Scale;
			DodgeAccum.Y = DodgeAccum.Y * Scale;
		}
		const FFixedVector DodgeScaled(
			DodgeAccum.X * IdleDodgeStrength,
			DodgeAccum.Y * IdleDodgeStrength,
			FFixedPoint::Zero);
		FFixedVector DodgeSmoothed(
			DodgeScaled.X * (FFixedPoint::One - SmoothKeep)
				+ Move.AvoidanceOutput.SteerDir.X * SmoothKeep,
			DodgeScaled.Y * (FFixedPoint::One - SmoothKeep)
				+ Move.AvoidanceOutput.SteerDir.Y * SmoothKeep,
			FFixedPoint::Zero);
		if (DodgeSmoothed.SizeSquared() <= FFixedPoint::Epsilon)
		{
			DodgeSmoothed = FFixedVector::ZeroVector;
		}
		Move.AvoidanceOutput.SteerDir = DodgeSmoothed;
		Move.AvoidanceOutput.SpeedScale = FFixedPoint::One;
	}

#if !UE_BUILD_SHIPPING
	static void ReportPinnedMover(
		USeinWorldSubsystem& World,
		const FSeinCollisionSpatialHash& Hash,
		const ISeinComponentStorage* MoveStorage,
		const ISeinComponentStorage* NavStorage,
		const ISeinComponentStorage* ExtentsStorage,
		const ISeinComponentStorage* BrokerStorage,
		FSeinEntityHandle SelfHandle,
		const FSeinEntity& SelfEntity,
		const FSeinMovementPayload& Move,
		const FFixedVector& Velocity,
		FFixedPoint MovingSpeedFloor,
		FFixedPoint FalloffRadii,
		bool bResolveThroughIdlers)
	{
		if (!Move.bHasTarget
			|| !UE_LOG_ACTIVE(LogSeinAvoidance, Verbose)
			|| (World.GetCurrentTick() % 15) != 0)
		{
			return;
		}

		const FFixedVector Position = SelfEntity.Transform.GetLocation();
		FFixedVector ToGoal = Move.TargetLocation - Position;
		ToGoal.Z = FFixedPoint::Zero;
		const FFixedPoint GoalDistance = ToGoal.Size();
		FFixedVector Heading = FFixedVector::ZeroVector;
		if (GoalDistance > FFixedPoint::Epsilon)
		{
			Heading = FFixedVector(
				ToGoal.X / GoalDistance,
				ToGoal.Y / GoalDistance,
				FFixedPoint::Zero);
		}
		const FSeinNavigationPayload* Navigation = NavStorage
			? static_cast<const FSeinNavigationPayload*>(
				NavStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FSeinExtentsPayload* Extents = ExtentsStorage
			? static_cast<const FSeinExtentsPayload*>(
				ExtentsStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		FFixedPoint Radius =
			USeinMovement::ResolveCollisionRadius(Extents, Navigation);
		if (Radius <= FFixedPoint::Zero)
		{
			Radius = FFixedPoint::FromInt(50);
		}
		const FSeinBrokerMembershipData* Broker = BrokerStorage
			? static_cast<const FSeinBrokerMembershipData*>(
				BrokerStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FSeinEntityHandle BrokerHandle = Broker
			? Broker->CurrentBrokerHandle
			: FSeinEntityHandle();
		const int64 CohesionId = Broker ? Broker->CohesionGroupId : 0;

		TArray<FSeinEntityHandle> Neighbors;
		Hash.QueryRadius(
			Position, Radius * FFixedPoint::FromInt(4),
			Neighbors, SelfHandle);
		int32 Kept = 0;
		int32 Static = 0;
		int32 Group = 0;
		int32 Idle = 0;
		int32 Weight = 0;
		int32 Behind = 0;
		int32 NotClosing = 0;
		int32 PastGoal = 0;
		int32 Far = 0;
		FFixedPoint MinDistanceSquared = FFixedPoint::FromInt(999999);
		const FFixedPoint GoalDistanceSquared = GoalDistance * GoalDistance;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity =
				World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;
			const FSeinMovementPayload* OtherMove = MoveStorage
				? static_cast<const FSeinMovementPayload*>(
					MoveStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			if (!OtherMove)
			{
				++Static;
				continue;
			}
			FFixedVector ToOther =
				OtherEntity->Transform.GetLocation() - Position;
			ToOther.Z = FFixedPoint::Zero;
			const FFixedPoint DistanceSquared = ToOther.SizeSquared();
			if (DistanceSquared < MinDistanceSquared)
			{
				MinDistanceSquared = DistanceSquared;
			}
			const FSeinBrokerMembershipData* OtherBroker = BrokerStorage
				? static_cast<const FSeinBrokerMembershipData*>(
					BrokerStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const bool bSameBroker = BrokerHandle.IsValid()
				&& OtherBroker
				&& OtherBroker->CurrentBrokerHandle == BrokerHandle;
			const bool bSameCohesion = CohesionId != 0
				&& OtherBroker
				&& OtherBroker->CohesionGroupId == CohesionId;
			if (bSameBroker || bSameCohesion)
			{
				++Group;
				continue;
			}

			FNeighborGateOutput Gate;
			const ENeighborSkipReason Reason = ClassifyIndividualNeighbor(
				Position, Heading, Velocity,
				GoalDistanceSquared, Radius,
				Move.AvoidanceWeight, Move.bAvoidSameWeights,
				bResolveThroughIdlers, FalloffRadii,
				NavStorage, ExtentsStorage,
				OtherHandle, *OtherEntity, *OtherMove, Gate);
			switch (Reason)
			{
			case ENeighborSkipReason::Idle:       ++Idle;       continue;
			case ENeighborSkipReason::Weight:     ++Weight;     continue;
			case ENeighborSkipReason::ZeroDist:   ++Far;        continue;
			case ENeighborSkipReason::Behind:     ++Behind;     continue;
			case ENeighborSkipReason::NotClosing:  ++NotClosing; continue;
			case ENeighborSkipReason::PastGoal:   ++PastGoal;   continue;
			case ENeighborSkipReason::ZeroRadius:
			case ENeighborSkipReason::Far:        ++Far;        continue;
			case ENeighborSkipReason::None:       break;
			}
			++Kept;
		}

		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("[GRIND] t=%d h=%d:%d grp=%d:%d coh=%lld goalDist=%.0f tgt=(%.0f,%.0f) nbrs=%d kept=%d skip{static=%d grp=%d idle=%d wt=%d behind=%d notClosing=%d pastGoal=%d far=%d} minD=%.0f steer=%.3f scale=%.3f"),
			World.GetCurrentTick(), SelfHandle.Index, SelfHandle.Generation,
			BrokerHandle.Index, BrokerHandle.Generation, CohesionId,
			GoalDistance.ToFloat(),
			Move.TargetLocation.X.ToFloat(), Move.TargetLocation.Y.ToFloat(),
			Neighbors.Num(), Kept,
			Static, Group, Idle, Weight, Behind,
			NotClosing, PastGoal, Far,
			SeinMath::Sqrt(MinDistanceSquared).ToFloat(),
			Move.AvoidanceOutput.SteerDir.Size().ToFloat(),
			Move.AvoidanceOutput.SpeedScale.ToFloat());
	}
#endif

	static void FinalizeMovingOutput(
		const FAvoidanceOutputParameters& Parameters,
		int32 Index,
		FSeinMovementPayload& Movement,
		FFixedVector Accum,
		FFixedPoint ArrivalFade,
		FFixedPoint SelfRadius,
		FFixedPoint GoalDistanceSquared,
		FSeinEntityHandle SelfBrokerHandle,
		int64 SelfCohesionId,
		const FSeinCommandBrokerData* SelfBrokerData)
	{
		FFixedPoint AccumLength = Accum.Size();
		if (AccumLength > Parameters.MaxSteerMagnitude
			&& AccumLength > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale =
				Parameters.MaxSteerMagnitude / AccumLength;
			Accum.X = Accum.X * Scale;
			Accum.Y = Accum.Y * Scale;
			AccumLength = Parameters.MaxSteerMagnitude;
		}
		// Fade the capped response, not the uncapped neighbour sum. Dense crowds
		// commonly saturate MaxSteerMagnitude; scaling before the cap would leave
		// them pinned at full steering through most of the arrival band.
		if (ArrivalFade < FFixedPoint::One)
		{
			Accum.X = Accum.X * ArrivalFade;
			Accum.Y = Accum.Y * ArrivalFade;
			AccumLength = AccumLength * ArrivalFade;
		}

		const FFixedVector Scaled(
			Accum.X * Movement.AvoidanceStrength,
			Accum.Y * Movement.AvoidanceStrength,
			FFixedPoint::Zero);
		FFixedVector Smoothed(
			Scaled.X * (FFixedPoint::One - Parameters.SmoothKeep)
				+ Movement.AvoidanceOutput.SteerDir.X
					* Parameters.SmoothKeep,
			Scaled.Y * (FFixedPoint::One - Parameters.SmoothKeep)
				+ Movement.AvoidanceOutput.SteerDir.Y
					* Parameters.SmoothKeep,
			FFixedPoint::Zero);
		const bool bSteerCleared =
			Smoothed.SizeSquared() <= FFixedPoint::Epsilon;
		if (bSteerCleared)
		{
			Smoothed = FFixedVector::ZeroVector;
		}
		Movement.AvoidanceOutput.SteerDir = Smoothed;

		FFixedPoint BrakeTerm = FFixedPoint::One;
		if (Parameters.BrakeStrength > FFixedPoint::Zero
			&& Parameters.MaxSteerMagnitude > FFixedPoint::Zero
			&& !bSteerCleared)
		{
			FFixedPoint Yield =
				(AccumLength * Movement.AvoidanceStrength)
				/ Parameters.MaxSteerMagnitude;
			if (Yield > FFixedPoint::One)
			{
				Yield = FFixedPoint::One;
			}
			BrakeTerm =
				FFixedPoint::One - Parameters.BrakeStrength * Yield;
		}

		// Inner cohesion compares one member against its immediate broker.
		FFixedPoint InnerTerm = FFixedPoint::One;
		if (Parameters.bCohesionEnabled && SelfBrokerHandle.IsValid())
		{
			if (const FCohesionAggregate* Aggregate =
				Parameters.GroupAggregates.Find(SelfBrokerHandle))
			{
				if (Aggregate->Count >= 2)
				{
					const FFixedPoint Mean = Aggregate->SumDist
						/ FFixedPoint::FromInt(Aggregate->Count);
					const FFixedPoint SelfDistance =
						SeinMath::Sqrt(GoalDistanceSquared);
					const FFixedPoint Normalization =
						SelfRadius * Parameters.CohesionRangeRadii;
					FFixedPoint Deviation =
						(SelfDistance - Mean) / Normalization;
					if (Deviation > FFixedPoint::One)
					{
						Deviation = FFixedPoint::One;
					}
					if (Deviation < -FFixedPoint::One)
					{
						Deviation = -FFixedPoint::One;
					}
					const FFixedPoint Deadband =
						FFixedPoint::FromInt(3) / FFixedPoint::FromInt(20);
					const FFixedPoint Span = FFixedPoint::One - Deadband;
					if (Deviation > Deadband)
					{
						const FFixedPoint SelfProgress =
							Parameters.ActualProgress[Index];
						const bool bMakingHeadway =
							SelfProgress == Parameters.ProgressUnknown
							|| SelfProgress > Parameters.FloorPerTick;
						if (bMakingHeadway)
						{
							const FFixedPoint T =
								(Deviation - Deadband) / Span;
							InnerTerm = FFixedPoint::One
								+ (Parameters.CohesionBoost
									- FFixedPoint::One) * T;
						}
					}
					else if (Deviation < -Deadband)
					{
						const FFixedPoint T =
							(-Deviation - Deadband) / Span;
						InnerTerm = FFixedPoint::One
							- Parameters.CohesionHoldBack * T;
					}
				}
			}
		}

		// Outer cohesion compares the broker against the multi-broker order.
		FFixedPoint OuterTerm = FFixedPoint::One;
		if (Parameters.bCohesionEnabled
			&& SelfCohesionId != 0
			&& SelfBrokerData
			&& SelfBrokerData->bPaceSquadsTogether)
		{
			if (const FOuterCohesionAggregate* OuterAggregate =
				Parameters.OuterAggregates.Find(SelfCohesionId))
			{
				if (OuterAggregate->DistinctBrokerCount >= 2)
				{
					if (const FCohesionAggregate* SelfAggregate =
						Parameters.GroupAggregates.Find(SelfBrokerHandle))
					{
						if (SelfAggregate->Count >= 1)
						{
							const FFixedPoint SelfBrokerMean =
								SelfAggregate->SumDist
								/ FFixedPoint::FromInt(SelfAggregate->Count);
							const FFixedPoint GroupMean =
								OuterAggregate->SumOfBrokerMeans
								/ FFixedPoint::FromInt(
									OuterAggregate->DistinctBrokerCount);
							const FFixedPoint Normalization =
								SelfRadius * Parameters.CohesionRangeRadii;
							FFixedPoint Deviation =
								(SelfBrokerMean - GroupMean) / Normalization;
							if (Deviation > FFixedPoint::One)
							{
								Deviation = FFixedPoint::One;
							}
							if (Deviation < -FFixedPoint::One)
							{
								Deviation = -FFixedPoint::One;
							}
							const FFixedPoint Deadband =
								FFixedPoint::FromInt(3)
								/ FFixedPoint::FromInt(20);
							const FFixedPoint Span = FFixedPoint::One - Deadband;
							if (Deviation > Deadband)
							{
								const FFixedPoint T =
									(Deviation - Deadband) / Span;
								OuterTerm = FFixedPoint::One
									+ (Parameters.CohesionBoost
										- FFixedPoint::One) * T;
							}
							else if (Deviation < -Deadband)
							{
								const FFixedPoint T =
									(-Deviation - Deadband) / Span;
								OuterTerm = FFixedPoint::One
									- Parameters.CohesionHoldBack * T;
							}
						}
					}
				}
			}
		}

		FFixedPoint CohesionTerm;
		if (InnerTerm > FFixedPoint::One)
		{
			const FFixedPoint OuterAdd = OuterTerm > FFixedPoint::One
				? OuterTerm
				: FFixedPoint::One;
			CohesionTerm = InnerTerm * OuterAdd;
		}
		else if (InnerTerm < FFixedPoint::One)
		{
			const FFixedPoint Product = InnerTerm * OuterTerm;
			CohesionTerm = Product < FFixedPoint::One
				? Product
				: FFixedPoint::One;
		}
		else
		{
			CohesionTerm = OuterTerm;
		}

		const FFixedPoint TargetScale = BrakeTerm * CohesionTerm;
		FFixedPoint SmoothedScale =
			TargetScale * (FFixedPoint::One - Parameters.SmoothKeep)
			+ Movement.AvoidanceOutput.SpeedScale * Parameters.SmoothKeep;
		FFixedPoint OneDelta = FFixedPoint::One - SmoothedScale;
		if (OneDelta < FFixedPoint::Zero)
		{
			OneDelta = -OneDelta;
		}
		if (OneDelta <= FFixedPoint::Epsilon)
		{
			SmoothedScale = FFixedPoint::One;
		}
		if (SmoothedScale < FFixedPoint::Zero)
		{
			SmoothedScale = FFixedPoint::Zero;
		}

		if (SmoothedScale < FFixedPoint::One
			&& Movement.TopSpeed > FFixedPoint::Zero)
		{
			FFixedPoint MinimumScale =
				(Parameters.MovingSpeedFloor * FFixedPoint::FromInt(2))
				/ Movement.TopSpeed;
			if (MinimumScale > FFixedPoint::One)
			{
				MinimumScale = FFixedPoint::One;
			}
			if (SmoothedScale < MinimumScale)
			{
				SmoothedScale = MinimumScale;
			}
		}
		Movement.AvoidanceOutput.SpeedScale = SmoothedScale;
	}

	static void ResolveIdleGap(
		const FIdleBlockerSet& Blockers,
		const FFixedVector& Heading,
		const FFixedVector& Right,
		const FFixedVector& ToGoal,
		FFixedPoint GoalDistanceSquared,
		FFixedPoint BendCapCos,
		const FFixedPoint* GapCos,
		const FFixedPoint* GapSin,
		FFixedPoint IdleResolveStrength,
		FFixedVector& InOutAccum)
	{
		if (Blockers.Count <= 0) return;

		const FFixedPoint InverseGoalDistance =
			FFixedPoint::One / SeinMath::Sqrt(GoalDistanceSquared);
		const FFixedVector GoalDirection(
			ToGoal.X * InverseGoalDistance,
			ToGoal.Y * InverseGoalDistance,
			FFixedPoint::Zero);
		const FFixedVector LeftPerpendicular(
			-Heading.Y, Heading.X, FFixedPoint::Zero);
		const bool bWedgeEnabled = BendCapCos > -FFixedPoint::One;

		bool bGapFound = false;
		FFixedVector GapDirection = Heading;
		FFixedPoint BestGoalDot = FFixedPoint::MinValue;
		for (int32 CandidateIndex = 0;
			CandidateIndex < GapCandidateCount && !bGapFound;
			++CandidateIndex)
		{
			if (bWedgeEnabled && GapCos[CandidateIndex] < BendCapCos)
			{
				break;
			}
			const int32 SideCount = CandidateIndex == 0 ? 1 : 2;
			for (int32 SideIndex = 0; SideIndex < SideCount; ++SideIndex)
			{
				const FFixedPoint Sign = SideIndex == 0
					? FFixedPoint::One
					: -FFixedPoint::One;
				const FFixedVector Candidate(
					Heading.X * GapCos[CandidateIndex]
						+ LeftPerpendicular.X
							* (GapSin[CandidateIndex] * Sign),
					Heading.Y * GapCos[CandidateIndex]
						+ LeftPerpendicular.Y
							* (GapSin[CandidateIndex] * Sign),
					FFixedPoint::Zero);
				bool bBlocked = false;
				for (int32 BlockerIndex = 0;
					BlockerIndex < Blockers.Count;
					++BlockerIndex)
				{
					if (Candidate.X * Blockers.Directions[BlockerIndex].X
							+ Candidate.Y * Blockers.Directions[BlockerIndex].Y
						> Blockers.CosHalfAngles[BlockerIndex])
					{
						bBlocked = true;
						break;
					}
				}
				if (bBlocked) continue;

				const FFixedPoint GoalDot =
					Candidate.X * GoalDirection.X
					+ Candidate.Y * GoalDirection.Y;
				if (!bGapFound || GoalDot > BestGoalDot)
				{
					BestGoalDot = GoalDot;
					GapDirection = Candidate;
					bGapFound = true;
				}
			}
		}

		if (bGapFound)
		{
			const FFixedPoint LateralComponent =
				GapDirection.X * Right.X + GapDirection.Y * Right.Y;
			InOutAccum.X += Right.X
				* (LateralComponent * IdleResolveStrength);
			InOutAccum.Y += Right.Y
				* (LateralComponent * IdleResolveStrength);
			return;
		}

		FFixedPoint DetourSum = FFixedPoint::Zero;
		for (int32 BlockerIndex = 0;
			BlockerIndex < Blockers.Count;
			++BlockerIndex)
		{
			DetourSum += Blockers.Detours[BlockerIndex];
		}
		InOutAccum.X += Right.X * (DetourSum * IdleResolveStrength);
		InOutAccum.Y += Right.Y * (DetourSum * IdleResolveStrength);
	}

	static bool IsGenuineCrossing(
		const FAvoidanceNeighborParameters& Parameters,
		const FMoverAvoidanceSnapshot& Self,
		const FSeinEntity& OtherEntity,
		const FSeinMovementPayload& OtherMovement)
	{
		if (!Parameters.bDoSiDoEnabled || !OtherMovement.bHasTarget)
		{
			return false;
		}

		FFixedVector OtherToGoal = OtherMovement.TargetLocation
			- OtherEntity.Transform.GetLocation();
		OtherToGoal.Z = FFixedPoint::Zero;
		FFixedVector ToOtherEarly =
			OtherEntity.Transform.GetLocation() - Self.Position;
		ToOtherEarly.Z = FFixedPoint::Zero;
		const FFixedPoint BodySeparationSquared =
			ToOtherEarly.SizeSquared();
		const FFixedVector RelativeVelocityEarly(
			Self.Velocity.X - OtherMovement.Velocity.X,
			Self.Velocity.Y - OtherMovement.Velocity.Y,
			FFixedPoint::Zero);
		const FFixedPoint ClosingEarly =
			ToOtherEarly.X * RelativeVelocityEarly.X
			+ ToOtherEarly.Y * RelativeVelocityEarly.Y;
		const FFixedPoint IntentDot =
			Self.ToGoal.X * OtherToGoal.X
			+ Self.ToGoal.Y * OtherToGoal.Y;
		FFixedVector GoalSeparation =
			OtherMovement.TargetLocation - Self.Movement.TargetLocation;
		GoalSeparation.Z = FFixedPoint::Zero;
		const FFixedPoint GoalSeparationSquared =
			GoalSeparation.SizeSquared();
		return ClosingEarly > FFixedPoint::Zero
			&& IntentDot < FFixedPoint::Zero
			&& GoalSeparationSquared
				> Parameters.CrossingGoalDivergenceSquared
					* BodySeparationSquared;
	}

	static void AccumulateSameGroupCrossing(
		const FAvoidanceNeighborParameters& Parameters,
		const FMoverAvoidanceSnapshot& Self,
		FSeinEntityHandle OtherHandle,
		const FSeinEntity& OtherEntity,
		const FSeinMovementPayload& OtherMovement,
		bool bGenuineCrossing,
		FFixedVector& OutAccum)
	{
		if (!bGenuineCrossing
			|| OtherMovement.Velocity.SizeSquared()
				<= Parameters.MovingSpeedFloor
					* Parameters.MovingSpeedFloor)
		{
			return;
		}
		FFixedVector ToOther =
			OtherEntity.Transform.GetLocation() - Self.Position;
		ToOther.Z = FFixedPoint::Zero;
		const FFixedPoint DistanceSquared = ToOther.SizeSquared();
		if (DistanceSquared <= FFixedPoint::Epsilon) return;
		const FSeinNavigationPayload* Navigation = Parameters.NavStorage
			? static_cast<const FSeinNavigationPayload*>(
				Parameters.NavStorage->GetComponentRaw(OtherHandle))
			: nullptr;
		const FSeinExtentsPayload* Extents = Parameters.ExtentsStorage
			? static_cast<const FSeinExtentsPayload*>(
				Parameters.ExtentsStorage->GetComponentRaw(OtherHandle))
			: nullptr;
		const FFixedPoint Radius =
			USeinMovement::ResolveCollisionRadius(Extents, Navigation);
		if (Radius <= FFixedPoint::Zero) return;
		const FFixedPoint Distance = SeinMath::Sqrt(DistanceSquared);
		const FFixedPoint Range =
			(Self.Radius + Radius) * Parameters.FalloffRadii;
		if (Distance >= Range) return;
		const FFixedPoint Falloff =
			FFixedPoint::One - (Distance / Range);
		const FFixedVector Steer = ComputeDoSiDoSteer(
			Self.Handle, OtherHandle, Self.Position,
			OtherEntity.Transform.GetLocation());
		const FFixedPoint Magnitude =
			(FFixedPoint::One + Parameters.HeadOnBase)
			* Falloff * Parameters.DoSiDoStrength;
		OutAccum.X += Steer.X * Magnitude;
		OutAccum.Y += Steer.Y * Magnitude;
	}

	static void AccumulateBlobResponse(
		const FAvoidanceNeighborParameters& Parameters,
		const FMoverAvoidanceSnapshot& Self,
		FSeinEntityHandle OtherBrokerHandle,
		const FSeinCommandBrokerData& OtherBrokerData,
		FFixedVector& OutAccum)
	{
		const FFixedVector BlobCentroid = OtherBrokerData.Centroid;
		const FFixedPoint BlobExtent = OtherBrokerData.FormationRadius;
		FFixedVector ToBlob(
			BlobCentroid.X - Self.Position.X,
			BlobCentroid.Y - Self.Position.Y,
			FFixedPoint::Zero);
		const FFixedPoint BlobDistanceSquared = ToBlob.SizeSquared();
		if (BlobDistanceSquared <= FFixedPoint::Epsilon) return;
		if (ToBlob.X * Self.Heading.X + ToBlob.Y * Self.Heading.Y
			<= FFixedPoint::Zero)
		{
			return;
		}
		if (Self.GoalDistanceSquared > FFixedPoint::Zero
			&& BlobDistanceSquared >= Self.GoalDistanceSquared)
		{
			return;
		}
		const FFixedPoint BlobDistance = SeinMath::Sqrt(BlobDistanceSquared);
		const FFixedPoint BlobRange =
			(Self.Radius + BlobExtent) * Parameters.FalloffRadii;
		if (BlobDistance >= BlobRange) return;
		const FFixedPoint BlobFalloff =
			FFixedPoint::One - (BlobDistance / BlobRange);
		const FFixedPoint BlobMagnitude =
			(FFixedPoint::One + Parameters.HeadOnBase) * BlobFalloff;
		if (Self.bIsBlob)
		{
			const FFixedVector Steer = ComputeDoSiDoSteer(
				Self.BrokerHandle, OtherBrokerHandle,
				Self.BrokerCentroid, BlobCentroid);
			OutAccum.X += Steer.X * BlobMagnitude;
			OutAccum.Y += Steer.Y * BlobMagnitude;
			return;
		}

		const FFixedPoint SideDot =
			ToBlob.X * Self.Right.X + ToBlob.Y * Self.Right.Y;
		const FFixedPoint Band = Self.Radius / FFixedPoint::FromInt(4);
		const FFixedPoint Turn = SideDot > Band
			? -FFixedPoint::One
			: SideDot < -Band
				? FFixedPoint::One
				: Self.Handle < OtherBrokerHandle
					? FFixedPoint::One
					: -FFixedPoint::One;
		OutAccum.X += Self.Right.X * (BlobMagnitude * Turn);
		OutAccum.Y += Self.Right.Y * (BlobMagnitude * Turn);
	}

	static void AccumulateIndividualResponse(
		const FAvoidanceNeighborParameters& Parameters,
		const FMoverAvoidanceSnapshot& Self,
		FSeinEntityHandle OtherHandle,
		const FSeinEntity& OtherEntity,
		const FSeinMovementPayload& OtherMovement,
		bool bGenuineCrossing,
		FIdleBlockerSet& OutIdleBlockers,
		FFixedVector& OutAccum)
	{
		FNeighborGateOutput Gate;
		if (ClassifyIndividualNeighbor(
				Self.Position, Self.Heading, Self.Velocity,
				Self.GoalDistanceSquared, Self.Radius,
				Self.Movement.AvoidanceWeight,
				Self.Movement.bAvoidSameWeights,
				Parameters.bResolveThroughIdlers,
				Parameters.FalloffRadii,
				Parameters.NavStorage, Parameters.ExtentsStorage,
				OtherHandle, OtherEntity, OtherMovement, Gate)
			!= ENeighborSkipReason::None)
		{
			return;
		}

		FFixedPoint HeadOn = FFixedPoint::One + Parameters.HeadOnBase;
		const FFixedVector OtherVelocity = OtherMovement.Velocity;
		const FFixedPoint OtherSpeed = OtherVelocity.Size();
		if (OtherSpeed > Parameters.MovingSpeedFloor)
		{
			const FFixedPoint Cosine =
				(Self.Heading.X * OtherVelocity.X
					+ Self.Heading.Y * OtherVelocity.Y)
				/ OtherSpeed;
			HeadOn = (FFixedPoint::One - Cosine) + Parameters.HeadOnBase;
		}

		if (bGenuineCrossing)
		{
			const FFixedVector Steer = ComputeDoSiDoSteer(
				Self.Handle, OtherHandle, Self.Position,
				OtherEntity.Transform.GetLocation());
			const FFixedPoint Weight =
				HeadOn * Gate.Falloff * Parameters.DoSiDoStrength;
			OutAccum.X += Steer.X * Weight;
			OutAccum.Y += Steer.Y * Weight;
			return;
		}

		const FFixedPoint SideDot =
			Gate.ToOther.X * Self.Right.X + Gate.ToOther.Y * Self.Right.Y;
		const FFixedPoint LateralBand =
			Self.Radius / FFixedPoint::FromInt(4);
		FFixedPoint TurnSign;
		if (SideDot > LateralBand)
		{
			TurnSign = -FFixedPoint::One;
		}
		else if (SideDot < -LateralBand)
		{
			TurnSign = FFixedPoint::One;
		}
		else
		{
			TurnSign = Self.Handle.Index < OtherHandle.Index
				? FFixedPoint::One
				: -FFixedPoint::One;
		}

		if (Gate.bOtherIdle)
		{
			if (OutIdleBlockers.Count < MaxIdleBlockers)
			{
				const FFixedPoint InverseDistance =
					FFixedPoint::One / Gate.Distance;
				OutIdleBlockers.Directions[OutIdleBlockers.Count] =
					FFixedVector(
						Gate.ToOther.X * InverseDistance,
						Gate.ToOther.Y * InverseDistance,
						FFixedPoint::Zero);
				FFixedPoint SinHalfAngle =
					(Self.Radius + Gate.OtherRadius) * InverseDistance;
				if (SinHalfAngle > FFixedPoint::One)
				{
					SinHalfAngle = FFixedPoint::One;
				}
				FFixedPoint CosHalfAngleSquared =
					FFixedPoint::One - SinHalfAngle * SinHalfAngle;
				if (CosHalfAngleSquared < FFixedPoint::Zero)
				{
					CosHalfAngleSquared = FFixedPoint::Zero;
				}
				OutIdleBlockers.CosHalfAngles[OutIdleBlockers.Count] =
					SeinMath::Sqrt(CosHalfAngleSquared);
				OutIdleBlockers.Detours[OutIdleBlockers.Count] =
					HeadOn * Gate.Falloff * TurnSign;
				++OutIdleBlockers.Count;
			}
			return;
		}

		const FFixedPoint Weight = HeadOn * Gate.Falloff * TurnSign;
		OutAccum.X += Self.Right.X * Weight;
		OutAccum.Y += Self.Right.Y * Weight;
	}

	static void AccumulateNeighborResponses(
		const FAvoidanceNeighborParameters& Parameters,
		const FMoverAvoidanceSnapshot& Self,
		const TArray<FSeinEntityHandle>& Neighbors,
		FIdleBlockerSet& OutIdleBlockers,
		FFixedVector& OutAccum)
	{
		TSet<FSeinEntityHandle> VisitedBlobBrokers;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity =
				Parameters.World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;
			const FSeinMovementPayload* OtherMovement =
				Parameters.MoveStorage
					? static_cast<const FSeinMovementPayload*>(
						Parameters.MoveStorage->GetComponentRaw(OtherHandle))
					: nullptr;
			if (!OtherMovement) continue;

			const FSeinBrokerMembershipData* OtherBroker =
				Parameters.BrokerStorage
					? static_cast<const FSeinBrokerMembershipData*>(
						Parameters.BrokerStorage->GetComponentRaw(OtherHandle))
					: nullptr;
			const FSeinEntityHandle OtherBrokerHandle = OtherBroker
				? OtherBroker->CurrentBrokerHandle
				: FSeinEntityHandle();
			if (OtherBrokerHandle.IsValid()
				&& VisitedBlobBrokers.Contains(OtherBrokerHandle))
			{
				continue;
			}

			const FSeinCommandBrokerData* OtherBrokerData =
				OtherBrokerHandle.IsValid()
					&& Parameters.BrokerDataStorage
					&& OtherBrokerHandle != Self.BrokerHandle
				? static_cast<const FSeinCommandBrokerData*>(
					Parameters.BrokerDataStorage->GetComponentRaw(
						OtherBrokerHandle))
				: nullptr;
			const bool bOtherIsBlob = OtherBrokerData
				&& OtherBrokerData->bAvoidAsCohesiveBody
				&& OtherBrokerData->FormationRadius > FFixedPoint::Zero;
			const bool bGenuineCrossing = IsGenuineCrossing(
				Parameters, Self, *OtherEntity, *OtherMovement);

			const bool bSameBroker = Self.BrokerHandle.IsValid()
				&& OtherBrokerHandle == Self.BrokerHandle;
			const int64 OtherCohesionId = OtherBroker
				? OtherBroker->CohesionGroupId
				: 0;
			const bool bSameCohesion = Self.CohesionId != 0
				&& Self.CohesionId == OtherCohesionId;
			if (bSameBroker || bSameCohesion)
			{
				AccumulateSameGroupCrossing(
					Parameters, Self, OtherHandle, *OtherEntity,
					*OtherMovement, bGenuineCrossing, OutAccum);
				continue;
			}

			if (bOtherIsBlob)
			{
				VisitedBlobBrokers.Add(OtherBrokerHandle);
				AccumulateBlobResponse(
					Parameters, Self, OtherBrokerHandle,
					*OtherBrokerData, OutAccum);
				continue;
			}

			AccumulateIndividualResponse(
				Parameters, Self, OtherHandle, *OtherEntity,
				*OtherMovement, bGenuineCrossing,
				OutIdleBlockers, OutAccum);
		}
	}

	static void GatherStartOfTickState(
		USeinWorldSubsystem& World,
		ISeinComponentStorage* MoveStorage,
		const ISeinComponentStorage* BrokerStorage,
		const ISeinComponentStorage* BrokerDataStorage,
		bool bCohesionEnabled,
		FFixedPoint MovingSpeedFloor,
		FFixedPoint FloorPerTickSq,
		FFixedPoint ProgressUnknown,
		FAvoidanceTickState& OutState)
	{
		const int32 ActiveCount = World.GetEntityPool().GetActiveCount();
		OutState.LiveHandles.Reserve(ActiveCount);
		TArray<FFixedPoint> ActualDispSq;
		ActualDispSq.Reserve(ActiveCount);
		OutState.ActualProgress.Reserve(ActiveCount);
		OutState.PreviousMovementState.Reserve(ActiveCount);

		World.GetEntityPool().ForEachEntity([&](
			FSeinEntityHandle Handle,
			const FSeinEntity& Entity)
		{
			OutState.LiveHandles.Add(Handle);
			ActualDispSq.Add(FFixedPoint::FromInt(-1));
			OutState.ActualProgress.Add(ProgressUnknown);
			OutState.PreviousMovementState.AddDefaulted();
			FSeinMovementPayload* Move = MoveStorage
				? static_cast<FSeinMovementPayload*>(
					MoveStorage->GetComponentRawForDeferredMutation(Handle))
				: nullptr;
			if (!Move) return;
			OutState.PreviousMovementState.Last().PrevTickLocation =
				Move->PrevTickLocation;
			OutState.PreviousMovementState.Last().AvoidanceOutput =
				Move->AvoidanceOutput;

			// Velocity is pre-collision intent. The transform delta is the honest
			// previous-tick displacement and progress sample.
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
					FFixedVector PrevToGoal =
						Move->TargetLocation - Move->PrevTickLocation;
					PrevToGoal.Z = FFixedPoint::Zero;
					FFixedVector NowToGoal = Move->TargetLocation - PosNow;
					NowToGoal.Z = FFixedPoint::Zero;
					OutState.ActualProgress.Last() =
						PrevToGoal.Size() - NowToGoal.Size();
				}
			}
			Move->PrevTickLocation = PosNow;

			if (!bCohesionEnabled
				|| !Move->bHasTarget
				|| Move->AvoidanceStrength <= FFixedPoint::Zero)
			{
				return;
			}
			const FSeinBrokerMembershipData* Broker = BrokerStorage
				? static_cast<const FSeinBrokerMembershipData*>(
					BrokerStorage->GetComponentRaw(Handle))
				: nullptr;
			if (!Broker || !Broker->CurrentBrokerHandle.IsValid()) return;

			const FFixedPoint DispSq = ActualDispSq.Last();
			const bool bActuallyMoving = DispSq >= FFixedPoint::Zero
				? DispSq > FloorPerTickSq
				: Move->Velocity.SizeSquared()
					> MovingSpeedFloor * MovingSpeedFloor;
			if (!bActuallyMoving) return;

			FFixedVector ToGoal =
				Move->TargetLocation - Entity.Transform.GetLocation();
			ToGoal.Z = FFixedPoint::Zero;
			FCohesionAggregate& Aggregate =
				OutState.GroupAggregates.FindOrAdd(
					Broker->CurrentBrokerHandle);
			if (!Aggregate.bStamped)
			{
				Aggregate.CohesionGroupId = Broker->CohesionGroupId;
				const FSeinCommandBrokerData* BrokerData = BrokerDataStorage
					? static_cast<const FSeinCommandBrokerData*>(
						BrokerDataStorage->GetComponentRaw(
							Broker->CurrentBrokerHandle))
					: nullptr;
				Aggregate.bPaceSquads = BrokerData
					&& BrokerData->bPaceSquadsTogether;
				Aggregate.bStamped = true;
			}
			Aggregate.Count += 1;
			Aggregate.SumDist = Aggregate.SumDist + ToGoal.Size();
		});
	}

	static void BuildOuterCohesionAggregates(
		bool bCohesionEnabled,
		const TMap<FSeinEntityHandle, FCohesionAggregate>& GroupAggregates,
		TMap<int64, FOuterCohesionAggregate>& OutAggregates)
	{
		if (!bCohesionEnabled) return;

		for (const TPair<FSeinEntityHandle, FCohesionAggregate>& Pair
			: GroupAggregates)
		{
			const FCohesionAggregate& Aggregate = Pair.Value;
			if (Aggregate.Count <= 0
				|| !Aggregate.bPaceSquads
				|| Aggregate.CohesionGroupId == 0)
			{
				continue;
			}
			const FFixedPoint BrokerMean = Aggregate.SumDist
				/ FFixedPoint::FromInt(Aggregate.Count);
			FOuterCohesionAggregate& Outer =
				OutAggregates.FindOrAdd(Aggregate.CohesionGroupId);
			Outer.DistinctBrokerCount += 1;
			Outer.SumOfBrokerMeans =
				Outer.SumOfBrokerMeans + BrokerMean;
		}
	}

	static void PublishDeferredMovementState(
		ISeinComponentStorage* MoveStorage,
		const FAvoidanceTickState& State)
	{
		for (int32 Index = 0; Index < State.LiveHandles.Num(); ++Index)
		{
			FSeinMovementPayload* Move = MoveStorage
				? static_cast<FSeinMovementPayload*>(
					MoveStorage->GetComponentRawForDeferredMutation(
						State.LiveHandles[Index]))
				: nullptr;
			if (!Move) continue;
			const FDeferredMovementState& Before =
				State.PreviousMovementState[Index];
			if (Move->PrevTickLocation != Before.PrevTickLocation
				|| Move->AvoidanceOutput.SteerDir
					!= Before.AvoidanceOutput.SteerDir
				|| Move->AvoidanceOutput.SpeedScale
					!= Before.AvoidanceOutput.SpeedScale)
			{
				MoveStorage->CommitDeferredMutation(State.LiveHandles[Index]);
			}
		}
	}
	static void ComputeMoverAvoidance(
		const FAvoidanceWorkerParameters& Parameters,
		int32 Index)
	{
		USeinWorldSubsystem& World = Parameters.World;
		const FSeinCollisionSpatialHash& Hash = Parameters.Hash;
		const TArray<FSeinEntityHandle>& LiveHandles = Parameters.LiveHandles;
		ISeinComponentStorage* MoveStorage = Parameters.MoveStorage;
		const ISeinComponentStorage* ReadOnlyMoveStorage =
			Parameters.ReadOnlyMoveStorage;
		const ISeinComponentStorage* NavStorage = Parameters.NavStorage;
		const ISeinComponentStorage* ExtentsStorage = Parameters.ExtentsStorage;
		const ISeinComponentStorage* BrokerStorage = Parameters.BrokerStorage;
		const ISeinComponentStorage* BrokerDataStorage =
			Parameters.BrokerDataStorage;
		const FAvoidanceNeighborParameters& NeighborParameters =
			Parameters.NeighborParameters;
		const FAvoidanceOutputParameters& OutputParameters =
			Parameters.OutputParameters;
		const FFixedPoint* GapCos = Parameters.GapCos;
		const FFixedPoint* GapSin = Parameters.GapSin;
		const bool bIdleDodgeEnabled = Parameters.bIdleDodgeEnabled;
		const FFixedPoint MovingSpeedFloor = Parameters.MovingSpeedFloor;
		const FFixedPoint FalloffRadii = Parameters.FalloffRadii;
		const FFixedPoint SmoothKeep = Parameters.SmoothKeep;
		const FFixedPoint ArrivalReleaseRadii =
			Parameters.ArrivalReleaseRadii;
		const FFixedPoint ArrivalFadeInnerRadii =
			Parameters.ArrivalFadeInnerRadii;
		const FFixedPoint MaxSteerMagnitude = Parameters.MaxSteerMagnitude;
		const FFixedPoint IdleResolveStrength = Parameters.IdleResolveStrength;
		const FFixedPoint IdleDodgeStrength = Parameters.IdleDodgeStrength;
		const FFixedPoint BendCapCos = Parameters.BendCapCos;
		const FFixedPoint LookaheadSeconds = Parameters.LookaheadSeconds;

		const FSeinEntityHandle SelfHandle = LiveHandles[Index];
		const FSeinEntity* SelfEntityPtr =
			World.GetEntityPool().Get(SelfHandle);
		if (!SelfEntityPtr) return;
		const FSeinEntity& SelfEntity = *SelfEntityPtr;

		// Per-body neighbour scratch — MUST be a local (one buffer per body invocation)
		// so concurrent QueryRadius calls never share it.
		TArray<FSeinEntityHandle> Neighbors;

		FSeinMovementPayload* Move = MoveStorage
			? static_cast<FSeinMovementPayload*>(
				MoveStorage->GetComponentRawForDeferredMutation(SelfHandle))
			: nullptr;
		if (!Move) return;
		// Opted out → leave AvoidanceOutput UNTOUCHED (its default zero steer / unit scale),
		// so the unit's motion is bit-identical to a world with no avoidance.
		if (Move->AvoidanceStrength <= FFixedPoint::Zero) return;

		// Idle dodge is a one-sided self write. Ordered movers cannot be
		// triggered by another idler, so the branch cannot cascade in this pass.
		if (!Move->bHasTarget)
		{
			ComputeIdleDodge(
				World, Hash, ReadOnlyMoveStorage, NavStorage,
				ExtentsStorage, SelfHandle, SelfEntity, *Move,
				bIdleDodgeEnabled, MovingSpeedFloor, FalloffRadii,
				MaxSteerMagnitude, IdleDodgeStrength, SmoothKeep);
			return;
		}

		// Heading from end-of-last-tick velocity (the same snapshot value for every unit
		// at PreTick). Stopped/too-slow → release and bail.
		const FFixedVector Vel = Move->Velocity;
		const FFixedPoint Speed = Vel.Size();
		if (Speed <= MovingSpeedFloor)
		{
#if !UE_BUILD_SHIPPING
			ReportPinnedMover(
				World, Hash, ReadOnlyMoveStorage, NavStorage,
				ExtentsStorage, BrokerStorage, SelfHandle,
				SelfEntity, *Move, Vel, MovingSpeedFloor,
				FalloffRadii, NeighborParameters.bResolveThroughIdlers);
#endif
			ClearAvoidanceOutput(*Move);
			return;
		}
		const FFixedVector Heading(Vel.X / Speed, Vel.Y / Speed, FFixedPoint::Zero);
		const FFixedVector Right(Heading.Y, -Heading.X, FFixedPoint::Zero); // planar right of heading

		// Body radius from the movement/nav FOOTPRINT cascade — NOT collision extents.
		// Hoisted-storage pointers feed the no-lookup ResolveCollisionRadius overload.
		const FSeinNavigationPayload* SelfNav = NavStorage ? static_cast<const FSeinNavigationPayload*>(NavStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinExtentsPayload* SelfExt = ExtentsStorage ? static_cast<const FSeinExtentsPayload*>(ExtentsStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FFixedPoint SelfRadius = USeinMovement::ResolveCollisionRadius(SelfExt, SelfNav);
		if (SelfRadius <= FFixedPoint::Zero)
		{
			ClearAvoidanceOutput(*Move);
			return;
		}

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

		// ARRIVAL-RELEASE FADE: ramp avoidance down as the unit closes on its goal so
		// path-attraction + the collision floor own the endgame. Without this, a destination
		// inside/behind a standing cluster makes the unit orbit the perimeter forever.
		// Outer radius = full release begins (avoidance starts fading).
		// Inner radius = avoidance fully off (hard cut, collision floor takes over).
		// Between them the output scales linearly. If inner >= outer, legacy hard cut.
		const FFixedPoint OuterRadius = SelfRadius * ArrivalReleaseRadii;
		const FFixedPoint InnerRadius = SelfRadius * ArrivalFadeInnerRadii;
		const FFixedPoint OuterRadiusSq = OuterRadius * OuterRadius;
		FFixedPoint ArrivalFade = FFixedPoint::One;
		if (GoalDistSq <= OuterRadiusSq)
		{
			if (InnerRadius >= OuterRadius || GoalDistSq <= InnerRadius * InnerRadius)
			{
				ClearAvoidanceOutput(*Move);
				return;
			}
			const FFixedPoint GoalDist = SeinMath::Sqrt(GoalDistSq);
			ArrivalFade = (GoalDist - InnerRadius) / (OuterRadius - InnerRadius);
		}

		// Speed-scaled perception radius (footprint-based; "personal space" needs no
		// separate authored radius).
		const FFixedPoint Perception = SelfRadius * FFixedPoint::FromInt(2) + Speed * LookaheadSeconds;

		Neighbors.Reset();
		Hash.QueryRadius(SelfPos, Perception, Neighbors, SelfHandle);
		FIdleBlockerSet IdleBlockers;
		FFixedVector Accum = FFixedVector::ZeroVector;
		const FMoverAvoidanceSnapshot SelfSnapshot{
			SelfHandle, SelfBrokerHandle, SelfCohesionId, *Move,
			SelfBrokerData, bSelfIsBlob, SelfBrokerCentroid, SelfPos,
			ToGoal, GoalDistSq, Heading, Right, Vel, SelfRadius};
		AccumulateNeighborResponses(
			NeighborParameters, SelfSnapshot, Neighbors,
			IdleBlockers, Accum);
		ResolveIdleGap(
			IdleBlockers, Heading, Right, ToGoal, GoalDistSq,
			BendCapCos, GapCos, GapSin, IdleResolveStrength, Accum);

		FinalizeMovingOutput(
			OutputParameters, Index, *Move, Accum, ArrivalFade, SelfRadius,
			GoalDistSq, SelfBrokerHandle, SelfCohesionId,
			SelfBrokerData);
	}
}

void FSeinAvoidanceDefaultKernel::Execute(
	USeinWorldSubsystem& World) const
{
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

	// --- Tunables: the model-shape constants are authored on THIS class's CDO (edit via a Blueprint
	//     subclass slotted in Project Settings > AvoidanceClass; captured for determinism by that
	//     class-path + identical content, so they leave the settings fingerprint). The model-AGNOSTIC
	//     harness knobs (Moving Speed Floor / Bend Cap) + the Idle Re-Seek switch still come from
	//     plugin settings. Per-unit dials (strength/weight/same-weights) live on FSeinMovementPayload. ---
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint LookaheadSeconds    = Policy.AvoidanceLookaheadSeconds;
	const FFixedPoint MovingSpeedFloor    = Settings->AvoidanceMovingSpeedFloor;
	const FFixedPoint FalloffRadii        = Policy.AvoidanceFalloffRadii;
	const FFixedPoint SmoothKeep          = Policy.AvoidanceSmoothKeep;
	const FFixedPoint HeadOnBase          = Policy.AvoidanceHeadOnBase;
	const FFixedPoint ArrivalReleaseRadii = Policy.AvoidanceArrivalReleaseRadii;
	const FFixedPoint ArrivalFadeInnerRadii = Policy.AvoidanceArrivalFadeInnerRadii;
	const FFixedPoint MaxSteerMagnitude   = Policy.AvoidanceMaxSteerMagnitude;
	const FFixedPoint BrakeStrength       = Policy.AvoidanceBrakeStrength;
	const FFixedPoint CohesionHoldBack    = Policy.AvoidanceCohesionHoldBack;
	const FFixedPoint CohesionBoost       = Policy.AvoidanceCohesionCatchUpBoost;
	const FFixedPoint CohesionRangeRadii  = Policy.AvoidanceCohesionRangeRadii;
	// Do-si-do (crossing slide-past) dials. Strength 0 = the crossing steer is off entirely
	// (pure group-skip + geometric sidewalk, the pre-do-si-do behaviour). CrossGoalDivergence is
	// the goal-separation-vs-body-separation ratio that marks a GENUINE crossing (paired with
	// opposed travel intent); larger = crossings recognised more rarely = more packing preserved.
	const FFixedPoint DoSiDoStrength      = Policy.AvoidanceDoSiDoStrength;
	const FFixedPoint DoSiDoCrossDiverge  = Policy.AvoidanceCrossingGoalDivergence;
	const FFixedPoint KconvSq             = DoSiDoCrossDiverge * DoSiDoCrossDiverge;
	const bool bDoSiDoEnabled             = DoSiDoStrength > FFixedPoint::Zero;
	// RESOLVE-THROUGH (mover-resolves-around-idlers). Strength 0 = the bulldoze-idle rule stands
	// bit-exact (a mover plows through parked units, collision shoves them). > 0 = a moving unit
	// steers around an idle neighbour whose AvoidanceWeight qualifies (heavier-or-equal), scaled by
	// this. Orbit-safe under the goal-relative bend cap (which guarantees forward progress).
	const FFixedPoint IdleResolveStrength = Policy.AvoidanceIdleResolveStrength;
	const bool bResolveThroughIdlers      = IdleResolveStrength > FFixedPoint::Zero;
	// IDLER-DODGES-MOVER: an idle unit steps aside for an approaching qualifying mover. Gated ALSO
	// on bIdleReseek — the shipped re-seek owns the walk back to slot (the dodge only suppresses its
	// release while active), so a dodge is meaningless without a return path. Strength 0 OR re-seek
	// off → the true-idle branch takes the exact ClearOutput (bit-exact today).
	const FFixedPoint IdleDodgeStrength   = Policy.AvoidanceIdleDodgeStrength;
	const bool bIdleDodgeEnabled          = Settings->bIdleReseek && IdleDodgeStrength > FFixedPoint::Zero;
	// Bend cap, also read here (not only in ApplyAvoidanceSteer): the idle GAP-SEEK below bounds its
	// candidate headings to the same goal-relative wedge, so it never proposes a thread the downstream
	// cap would just clamp away. -1 (OFF sentinel) = no wedge limit (full candidate span).
	const FFixedPoint BendCapCos          = Settings->AvoidanceBendCapCos;
	// Cohesion off entirely when both sides are neutral — the aggregate pre-pass is skipped
	// and every unit's CohesionScale is exactly One (bit-exact no-op).
	const bool bCohesionEnabled = CohesionHoldBack > FFixedPoint::Zero || CohesionBoost > FFixedPoint::One;
	// The moving-speed floor expressed as a PER-TICK displacement, for the honest-motion
	// tests below (PrevTickLocation deltas are per-tick, not per-second).
	const int32 TickRate = GetDefault<USeinARTSCoreSettings>()->SimulationTickRate > 0
		? GetDefault<USeinARTSCoreSettings>()->SimulationTickRate : 30;
	const FFixedPoint FloorPerTick   = MovingSpeedFloor / FFixedPoint::FromInt(TickRate);
	const FFixedPoint FloorPerTickSq = FloorPerTick * FloorPerTick;

	// IDLE GAP-SEEK candidate rotations (resolve-through, mover side). When Idle Resolve is on, a
	// mover facing a field of loose idle units does NOT sum a repulsor away from them (two idlers
	// flanking a passable lane cancel to a detour) — it samples headings rotated off its travel
	// direction by these FIXED offsets and threads the nearest one no idler's footprint-cone covers.
	// The (cos, sin) of each offset are the ONLY trig here and are built ONCE, serially, outside the
	// parallel body; the per-unit scan is pure dot-products (no atan2/asin, no angular-interval bins),
	// so it is bit-stable. Offsets 0,10,...,80 deg; the per-unit scan stops at the bend-cap wedge.
	FFixedPoint GapCos[GapCandidateCount];
	FFixedPoint GapSin[GapCandidateCount];
	if (bResolveThroughIdlers)
	{
		for (int32 k = 0; k < GapCandidateCount; ++k)
		{
			const FFixedPoint Ang = FFixedPoint::DegToRad * FFixedPoint::FromInt(k * 10);
			GapCos[k] = SeinMath::Cos(Ang);
			GapSin[k] = SeinMath::Sin(Ang);
		}
	}

	// Hoist component-storage lookups out of the per-entity / per-neighbour loop:
	// GetComponent<T>() is a hashmap lookup by UScriptStruct* per call; resolving each
	// storage once turns every access into an O(1) indexed get.
	ISeinComponentStorage* MoveStorage =
		World.GetComponentStorageMutable(
			FSeinMovementPayload::StaticStruct());
	const ISeinComponentStorage* ReadOnlyMoveStorage = MoveStorage;
	const ISeinComponentStorage* NavStorage =
		World.GetComponentStorageRaw(
			FSeinNavigationPayload::StaticStruct());
	const ISeinComponentStorage* ExtentsStorage =
		World.GetComponentStorageRaw(
			FSeinExtentsPayload::StaticStruct());
	// Group identity — TWO-LAYER, matching the formation model: the immediate broker
	// (squad / loose-order group) and the per-order cohesion id spanning brokers of one
	// multi-element order. Same-group neighbours are never avoided (the group converges
	// and packs; the collision floor keeps bodies apart).
	const ISeinComponentStorage* BrokerStorage =
		World.GetComponentStorageRaw(
			FSeinBrokerMembershipData::StaticStruct());
	// Broker-LEVEL data (Centroid / FormationRadius / bAvoidAsCohesiveBody) for the blob-obstacle
	// scope: read per neighbour via its CurrentBrokerHandle. Prior-tick snapshot (broker maintenance
	// is PostTick), read-only in the parallel body → contract-safe.
	const ISeinComponentStorage* BrokerDataStorage =
		World.GetComponentStorageRaw(
			FSeinCommandBrokerData::StaticStruct());

	// Serial pool-order gather. Every array remains index-aligned with the
	// canonical live-handle sequence consumed by the parallel stage.
	const FFixedPoint ProgressUnknown = FFixedPoint::FromInt(-1000000);
	FAvoidanceTickState TickState;
	GatherStartOfTickState(
		World, MoveStorage, BrokerStorage, BrokerDataStorage,
		bCohesionEnabled, MovingSpeedFloor, FloorPerTickSq,
		ProgressUnknown, TickState);
	BuildOuterCohesionAggregates(
		bCohesionEnabled, TickState.GroupAggregates,
		TickState.OuterAggregates);

	const TArray<FSeinEntityHandle>& LiveHandles = TickState.LiveHandles;
	const TArray<FFixedPoint>& ActualProgress = TickState.ActualProgress;
	const TMap<FSeinEntityHandle, FCohesionAggregate>& GroupAggregates =
		TickState.GroupAggregates;
	const TMap<int64, FOuterCohesionAggregate>& OuterAggregates =
		TickState.OuterAggregates;
	const FAvoidanceOutputParameters OutputParameters{
		GroupAggregates,
		OuterAggregates,
		ActualProgress,
		bCohesionEnabled,
		ProgressUnknown,
		FloorPerTick,
		MovingSpeedFloor,
		MaxSteerMagnitude,
		SmoothKeep,
		BrakeStrength,
		CohesionHoldBack,
		CohesionBoost,
		CohesionRangeRadii};
	const FAvoidanceNeighborParameters NeighborParameters{
		World,
		ReadOnlyMoveStorage,
		NavStorage,
		ExtentsStorage,
		BrokerStorage,
		BrokerDataStorage,
		bDoSiDoEnabled,
		bResolveThroughIdlers,
		MovingSpeedFloor,
		FalloffRadii,
		HeadOnBase,
		DoSiDoStrength,
		KconvSq};
	const FAvoidanceWorkerParameters WorkerParameters{
		World,
		Hash,
		LiveHandles,
		MoveStorage,
		ReadOnlyMoveStorage,
		NavStorage,
		ExtentsStorage,
		BrokerStorage,
		BrokerDataStorage,
		NeighborParameters,
		OutputParameters,
		GapCos,
		GapSin,
		bIdleDodgeEnabled,
		MovingSpeedFloor,
		FalloffRadii,
		SmoothKeep,
		ArrivalReleaseRadii,
		ArrivalFadeInnerRadii,
		MaxSteerMagnitude,
		IdleResolveStrength,
		IdleDodgeStrength,
		BendCapCos,
		LookaheadSeconds};

	SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
	{
		ComputeMoverAvoidance(WorkerParameters, Index);
	});

	// Publish only real state changes, serially in canonical handle order.
	PublishDeferredMovementState(MoveStorage, TickState);
}
