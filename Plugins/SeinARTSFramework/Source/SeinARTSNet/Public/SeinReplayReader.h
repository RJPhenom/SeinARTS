/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.h
 * @brief   Local-only playback of a .seinreplay file (the writer's other half).
 *
 * Reads a bounded v8 file prefix first and rejects incompatible command,
 * simulation-content, or config identities before decoding. Body decode selects types only
 * by index from the frozen command/match catalogs; replay data never resolves
 * an object path or triggers package loads.
 * Playback installs the canonical settings snapshot, seeds the sim PRNG, then
 * feeds recorded turns at their matching tick boundaries.
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
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinReplayReader.generated.h"

class USeinWorldSubsystem;

UCLASS()
class SEINARTSNET_API USeinReplayReader : public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	/** Read + validate a v8 .seinreplay file. Legacy, oversized, corrupt,
	 *  incompatible, or structurally invalid files fail closed without replacing
	 *  a previously validated replay. Active playback must be stopped first. */
	bool LoadFromFile(const FString& Path);

	/** Begin playback. Requires a pristine non-running tick-0 Lobby world,
	 *  seeds the PRNG, starts the sim, and drains the recorded turn
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

	/** Abort playback and unhook the private replay turn-boundary notifier. An explicit abort leaves
	 *  simulation control to the caller; natural completion stops at EndTick. */
	void Stop();

	/** True if currently driving the sim from the loaded turn buffer. */
	bool IsPlaying() const { return bPlaying; }

	/** Number of turns in the loaded buffer (zero before LoadFromFile success). */
	int32 GetTurnCount() const { return Loaded.Turns.Num(); }

	/** Read the loaded header (zero / default if not loaded). */
	const FSeinReplayHeader& GetHeader() const { return Loaded.Header; }

private:
	/** Hook bound to Core's private replay tick notifier. Drains only a loaded
	 *  turn whose `TurnId * TicksPerTurn` is the exact upcoming sim tick. */
	void HandleSimTick(int32 CompletedTick);
	bool DrainTurnsForUpcomingTick(
		USeinWorldSubsystem* WorldSub,
		int64 UpcomingTick);

	/** Natural completion or protocol failure: halt the sim and detach. */
	void HaltPlayback(USeinWorldSubsystem* WorldSub, const TCHAR* Reason);

	/** Resolve helper. */
	USeinWorldSubsystem* GetWorldSubsystem() const;

	UPROPERTY()
	FSeinReplay Loaded;

	bool bLoaded = false;
	bool bPlaying = false;
	bool bOwnsExternalCommandIngress = false;
	FSeinMatchBootstrapAuthorityHandle BootstrapAuthority;

	/** Cursor into Loaded.Turns. Advanced by HandleSimTick; entries before
	 *  this index have already been enqueued. */
	int32 NextTurnIndex = 0;

	/** Exact world whose notifier owns SimTickHandle. The reader is GI-owned,
	 *  so GetWorld() may already name a destination world during teardown. */
	TWeakObjectPtr<USeinWorldSubsystem> BoundWorldSubsystem;
	FDelegateHandle SimTickHandle;
};
