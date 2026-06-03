/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchFlowBPFL.h
 * @brief   BP surface for match settings + match-flow operations (DESIGN §18).
 *          Settings read is BlueprintPure (immutable post-StartMatch);
 *          flow mutations route through the command buffer so the txn log
 *          captures state-machine transitions for replay.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinMatchSettings.h"
#include "SeinMatchFlowBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Match Flow Library"))
class SEINARTSCOREENTITY_API USeinMatchFlowBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Reads -----------------------------------------------------------------

	/** Read the current match settings snapshot. Immutable after `StartMatch`. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match Settings"))
	static FSeinMatchSettings SeinGetMatchSettings(const UObject* WorldContextObject);

	/** Current match state (Lobby / Starting / Playing / Paused / Ending / Ended). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match State"))
	static ESeinMatchState SeinGetMatchState(const UObject* WorldContextObject);

	// Mutations (route through command buffer for lockstep determinism) ---

	/** Enqueue a StartMatch command. Settings payload carried via `FInstancedStruct`;
	 *  the sim snapshots at handler-dispatch time. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start Match"))
	static void SeinStartMatch(const UObject* WorldContextObject, const FSeinMatchSettings& Settings);

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

	/** Enqueue a restart command. Resets match state to Lobby. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Restart Match"))
	static void SeinRestartMatch(const UObject* WorldContextObject);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
