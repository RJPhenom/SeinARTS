/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionBPFL.h
 * @brief   Production queries and deterministic runtime policy overrides. Queue mutation
 *          (enqueue, rally, etc.) lives as one-arg convenience methods on
 *          USeinAbility — production is unified into the ability surface,
 *          and an ability's BP graph mutates production via Self.* calls.
 *          Cancel Production on USeinAbility shares cancellation/refund logic
 *          with the queue-index command (Command_Type_CancelProduction).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Components/SeinProductionPayload.h"
#include "Components/SeinProductionPolicy.h"
#include "SeinProductionBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Production Library"))
class SEINARTSCOREENTITY_API USeinProductionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Check whether another item can be queued. Used includes queued and completed purchases in the effective scope. Producer overrides take precedence over player overrides and class defaults. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Can Enqueue Production", SeinDeterministic))
	static bool SeinCanEnqueueProduction(const UObject* WorldContextObject, FSeinEntityHandle Producer,
		TSubclassOf<ASeinActor> ProducibleClass, ESeinProductionQueueResult& Result,
		FSeinProductionQueueSettings& Settings, int64& Used);

	/** Override this producible's policy for one producer. Existing entries and completed history remain. Returns false for an invalid amount or unauthorized call. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Production Unit Queue Policy", SeinDeterministic))
	static bool SeinSetProductionUnitQueuePolicy(const UObject* WorldContextObject, FSeinEntityHandle Producer,
		TSubclassOf<ASeinActor> ProducibleClass, FSeinProductionQueueSettings Settings);

	/** Clear this producer's override for the item, restoring the player override or authored defaults. Does not clear history. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Clear Production Unit Queue Policy", SeinDeterministic))
	static bool SeinClearProductionUnitQueuePolicy(const UObject* WorldContextObject, FSeinEntityHandle Producer,
		TSubclassOf<ASeinActor> ProducibleClass);

	/** Override this producible's policy for a player. Producer overrides take precedence. Existing entries and completed history remain. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Player Queue Policy", SeinDeterministic))
	static bool SeinSetPlayerQueuePolicy(const UObject* WorldContextObject, FSeinPlayerID Player,
		TSubclassOf<ASeinActor> ProducibleClass, FSeinProductionQueueSettings Settings);

	/** Clear this player's override for the item, restoring authored defaults where no producer override exists. Does not clear history. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Clear Player Queue Policy", SeinDeterministic))
	static bool SeinClearPlayerQueuePolicy(const UObject* WorldContextObject, FSeinPlayerID Player,
		TSubclassOf<ASeinActor> ProducibleClass);

	// ==================== Read ====================

	/** Read FSeinProductionPayload for an entity. Returns false and logs a warning on invalid
	 *  handle or missing component; OutData is untouched on failure. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static bool SeinGetProductionData(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle, FSeinProductionPayload& OutData);

	/** Batch read FSeinProductionPayload. Invalid/missing entities are skipped (warning logged). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static TArray<FSeinProductionPayload> SeinGetProductionDataMany(const UObject* WorldContextObject,
		const TArray<FSeinEntityHandle>& EntityHandles);

	/** Check if a player has a specific tech tag. Convenience wrapper around
	 *  `FSeinPlayerState::HasPlayerTag` for BP graphs that don't want to pull
	 *  the player state directly. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Player Has Tech Tag"))
	static bool SeinPlayerHasTechTag(const UObject* WorldContextObject,
		FSeinPlayerID PlayerID, FGameplayTag TechTag);

	/** Get all unlocked tech tags for a player. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Player Tech Tags"))
	static FGameplayTagContainer SeinGetPlayerTechTags(const UObject* WorldContextObject,
		FSeinPlayerID PlayerID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
