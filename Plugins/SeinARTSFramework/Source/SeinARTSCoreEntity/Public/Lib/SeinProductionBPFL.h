/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionBPFL.h
 * @brief   Read-only BPFL for production state. Producer-side mutation
 *          (enqueue, rally, etc.) lives as one-arg convenience methods on
 *          USeinAbility — production is unified into the ability surface,
 *          and an ability's BP graph mutates production via Self.* calls.
 *          Cancel-by-queue-index stays its own command (Command_Type_CancelProduction).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Components/SeinProductionComponent.h"
#include "SeinProductionBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Production Library"))
class SEINARTSCOREENTITY_API USeinProductionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// ==================== Read ====================

	/** Read FSeinProductionComponent for an entity. Returns false and logs a warning on invalid
	 *  handle or missing component; OutData is untouched on failure. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static bool SeinGetProductionData(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle, FSeinProductionComponent& OutData);

	/** Batch read FSeinProductionComponent. Invalid/missing entities are skipped (warning logged). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static TArray<FSeinProductionComponent> SeinGetProductionDataMany(const UObject* WorldContextObject,
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
