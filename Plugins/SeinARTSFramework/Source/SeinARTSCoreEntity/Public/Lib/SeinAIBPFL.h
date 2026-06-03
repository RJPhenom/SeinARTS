/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIBPFL.h
 * @brief   BP surface for AI authoring (DESIGN §16). Two query flavors
 *          (all-entities perfect-info, visible-per-player fog-respecting)
 *          plus a forwarded `SeinEmitCommand`. Designer picks which
 *          query style fits their AI design.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinAIBPFL.generated.h"

struct FSeinCommand;
class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS AI Library"))
class SEINARTSCOREENTITY_API USeinAIBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Perfect-information query — every live entity whose tags match the query.
	 *  Ignores fog. Fast; suits "cheating AI" patterns common in RTS. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|AI",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query All Entities"))
	static TArray<FSeinEntityHandle> SeinQueryAllEntities(const UObject* WorldContextObject, FGameplayTagQuery Query);

	/** Fog-respecting query — only entities visible to `ObserverPlayer` per §12.
	 *  Falls back to all-entities if the vision subsystem isn't loaded. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|AI",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Visible Entities For Player"))
	static TArray<FSeinEntityHandle> SeinQueryVisibleEntitiesForPlayer(const UObject* WorldContextObject, FSeinPlayerID ObserverPlayer, FGameplayTagQuery Query);

	/** Forward a command into the lockstep buffer. Thin wrapper around
	 *  `USeinWorldSubsystem::EnqueueCommand`; here for BP discoverability. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Emit Command"))
	static void SeinEmitCommand(const UObject* WorldContextObject, const FSeinCommand& Command);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
