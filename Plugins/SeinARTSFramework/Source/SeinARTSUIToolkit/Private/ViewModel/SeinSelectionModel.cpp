/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionModel.cpp
 * @brief   Selection model implementation.
 */

#include "ViewModel/SeinSelectionModel.h"
#include "ViewModel/SeinEntityViewModel.h"
#include "Core/SeinUISubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Actor/SeinActor.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void USeinSelectionModel::Initialize(USeinUISubsystem* InOwningSubsystem)
{
	OwningSubsystem = InOwningSubsystem;

	// First attempt. Almost always a no-op: WorldSubsystem::Initialize runs
	// before the player controller is spawned, so GetFirstPlayerController()
	// returns null here. The UI subsystem's sim-tick handler retries every
	// tick until the PC is available.
	EnsurePlayerControllerBound();
}

void USeinSelectionModel::EnsurePlayerControllerBound()
{
	if (CachedPlayerController.IsValid())
	{
		return;
	}

	UWorld* World = OwningSubsystem.IsValid() ? OwningSubsystem->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(World->GetFirstPlayerController());
	if (!PC)
	{
		return;
	}

	CachedPlayerController = PC;
	PC->OnSelectionChanged.AddDynamic(this, &USeinSelectionModel::HandleSelectionChanged);

	// Prime the VM list + fire an initial broadcast so any widgets already
	// bound to OnSelectionChanged (e.g. from Event Construct) get their first
	// notification with the current selection state.
	HandleSelectionChanged();
}

void USeinSelectionModel::Deinitialize()
{
	if (CachedPlayerController.IsValid())
	{
		CachedPlayerController->OnSelectionChanged.RemoveDynamic(this, &USeinSelectionModel::HandleSelectionChanged);
	}
	CachedPlayerController.Reset();
	SelectedViewModels.Empty();
	CachedSelectionAbilities.Empty();
	BuiltAbilityCacheGeneration = 0;
}

void USeinSelectionModel::HandleSelectionChanged()
{
	RebuildFromController();
	InvalidateAbilityCache();
	OnSelectionChanged.Broadcast();
}

void USeinSelectionModel::InvalidateAbilityCache()
{
	++AbilityCacheGeneration;
	if (AbilityCacheGeneration == 0)
	{
		// A wrap is practically unreachable, but preserving a distinct invalid
		// generation keeps the contract total even in a soak test lasting years.
		AbilityCacheGeneration = 1;
		BuiltAbilityCacheGeneration = 0;
	}
}

void USeinSelectionModel::RebuildFromController()
{
	SelectedViewModels.Empty();

	if (!CachedPlayerController.IsValid() || !OwningSubsystem.IsValid())
	{
		CachedFocusIndex = -1;
		return;
	}

	ASeinPlayerController* PC = CachedPlayerController.Get();
	USeinUISubsystem* Subsystem = OwningSubsystem.Get();

	CachedFocusIndex = PC->ActiveFocusIndex;

	TArray<ASeinActor*> SelectedActors = PC->GetValidSelectedActors();
	for (ASeinActor* Actor : SelectedActors)
	{
		if (Actor && Actor->HasValidEntity())
		{
			USeinEntityViewModel* VM = Subsystem->GetEntityViewModel(Actor->GetEntityHandle());
			if (VM)
			{
				SelectedViewModels.Add(VM);
			}
		}
	}
}

// ==================== Queries ====================

TArray<USeinEntityViewModel*> USeinSelectionModel::GetSelectedViewModels() const
{
	TArray<USeinEntityViewModel*> Result;
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (VM)
		{
			Result.Add(VM);
		}
	}
	return Result;
}

USeinEntityViewModel* USeinSelectionModel::GetFocusedViewModel() const
{
	if (CachedFocusIndex < 0 || CachedFocusIndex >= SelectedViewModels.Num())
	{
		return nullptr;
	}
	return SelectedViewModels[CachedFocusIndex];
}

USeinEntityViewModel* USeinSelectionModel::GetPrimaryViewModel() const
{
	// If a specific entity is focused, return that
	USeinEntityViewModel* Focused = GetFocusedViewModel();
	if (Focused)
	{
		return Focused;
	}

	// Otherwise return the first selected entity
	if (SelectedViewModels.Num() > 0 && SelectedViewModels[0])
	{
		return SelectedViewModels[0];
	}

	return nullptr;
}

TArray<FSeinAbilityInfo> USeinSelectionModel::GetSelectionAbilities() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Selection_GetAbilities);
	EnsureAbilityCache();
	return CachedSelectionAbilities;
}

void USeinSelectionModel::EnsureAbilityCache() const
{
	if (BuiltAbilityCacheGeneration == AbilityCacheGeneration)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Selection_RebuildAbilityCache);
	CachedSelectionAbilities.Reset();

	// Group by tag across all selected view models. Each view model returns
	// per-entity-aggregated infos (squad-aware for squads, single-AC for
	// lone units), so the selection-level merge composes via the same
	// MergeAbilityInfos rules. Order: first-encounter per tag in selection
	// iteration order — stable across ticks given a stable selection list.
	TArray<FGameplayTag> TagOrder;
	TMap<FGameplayTag, TArray<FSeinAbilityInfo>> PerTag;

	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (!VM) continue;
		for (const FSeinAbilityInfo& Info : VM->GetAbilities())
		{
			if (!Info.AbilityTag.IsValid()) continue;
			if (!PerTag.Contains(Info.AbilityTag))
			{
				TagOrder.Add(Info.AbilityTag);
			}
			PerTag.FindOrAdd(Info.AbilityTag).Add(Info);
		}
	}

	CachedSelectionAbilities.Reserve(TagOrder.Num());
	for (const FGameplayTag& Tag : TagOrder)
	{
		CachedSelectionAbilities.Add(USeinEntityViewModel::MergeAbilityInfos(PerTag[Tag]));
	}
	BuiltAbilityCacheGeneration = AbilityCacheGeneration;
}

FSeinAbilityInfo USeinSelectionModel::GetSelectionAbilityByTag(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return FSeinAbilityInfo();
	EnsureAbilityCache();
	for (const FSeinAbilityInfo& Info : CachedSelectionAbilities)
	{
		if (Info.AbilityTag == Tag)
		{
			return Info;
		}
	}
	return FSeinAbilityInfo();
}

bool USeinSelectionModel::SelectionHasAbilityWithTag(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return false;
	EnsureAbilityCache();
	for (const FSeinAbilityInfo& Info : CachedSelectionAbilities)
	{
		if (Info.AbilityTag == Tag)
		{
			return true;
		}
	}
	return false;
}

int32 USeinSelectionModel::GetSelectionCount() const
{
	return SelectedViewModels.Num();
}

int32 USeinSelectionModel::GetActiveFocusIndex() const
{
	return CachedFocusIndex;
}

bool USeinSelectionModel::IsEntitySelected(FSeinEntityHandle Handle) const
{
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (VM && VM->Entity == Handle)
		{
			return true;
		}
	}
	return false;
}

bool USeinSelectionModel::IsEntityFocused(FSeinEntityHandle Handle) const
{
	USeinEntityViewModel* Focused = GetFocusedViewModel();
	return Focused && Focused->Entity == Handle;
}
