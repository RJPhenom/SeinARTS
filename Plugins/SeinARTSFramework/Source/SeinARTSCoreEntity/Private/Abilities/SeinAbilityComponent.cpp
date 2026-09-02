/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityComponent.cpp
 * @brief   FSeinAbilityPayload sim-payload implementation — tag-based lookup
 *          across an entity's granted ability instances, plus command-context
 *          resolver that picks an ability tag from DefaultCommands.
 *
 *          Phase 4 architecture: ability storage is int32 pool IDs, not live
 *          UObject refs. Accessors take a `USeinWorldSubsystem&` and route
 *          through `World.GetAbilityInstance(ID)` for the lookup.
 */

#include "Components/SeinAbilityPayload.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinAbility* FSeinAbilityPayload::GetActiveAbility(const USeinWorldSubsystem& World) const
{
	return World.GetAbilityInstance(ActiveAbilityID);
}

TArray<USeinAbility*> FSeinAbilityPayload::GetAbilityInstances(const USeinWorldSubsystem& World) const
{
	TArray<USeinAbility*> Out;
	Out.Reserve(AbilityInstanceIDs.Num());
	for (int32 ID : AbilityInstanceIDs)
	{
		if (USeinAbility* A = World.GetAbilityInstance(ID))
		{
			Out.Add(A);
		}
	}
	return Out;
}

TArray<USeinAbility*> FSeinAbilityPayload::GetActivePassives(const USeinWorldSubsystem& World) const
{
	TArray<USeinAbility*> Out;
	Out.Reserve(ActivePassiveIDs.Num());
	for (int32 ID : ActivePassiveIDs)
	{
		if (USeinAbility* A = World.GetAbilityInstance(ID))
		{
			Out.Add(A);
		}
	}
	return Out;
}

USeinAbility* FSeinAbilityPayload::FindAbilityByTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const
{
	for (int32 ID : AbilityInstanceIDs)
	{
		USeinAbility* A = World.GetAbilityInstance(ID);
		if (A && A->AbilityTag == Tag)
		{
			return A;
		}
	}
	return nullptr;
}

bool FSeinAbilityPayload::HasAbilityWithTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const
{
	return FindAbilityByTag(World, Tag) != nullptr;
}

USeinAbility* FSeinAbilityPayload::FindMoveAbility(const USeinWorldSubsystem& World) const
{
	for (int32 ID : AbilityInstanceIDs)
	{
		USeinAbility* A = World.GetAbilityInstance(ID);
		if (A && A->bIsMoveAbility) return A;
	}
	return nullptr;
}

bool FSeinAbilityPayload::HasMoveAbility(const USeinWorldSubsystem& World) const
{
	return FindMoveAbility(World) != nullptr;
}

bool FSeinAbilityPayload::HasAbilityOfClass(const USeinWorldSubsystem& World, const UClass* AbilityClass) const
{
	if (!AbilityClass) return false;
	for (int32 ID : AbilityInstanceIDs)
	{
		const USeinAbility* A = World.GetAbilityInstance(ID);
		if (A && A->GetClass() == AbilityClass) return true;
	}
	return false;
}

int32 FSeinAbilityPayload::GetAbilityGrantCount(const USeinWorldSubsystem& World, const UClass* AbilityClass) const
{
	if (!AbilityClass) return 0;
	// Walk parallel to AbilityGrantCounts. Entries are kept in lockstep with
	// AbilityInstanceIDs by the grant/revoke BPFL — if the invariant ever
	// drifts (stale pool slot, etc.), we read 0 for the mismatched slot,
	// which is the conservative answer.
	const int32 N = FMath::Min(AbilityInstanceIDs.Num(), AbilityGrantCounts.Num());
	for (int32 i = 0; i < N; ++i)
	{
		const USeinAbility* A = World.GetAbilityInstance(AbilityInstanceIDs[i]);
		if (A && A->GetClass() == AbilityClass) return AbilityGrantCounts[i];
	}
	return 0;
}

FGameplayTag FSeinAbilityPayload::ResolveCommandContext(const FGameplayTagContainer& Context) const
{
	// Find the highest-priority mapping whose RequiredContext tags are all present in Context.
	const FSeinCommandMapping* BestMatch = nullptr;

	for (const FSeinCommandMapping& Mapping : DefaultCommands)
	{
		if (!Mapping.AbilityTag.IsValid())
		{
			continue;
		}
		if (!Context.HasAll(Mapping.RequiredContext))
		{
			continue;
		}
		if (!BestMatch || Mapping.Priority > BestMatch->Priority)
		{
			BestMatch = &Mapping;
		}
	}

	return BestMatch ? BestMatch->AbilityTag : FallbackAbilityTag;
}
