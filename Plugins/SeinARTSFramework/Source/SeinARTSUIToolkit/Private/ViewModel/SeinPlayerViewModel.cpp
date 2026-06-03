/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerViewModel.cpp
 * @brief   Player ViewModel implementation.
 */

#include "ViewModel/SeinPlayerViewModel.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinPlayerState.h"

void USeinPlayerViewModel::Initialize(FSeinPlayerID InPlayerID, USeinWorldSubsystem* InWorldSubsystem)
{
	PlayerID = InPlayerID;
	WorldSubsystem = InWorldSubsystem;

	// Initial refresh to populate cached data
	Refresh();
}

void USeinPlayerViewModel::Refresh()
{
	if (!WorldSubsystem.IsValid())
	{
		return;
	}

	const FSeinPlayerState* State = WorldSubsystem->GetPlayerState(PlayerID);
	if (!State)
	{
		return;
	}

	bIsEliminated = State->bEliminated;

	// Cache resources as float for display
	CachedResources.Empty(State->Resources.Num());
	for (const auto& Pair : State->Resources)
	{
		CachedResources.Add(Pair.Key, Pair.Value.ToFloat());
	}

	CachedTechTags = State->PlayerTags;

	OnRefreshed.Broadcast();
}

// ==================== Resource Access ====================

float USeinPlayerViewModel::GetResource(FGameplayTag ResourceTag) const
{
	const float* Found = CachedResources.Find(ResourceTag);
	return Found ? *Found : 0.0f;
}

TMap<FGameplayTag, float> USeinPlayerViewModel::GetAllResources() const
{
	return CachedResources;
}

TArray<FGameplayTag> USeinPlayerViewModel::GetResourceTags() const
{
	TArray<FGameplayTag> Tags;
	CachedResources.GetKeys(Tags);
	return Tags;
}

bool USeinPlayerViewModel::CanAfford(const TMap<FGameplayTag, float>& Cost) const
{
	for (const auto& Pair : Cost)
	{
		const float* Have = CachedResources.Find(Pair.Key);
		if (!Have || *Have < Pair.Value)
		{
			return false;
		}
	}
	return true;
}

// ==================== Tech Access ====================

bool USeinPlayerViewModel::HasTechTag(FGameplayTag Tag) const
{
	return CachedTechTags.HasTag(Tag);
}

FGameplayTagContainer USeinPlayerViewModel::GetUnlockedTechTags() const
{
	return CachedTechTags;
}
