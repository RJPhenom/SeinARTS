/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayHeader.h
 * @brief   Replay file metadata (DESIGN §18). Header captures enough info to
 *          re-run a match deterministically: framework + game version, map,
 *          seed, settings snapshot, player registrations, tick range.
 *          Serialized as a USTRUCT via Unreal's native serialization.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "SeinReplayHeader.generated.h"

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
 * Replay file header. Paired with a binary command-log tail on disk for
 * deterministic playback. V1 ships the struct + save/load skeleton;
 * full tick-grouped binary command-log serialization is a follow-up pass
 * when the replay viewer app lands.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinReplayHeader
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FString FrameworkVersion;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FString GameVersion;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FName MapIdentifier;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int64 RandomSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinMatchSettings SettingsSnapshot;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	TArray<FSeinPlayerRegistration> Players;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 StartTick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 EndTick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FDateTime RecordedAt;
};
