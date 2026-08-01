/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayWriter.h
 * @brief   Bounded, append-only v9 replay recording.
 *
 * The recorder persists a digest-chained chunk journal while the match is
 * running. Only turns whose first simulation tick has completed are promoted
 * to the durable timeline; the ordinary input-delay future tail remains in a
 * small resident queue and is never made executable by an interrupted file.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/SeinReplayHeader.h"
#include "SeinNetProtocolTypes.h"
#include "SeinReplayWriter.generated.h"

struct FSeinReplayWriterPendingTurn
{
	int32 TurnId = INDEX_NONE;
	FSeinOpaqueCommandBatch OpaqueCommands;
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

	/** Capture and append a checkpoint at the current quiescent tick boundary.
	 *  The first checkpoint is mandatory and must be tick zero. A required
	 *  failure aborts recording; an optional periodic capture is retried later. */
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

#if WITH_DEV_AUTOMATION_TESTS
	/** Force the same applied-turn/progress flush used by deferred maintenance. */
	void FlushAppliedProgressForTests();
#endif

private:
	void ScheduleMaintenance();
	void RunDeferredMaintenance(bool bForce);
	bool FlushEligibleTurns(bool bForce);
	bool AppendProgress(int32 EndTick, bool bForceDuplicate);
	bool AppendFrontierFrame(uint8 FrameType, int32 EndTick);
	bool AppendJournalFrame(
		uint8 FrameType,
		int32 FirstTurn,
		int32 LastTurn,
		int32 TimelineTick,
		TConstArrayView<uint8> Payload);
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
	int32 NextCheckpointRetryTick = 0;
	int32 CheckpointRetryBackoffTicks = 0;

	/** One-shot escalation latch: set when checkpoint retry backoff saturates
	 *  (chronic capture failure), so the Error-level surfacing fires once
	 *  per recording instead of drowning in the per-retry warnings. */
	bool bLoggedChronicCheckpointFailure = false;
};
