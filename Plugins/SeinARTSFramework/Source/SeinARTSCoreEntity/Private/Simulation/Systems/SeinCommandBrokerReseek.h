/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCommandBrokerReseek.h
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       25 Aug 2026
 * @brief        Implements deterministic idle return for brokered formations
 *               and displaced loose units.
 *
 *               This private kernel owns re-seek pairing, traffic clearance,
 *               release cadence, and reusable query scratch. The command
 *               broker system remains responsible for tick orchestration.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinAbility.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Settings/PluginSettings.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

/** Private deterministic idle re-seek implementation used by the command
 *  broker system. All persistent state remains in canonical components. */
class FSeinCommandBrokerReseek final
{
public:
	/** Collects settled, displaced movers that do not currently belong to a
	 *  broker. Broker creation is deferred until after storage iteration. */
	void CollectLooseReturnCandidates(
		USeinWorldSubsystem& World,
		TArray<FSeinEntityHandle>& OutCandidates) const
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		if (!Settings || !Settings->bIdleReseek)
		{
			return;
		}

		const ISeinComponentStorage* MovementStorage =
			World.GetComponentStorageRaw(FSeinMovementPayload::StaticStruct());
		if (!MovementStorage)
		{
			return;
		}

		MovementStorage->ForEachLiveComponent([&](
			FSeinEntityHandle Handle, const void* RawComponent)
		{
			const FSeinMovementPayload* Movement =
				static_cast<const FSeinMovementPayload*>(RawComponent);
			if (!Movement || !Movement->bHomeSeeded
				|| Movement->bHasTarget
				|| Movement->Velocity.SizeSquared() > FFixedPoint::Epsilon)
			{
				return;
			}

			if (!World.GetEntityPool().IsValid(Handle)) return;
			const FSeinEntity* Entity = World.GetEntity(Handle);
			if (!Entity) return;
			// Preserve broker-first semantics: a broker carrier is never loose.
			if (World.GetComponent<FSeinCommandBrokerData>(Handle)) return;

			const FSeinBrokerMembershipData* Membership =
				World.GetComponent<FSeinBrokerMembershipData>(Handle);
			const bool bBrokered = Membership
				&& Membership->CurrentBrokerHandle.IsValid()
				&& World.GetEntityPool().IsValid(
					Membership->CurrentBrokerHandle);
			const FSeinContainmentMemberData* Containment =
				World.GetComponent<FSeinContainmentMemberData>(Handle);
			const bool bContained = Containment
				&& Containment->CurrentContainer.IsValid();
			const FSeinAbilityPayload* AbilityComponent =
				World.GetComponent<FSeinAbilityPayload>(Handle);
			const USeinAbility* ActiveAbility = AbilityComponent
				? AbilityComponent->GetActiveAbility(World)
				: nullptr;
			if (bBrokered || bContained
				|| (ActiveAbility && ActiveAbility->bIsActive))
			{
				return;
			}

			FFixedVector Delta =
				Entity->Transform.GetLocation() - Movement->HomePos;
			Delta.Z = FFixedPoint::Zero;
			const FFixedPoint Threshold = GetEffectiveThreshold(
				World,
				Handle,
				Settings->ReseekDisplacementThreshold);
			if (Delta.SizeSquared() > Threshold * Threshold)
			{
				OutCandidates.Add(Handle);
			}
		});
	}

	/** Evaluates one broker after ordinary order dispatch has completed for the
	 *  tick. May enqueue one-member internal return orders. */
	void ProcessBroker(
		USeinWorldSubsystem& World,
		FSeinCommandBrokerData& Broker,
		int32 CurrentTick)
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		if (!Settings || !Settings->bIdleReseek
			|| Broker.Members.IsEmpty()
			|| Broker.SettledSlotPositions.Num() != Broker.Members.Num()
			|| CurrentTick < Broker.NextReseekAllowedTick)
		{
			return;
		}

		const int32 TickRate = Settings->SimulationTickRate > 1
			? Settings->SimulationTickRate
			: 1;
		const int32 WatchTicks = SecondsToAtLeastOneTick(
			Settings->ReseekWatchInterval, TickRate);
		const int32 ReleaseTicks = SecondsToAtLeastOneTick(
			Settings->ReseekReleaseInterval, TickRate);
		const int32 MaxEpisodeTicks = SecondsToOptionalTickLimit(
			Settings->ReseekMaxEpisodeSeconds, TickRate);
		Broker.NextReseekAllowedTick = CurrentTick + WatchTicks;

		const bool bEpisodeCapped = Broker.ReseekEpisodeStartTick != 0
			&& MaxEpisodeTicks > 0
			&& CurrentTick - Broker.ReseekEpisodeStartTick > MaxEpisodeTicks;
		if (bEpisodeCapped)
		{
			Broker.ReseekEpisodeStartTick = 0;
			Broker.NextReseekAllowedTick = CurrentTick + (TickRate * 2);
			return;
		}
		if (HasForeignOrder(Broker))
		{
			return;
		}

		TSet<FSeinEntityHandle> ReleasedMembers;
		TArray<FSeinEntityHandle> UnreleasedMembers;
		TArray<FFixedVector> PairedSlots;
		if (!BuildReturnPairing(
			World, Broker, ReleasedMembers,
			UnreleasedMembers, PairedSlots)
			|| UnreleasedMembers.IsEmpty())
		{
			return;
		}

		TArray<FFixedVector> TrafficPositions;
		TArray<FFixedPoint> TrafficRadii;
		GatherMovingForeignTraffic(
			World, Broker, UnreleasedMembers,
			TrafficPositions, TrafficRadii);

		const FReleaseScanResult ReleaseResult = ReleaseEligibleMembers(
			World, Broker, CurrentTick, TickRate,
			Settings->ReseekDisplacementThreshold,
			UnreleasedMembers, PairedSlots,
			TrafficPositions, TrafficRadii);
		const bool bInFlight = !ReleasedMembers.IsEmpty()
			|| ReleaseResult.ReleasedCount > 0;
		if (!ReleaseResult.bAnyDisplaced && !bInFlight)
		{
			if (Broker.ReseekEpisodeStartTick != 0)
			{
				Broker.ReseekEpisodeStartTick = 0;
				Broker.NextReseekAllowedTick = CurrentTick + TickRate;
			}
		}
		else
		{
			Broker.NextReseekAllowedTick = CurrentTick + ReleaseTicks;
		}
	}

	/** Creates brokers for the loose return candidates after sparse component
	 *  iteration has ended. Candidate order is deterministic storage order. */
	void IssueLooseReturns(
		USeinWorldSubsystem& World,
		const TArray<FSeinEntityHandle>& Candidates) const
	{
		for (const FSeinEntityHandle& Handle : Candidates)
		{
			const FSeinMovementPayload* Movement =
				World.GetComponent<FSeinMovementPayload>(Handle);
			if (!Movement || !Movement->bHomeSeeded) continue;

			// An earlier candidate may have synchronously created this membership.
			const FSeinBrokerMembershipData* Membership =
				World.GetComponent<FSeinBrokerMembershipData>(Handle);
			if (Membership && Membership->CurrentBrokerHandle.IsValid()
				&& World.GetEntityPool().IsValid(
					Membership->CurrentBrokerHandle))
			{
				continue;
			}

			FSeinBrokerQueuedOrder Order;
			Order.TargetMembers.Add(Handle);
			Order.PreplacedMembers.Add(Handle);
			Order.PreplacedPositions.Add(Movement->HomePos);
			Order.Context.AddTag(
				SeinARTSTags::Command_Context_RightClick);
			Order.Context.AddTag(
				SeinARTSTags::Command_Context_Target_Ground);
			Order.TargetLocation = Movement->HomePos;

			TArray<FSeinEntityHandle> Members;
			Members.Add(Handle);
			World.CreateBrokerForMembers(
				Members, World.GetEntityOwner(Handle), Order);
		}
	}

private:
	struct FReleaseScanResult
	{
		bool bAnyDisplaced = false;
		int32 ReleasedCount = 0;
	};

	static int32 SecondsToAtLeastOneTick(
		FFixedPoint Seconds,
		int32 TickRate)
	{
		int32 Ticks =
			(Seconds * FFixedPoint::FromInt(TickRate)).ToInt();
		if (Ticks < 1)
		{
			Ticks = 1;
		}
		return Ticks;
	}

	static int32 SecondsToOptionalTickLimit(
		FFixedPoint Seconds,
		int32 TickRate)
	{
		int32 Ticks =
			(Seconds * FFixedPoint::FromInt(TickRate)).ToInt();
		if (Ticks < 0)
		{
			Ticks = 0;
		}
		if (Ticks == 0 && Seconds > FFixedPoint::Zero)
		{
			Ticks = 1;
		}
		return Ticks;
	}

	static FFixedPoint GetEffectiveThreshold(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Member,
		FFixedPoint ConfiguredThreshold)
	{
		FFixedPoint Acceptance = FFixedPoint::Zero;
		if (const FSeinNavigationPayload* Navigation =
			World.GetComponent<FSeinNavigationPayload>(Member))
		{
			Acceptance = Navigation->AcceptanceRadius;
		}
		if (Acceptance <= FFixedPoint::Zero)
		{
			Acceptance =
				FSeinNavigationPayload::DefaultArrivalAcceptance();
		}

		const FFixedPoint HysteresisFloor = Acceptance + Acceptance;
		return ConfiguredThreshold < HysteresisFloor
			? HysteresisFloor
			: ConfiguredThreshold;
	}

	static bool HasForeignOrder(const FSeinCommandBrokerData& Broker)
	{
		for (const FSeinBrokerQueuedOrder& Order : Broker.OrderQueue)
		{
			if (!(Order.bIsInternalPrefix
				&& !Order.PreplacedPositions.IsEmpty()))
			{
				return true;
			}
		}
		return false;
	}

	static bool BuildReturnPairing(
		USeinWorldSubsystem& World,
		const FSeinCommandBrokerData& Broker,
		TSet<FSeinEntityHandle>& OutReleasedMembers,
		TArray<FSeinEntityHandle>& OutUnreleasedMembers,
		TArray<FFixedVector>& OutPairedSlots)
	{
		TArray<FFixedVector> ClaimedSlots;
		for (const FSeinBrokerQueuedOrder& Order : Broker.OrderQueue)
		{
			for (const FSeinEntityHandle& Member : Order.PreplacedMembers)
			{
				OutReleasedMembers.Add(Member);
			}
			ClaimedSlots.Append(Order.PreplacedPositions);
		}

		if (Broker.bSettledSlotsMemberAligned)
		{
			for (int32 Index = 0; Index < Broker.Members.Num(); ++Index)
			{
				if (OutReleasedMembers.Contains(Broker.Members[Index]))
				{
					continue;
				}
				OutUnreleasedMembers.Add(Broker.Members[Index]);
				OutPairedSlots.Add(Broker.SettledSlotPositions[Index]);
			}
			return true;
		}

		for (const FSeinEntityHandle& Member : Broker.Members)
		{
			if (!OutReleasedMembers.Contains(Member))
			{
				OutUnreleasedMembers.Add(Member);
			}
		}

		TArray<FFixedVector> UnclaimedSlots;
		for (const FFixedVector& Slot : Broker.SettledSlotPositions)
		{
			bool bClaimed = false;
			for (const FFixedVector& ClaimedSlot : ClaimedSlots)
			{
				if (ClaimedSlot.X == Slot.X && ClaimedSlot.Y == Slot.Y)
				{
					bClaimed = true;
					break;
				}
			}
			if (!bClaimed)
			{
				UnclaimedSlots.Add(Slot);
			}
		}

		if (OutUnreleasedMembers.Num() != UnclaimedSlots.Num())
		{
			return false;
		}

		OutPairedSlots = UnclaimedSlots;
		USeinDefaultCommandBrokerResolver::ReassignSlots(
			&World, OutUnreleasedMembers, OutPairedSlots,
			Broker.AnchorFacing, true, true);
		return true;
	}

	void GatherMovingForeignTraffic(
		USeinWorldSubsystem& World,
		const FSeinCommandBrokerData& Broker,
		const TArray<FSeinEntityHandle>& UnreleasedMembers,
		TArray<FFixedVector>& OutPositions,
		TArray<FFixedPoint>& OutRadii)
	{
		FFixedVector Min = Broker.SettledSlotPositions[0];
		FFixedVector Max = Min;
		const auto GrowBounds = [&Min, &Max](const FFixedVector& Position)
		{
			if (Position.X < Min.X) Min.X = Position.X;
			if (Position.Y < Min.Y) Min.Y = Position.Y;
			if (Position.X > Max.X) Max.X = Position.X;
			if (Position.Y > Max.Y) Max.Y = Position.Y;
		};
		for (const FFixedVector& Slot : Broker.SettledSlotPositions)
		{
			GrowBounds(Slot);
		}
		for (const FSeinEntityHandle& Member : UnreleasedMembers)
		{
			if (const FSeinEntity* Entity = World.GetEntity(Member))
			{
				GrowBounds(Entity->Transform.GetLocation());
			}
		}

		const FFixedVector Centre(
			(Min.X + Max.X) / FFixedPoint::Two,
			(Min.Y + Max.Y) / FFixedPoint::Two,
			Broker.SettledSlotPositions[0].Z);
		const FFixedVector HalfSpan(
			Max.X - Centre.X, Max.Y - Centre.Y, FFixedPoint::Zero);
		const FFixedPoint GatherRadius =
			HalfSpan.Size() + FFixedPoint::FromInt(400);

		NeighborScratch.Reset();
		World.GetCollisionSpatialHash().QueryRadius(
			Centre, GatherRadius, NeighborScratch, FSeinEntityHandle());
		for (const FSeinEntityHandle& Neighbor : NeighborScratch)
		{
			if (Broker.Members.Contains(Neighbor)) continue;
			const FSeinMovementPayload* Movement =
				World.GetComponent<FSeinMovementPayload>(Neighbor);
			if (!Movement) continue;
			if (!Movement->bHasTarget
				&& Movement->Velocity.SizeSquared() <= FFixedPoint::Epsilon)
			{
				continue;
			}
			const FSeinEntity* Entity = World.GetEntity(Neighbor);
			if (!Entity) continue;

			OutPositions.Add(Entity->Transform.GetLocation());
			OutRadii.Add(GetBodyRadius(World, Neighbor));
		}
	}

	static FReleaseScanResult ReleaseEligibleMembers(
		USeinWorldSubsystem& World,
		FSeinCommandBrokerData& Broker,
		int32 CurrentTick,
		int32 TickRate,
		FFixedPoint ConfiguredThreshold,
		const TArray<FSeinEntityHandle>& UnreleasedMembers,
		const TArray<FFixedVector>& PairedSlots,
		const TArray<FFixedVector>& TrafficPositions,
		const TArray<FFixedPoint>& TrafficRadii)
	{
		FReleaseScanResult Result;
		const int32 JitterWindow = (TickRate * 3) / 2;
		const int32 WindowTicks = JitterWindow > 1 ? JitterWindow : 1;
		for (int32 Index = 0; Index < UnreleasedMembers.Num(); ++Index)
		{
			const FSeinEntityHandle Member = UnreleasedMembers[Index];
			const FSeinMovementPayload* Movement =
				World.GetComponent<FSeinMovementPayload>(Member);
			const FSeinEntity* Entity = World.GetEntity(Member);
			if (!Movement || !Entity) continue;

			const FFixedVector Position = Entity->Transform.GetLocation();
			const FFixedPoint EffectiveThreshold = GetEffectiveThreshold(
				World, Member, ConfiguredThreshold);
			const FFixedPoint DeltaX = Position.X - PairedSlots[Index].X;
			const FFixedPoint DeltaY = Position.Y - PairedSlots[Index].Y;
			if (DeltaX * DeltaX + DeltaY * DeltaY
				<= EffectiveThreshold * EffectiveThreshold)
			{
				continue;
			}

			Result.bAnyDisplaced = true;
			const FSeinAbilityPayload* AbilityComponent =
				World.GetComponent<FSeinAbilityPayload>(Member);
			const USeinAbility* ActiveAbility = AbilityComponent
				? AbilityComponent->GetActiveAbility(World)
				: nullptr;
			if ((ActiveAbility && ActiveAbility->bIsActive)
				|| Movement->bHasTarget
				|| Movement->Velocity.SizeSquared() > FFixedPoint::Epsilon)
			{
				continue;
			}

			if (Broker.ReseekEpisodeStartTick == 0)
			{
				Broker.ReseekEpisodeStartTick = CurrentTick;
			}
			const int32 Jitter = static_cast<int32>(
				GetTypeHash(Member) % static_cast<uint32>(WindowTicks));
			if (CurrentTick < Broker.ReseekEpisodeStartTick + Jitter)
			{
				continue;
			}

			if (!IsCorridorClear(
				World, Member, Position, PairedSlots[Index],
				TrafficPositions, TrafficRadii))
			{
				continue;
			}

			FSeinBrokerQueuedOrder Order;
			Order.TargetMembers.Add(Member);
			Order.PreplacedMembers.Add(Member);
			Order.PreplacedPositions.Add(PairedSlots[Index]);
			Order.Context.AddTag(
				SeinARTSTags::Command_Context_RightClick);
			Order.Context.AddTag(
				SeinARTSTags::Command_Context_Target_Ground);
			Order.TargetLocation = Broker.Anchor;
			Order.bIsInternalPrefix = true;
			Broker.OrderQueue.Add(Order);
			++Result.ReleasedCount;
		}
		return Result;
	}

	static bool IsCorridorClear(
		const USeinWorldSubsystem& World,
		FSeinEntityHandle Member,
		const FFixedVector& Start,
		const FFixedVector& End,
		const TArray<FFixedVector>& TrafficPositions,
		const TArray<FFixedPoint>& TrafficRadii)
	{
		const FFixedPoint MemberRadius = GetBodyRadius(World, Member);
		for (int32 Index = 0; Index < TrafficPositions.Num(); ++Index)
		{
			const FFixedPoint Halfwidth = MemberRadius
				+ TrafficRadii[Index] + FFixedPoint::FromInt(100);
			if (PlanarSegmentDistanceSquared(
				TrafficPositions[Index], Start, End)
				< Halfwidth * Halfwidth)
			{
				return false;
			}
		}
		return true;
	}

	static FFixedPoint GetBodyRadius(
		const USeinWorldSubsystem& World,
		FSeinEntityHandle Entity)
	{
		FFixedPoint Radius = FFixedPoint::Zero;
		if (const FSeinExtentsPayload* Extents =
			World.GetComponent<FSeinExtentsPayload>(Entity))
		{
			Radius = SeinExtentsHelpers::GetColliderBoundingRadius(*Extents);
		}
		if (Radius <= FFixedPoint::Zero)
		{
			if (const FSeinNavigationPayload* Navigation =
				World.GetComponent<FSeinNavigationPayload>(Entity))
			{
				Radius = Navigation->FallbackFootprintRadius;
			}
		}
		if (Radius <= FFixedPoint::Zero)
		{
			Radius = FFixedPoint::FromInt(50);
		}
		return Radius;
	}

	static FFixedPoint PlanarSegmentDistanceSquared(
		const FFixedVector& Point,
		const FFixedVector& Start,
		const FFixedVector& End)
	{
		const FFixedPoint SegmentX = End.X - Start.X;
		const FFixedPoint SegmentY = End.Y - Start.Y;
		const FFixedPoint SegmentLengthSquared =
			SegmentX * SegmentX + SegmentY * SegmentY;
		FFixedPoint Alpha = FFixedPoint::Zero;
		if (SegmentLengthSquared > FFixedPoint::Epsilon)
		{
			Alpha = ((Point.X - Start.X) * SegmentX
				+ (Point.Y - Start.Y) * SegmentY)
				/ SegmentLengthSquared;
			if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
			if (Alpha > FFixedPoint::One) Alpha = FFixedPoint::One;
		}

		const FFixedPoint ClosestX = Start.X + SegmentX * Alpha;
		const FFixedPoint ClosestY = Start.Y + SegmentY * Alpha;
		const FFixedPoint DeltaX = Point.X - ClosestX;
		const FFixedPoint DeltaY = Point.Y - ClosestY;
		return DeltaX * DeltaX + DeltaY * DeltaY;
	}

	/** Reused per scan; command-broker ticks are serial. */
	TArray<FSeinEntityHandle> NeighborScratch;
};
