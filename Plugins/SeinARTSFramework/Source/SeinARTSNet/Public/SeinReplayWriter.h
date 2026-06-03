/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayWriter.h
 * @brief   Server-side replay capture. Buffers every dispatched turn in
 *          memory; flushes to disk on FinishRecording.
 *
 * Lifecycle:
 *   1. USeinNetSubsystem::StartLockstepSession (server) → StartRecording
 *   2. USeinNetSubsystem::ServerCheckTurnComplete (each fan-out) → RecordTurn
 *   3. USeinNetSubsystem::Deinitialize (or Sein.Net.SaveReplay) → FinishRecording
 *
 * In-memory buffer keeps writes fast (no per-turn IO at 10 Hz). If the
 * editor crashes mid-match, the in-memory buffer is lost — Phase 4b can
 * add periodic flushing if needed.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/SeinReplayTurn.h"
#include "SeinReplayWriter.generated.h"

UCLASS()
class SEINARTSNET_API USeinReplayWriter : public UObject
{
	GENERATED_BODY()

public:
	/** Open a new recording. Resets the buffer and stores the header. Idempotent
	 *  if called while already recording (logs warning, replaces header). */
	void StartRecording(const FSeinReplayHeader& Header);

	/** Append one assembled-turn record. No-op if not recording. */
	void RecordTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Serialize the buffer to disk and close. Returns the resolved file path
	 *  on success, empty string on failure (or if not recording). Writes to
	 *  Saved/Replays/<MapName>_<UTCStamp>.seinreplay by default. */
	FString FinishRecording();

	/** True if StartRecording was called and FinishRecording hasn't run yet. */
	bool IsRecording() const { return bRecording; }

	/** How many turn records are in the buffer. */
	int32 GetBufferedTurnCount() const { return Buffer.Turns.Num(); }

private:
	UPROPERTY()
	FSeinReplay Buffer;

	bool bRecording = false;
};
