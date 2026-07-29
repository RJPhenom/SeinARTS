/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayBPFL.h
 * @brief   Replay-header construction and bounded metadata persistence.
 *          Executable replay journals are owned by SeinARTSNet's replay
 *          writer/reader; this CoreEntity library never creates or plays one.
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

	/** Fill replay metadata from the current subsystem state. Executable
	 *  recorders may use this as the initial header for their command journal.
	 *  MapIdentifier is retained for Blueprint compatibility; the canonical
	 *  long package name of the current world always wins. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Build Replay Header"))
	static FSeinReplayHeader SeinBuildReplayHeader(const UObject* WorldContextObject, FName MapIdentifier);

	/** Save only a bounded replay-header metadata document. This does not
	 *  contain a command journal and cannot be used for replay playback. A valid
	 *  world context binds serialization to that world's frozen protocol catalog
	 *  and generated simulation-content identity; null is supported for worldless
	 *  tooling and uses current project defaults. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Save Replay Header Metadata"))
	static bool SeinSaveReplayHeaderMetadata(const UObject* WorldContextObject,
		const FSeinReplayHeader& Header, const FString& FilePath);

	/** Load only a bounded replay-header metadata document. This never loads
	 *  packages or starts executable replay playback. A valid world context uses
	 *  that world's frozen protocol catalog and rejects foreign simulation
	 *  content before decoding; null worldless tooling uses current project
	 *  defaults without claiming active-session compatibility. A non-null
	 *  context without a simulation world fails. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Load Replay Header Metadata"))
	static bool SeinLoadReplayHeaderMetadata(const UObject* WorldContextObject,
		const FString& FilePath, FSeinReplayHeader& OutHeader);

	/** Deprecated compatibility wrapper for Save Replay Header Metadata. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Save Replay (Deprecated: Header Metadata Only)",
			DeprecatedFunction,
			DeprecationMessage = "This saves header metadata only, not an executable replay journal. Use Save Replay Header Metadata; executable replay journals use the SeinARTSNet replay writer."))
	static bool SeinSaveReplay(const UObject* WorldContextObject, const FSeinReplayHeader& Header, const FString& FilePath);

	/** Deprecated compatibility wrapper for Load Replay Header Metadata. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Load Replay (Deprecated: Header Metadata Only)",
			DeprecatedFunction,
			DeprecationMessage = "This loads header metadata only and cannot load an executable replay journal. Use Load Replay Header Metadata; executable replay journals use the SeinARTSNet replay reader."))
	static bool SeinLoadReplay(const UObject* WorldContextObject, const FString& FilePath, FSeinReplayHeader& OutHeader);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
