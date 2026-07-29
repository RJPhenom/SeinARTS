/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayHeader.h
 * @brief   Replay file metadata. Header captures enough information to
 *          re-run a match deterministically: framework + game version, map,
 *          seed, settings snapshot, player registrations, tick range.
 *          Persisted by the bounded v8 replay codec; replay files never invoke
 *          generic reflected/native struct deserialization on hostile bytes.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinReplayHeader.generated.h"

class UWorld;

/**
 * Replayable per-player registration snapshot.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPlayerRegistration
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinPlayerID PlayerID;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinFactionID FactionID;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	uint8 TeamID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	bool bIsAI = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	bool bIsSpectator = false;
};

/**
 * Replay file header. Paired with the exact assembled-turn journal on disk.
 * Full recordings begin from pristine tick 0 and reproduce every completed
 * tick through inclusive EndTick.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinReplayHeader
{
	GENERATED_BODY()

	/** Frozen command-schema + authority-policy compatibility identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FGuid CommandProtocolDigest;

	/** Canonical digest of the installed match settings, including extensions. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FGuid MatchSettingsDigest;

	/** Agreed proof of the deterministic tick-zero state this journal extends. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinMatchBootstrapReceipt BootstrapReceipt;

	/** Determinism-relevant core + extension plugin settings fingerprint. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 ConfigFingerprint = 0;

	/** Manual deterministic-framework compatibility epoch. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FString FrameworkVersion;

	/** Unreal local network identity (engine + project + ProjectVersion/override). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FString GameVersion;

	/** Canonical long package name with any PIE prefix removed. Stored as a
	 *  bounded string so inspecting replay metadata never depends on, or adds
	 *  attacker-controlled entries to, Unreal's process-global FName pool. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FString MapIdentifier;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int64 RandomSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinMatchSettings SettingsSnapshot;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	TArray<FSeinPlayerRegistration> Players;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 StartTick = 0;

	/** Inclusive last simulation tick actually observed by the recorder. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 EndTick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FDateTime RecordedAt;
};

/**
 * Runtime identities that make a replay executable rather than merely
 * structurally readable. The framework epoch is bumped when deterministic
 * framework behaviour changes incompatibly. The game identity uses Unreal's
 * pluggable local network version for executable/runtime compatibility.
 * Generated simulation-content compatibility is carried independently by the
 * bootstrap receipt, so projects do not rely on a manually bumped path/version
 * token to prove Blueprint gameplay content.
 */
namespace SeinReplayCompatibility
{
	SEINARTSCOREENTITY_API FString GetFrameworkVersion();
	SEINARTSCOREENTITY_API FString GetGameVersion();
	SEINARTSCOREENTITY_API FName GetMapIdentifier(const UWorld* World);
	SEINARTSCOREENTITY_API void StampCurrent(
		FSeinReplayHeader& Header,
		const UWorld* World);
	SEINARTSCOREENTITY_API bool ValidateCurrent(
		const FSeinReplayHeader& Header,
		const UWorld* World,
		FString& OutError);
	SEINARTSCOREENTITY_API bool ValidatePlayerManifest(
		const FSeinReplayHeader& Header,
		FString& OutError);
}
