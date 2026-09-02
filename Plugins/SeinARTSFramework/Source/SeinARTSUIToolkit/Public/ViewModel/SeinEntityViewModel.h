/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityViewModel.h
 * @brief   Generic ViewModel providing a read-only lens into any sim entity's
 *          data. Widgets bind to this for real-time display of entity state.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Data/SeinUITypes.h"
#include "GameplayTagContainer.h"
#include "SeinEntityViewModel.generated.h"

class UTexture2D;
class USeinWorldSubsystem;
class USeinActorBridgeSubsystem;
class ASeinActor;
struct FInstancedStruct;

/**
 * UI-friendly snapshot of one production queue entry. Resolves icon /
 * display name from the producible's CDO so widgets bind directly
 * without a per-entry CDO lookup. Returned by USeinEntityViewModel::GetProductionQueue.
 */
USTRUCT(BlueprintType)
struct SEINARTSUITOOLKIT_API FSeinProductionQueueItemInfo
{
	GENERATED_BODY()

	/** 0 = front (currently building), 1+ = waiting in line. Use as the
	 *  argument for `MakeCancelProductionCommand` when wiring a click-to-cancel. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	int32 QueueIndex = 0;

	/** Display name from the producible's identity (or effect name for research). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	FText DisplayName;

	/** Icon from the producible's identity. Null if the producible has no icon set
	 *  or for research entries (fall back to a research-icon convention in your widget). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Identity tag of the producible (for icon-by-tag fallbacks or click filters). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	FGameplayTag IdentityTag;

	/** 0..1 — only non-zero on the front entry (queue-tail entries are 0 until they advance). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	float ProgressPercent = 0.0f;

	/** Total build time in sim-seconds — useful for absolute remaining-time UI. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	float TotalBuildTime = 0.0f;

	/** True only on the front entry, only if the entry reached 100% but its
	 *  AtCompletion cost can't yet be paid (e.g. pop cap full). DESIGN §9
	 *  stall-at-completion. Widgets typically render this as a pulsing red overlay. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	bool bStalledAtCompletion = false;

	/** True if this entry is a research item (vs. a unit). Widgets can use this
	 *  to swap the slot template (research entries are typically a single-slot
	 *  display rather than a queue-of-many). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Production")
	bool bIsResearch = false;
};

/** Broadcast when the ViewModel has been refreshed with new sim data. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityViewModelRefreshed);

/** Broadcast when the entity this ViewModel tracks has been destroyed or invalidated. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityViewModelInvalidated);

/**
 * Lightweight info struct for a single ability, suitable for UI display.
 * Avoids exposing the full USeinAbility UObject to the render layer.
 */
USTRUCT(BlueprintType)
struct SEINARTSUITOOLKIT_API FSeinAbilityInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability", meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, float> ResourceCost;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	float Cooldown = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	float CooldownRemaining = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** When `bIsEnabled` is false, the first failing activation gate. Surface as
	 *  a tooltip ("Requires tech X" / "On cooldown" / "Can't afford"). Meaningless
	 *  when `bIsEnabled` is true. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	ESeinAbilityUnavailableReason DisabledReason = ESeinAbilityUnavailableReason::None;

	/** True if the ability can be activated right now (i.e. would pass the activation
	 *  gate if the player clicked the button). Walks cooldown → BlockedTags →
	 *  RequiredEntityTags → RequiredPlayerTags → CanActivate → affordability —
	 *  the same gates as `ProcessCommands::ActivateAbility`. Target-validation
	 *  gates (range / LOS / ValidTargetTags) are NOT evaluated here because the
	 *  view-model has no per-button target context; those fire on the actual
	 *  click. Drives "is this button enabled / visible" in UI binding. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsPassive = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsOnCooldown = false;
};

/**
 * Generic ViewModel for any sim entity.
 *
 * Created and cached by USeinUISubsystem. Automatically refreshed each sim tick.
 * Widgets can:
 *   - Bind to BlueprintReadOnly properties via UMG native binding (auto-updates)
 *   - Bind to BlueprintCallable getters via UMG binding functions
 *   - Listen to OnRefreshed for event-driven updates
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinEntityViewModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Identity (cached from FSeinIdentityPayload, always readable) ==========

	/** The entity handle this ViewModel tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FSeinEntityHandle Entity;

	/** Display name from FSeinIdentityPayload. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FText DisplayName;

	/** Description from FSeinIdentityPayload. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FText Description;

	/** Icon texture from FSeinIdentityPayload. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Portrait texture from FSeinIdentityPayload. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	TObjectPtr<UTexture2D> Portrait = nullptr;

	/** Identity gameplay tag (from FSeinIdentityPayload). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FGameplayTag IdentityTag;

	/** Owning player ID. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FSeinPlayerID OwnerPlayerID;

	/** Whether the entity is still alive in the simulation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	bool bIsAlive = false;

	// ========== Generic Data Access ==========

	/**
	 * Get a resolved attribute value (base + all modifiers applied).
	 * @param ComponentType - The USTRUCT type of the sim component (e.g., FSeinMovementPayload::StaticStruct())
	 * @param FieldName - The FName of the FFixedPoint field on that struct
	 * @return The resolved value as float, or 0 if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	float GetResolvedAttribute(UScriptStruct* ComponentType, FName FieldName) const;

	/**
	 * Get a base attribute value (no modifiers).
	 * @param ComponentType - The USTRUCT type of the sim component
	 * @param FieldName - The FName of the FFixedPoint field
	 * @return The base value as float, or 0 if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	float GetBaseAttribute(UScriptStruct* ComponentType, FName FieldName) const;

	/** Check if the entity has a specific component type. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	bool HasComponent(UScriptStruct* ComponentType) const;

	/**
	 * Get a full copy of a component's data as an FInstancedStruct.
	 * For advanced Blueprint use where the designer knows the component type.
	 * Returns an empty struct if the component is not present.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FInstancedStruct GetComponentData(UScriptStruct* ComponentType) const;

	/** Get the entity's current gameplay tags. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FGameplayTagContainer GetTags() const;

	// ========== Relationship ==========

	/** Get the relationship between this entity and a specific player. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	ESeinRelation GetRelationToPlayer(FSeinPlayerID PlayerID) const;

	/** Get the relationship between this entity and the local player. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	ESeinRelation GetRelationToLocalPlayer() const;

	// ========== Ability Access ==========

	/** Get info structs for all abilities on this entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	TArray<FSeinAbilityInfo> GetAbilities() const;

	/** Get ability info for a specific ability tag. Returns empty if not found. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FSeinAbilityInfo GetAbilityByTag(FGameplayTag Tag) const;

	/** Check if the entity has an ability with the given tag. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	bool HasAbilityWithTag(FGameplayTag Tag) const;

	// ========== Production Queue (only meaningful for entities with a Production Component) ==========

	/** Snapshot the entity's production queue as a UI-friendly array. Empty if
	 *  the entity has no FSeinProductionPayload component or its queue is empty.
	 *  Index 0 = currently-building (front), 1+ = waiting. Each entry has
	 *  pre-resolved icon + display name from the producible's CDO. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	TArray<FSeinProductionQueueItemInfo> GetProductionQueue() const;

	// ========== Lifecycle ==========

	/** Fired after Refresh() updates cached data. Widgets bind to this for event-driven updates. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Entity")
	FOnEntityViewModelRefreshed OnRefreshed;

	/** Fired when the tracked entity is destroyed. Widgets should unbind and clear references. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Entity")
	FOnEntityViewModelInvalidated OnInvalidated;

	/**
	 * Initialize this ViewModel with an entity handle.
	 * Called by USeinUISubsystem when creating the ViewModel.
	 * Caches identity data from FSeinIdentityPayload.
	 */
	void Initialize(FSeinEntityHandle InHandle, USeinWorldSubsystem* InWorldSubsystem);

	/**
	 * Refresh cached data from the simulation.
	 * Called by USeinUISubsystem each sim tick.
	 */
	void Refresh();

	/**
	 * Mark this ViewModel as invalidated (entity destroyed).
	 * Fires OnInvalidated and clears data.
	 */
	void Invalidate();

private:
	/** Cached world subsystem for sim data access. */
	UPROPERTY()
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Build an FSeinAbilityInfo from a USeinAbility instance.
	 *
	 *  `OwnerOverride` lets squad callers point the availability check at the
	 *  entity that ACTUALLY owns the ability instance (a squad member when
	 *  the squad-aware aggregation routed a member-owned ability through the
	 *  squad's deduped list). Without the override, the check runs against
	 *  `Entity` (the squad selection handle), the squad's own AC doesn't
	 *  contain the member ability, the availability lookup fails, and the
	 *  UI silently filters the ability out as "disabled."
	 *
	 *  Default-constructed (invalid) override falls back to `Entity` —
	 *  preserves existing single-entity behavior. */
	FSeinAbilityInfo BuildAbilityInfo(const class USeinAbility* Ability,
		FSeinEntityHandle OwnerOverride = FSeinEntityHandle()) const;

public:
	/** Merge N FSeinAbilityInfos for the SAME ability tag into one
	 *  aggregated FSeinAbilityInfo. Used by both the squad-internal
	 *  aggregation (members + squad's own all hold Move → merge into one
	 *  Move entry) and the selection-level aggregation
	 *  (USeinSelectionModel walks selected view models and merges their
	 *  per-tag outputs).
	 *
	 *  Class-level fields (Name, Icon, AbilityTag, TargetType, ResourceCost,
	 *  Cooldown total, bIsPassive) come from the first input — all inputs
	 *  share these because they're CDO-level. Runtime state aggregates:
	 *    - bIsEnabled    : OR (enabled if ANY owner can fire)
	 *    - bIsOnCooldown : AND of all (only "on cooldown" if NONE is ready)
	 *    - bIsActive     : OR (active if ANY owner is mid-execution)
	 *    - CooldownRemaining : MIN (shortest = "when can the soonest-ready
	 *                          owner next fire")
	 *    - DisabledReason: None if any enabled, else first disabled's reason
	 *
	 *  Math composes: aggregating squad-aware view-model outputs at the
	 *  selection level gives the same result as aggregating raw instances
	 *  end-to-end, because OR-of-OR = OR and MIN-of-MIN = MIN.
	 *
	 *  Empty input returns a default-constructed (invalid) info. Caller
	 *  should not pass mixed-tag inputs — undefined which tag's class
	 *  fields end up in the merged result. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Ability",
		meta = (DisplayName = "Merge Ability Infos"))
	static FSeinAbilityInfo MergeAbilityInfos(const TArray<FSeinAbilityInfo>& Inputs);
};
