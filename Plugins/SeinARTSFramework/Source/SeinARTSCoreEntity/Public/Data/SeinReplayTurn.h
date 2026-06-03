/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayTurn.h
 * @brief   Per-turn record + top-level replay container. Pairs with
 *          FSeinReplayHeader (SeinReplayHeader.h) — together they define
 *          the .seinreplay file format the SeinReplayWriter writes.
 *
 * Determinism contract: replaying with the same SessionSeed (Header.RandomSeed)
 * + same map + same plugin-settings + same command stream MUST produce
 * bit-identical state on every machine. The header captures every input
 * that affects determinism; this file captures the per-turn command stream.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommand.h"
#include "Data/SeinReplayHeader.h"
#include "SeinReplayTurn.generated.h"

/** One turn in the replay — same shape the server's gate fans out. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinReplayTurnRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	int32 TurnId = 0;

	/** Slot-sorted commands assembled by the server's gate, then stamped
	 *  with each command's authoritative PlayerID. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	TArray<FSeinCommand> Commands;
};

/** Top-level replay container — the unit of data written to a .seinreplay file. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinReplay
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	FSeinReplayHeader Header;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Replay")
	TArray<FSeinReplayTurnRecord> Turns;
};
