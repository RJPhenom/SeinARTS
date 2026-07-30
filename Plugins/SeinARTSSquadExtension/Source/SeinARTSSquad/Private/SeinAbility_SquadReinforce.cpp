/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbility_SquadReinforce.cpp
 * @brief   Starter squad-reinforce ability — enqueues a reinforce entry on
 *          the squad's reinforce queue. Cost from the slot, charged at
 *          enqueue. Squad system handles build progression + spawn.
 */

#include "SeinAbility_SquadReinforce.h"
#include "Components/SeinSquadComponent.h"
#include "Lib/SeinResourceBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Data/SeinResourceTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadReinforce, Log, All);

namespace SeinSquadReinforceLocal
{
	// First slot eligible to enqueue right now: empty AND off-cooldown.
	// (Per-slot CurrentCooldown is set by the squad system after a previous
	// reinforce completes; non-zero blocks re-enqueue until decay.)
	static int32 FindFirstEnqueuableSlot(const FSeinSquadComponent& Squad)
	{
		for (int32 i = 0; i < Squad.Slots.Num(); ++i)
		{
			const FSeinSquadSlot& Slot = Squad.Slots[i];
			if (Slot.CurrentOccupant.IsValid()) continue;
			if (Slot.CurrentCooldown > FFixedPoint::Zero) continue;
			if (!Slot.Entity || Slot.Entity->HasAnyClassFlags(CLASS_Abstract)) continue;
			// Identify the slot via its discriminator tag (first tag in container).
			FGameplayTag DiscTag;
			for (const FGameplayTag& Tag : Slot.SlotTags) { DiscTag = Tag; break; }
			if (!DiscTag.IsValid()) continue;
			// Don't enqueue duplicates for a slot already in the queue.
			bool bAlreadyQueued = false;
			for (const FSeinSquadReinforceEntry& Entry : Squad.ReinforceQueue)
			{
				if (Entry.SlotTag == DiscTag) { bAlreadyQueued = true; break; }
			}
			if (bAlreadyQueued) continue;
			return i;
		}
		return INDEX_NONE;
	}
}

USeinAbility_SquadReinforce::USeinAbility_SquadReinforce()
{
	// Cost lives on the slot, not on the ability — clear the ability-level field
	// so the framework's standard cost gate no-ops. We charge per-slot ourselves
	// in OnActivate.
	ResourceCost = FSeinResourceCost();
	Cooldown = FFixedPoint::Zero;
	bIsPassive = false;
	TargetType = ESeinAbilityTargetType::Self;
	CooldownScope = ESeinCooldownScope::Squad;
}

bool USeinAbility_SquadReinforce::CanActivate_Implementation()
{
	if (!WorldSubsystem) return false;
	const FSeinSquadComponent* Squad = WorldSubsystem->GetComponent<FSeinSquadComponent>(OwnerEntity);
	if (!Squad)
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: owner %s is not a squad (no FSeinSquadComponent)"),
			*OwnerEntity.ToString());
		return false;
	}
	if (!Squad->bCanReinforce)
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: squad %s has bCanReinforce=false"),
			*OwnerEntity.ToString());
		return false;
	}
	const int32 SlotIdx = SeinSquadReinforceLocal::FindFirstEnqueuableSlot(*Squad);
	if (SlotIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: squad %s has no enqueuable slot"),
			*OwnerEntity.ToString());
		return false;
	}
	// Affordability check on the chosen slot's cost.
	const FSeinResourceCost& SlotCost = Squad->Slots[SlotIdx].ReinforceCost;
	const FSeinPlayerID Owner = WorldSubsystem->GetEntityOwner(OwnerEntity);
	if (!SlotCost.IsEmpty() && !USeinResourceBPFL::SeinCanAfford(WorldSubsystem, Owner, SlotCost))
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: player %s cannot afford reinforce (slot index %d)"),
			*Owner.ToString(), SlotIdx);
		return false;
	}
	return true;
}

void USeinAbility_SquadReinforce::OnActivate_Implementation()
{
	if (!WorldSubsystem) { EndAbility(); return; }
	FSeinSquadComponent* Squad = WorldSubsystem->GetComponentMutable<FSeinSquadComponent>(OwnerEntity);
	if (!Squad) { EndAbility(); return; }

	const int32 SlotIdx = SeinSquadReinforceLocal::FindFirstEnqueuableSlot(*Squad);
	if (SlotIdx == INDEX_NONE) { EndAbility(); return; }

	const FSeinSquadSlot& Slot = Squad->Slots[SlotIdx];
	const FSeinResourceCost& SlotCost = Slot.ReinforceCost;
	const FSeinPlayerID Owner = WorldSubsystem->GetEntityOwner(OwnerEntity);

	// Resolve the slot's discriminator tag (first tag in container).
	FGameplayTag SlotTag;
	for (const FGameplayTag& Tag : Slot.SlotTags) { SlotTag = Tag; break; }

	// Charge cost (skipped silently if empty; CanAfford was checked above).
	if (!SlotCost.IsEmpty())
	{
		USeinResourceBPFL::SeinDeduct(WorldSubsystem, Owner, SlotCost);
	}

	// Enqueue reinforce entry. Squad system progresses BuildProgress each tick
	// and spawns the member when BuildProgress >= TotalBuildTime.
	FSeinSquadReinforceEntry Entry;
	Entry.SlotTag = SlotTag;
	Entry.BuildProgress = FFixedPoint::Zero;
	Entry.TotalBuildTime = Slot.ReinforceBuildTime;
	Entry.DeductedCost = SlotCost;
	Squad->ReinforceQueue.Add(Entry);

	UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("Squad %s queued reinforce for slot %s (build=%s, cooldown=%s)"),
		*OwnerEntity.ToString(), *SlotTag.ToString(),
		*Entry.TotalBuildTime.ToString(), *Slot.ReinforceCooldown.ToString());

	// Ability is one-shot — end immediately. The reinforcement progresses on
	// its own via the squad system; no further per-tick work for this ability.
	EndAbility();
}
