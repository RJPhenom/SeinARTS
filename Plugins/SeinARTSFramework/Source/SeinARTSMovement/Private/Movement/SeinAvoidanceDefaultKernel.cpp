/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAvoidanceDefaultKernel.cpp
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
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

	static void ClearAvoidanceOutput(FSeinMovementComponent& Movement)
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
		FSeinMovementComponent& Move,
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

		const FSeinNavigationComponent* SelfNavigation = NavStorage
			? static_cast<const FSeinNavigationComponent*>(
				NavStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FSeinExtentsComponent* SelfExtents = ExtentsStorage
			? static_cast<const FSeinExtentsComponent*>(
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
			const FSeinMovementComponent* OtherMove = MoveStorage
				? static_cast<const FSeinMovementComponent*>(
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
			const FSeinNavigationComponent* OtherNavigation = NavStorage
				? static_cast<const FSeinNavigationComponent*>(
					NavStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const FSeinExtentsComponent* OtherExtents = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(
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
		const FSeinMovementComponent& Move,
		const FFixedVector& Velocity,
		FFixedPoint MovingSpeedFloor,
		FFixedPoint FalloffRadii)
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
		const FSeinNavigationComponent* Navigation = NavStorage
			? static_cast<const FSeinNavigationComponent*>(
				NavStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		const FSeinExtentsComponent* Extents = ExtentsStorage
			? static_cast<const FSeinExtentsComponent*>(
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
		int32 IdleTrue = 0;
		int32 IdlePinned = 0;
		int32 Weight = 0;
		int32 Behind = 0;
		int32 NotClosing = 0;
		int32 PastGoal = 0;
		int32 Far = 0;
		FFixedPoint MinDistanceSquared = FFixedPoint::FromInt(999999);
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity =
				World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;
			const FSeinMovementComponent* OtherMove = MoveStorage
				? static_cast<const FSeinMovementComponent*>(
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
			if (OtherMove->Velocity.SizeSquared()
				<= MovingSpeedFloor * MovingSpeedFloor)
			{
				OtherMove->bHasTarget ? ++IdlePinned : ++IdleTrue;
				continue;
			}
			const bool bQualifies = Move.bAvoidSameWeights
				? OtherMove->AvoidanceWeight >= Move.AvoidanceWeight
				: OtherMove->AvoidanceWeight > Move.AvoidanceWeight;
			if (!bQualifies)
			{
				++Weight;
				continue;
			}
			if (ToOther.X * Heading.X + ToOther.Y * Heading.Y
				<= FFixedPoint::Zero)
			{
				++Behind;
				continue;
			}
			const FFixedVector RelativeVelocity(
				Velocity.X - OtherMove->Velocity.X,
				Velocity.Y - OtherMove->Velocity.Y,
				FFixedPoint::Zero);
			if (ToOther.X * RelativeVelocity.X
					+ ToOther.Y * RelativeVelocity.Y
				<= FFixedPoint::Zero)
			{
				++NotClosing;
				continue;
			}
			if (GoalDistance > FFixedPoint::Zero
				&& DistanceSquared >= GoalDistance * GoalDistance)
			{
				++PastGoal;
				continue;
			}
			const FSeinNavigationComponent* OtherNavigation = NavStorage
				? static_cast<const FSeinNavigationComponent*>(
					NavStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const FSeinExtentsComponent* OtherExtents = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(
					ExtentsStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			const FFixedPoint OtherRadius =
				USeinMovement::ResolveCollisionRadius(
					OtherExtents, OtherNavigation);
			const FFixedPoint Range =
				(Radius + OtherRadius) * FalloffRadii;
			if (DistanceSquared >= Range * Range)
			{
				++Far;
				continue;
			}
			++Kept;
		}

		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("[GRIND] t=%d h=%d:%d grp=%d:%d coh=%lld goalDist=%.0f tgt=(%.0f,%.0f) nbrs=%d kept=%d skip{static=%d grp=%d idleTrue=%d idlePinned=%d wt=%d behind=%d notClosing=%d pastGoal=%d far=%d} minD=%.0f steer=%.3f scale=%.3f"),
			World.GetCurrentTick(), SelfHandle.Index, SelfHandle.Generation,
			BrokerHandle.Index, BrokerHandle.Generation, CohesionId,
			GoalDistance.ToFloat(),
			Move.TargetLocation.X.ToFloat(), Move.TargetLocation.Y.ToFloat(),
			Neighbors.Num(), Kept,
			Static, Group, IdleTrue, IdlePinned, Weight, Behind,
			NotClosing, PastGoal, Far,
			SeinMath::Sqrt(MinDistanceSquared).ToFloat(),
			Move.AvoidanceOutput.SteerDir.Size().ToFloat(),
			Move.AvoidanceOutput.SpeedScale.ToFloat());
	}
#endif

	static void FinalizeMovingOutput(
		const FAvoidanceOutputParameters& Parameters,
		int32 Index,
		FSeinMovementComponent& Movement,
		FFixedVector Accum,
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
			FSeinMovementComponent* Move = MoveStorage
				? static_cast<FSeinMovementComponent*>(
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
			FSeinMovementComponent* Move = MoveStorage
				? static_cast<FSeinMovementComponent*>(
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
}

void FSeinAvoidanceDefaultKernel::Execute(
	USeinWorldSubsystem& World) const
{
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

	// --- Tunables: the model-shape constants are authored on THIS class's CDO (edit via a Blueprint
	//     subclass slotted in Project Settings > AvoidanceClass; captured for determinism by that
	//     class-path + identical content, so they leave the settings fingerprint). The model-AGNOSTIC
	//     harness knobs (Moving Speed Floor / Bend Cap) + the Idle Re-Seek switch still come from
	//     plugin settings. Per-unit dials (strength/weight/same-weights) live on FSeinMovementComponent. ---
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint LookaheadSeconds    = Policy.AvoidanceLookaheadSeconds;
	const FFixedPoint MovingSpeedFloor    = Settings->AvoidanceMovingSpeedFloor;
	const FFixedPoint FalloffRadii        = Policy.AvoidanceFalloffRadii;
	const FFixedPoint SmoothKeep          = Policy.AvoidanceSmoothKeep;
	const FFixedPoint HeadOnBase          = Policy.AvoidanceHeadOnBase;
	const FFixedPoint ArrivalReleaseRadii = Policy.AvoidanceArrivalReleaseRadii;
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
	constexpr int32 GapCandidateCount = 9;
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
			FSeinMovementComponent::StaticStruct());
	const ISeinComponentStorage* ReadOnlyMoveStorage = MoveStorage;
	const ISeinComponentStorage* NavStorage =
		World.GetComponentStorageRaw(
			FSeinNavigationComponent::StaticStruct());
	const ISeinComponentStorage* ExtentsStorage =
		World.GetComponentStorageRaw(
			FSeinExtentsComponent::StaticStruct());
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

	SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
	{
		const FSeinEntityHandle SelfHandle = LiveHandles[Index];
		const FSeinEntity* SelfEntityPtr =
			World.GetEntityPool().Get(SelfHandle);
		if (!SelfEntityPtr) return;
		const FSeinEntity& SelfEntity = *SelfEntityPtr;

		// Per-body neighbour scratch — MUST be a local (one buffer per body invocation)
		// so concurrent QueryRadius calls never share it.
		TArray<FSeinEntityHandle> Neighbors;

		FSeinMovementComponent* Move = MoveStorage
			? static_cast<FSeinMovementComponent*>(
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
				FalloffRadii);
#endif
			ClearAvoidanceOutput(*Move);
			return;
		}
		const FFixedVector Heading(Vel.X / Speed, Vel.Y / Speed, FFixedPoint::Zero);
		const FFixedVector Right(Heading.Y, -Heading.X, FFixedPoint::Zero); // planar right of heading

		// Body radius from the movement/nav FOOTPRINT cascade — NOT collision extents.
		// Hoisted-storage pointers feed the no-lookup ResolveCollisionRadius overload.
		const FSeinNavigationComponent* SelfNav = NavStorage ? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(SelfHandle)) : nullptr;
		const FSeinExtentsComponent* SelfExt = ExtentsStorage ? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle)) : nullptr;
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

		// ARRIVAL-RELEASE FADE: stop steering (and braking) as the unit closes on its goal
		// so path-attraction + the collision floor own the endgame. Without this, a
		// destination inside/behind a standing cluster makes the unit orbit the perimeter
		// forever. (This is the mechanism that was dead in the original model.)
		const FFixedPoint ReleaseRadius = SelfRadius * ArrivalReleaseRadii;
		if (GoalDistSq <= ReleaseRadius * ReleaseRadius)
		{
			ClearAvoidanceOutput(*Move);
			return;
		}

		// Speed-scaled perception radius (footprint-based; "personal space" needs no
		// separate authored radius).
		const FFixedPoint Perception = SelfRadius * FFixedPoint::FromInt(2) + Speed * LookaheadSeconds;

		Neighbors.Reset();
		Hash.QueryRadius(SelfPos, Perception, Neighbors, SelfHandle);

		// Body-local dedup for the blob scope: a blob-flagged foreign squad contributes ONE steer
		// (from its Centroid/FormationRadius), not one per member in range. Fresh per invocation.
		TSet<FSeinEntityHandle> VisitedBlobBrokers;

		// IDLE-BLOCKER buffer for the post-loop gap-seek (resolve-through, mover side). Idle
		// neighbours are NOT accumulated into Accum below — they are collected here as blocked
		// bearings and resolved as ONE steer (thread the gap, or detour if none admits). Fixed
		// stack storage (no per-unit alloc in the hot body); a crowd denser than this cap is a wall
		// → detour regardless, so the overflow is harmless. Each entry: the unit dir to the idler,
		// the cos of its footprint-inflated subtended half-angle (the cone it blocks), and the
		// pre-gap-seek geometric detour term (used only in the no-gap fallback).
		constexpr int32 MaxIdleBlockers = 24;
		FFixedVector IdleDir[MaxIdleBlockers];
		FFixedPoint  IdleCosAlpha[MaxIdleBlockers];
		FFixedPoint  IdleDetour[MaxIdleBlockers];
		int32 NumIdleBlockers = 0;

		FFixedVector Accum = FFixedVector::ZeroVector;
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;

			// UNIT-TO-UNIT ONLY. A neighbour with no movement component is static geometry
			// — a wall, a building, a prop — and nav blocking already routes units clear of
			// those. (The spatial hash holds ALL colliders, walls included, so the filter
			// must live here.) Fetched once and reused for head-on below.
			const FSeinMovementComponent* OtherMove = ReadOnlyMoveStorage
				? static_cast<const FSeinMovementComponent*>(
					ReadOnlyMoveStorage->GetComponentRaw(OtherHandle))
				: nullptr;
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
			// and the post-loop gap-seek — the mover threads/weaves around it. At strength 0 the
			// short-circuit is byte-identical to the old unconditional continue.
			//
			// IDLE = "carries no move order" (bHasTarget false), NOT "not currently moving". This is
			// the decouple linchpin: an idler stepping aside (idle-dodge) now writes a real velocity
			// so the anim BP and re-seek see its motion, so a velocity test would flip a dodging idler
			// to "mover" and break both this gate and the gap-seek. Keying on the order is also more
			// honest — a body-blocked COMMANDED unit (a pinned presser) is a mover, and now takes the
			// moving-neighbour path (do-si-do / geometric) rather than being bulldozed.
			const bool bOtherIdle = !OtherMove->bHasTarget;
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
			// traffic. Everything else takes the geometric side-pick: idle neighbours are diverted
			// out to the post-loop gap-seek, and moving groups keep their real sides to cleave apart.
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
				// GEOMETRIC SIDE PICK — the side of the heading the neighbour sits on. Shared by the
				// moving-neighbour steer AND the idle-blocker detour term. Inside a "dead-ahead" band
				// the side is undefined → break it deterministically by handle index.
				const FFixedPoint SideDot = ToOther.X * Right.X + ToOther.Y * Right.Y;
				const FFixedPoint LateralBand = SelfRadius / FFixedPoint::FromInt(4);
				FFixedPoint TurnSign;
				if (SideDot > LateralBand)        { TurnSign = -FFixedPoint::One; } // neighbour on right → steer left
				else if (SideDot < -LateralBand)  { TurnSign =  FFixedPoint::One; } // neighbour on left  → steer right
				else { TurnSign = (SelfHandle.Index < OtherHandle.Index) ? FFixedPoint::One : -FFixedPoint::One; }

				// IDLE neighbour (no move order): do NOT sum a repulsor — record it as a blocked
				// bearing for the post-loop GAP-SEEK (thread the goal-aligned gap; detour only if no
				// gap admits). Only reachable with Idle Resolve on (idlers were else skipped above).
				if (bOtherIdle)
				{
					if (NumIdleBlockers < MaxIdleBlockers)
					{
						const FFixedPoint InvDist = FFixedPoint::One / Dist;
						IdleDir[NumIdleBlockers] = FFixedVector(ToOther.X * InvDist, ToOther.Y * InvDist, FFixedPoint::Zero);
						// Footprint-inflated subtended half-angle: sin(a) = (SelfR+OtherR)/Dist, clamped
						// to 1 (an overlapping idler blocks the whole forward hemisphere on its bearing).
						// The cone it blocks is cos(a) = sqrt(1 - sin^2 a); a candidate heading is blocked
						// when its dot with this bearing exceeds cos(a).
						FFixedPoint SinA = (SelfRadius + OtherRadius) * InvDist;
						if (SinA > FFixedPoint::One) SinA = FFixedPoint::One;
						FFixedPoint CosASq = FFixedPoint::One - SinA * SinA;
						if (CosASq < FFixedPoint::Zero) CosASq = FFixedPoint::Zero;
						IdleCosAlpha[NumIdleBlockers] = SeinMath::Sqrt(CosASq);
						IdleDetour[NumIdleBlockers]   = HeadOn * Falloff * TurnSign;
						++NumIdleBlockers;
					}
					continue; // resolved post-loop by the gap-seek
				}

				// MOVING NEIGHBOUR — GROUP CLEAVE (was: sidewalk shift). Two co-directional groups
				// keep their real geometric sides and split around each other along the contact line,
				// instead of all shifting one way (which read as the whole crowd rotating past). Opposed
				// crossings are owned by the do-si-do branch above, and genuine head-on mirror tension
				// is likewise the do-si-do's job, so no forced side is needed here anymore.
				const FFixedPoint W = HeadOn * Falloff * TurnSign;
				Accum.X += Right.X * W;
				Accum.Y += Right.Y * W;
			}
		}

		// IDLE GAP-SEEK RESOLUTION (resolve-through, mover side). The idle blockers collected above
		// become ONE steer: sample headings rotated off the travel direction by the fixed candidate
		// offsets (bounded by the bend-cap wedge), and take the one NEAREST the travel direction that
		// no idler's footprint-cone covers, leaning toward the goal on ties — a gap thread. If EVERY
		// in-wedge heading is blocked (a solid idle wall), fall back to the summed geometric detour
		// (route around), orbit-safe under the downstream bend cap. Empty list → no idle contribution.
		if (NumIdleBlockers > 0)
		{
			const FFixedPoint InvGoal = FFixedPoint::One / SeinMath::Sqrt(GoalDistSq);
			const FFixedVector GoalDir(ToGoal.X * InvGoal, ToGoal.Y * InvGoal, FFixedPoint::Zero);
			const FFixedVector LeftPerp(-Heading.Y, Heading.X, FFixedPoint::Zero); // +offset side; Right = -LeftPerp
			const bool bWedge = BendCapCos > -FFixedPoint::One;

			bool bGapFound = false;
			FFixedVector GapDir = Heading;
			FFixedPoint BestGoalDot = FFixedPoint::MinValue;
			for (int32 k = 0; k < GapCandidateCount && !bGapFound; ++k)
			{
				if (bWedge && GapCos[k] < BendCapCos) break; // past the cap wedge — no wider thread is reachable
				const int32 SideCount = (k == 0) ? 1 : 2;    // 0 deg has a single candidate (dead ahead)
				for (int32 si = 0; si < SideCount; ++si)
				{
					const FFixedPoint Sign = (si == 0) ? FFixedPoint::One : -FFixedPoint::One;
					const FFixedVector Cand(
						Heading.X * GapCos[k] + LeftPerp.X * (GapSin[k] * Sign),
						Heading.Y * GapCos[k] + LeftPerp.Y * (GapSin[k] * Sign),
						FFixedPoint::Zero);
					bool bBlocked = false;
					for (int32 b = 0; b < NumIdleBlockers; ++b)
					{
						if (Cand.X * IdleDir[b].X + Cand.Y * IdleDir[b].Y > IdleCosAlpha[b]) { bBlocked = true; break; }
					}
					if (bBlocked) continue;
					// Clear: keep the more goal-aligned of this offset's two sides (deterministic — the
					// +side, evaluated first, wins an exact tie). The outer !bGapFound then stops at the
					// nearest-to-travel offset that cleared, so the thread deviates as little as it can.
					const FFixedPoint GoalDot = Cand.X * GoalDir.X + Cand.Y * GoalDir.Y;
					if (!bGapFound || GoalDot > BestGoalDot) { BestGoalDot = GoalDot; GapDir = Cand; bGapFound = true; }
				}
			}

			if (bGapFound)
			{
				// Nudge toward the gap: the lateral (Right) component of the chosen heading, scaled by
				// resolve strength. A straight-through gap (GapDir == Heading) yields zero nudge — the
				// mover threads dead ahead instead of being shoved off a clear lane.
				const FFixedPoint LatComp = GapDir.X * Right.X + GapDir.Y * Right.Y;
				Accum.X += Right.X * (LatComp * IdleResolveStrength);
				Accum.Y += Right.Y * (LatComp * IdleResolveStrength);
			}
			else
			{
				// No admitting gap → the pre-gap-seek behaviour: summed geometric repulsion away from
				// the idle wall (each blocker's HeadOn*Falloff*side), scaled by resolve strength.
				FFixedPoint DetourSum = FFixedPoint::Zero;
				for (int32 b = 0; b < NumIdleBlockers; ++b) { DetourSum += IdleDetour[b]; }
				Accum.X += Right.X * (DetourSum * IdleResolveStrength);
				Accum.Y += Right.Y * (DetourSum * IdleResolveStrength);
			}
		}

		FinalizeMovingOutput(
			OutputParameters, Index, *Move, Accum, SelfRadius,
			GoalDistSq, SelfBrokerHandle, SelfCohesionId,
			SelfBrokerData);
	});

	// Publish only real state changes, serially in canonical handle order.
	PublishDeferredMovementState(MoveStorage, TickState);
}
