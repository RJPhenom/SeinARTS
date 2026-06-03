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
}

void USeinSelectionModel::HandleSelectionChanged()
{
	RebuildFromController();
	OnSelectionChanged.Broadcast();
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
	TArray<FSeinAbilityInfo> Out;

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

	Out.Reserve(TagOrder.Num());
	for (const FGameplayTag& Tag : TagOrder)
	{
		Out.Add(USeinEntityViewModel::MergeAbilityInfos(PerTag[Tag]));
	}
	return Out;
}

FSeinAbilityInfo USeinSelectionModel::GetSelectionAbilityByTag(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return FSeinAbilityInfo();

	// Collect per-view-model infos for this specific tag, then merge.
	// Each view model's GetAbilityByTag is squad-aware (returns the
	// per-tag merged info across the squad's contributors); composing
	// across the selection's view models gives the final selection-wide
	// merged info via MergeAbilityInfos.
	TArray<FSeinAbilityInfo> Infos;
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (!VM) continue;
		FSeinAbilityInfo Info = VM->GetAbilityByTag(Tag);
		if (Info.AbilityTag.IsValid())
		{
			Infos.Add(Info);
		}
	}

	if (Infos.Num() == 0) return FSeinAbilityInfo();
	return USeinEntityViewModel::MergeAbilityInfos(Infos);
}

bool USeinSelectionModel::SelectionHasAbilityWithTag(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return false;
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (VM && VM->HasAbilityWithTag(Tag))
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
