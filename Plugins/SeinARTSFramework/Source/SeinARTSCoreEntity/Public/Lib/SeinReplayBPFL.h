/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayBPFL.h
 * @brief   Replay save/load skeleton (DESIGN §18). V1 ships header-only
 *          persistence — `SaveReplay` writes the header struct to disk;
 *          full tick-grouped binary command-log serialization lands when
 *          the replay viewer app materializes.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/SeinReplayHeader.h"
#include "SeinReplayBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Replay Library"))
class SEINARTSCOREENTITY_API USeinReplayBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Fill a replay header snapshot from the current subsystem state
	 *  (settings, tick range, registered players). Intended as the header
	 *  portion of an eventual binary replay file. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Build Replay Header"))
	static FSeinReplayHeader SeinBuildReplayHeader(const UObject* WorldContextObject, FName MapIdentifier);

	/** Write a replay header to disk at `FilePath`. V1 ships header-only;
	 *  command-log tail serialization lands with the replay viewer app.
	 *  Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Save Replay"))
	static bool SeinSaveReplay(const UObject* WorldContextObject, const FSeinReplayHeader& Header, const FString& FilePath);

	/** Load a previously-saved replay header from disk. Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Load Replay"))
	static bool SeinLoadReplay(const UObject* WorldContextObject, const FString& FilePath, FSeinReplayHeader& OutHeader);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
