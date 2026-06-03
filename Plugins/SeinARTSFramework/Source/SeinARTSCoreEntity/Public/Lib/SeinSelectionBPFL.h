/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionBPFL.h
 * @brief   BP surface for selection + control-group operations (DESIGN §15).
 *
 *          Selection state lives client-side on the player controller; this
 *          BPFL provides stateless helpers the PC / UI code calls to emit
 *          the observer-command log entries, filter selections for command
 *          dispatch, and query `FSeinEntity::FLAG_SELECTABLE`.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinSelectionBPFL.generated.h"

class USeinWorldSubsystem;

/**
 * Partitioned-selection result from `SeinResolveMoveableSelection`. Consumers
 * dispatch Move on `MoveableNow`; `NeedsDisembark` first get an `Ability.Exit`
 * pre-command, then a follow-up Move.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMoveableSelectionResult
{
	GENERATED_BODY()

	/** Members ready to receive a Move dispatch immediately. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	TArray<FSeinEntityHandle> MoveableNow;

	/** Members that need to disembark first (their container is not in the selection). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	TArray<FSeinEntityHandle> NeedsDisembark;
};

UCLASS(meta = (DisplayName = "SeinARTS Selection Library"))
class SEINARTSCOREENTITY_API USeinSelectionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Observer-command emission. Each helper builds + enqueues the appropriate
	// SeinARTS.Command.Type.Observer.{Selection,ControlGroup}.* command so the
	// txn log reconstructs client UI state on replay. None of these mutate
	// client-side selection directly — the PC does that; these just log.
	// ====================================================================================================

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Replace Selection"))
	static void SeinReplaceSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members, int32 ActiveFocusIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Add To Selection"))
	static void SeinAddToSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove From Selection"))
	static void SeinRemoveFromSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Bind Control Group"))
	static void SeinBindControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex, const TArray<FSeinEntityHandle>& Members);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Add To Control Group"))
	static void SeinAddToControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex, const TArray<FSeinEntityHandle>& Members);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Recall Control Group"))
	static void SeinRecallControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex);

	// Filters / helpers.
	// ====================================================================================================

	/** Returns true iff the entity has FSeinEntity::FLAG_SELECTABLE set (DESIGN §15). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Selectable"))
	static bool SeinIsSelectable(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Sim-authoritative setter for the FLAG_SELECTABLE bit. Projectiles / ability
	 *  stubs / scenario pseudo-entities set false at spawn to drop out of UI. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Set Selectable"))
	static void SeinSetSelectable(const UObject* WorldContextObject, FSeinEntityHandle Entity, bool bSelectable);

	/** Filter a candidate list to entities this player owns. Used by command-
	 *  dispatch code before issuing ActivateAbility / BrokerOrder (DESIGN §15
	 *  "Selection does NOT require ownership; command issuance DOES"). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Filter By Ownership"))
	static TArray<FSeinEntityHandle> SeinFilterByOwnership(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Candidates);

	/** Entities currently holding the identity tag. Used by "select all by type"
	 *  UX (double-click an entity → select every live entity with the same tag). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Select All By Type"))
	static TArray<FSeinEntityHandle> SeinSelectAllByType(const UObject* WorldContextObject, FGameplayTag IdentityTag);

	/** Partition a selection for move commands (transport nuance — DESIGN §15).
	 *    - Members whose container is ALSO in the selection → skipped (the
	 *      container carries them).
	 *    - Members whose container is NOT in the selection → queued for
	 *      disembark + follow-up Move.
	 *    - Uncontained members → moveable immediately. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Resolve Moveable Selection"))
	static FSeinMoveableSelectionResult SeinResolveMoveableSelection(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& Selection);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);

	// Shared helper: enqueue a simple observer command carrying (player, member
	// list, optional group index / active focus index).
	static void EnqueueObserverCommand(
		USeinWorldSubsystem* World,
		FGameplayTag CommandType,
		FSeinPlayerID PlayerID,
		const TArray<FSeinEntityHandle>& Members,
		int32 GroupIndex = INDEX_NONE,
		int32 ActiveFocusIndex = INDEX_NONE);
};
