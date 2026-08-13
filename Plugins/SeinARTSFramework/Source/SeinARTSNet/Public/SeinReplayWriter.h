/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinReplayWriter.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       13 Aug 2026
 * @brief        Bounded, ordered, append-only v9 replay recording.
 *
 *               The recorder persists a digest-chained chunk journal while
 *               the match is running. Only turns whose first simulation tick
 *               has completed are promoted to the durable timeline; the
 *               ordinary input-delay future tail remains in a small resident
 *               queue and is never made executable by an interrupted file.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/Object.h"
#include "Data/SeinReplayHeader.h"
#include "SeinNetProtocolTypes.h"
#include "SeinReplayWriter.generated.h"

struct FSeinReplayWriterPendingTurn
{
	int32 TurnId = INDEX_NONE;
	FSeinOpaqueCommandBatch OpaqueCommands;
};

struct FSeinReplayAsyncAppendResult
{
	bool bSucceeded = false;
	FString Error;
};

struct FSeinReplayCheckpointEncodeWork;
#if WITH_DEV_AUTOMATION_TESTS
struct FSeinReplayAsyncAppendTestGate;
struct FSeinReplayAsyncCheckpointEncodeTestGate;
#endif

struct FSeinReplayAsyncCheckpointEncodeResult
{
	bool bSucceeded = false;
	int32 SnapshotTick = INDEX_NONE;
	/** Compatibility slot. Live checkpoint bytes remain on the private work
	 *  object so future shared state cannot extend their lifetime. */
	TArray<uint8> Envelope;
	FString Error;
};

enum class ESeinReplayAsyncAppendKind : uint8
{
	None,
	TurnBatch,
	Progress,
	Checkpoint,
};

UCLASS()
class SEINARTSNET_API USeinReplayWriter : public UObject
{
	GENERATED_BODY()

public:
	/** Validate and open a unique Saved/Replays/*.seinreplay.partial v9 journal.
	 *  Re-entering preserves the previous partial recording and starts a new one. */
	void StartRecording(const FSeinReplayHeader& Header);

	/** Encode then record one canonical assembled turn. This convenience seam is
	 *  retained for tests/tooling; live fan-out should pass its exact wire bytes
	 *  to RecordEncodedTurn instead. */
	void RecordTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Validate and retain the exact opaque fan-out bytes for one assembled turn. */
	void RecordEncodedTurn(
		int32 TurnId,
		const FSeinOpaqueCommandBatch& OpaqueCommands);

	/** Observe one completed sim tick. Calls must be contiguous from tick 1.
	 *  File/checkpoint maintenance is deferred beyond the completion callback. */
	void ObserveCompletedTick(int32 CompletedTick);

	/** Capture and durably append a checkpoint at the current quiescent tick
	 *  boundary before returning success. The first checkpoint is mandatory and
	 *  must be tick zero. A required failure aborts recording; an optional
	 *  capture/encode failure is retried later. Automatic periodic maintenance
	 *  keeps capture synchronous, then performs encoding and append in one
	 *  ordered background pipeline. */
	bool CaptureCheckpoint(bool bRequired);

	/** Flush the applied journal, append a terminal frontier, and atomically
	 *  publish the partial as *.seinreplay. Future input-delay turns are omitted. */
	FString FinishRecording();

	bool IsRecording() const { return bRecording; }

	/** Historical API name retained for compatibility. This is the total number
	 *  of accepted turn records, not the number currently resident in memory. */
	int32 GetBufferedTurnCount() const { return TotalRecordedTurnCount; }

	int32 GetObservedEndTick() const { return LastObservedCompletedTick; }
	bool HasTickObservationFailure() const { return bTickObservationFailed; }

	/** Streaming diagnostics used by tests and server telemetry. */
	const FString& GetActivePartialPath() const { return ActivePartialPath; }
	const FString& GetLastPublishedPath() const { return LastPublishedPath; }
	uint64 GetPersistedBytes() const { return PersistedBytes; }
	int32 GetPersistedTurnCount() const { return PersistedTurnCount; }
	int32 GetResidentTurnCount() const { return PendingTurns.Num(); }
	int32 GetPeakResidentTurnCount() const { return PeakResidentTurnCount; }
	uint64 GetResidentBytes() const { return ResidentBytes; }
	uint64 GetPeakResidentBytes() const { return PeakResidentBytes; }
	int32 GetPersistedCheckpointCount() const
	{
		return PersistedCheckpointCount;
	}
	bool IsCheckpointAppendPending() const
	{
		return PendingAppendKind == ESeinReplayAsyncAppendKind::Checkpoint;
	}
	bool IsCheckpointEncodePending() const
	{
		return PendingCheckpointEncodeFuture.IsValid();
	}

#if WITH_DEV_AUTOMATION_TESTS
	/** Queue one ordinary non-blocking maintenance pass for worker/drain tests. */
	void QueueAppliedProgressForTests();
	/** Run the exact scheduled maintenance body, including checkpoint cadence. */
	void RunScheduledMaintenanceForTests();
	/** Force the same applied-turn/progress flush used by deferred maintenance. */
	void FlushAppliedProgressForTests();
	/** Wait for checkpoint encoding and start its ordinary background append. */
	void ResolveCheckpointEncodeForTests();
	/** Hold the next checkpoint encode after snapshot payload serialization and
	 *  before envelope framing, without changing the production data path. */
	void HoldNextCheckpointEncodeForTests();
	/** Wait until the held checkpoint encode has entered its worker boundary. */
	bool WaitForHeldCheckpointEncodeForTests(uint32 WaitTimeMilliseconds) const;
	/** Release any armed or active checkpoint-encode gate. */
	void ReleaseHeldCheckpointEncodeForTests();
	/** Release the active checkpoint encoder after a wall-clock delay and
	 *  return the release timestamp for GC-overlap qualification. */
	TFuture<double> ReleaseHeldCheckpointEncodeAfterDelayForTests(
		uint32 DelayMilliseconds) const;
	/** Fail the next scheduled worker append before touching the file. */
	void FailNextBackgroundAppendForTests()
	{
		bFailNextBackgroundAppendForTests = true;
	}
	/** Hold the next background append after opening and positioning its file
	 *  handle, but before writing any bytes. */
	void HoldNextBackgroundAppendForTests();
	/** Hold the next checkpoint append after its file is open at the verified
	 *  offset, without consuming the gate on preceding turn/progress frames. */
	void HoldNextCheckpointAppendForTests();
	/** Wait until the held append worker has reached its storage boundary. */
	bool WaitForHeldBackgroundAppendForTests(uint32 WaitTimeMilliseconds) const;
	/** Release the held append after the writer enters its forced wait. */
	TFuture<bool> ReleaseHeldBackgroundAppendAfterWriterWaitForTests(
		uint32 WaitTimeMilliseconds) const;
	/** Release any armed or active append gate without waiting for pressure. */
	void ReleaseHeldBackgroundAppendForTests();
	/** Invalidate callbacks, release gates, and synchronously quiesce both
	 *  worker stages before a failed test tears down their world and file. */
	void AbortAndDrainBackgroundWorkForTests();
	/** Return the exact resident-turn limit used by the production writer. */
	int32 GetMaximumResidentTurnsForTests() const
	{
		return GetMaximumResidentTurns();
	}
#endif

private:
	void ScheduleMaintenance();
	void RunScheduledMaintenancePass();
	bool CaptureCheckpointInternal(
		bool bRequired,
		bool bAllowBackgroundAppend);
	bool HandleCheckpointFailure(
		bool bRequired,
		const FString& Reason);
	void RunDeferredMaintenance(bool bForce);
	bool ResolvePendingCheckpointEncode(
		bool bWait,
		bool bAppendSynchronously);
	void WaitAndDiscardPendingCheckpointEncode();
	void DiscardCompletedCheckpointEncode(
		uint64 ExpectedGeneration,
		uint64 ExpectedOperationId);
	bool ResolvePendingAppend(bool bWait);
	bool HasPendingAppend() const { return PendingAppendFuture.IsValid(); }
	void WaitAndDiscardPendingAppend();
	bool FlushEligibleTurns(bool bForce);
	bool AppendProgress(
		int32 EndTick,
		bool bForceDuplicate,
		bool bAsync = false);
	bool AppendFrontierFrame(
		uint8 FrameType,
		int32 EndTick,
		bool bAsync = false);
	bool AppendJournalFrame(
		uint8 FrameType,
		int32 FirstTurn,
		int32 LastTurn,
		int32 TimelineTick,
		TConstArrayView<uint8> Payload,
		bool bAsync = false);
	bool CanPublishFrontier(int32 EndTick, FString& OutError) const;
	bool IsPeriodicCheckpointDue() const;
	void FailRecording(
		const FString& Reason,
		bool bTickFailure = false);
	void ReleaseResidentTurns();
	void ResetForNewRecording();
	int32 GetEligiblePendingTurnCount() const;
	uint64 GetMaximumFileBytes() const;
	uint64 GetMaximumResidentBytes() const;
	int32 GetMaximumResidentTurns() const;

	UPROPERTY()
	FSeinReplayHeader RecordingHeader;

	TArray<FSeinReplayWriterPendingTurn> PendingTurns;
	FString ActivePartialPath;
	FString FinalFilePath;
	FString LastPublishedPath;
	FGuid PreviousFrameDigest;
	/** At most one append may be in flight because every frame commits the
	 *  previous frame digest and exact byte offset. The worker owns only copied
	 *  path/byte data; all UObject and journal-state mutation remains on the
	 *  game thread when ResolvePendingAppend observes completion. */
	TFuture<FSeinReplayAsyncAppendResult> PendingAppendFuture;
	/** Periodic checkpoint capture remains at a quiescent game-thread boundary.
	 *  Its detached, immutable snapshot is then encoded on the worker pool while
	 *  this game-thread-owned work object keeps every reflected reference rooted.
	 *  No journal append may overtake this future. */
	TSharedPtr<FSeinReplayCheckpointEncodeWork, ESPMode::ThreadSafe>
		PendingCheckpointEncodeWork;
	TFuture<FSeinReplayAsyncCheckpointEncodeResult>
		PendingCheckpointEncodeFuture;
	uint64 PendingCheckpointEncodeGeneration = MAX_uint64;
	uint64 PendingCheckpointEncodeOperationId = MAX_uint64;
	uint64 PendingAppendOperationId = MAX_uint64;
	uint64 NextAsyncOperationId = 1;
	ESeinReplayAsyncAppendKind PendingAppendKind =
		ESeinReplayAsyncAppendKind::None;
	FGuid PendingAppendDigest;
	uint64 PendingAppendByteCount = 0;
	uint64 PendingAppendResidentBytes = 0;
	int32 PendingAppendTurnCount = 0;
	int32 PendingAppendFirstTurn = INDEX_NONE;
	int32 PendingAppendLastTurn = INDEX_NONE;
	int32 PendingAppendTimelineTick = INDEX_NONE;
	int32 PendingAppendCheckpointTurnCount = INDEX_NONE;
	/** Source simulation identity for this epoch. The UObject outer may already
	 *  resolve to a destination world while committed travel retires the journal. */
	TWeakObjectPtr<UWorld> RecordingWorld;

	bool bRecording = false;
	bool bTickObservationFailed = false;
	bool bJournalObservationFailed = false;
	bool bHasInitialCheckpoint = false;
	bool bMaintenanceScheduled = false;
	bool bMaintenanceRunning = false;
	bool bFinalizing = false;
	uint64 RecordingGeneration = 0;
	uint64 NextFrameSequence = 0;
	uint64 PersistedBytes = 0;
	uint64 ResidentBytes = 0;
	uint64 PeakResidentBytes = 0;
	int32 PeakResidentTurnCount = 0;
	int32 TotalRecordedTurnCount = 0;
	int32 PersistedTurnCount = 0;
	int32 FirstPersistedTurn = INDEX_NONE;
	int32 LastPersistedTurn = INDEX_NONE;
	int64 NextExpectedTurn = INDEX_NONE;
	int32 LastObservedCompletedTick = 0;
	int32 LastProgressTick = INDEX_NONE;
	int32 LastCheckpointPersistedTurnCount = 0;
	int32 PersistedCheckpointCount = 0;
	int32 NextCheckpointRetryTick = 0;
	int32 CheckpointRetryBackoffTicks = 0;

	/** One-shot escalation latch: set when checkpoint retry backoff saturates
	 *  (chronic capture failure), so the Error-level surfacing fires once
	 *  per recording instead of drowning in the per-retry warnings. */
	bool bLoggedChronicCheckpointFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
	bool bFailNextBackgroundAppendForTests = false;
	TSharedPtr<FSeinReplayAsyncCheckpointEncodeTestGate, ESPMode::ThreadSafe>
		NextCheckpointEncodeTestGate;
	TSharedPtr<FSeinReplayAsyncCheckpointEncodeTestGate, ESPMode::ThreadSafe>
		ActiveCheckpointEncodeTestGate;
	TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>
		NextBackgroundAppendTestGate;
	TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>
		NextCheckpointAppendTestGate;
	TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>
		ActiveBackgroundAppendTestGate;
#endif
};
