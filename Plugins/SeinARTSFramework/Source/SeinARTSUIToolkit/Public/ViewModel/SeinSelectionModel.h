/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionModel.h
 * @brief   Tracks the local player's selection state and provides entity
 *          ViewModels for selected units. Bridges the player controller's
 *          selection system with the UI ViewModel layer.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "SeinSelectionModel.generated.h"

class USeinEntityViewModel;
class USeinUISubsystem;
class ASeinPlayerController;
struct FSeinAbilityInfo;

/** Broadcast when the selection or active focus changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSelectionModelChanged);

/**
 * Tracks the current selection and provides entity ViewModels for selected units.
 *
 * Owned by USeinUISubsystem. Automatically listens to the player controller's
 * OnSelectionChanged delegate and rebuilds its ViewModel list.
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinSelectionModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Selection Queries ==========

	/** Get ViewModels for all currently selected entities. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	TArray<USeinEntityViewModel*> GetSelectedViewModels() const;

	/**
	 * Get the ViewModel for the focused entity.
	 * Returns nullptr if active focus is "All" (-1).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	USeinEntityViewModel* GetFocusedViewModel() const;

	/**
	 * Get the "primary" ViewModel: the focused entity if one is focused,
	 * otherwise the first selected entity. Returns nullptr if nothing is selected.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	USeinEntityViewModel* GetPrimaryViewModel() const;

	/** Get the number of currently selected entities. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	int32 GetSelectionCount() const;

	/** Get the active focus index (-1 = "All", 0+ = specific entity). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	int32 GetActiveFocusIndex() const;

	/** Check if a specific entity is in the current selection. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	bool IsEntitySelected(FSeinEntityHandle Handle) const;

	/** Check if a specific entity is the currently focused entity. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	bool IsEntityFocused(FSeinEntityHandle Handle) const;

	// ========== Selection-wide Ability Aggregation ==========
	//
	// "What abilities can the WHOLE selection do?" Each selected entity's
	// ViewModel already returns a per-entity-aggregated FSeinAbilityInfo list
	// (squad-aware: a selected squad returns its squad+members deduped
	// union); this layer composes those per-entity outputs across the
	// selection using the same merge rules. From a selection POV a squad is
	// just one entity contributing its already-merged ability list —
	// squad-aware behavior is automatic.
	//
	// Aggregation rules (composed via USeinEntityViewModel::MergeAbilityInfos):
	//   - bIsEnabled       : OR  (enabled if ANY selected entity can fire)
	//   - bIsOnCooldown    : AND of all-on-cooldown (false if any ready)
	//   - bIsActive        : OR
	//   - CooldownRemaining: MIN (shortest = "soonest-ready owner's cooldown")
	//   - DisabledReason   : None if any enabled, else first disabled's reason
	//
	// Squad-scope cooldowns naturally produce equal cooldown values across
	// squad-internal owners, so MIN there is identity; for selections of
	// independently-cooldowned units (multiple infantry squads each with
	// their own Smoke cooldown) MIN gives the right "next-available" value.

	/** Deduped union of abilities across every selected entity. Use this for
	 *  action panels that show "every ability the current selection can do."
	 *  Order: stable, first-encountered tag per selection iteration. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	TArray<FSeinAbilityInfo> GetSelectionAbilities() const;

	/** Squad-aware lookup of a single tag across the selection. Returns
	 *  default-constructed (invalid) info if no selected entity holds the
	 *  tag. Useful for "is this specific ability available right now" widgets
	 *  that bind to a single button. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	FSeinAbilityInfo GetSelectionAbilityByTag(FGameplayTag Tag) const;

	/** True if any selected entity (or its squad members) holds the tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	bool SelectionHasAbilityWithTag(FGameplayTag Tag) const;

	// ========== Delegates ==========

	/** Fired when the selection or active focus changes. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Selection")
	FOnSelectionModelChanged OnSelectionChanged;

	// ========== Internal (called by USeinUISubsystem) ==========

	/** Initialize. Binds to the player controller's selection delegate if one is already available. */
	void Initialize(USeinUISubsystem* InOwningSubsystem);

	/** Unbind from delegates. */
	void Deinitialize();

	/**
	 * Bind to the local player controller's OnSelectionChanged if we haven't yet.
	 * Cheap no-op once bound. Called every sim tick by the UI subsystem so we
	 * catch the PC once it spawns (WorldSubsystem init runs before PC creation).
	 */
	void EnsurePlayerControllerBound();

private:
	/** Rebuild the ViewModel list from the current selection. */
	void RebuildFromController();

	/** Called when the player controller's selection changes. */
	UFUNCTION()
	void HandleSelectionChanged();

	/** Owning UI subsystem (for creating/getting entity ViewModels). */
	UPROPERTY()
	TWeakObjectPtr<USeinUISubsystem> OwningSubsystem;

	/** Cached player controller reference. */
	TWeakObjectPtr<ASeinPlayerController> CachedPlayerController;

	/** Current selection as entity ViewModels. */
	UPROPERTY()
	TArray<TObjectPtr<USeinEntityViewModel>> SelectedViewModels;

	/** Cached active focus index. */
	int32 CachedFocusIndex = -1;
};
