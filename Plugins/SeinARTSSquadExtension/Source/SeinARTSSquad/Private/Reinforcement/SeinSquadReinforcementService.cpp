/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadReinforcementService.cpp
 */

#include "Reinforcement/SeinSquadReinforcementService.h"

#include "Lib/SeinResourceBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadReinforcement, Log, All);

FGameplayTag FSeinSquadReinforcementService::ResolveCanonicalSlotTag(
	const FSeinSquadSlot& Slot)
{
	return Slot.GetCanonicalSlotTag();
}

bool FSeinSquadReinforcementService::IsSlotEnqueueable(
	const FSeinSquadComponent& Squad,
	int32 SlotIndex)
{
	if (!Squad.Slots.IsValidIndex(SlotIndex)) return false;
	const FSeinSquadSlot& Slot = Squad.Slots[SlotIndex];
	if (Slot.CurrentOccupant.IsValid()
		|| Slot.CurrentCooldown != FFixedPoint::Zero
		|| !Slot.Entity
		|| Slot.Entity->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	for (const FSeinSquadReinforceEntry& Entry : Squad.ReinforceQueue)
	{
		if (Entry.RequestedSlotIndex == SlotIndex)
		{
			return false;
		}
	}
	return true;
}

int32 FSeinSquadReinforcementService::FindFirstEnqueueableSlot(
	const FSeinSquadComponent& Squad)
{
	for (int32 SlotIndex = 0; SlotIndex < Squad.Slots.Num(); ++SlotIndex)
	{
		if (IsSlotEnqueueable(Squad, SlotIndex))
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool FSeinSquadReinforcementService::TryEnqueue(
	USeinWorldSubsystem& World,
	FSeinEntityHandle SquadHandle,
	int32 SlotIndex,
	int64& OutRequestID)
{
	OutRequestID = 0;
	if (!World.RequireStateMutationAuthorization(
			TEXT("QueueSquadReinforcement")))
	{
		return false;
	}

	FSeinSquadComponent* Squad =
		World.GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad || !Squad->bCanReinforce
		|| !IsSlotEnqueueable(*Squad, SlotIndex)
		|| Squad->NextReinforceRequestID <= 0
		|| Squad->NextReinforceRequestID == MAX_int64)
	{
		return false;
	}

	const FSeinSquadSlot& Slot = Squad->Slots[SlotIndex];
	if (Slot.ReinforceBuildTime < FFixedPoint::Zero
		|| Slot.ReinforceCooldown < FFixedPoint::Zero)
	{
		return false;
	}
	for (const auto& CostPair : Slot.ReinforceCost.Amounts)
	{
		if (!CostPair.Key.IsValid()
			|| CostPair.Value < FFixedPoint::Zero)
		{
			return false;
		}
	}
	if (const TSubclassOf<ASeinActor> SquadActorClass =
		World.GetEntityActorClass(SquadHandle))
	{
		if (Slot.Entity.Get() == SquadActorClass.Get())
		{
			return false;
		}
	}
	const FSeinResourceCost DeductedCost = Slot.ReinforceCost;
	const FSeinPlayerID ResourcePayer =
		World.GetEntityOwner(SquadHandle);
	if (!USeinResourceBPFL::SeinDeduct(
			&World, ResourcePayer, DeductedCost))
	{
		return false;
	}

	FSeinSquadReinforceEntry Entry;
	Entry.RequestID = Squad->NextReinforceRequestID++;
	Entry.RequestedSlotIndex = SlotIndex;
	Entry.SlotTag = ResolveCanonicalSlotTag(Slot);
	Entry.BuildProgress = FFixedPoint::Zero;
	Entry.TotalBuildTime = Slot.ReinforceBuildTime;
	Entry.DeductedCost = DeductedCost;
	Entry.ResourcePayer = ResourcePayer;
	OutRequestID = Entry.RequestID;
	Squad->ReinforceQueue.Add(Entry);

	UE_LOG(LogSeinSquadReinforcement, Verbose,
		TEXT("Squad %s queued reinforcement request %lld for slot %d (%s)."),
		*SquadHandle.ToString(), Entry.RequestID, SlotIndex,
		*Entry.SlotTag.ToString());
	return true;
}

bool FSeinSquadReinforcementService::CancelByRequestID(
	USeinWorldSubsystem& World,
	FSeinEntityHandle SquadHandle,
	int64 RequestID)
{
	if (RequestID <= 0
		|| !World.RequireStateMutationAuthorization(
			TEXT("CancelSquadReinforcement")))
	{
		return false;
	}
	FSeinSquadComponent* Squad =
		World.GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) return false;

	const int32 EntryIndex = Squad->ReinforceQueue.IndexOfByPredicate(
		[RequestID](const FSeinSquadReinforceEntry& Entry)
		{
			return Entry.RequestID == RequestID;
		});
	if (EntryIndex == INDEX_NONE) return false;

	const FSeinSquadReinforceEntry Entry =
		Squad->ReinforceQueue[EntryIndex];
	if (!USeinResourceBPFL::SeinTryReverseDeduction(
			&World, Entry.ResourcePayer, Entry.DeductedCost))
	{
		return false;
	}
	Squad->ReinforceQueue.RemoveAt(EntryIndex);
	return true;
}

int32 FSeinSquadReinforcementService::CancelForSlot(
	USeinWorldSubsystem& World,
	FSeinEntityHandle SquadHandle,
	int32 SlotIndex)
{
	if (SlotIndex < 0
		|| !World.RequireStateMutationAuthorization(
			TEXT("CancelSquadReinforcementForSlot")))
	{
		return 0;
	}
	FSeinSquadComponent* Squad =
		World.GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) return 0;

	int32 Cancelled = 0;
	for (int32 EntryIndex = Squad->ReinforceQueue.Num() - 1;
		EntryIndex >= 0; --EntryIndex)
	{
		const FSeinSquadReinforceEntry Entry =
			Squad->ReinforceQueue[EntryIndex];
		if (Entry.RequestedSlotIndex != SlotIndex) continue;
		if (!USeinResourceBPFL::SeinTryReverseDeduction(
				&World, Entry.ResourcePayer, Entry.DeductedCost))
		{
			continue;
		}
		Squad->ReinforceQueue.RemoveAt(EntryIndex);
		++Cancelled;
	}
	return Cancelled;
}
