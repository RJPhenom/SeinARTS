/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.h
 * @brief   Local-only playback of a .seinreplay file (the writer's other half).
 *
 * Frozen v8 files retain their bounded whole-body compatibility path. V9
 * trusted-local journals are scanned as digest-chained bounded frames, retain
 * only lightweight indexes, and lazily decode turn batches during playback.
 * V9 checkpoint decode may resolve/load referenced local packages only after
 * prefix compatibility and Saved/Replays containment have been admitted.
 * Playback restores canonical state, then feeds recorded turns at their exact
 * matching tick boundaries.
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
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinReplayReader.generated.h"

class USeinWorldSubsystem;
struct FSeinReplayReaderTestAccess;

UCLASS()
class SEINARTSNET_API USeinReplayReader : public UObject
{
	GENERATED_BODY()
	friend struct FSeinReplayReaderTestAccess;

public:
	virtual void BeginDestroy() override;

	/** Read + validate a replay file. Frozen v8 files retain their whole-body
	 *  compatibility path; v9 journals are digest-chained and indexed one bounded
	 *  frame at a time. Failure never replaces a previously validated replay.
	 *  Active playback must be stopped first. */
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

	/** Begin playback at TargetTick. V9 restores the nearest checkpoint at or
	 *  before the target and deterministically catches up through the indexed
	 *  turn journal. Frozen v8 files support tick zero only. */
	bool PlayFromTick(int32 TargetTick);

	/** Abort playback and unhook the private replay turn-boundary notifier. An explicit abort leaves
	 *  simulation control to the caller; natural completion stops at EndTick. */
	void Stop();

	/** True if currently driving the sim from the loaded turn buffer. */
	bool IsPlaying() const { return bPlaying; }

	/** Number of executable turns in the loaded replay. V9 does not retain them
	 *  as a whole-match command array. */
	int32 GetTurnCount() const
	{
		return bLoadedV9 ? LoadedJournalTurnCount : Loaded.Turns.Num();
	}

	/** Number of lightweight v9 frame descriptors retained after the bounded
	 *  validation scan. Zero for v8. */
	int32 GetIndexedFrameCount() const
	{
		return bLoadedV9
			? LoadedJournalTurnFrames.Num() + LoadedJournalCheckpoints.Num()
			: 0;
	}

	/** Number of decoded turn records currently resident during lazy v9
	 *  playback. Exposed for bounded-memory diagnostics. */
	int32 GetResidentTurnCount() const
	{
		return bLoadedV9 ? ResidentJournalTurns.Num() : Loaded.Turns.Num();
	}

	/** Read the loaded header (zero / default if not loaded). */
	const FSeinReplayHeader& GetHeader() const { return Loaded.Header; }

private:
	struct FIndexedJournalFrame
	{
		int64 FileOffset = 0;
		uint64 Sequence = 0;
		uint8 Type = 0;
		uint8 Flags = 0;
		int32 FirstTurn = INDEX_NONE;
		int32 LastTurn = INDEX_NONE;
		int32 TimelineTick = 0;
		uint32 PayloadBytes = 0;
		FGuid PreviousDigest;
		FGuid CurrentDigest;
		int32 FirstRecordOrdinal = 0;
		int32 RecordCount = 0;
	};

	bool LoadV9FromResolvedPath(
		const FString& ResolvedPath,
		int64 FileSize);
	bool PlayV8();
	bool PlayV9(int32 TargetTick);
	bool ValidateDecodedTurn(
		const FSeinReplayHeader& Header,
		const FSeinReplayTurnRecord& Turn,
		USeinWorldSubsystem* WorldSub,
		FString& OutError) const;
	bool ReadIndexedFramePayload(
		const FIndexedJournalFrame& Descriptor,
		TArray<uint8>& OutPayload,
		FString& OutError) const;
	bool ReadAndDecodeCheckpoint(
		const FIndexedJournalFrame& Descriptor,
		struct FSeinWorldSnapshot& OutSnapshot,
		FString& OutError) const;
	bool RevalidateLoadedJournalFrontier(FString& OutError) const;
	bool LoadResidentTurnFrame(
		int32 FrameIndex,
		USeinWorldSubsystem* WorldSub,
		FString& OutError);
	bool HandleJournalTurnReady(int32 Turn);
	void HandleJournalTurnConsume(int32 Turn);
	void ScheduleJournalPlaybackFailure(const FString& Reason);
	void HandleJournalPlaybackFailure(uint64 ExpectedPlaybackGeneration);
	void ResetJournalPlaybackCursor(int32 CheckpointTick);
	void ResetJournalLoadedState();

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
	bool bLoadedV9 = false;
	bool bLoadedJournalPartial = false;
	bool bOwnsJournalTurnGate = false;
	bool bJournalCatchUpActive = false;
	bool bJournalFailureScheduled = false;
	/** Invalidates queued callbacks whenever playback ownership changes. */
	uint64 PlaybackGeneration = 0;
	FSeinMatchBootstrapAuthorityHandle BootstrapAuthority;

	/** Cursor into Loaded.Turns. Advanced by HandleSimTick; entries before
	 *  this index have already been enqueued. */
	int32 NextTurnIndex = 0;

	FString LoadedJournalPath;
	int64 LoadedJournalFileSize = 0;
	int32 LoadedJournalTurnCount = 0;
	int32 LoadedJournalEarliestTurn = 0;
	int32 NextJournalTurnOrdinal = 0;
	int32 NextJournalFrameIndex = 0;
	int32 ResidentJournalFrameIndex = INDEX_NONE;
	int32 ResidentJournalRecordIndex = 0;
	int32 JournalSeekTargetTick = 0;
	FString PendingJournalFailureReason;
	TArray<FIndexedJournalFrame> LoadedJournalTurnFrames;
	TArray<FIndexedJournalFrame> LoadedJournalCheckpoints;
	FIndexedJournalFrame LoadedJournalDurableFrame;
	FSeinSnapshotBootstrapCheckpoint LoadedJournalBootstrapCheckpoint;

	/** Only one bounded v9 turn frame is decoded at a time. It remains reflected
	 *  because command payload structs may carry UObject references admitted by
	 *  the frozen local command catalog. */
	UPROPERTY()
	TArray<FSeinReplayTurnRecord> ResidentJournalTurns;

	/** Exact world whose notifier owns SimTickHandle. The reader is GI-owned,
	 *  so GetWorld() may already name a destination world during teardown. */
	TWeakObjectPtr<USeinWorldSubsystem> BoundWorldSubsystem;
	FDelegateHandle SimTickHandle;
};
