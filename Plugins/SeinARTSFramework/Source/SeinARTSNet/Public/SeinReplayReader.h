/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.h
 * @brief   Local-only playback of a .seinreplay file (the writer's other half).
 *
 * Phase 4a. Reads a `FSeinReplay` blob from disk, validates the header against
 * current sim state (logs warnings on mismatch but doesn't block playback),
 * seeds the sim PRNG from the header, then drives the sim forward by feeding
 * recorded turns into `USeinWorldSubsystem` at the matching tick boundaries.
 *
 * Playback runs in Standalone mode — networking gate is bypassed, the reader
 * is the authority. Use cases:
 *   - desync repro / regression testing
 *   - demo recordings + match casts
 *   - observer mode (Phase 4 polish: real-time delayed playback for live games)
 *
 * Usage:
 *   USeinReplayReader* Reader = NewObject<USeinReplayReader>(GetGameInstance());
 *   if (Reader->LoadFromFile("Match_20260428.seinreplay")) Reader->Play();
 *   ...
 *   Reader->Stop();
 *
 * Or via console: `Sein.Net.LoadReplay <filename>` / `Sein.Net.StopReplay`.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/SeinReplayTurn.h"
#include "SeinReplayReader.generated.h"

class USeinWorldSubsystem;

UCLASS()
class SEINARTSNET_API USeinReplayReader : public UObject
{
	GENERATED_BODY()

public:
	/** Read + deserialize a .seinreplay file. Resolves bare filenames against
	 *  `Saved/Replays/`. Returns true on success, false if the file is missing,
	 *  unreadable, or a serialization error occurs. */
	bool LoadFromFile(const FString& Path);

	/** Begin playback. Seeds PRNG from the loaded header, starts the sim if
	 *  not already running, hooks `OnSimTickCompleted` to drain the turn
	 *  stream into the sim's command buffer at the matching ticks.
	 *
	 *  Pre-conditions:
	 *   - `LoadFromFile` returned true
	 *   - World has a `USeinWorldSubsystem`
	 *   - Sim is in Standalone or NetworkingDisabled mode (live network sessions
	 *     refuse playback to avoid clobbering the lockstep gate)
	 *
	 *  Returns true on accept. */
	bool Play();

	/** Halt playback. Unhooks `OnSimTickCompleted`. Does NOT stop the sim — the
	 *  caller may want it to keep ticking against the post-replay state. */
	void Stop();

	/** True if currently driving the sim from the loaded turn buffer. */
	bool IsPlaying() const { return bPlaying; }

	/** Number of turns in the loaded buffer (zero before LoadFromFile success). */
	int32 GetTurnCount() const { return Loaded.Turns.Num(); }

	/** Read the loaded header (zero / default if not loaded). */
	const FSeinReplayHeader& GetHeader() const { return Loaded.Header; }

private:
	/** Hook bound to `USeinWorldSubsystem::OnSimTickCompleted`. Drains every
	 *  loaded turn whose `TurnId == sim's just-finished turn` into the
	 *  command buffer for the next tick. */
	void HandleSimTick(int32 CompletedTick);

	/** Resolve helper. */
	USeinWorldSubsystem* GetWorldSubsystem() const;

	UPROPERTY()
	FSeinReplay Loaded;

	bool bLoaded = false;
	bool bPlaying = false;

	/** Cursor into Loaded.Turns. Advanced by HandleSimTick; entries before
	 *  this index have already been enqueued. */
	int32 NextTurnIndex = 0;

	FDelegateHandle SimTickHandle;
};
