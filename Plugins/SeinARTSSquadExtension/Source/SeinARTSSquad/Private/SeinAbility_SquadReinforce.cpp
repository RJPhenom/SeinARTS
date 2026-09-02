/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbility_SquadReinforce.cpp
 * @brief   Starter squad-reinforce ability — enqueues a reinforce entry on
 *          the squad's reinforce queue. Cost from the slot, charged at
 *          enqueue. Squad system handles build progression + spawn.
 */

#include "SeinAbility_SquadReinforce.h"
#include "Components/SeinSquadPayload.h"
#include "Lib/SeinResourceBPFL.h"
#include "Reinforcement/SeinSquadReinforcementService.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Data/SeinResourceTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadReinforce, Log, All);

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
	const FSeinSquadPayload* Squad = WorldSubsystem->GetComponent<FSeinSquadPayload>(OwnerEntity);
	if (!Squad)
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: owner %s is not a squad (no FSeinSquadPayload)"),
			*OwnerEntity.ToString());
		return false;
	}
	if (!Squad->bCanReinforce)
	{
		UE_LOG(LogSeinSquadReinforce, Verbose, TEXT("CanActivate: squad %s has bCanReinforce=false"),
			*OwnerEntity.ToString());
		return false;
	}
	const int32 SlotIdx =
		FSeinSquadReinforcementService::FindFirstEnqueueableSlot(*Squad);
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
	const FSeinSquadPayload* Squad =
		WorldSubsystem->GetComponent<FSeinSquadPayload>(OwnerEntity);
	if (!Squad) { EndAbility(); return; }

	const int32 SlotIdx =
		FSeinSquadReinforcementService::FindFirstEnqueueableSlot(*Squad);
	if (SlotIdx == INDEX_NONE) { EndAbility(); return; }

	int64 RequestID = 0;
	if (!FSeinSquadReinforcementService::TryEnqueue(
			*WorldSubsystem, OwnerEntity, SlotIdx, RequestID))
	{
		UE_LOG(LogSeinSquadReinforce, Verbose,
			TEXT("Squad %s reinforcement enqueue lost atomic revalidation."),
			*OwnerEntity.ToString());
	}

	// Ability is one-shot — end immediately. The reinforcement progresses on
	// its own via the squad system; no further per-tick work for this ability.
	EndAbility();
}
