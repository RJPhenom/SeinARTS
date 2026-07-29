/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchFlowBPFL.h
 * @brief   BP surface for immutable match settings and in-match flow operations.
 *          Tick-zero startup belongs to the active bootstrap provider; runtime
 *          flow mutations route through the command buffer for replay.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinMatchSettings.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinMatchFlowBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Match Flow Library"))
class SEINARTSCOREENTITY_API USeinMatchFlowBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Reads -----------------------------------------------------------------

	/** Read the immutable match settings snapshot installed at bootstrap. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match Settings"))
	static FSeinMatchSettings SeinGetMatchSettings(const UObject* WorldContextObject);

	/** Current match state (Lobby / Starting / Playing / Paused / Ending / Ended). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match State"))
	static ESeinMatchState SeinGetMatchState(const UObject* WorldContextObject);

	/**
	 * Add receipt-only deterministic evidence while bootstrap is Applying.
	 * This does not create a persistent/queryable state slot; author those
	 * through a Canonical State Recipe and Set/Get State Value.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match|Bootstrap",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Register Bootstrap Evidence"))
	static bool SeinRegisterBootstrapEvidenceValue(
		const UObject* WorldContextObject,
		FName StableContributorID,
		int32 SchemaVersion,
		const FInstancedStruct& Value,
		FString& OutError);

	/** Transactionally update one state slot during deterministic simulation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|State",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Set State Value"))
	static bool SeinSetCanonicalStateValue(
		const UObject* WorldContextObject,
		const FSeinCanonicalStateKey& Key,
		const FInstancedStruct& Value,
		FString& OutError);

	/** Read a copy of one state slot. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|State",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get State Value"))
	static bool SeinGetCanonicalStateValue(
		const UObject* WorldContextObject,
		const FSeinCanonicalStateKey& Key,
		FInstancedStruct& OutValue);

	/** Launch tick zero after standalone bootstrap authorization, or resume a
	 *  stopped standalone match. Network topologies own their launch barrier. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start Standalone Simulation"))
	static bool SeinStartStandaloneSimulation(const UObject* WorldContextObject);

	// Mutations (route through command buffer for lockstep determinism) ---

	/** Enqueue an EndMatch command. `Winner` = victor; `Reason` = designer-authored
	 *  victory reason tag (`MyGame.Victory.Annihilation`, etc.). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "End Match"))
	static void SeinEndMatch(const UObject* WorldContextObject, FSeinPlayerID Winner, FGameplayTag Reason);

	/** Enqueue a pause request. Subject to match-settings default pause mode. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Pause"))
	static void SeinRequestPause(const UObject* WorldContextObject, FSeinPlayerID Requester);

	/** Enqueue a resume request. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Resume"))
	static void SeinRequestResume(const UObject* WorldContextObject, FSeinPlayerID Requester);

	/** Enqueue a concede command for the given player (V1 ends match immediately). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Concede Match"))
	static void SeinConcedeMatch(const UObject* WorldContextObject, FSeinPlayerID Conceding);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
