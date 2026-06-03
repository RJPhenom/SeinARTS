/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinPlayerState.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		FSeinPlayerState implementation. Player tag queries are driven
 *				by `PlayerTags` (refcount-backed, DESIGN §10) — grant/ungrant
 *				live on `USeinWorldSubsystem` so 0↔1 edges can route through
 *				the shared effect pipeline.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Core/SeinPlayerState.h"

FSeinPlayerState::FSeinPlayerState(FSeinPlayerID InPlayerID, FSeinFactionID InFactionID, uint8 InTeamID)
	: PlayerID(InPlayerID)
	, FactionID(InFactionID)
	, TeamID(InTeamID)
	, bEliminated(false)
	, bReady(false)
{
}

bool FSeinPlayerState::CanAfford(const FSeinResourceCost& Cost) const
{
	for (const auto& Entry : Cost.Amounts)
	{
		const FFixedPoint* Current = Resources.Find(Entry.Key);
		if (!Current || *Current < Entry.Value)
		{
			return false;
		}
	}
	return true;
}

bool FSeinPlayerState::DeductResources(const FSeinResourceCost& Cost)
{
	if (!CanAfford(Cost))
	{
		return false;
	}

	for (const auto& Entry : Cost.Amounts)
	{
		FFixedPoint& Current = Resources.FindChecked(Entry.Key);
		Current = Current - Entry.Value;
	}
	return true;
}

void FSeinPlayerState::AddResources(const FSeinResourceCost& Amount)
{
	for (const auto& Entry : Amount.Amounts)
	{
		FFixedPoint& Current = Resources.FindOrAdd(Entry.Key);
		Current = Current + Entry.Value;
	}
}

FFixedPoint FSeinPlayerState::GetResource(FGameplayTag ResourceTag) const
{
	const FFixedPoint* Found = Resources.Find(ResourceTag);
	return Found ? *Found : FFixedPoint::Zero;
}

void FSeinPlayerState::SetResource(FGameplayTag ResourceTag, FFixedPoint Amount)
{
	Resources.FindOrAdd(ResourceTag) = Amount;
}

// ==================== Tag Helpers ====================

bool FSeinPlayerState::HasPlayerTag(FGameplayTag Tag) const
{
	return Tag.IsValid() && PlayerTags.HasTag(Tag);
}

bool FSeinPlayerState::HasAllPlayerTags(const FGameplayTagContainer& Required) const
{
	if (Required.IsEmpty())
	{
		return true;
	}
	return PlayerTags.HasAll(Required);
}
