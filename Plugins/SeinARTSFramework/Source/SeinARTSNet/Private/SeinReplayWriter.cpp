/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinReplayWriter.cpp
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       13 Aug 2026
 * @brief        Persists bounded replay journals and ordered checkpoints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "SeinReplayWriter.h"

#include "SeinARTSNet.h"
#include "SeinNetCommandWireCodec.h"
#include "SeinReplayFileIO.h"
#include "SeinReplayFormat.h"
#include "SeinReplayJournalFormat.h"
#include "SeinReplayWireCodec.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Serialization/SeinSnapshotTransferTestHooks.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
struct FSeinReplayAsyncAppendTestGate
{
	FSharedEventRef WorkerEntered{EEventMode::ManualReset};
	FSharedEventRef WriterWaitEntered{EEventMode::ManualReset};
	FSharedEventRef ReleaseWorker{EEventMode::ManualReset};
};

struct FSeinReplayAsyncCheckpointEncodeTestGate
{
	FSharedEventRef WorkerEntered{EEventMode::ManualReset};
	FSharedEventRef ReleaseWorker{EEventMode::ManualReset};
};
#endif

struct FSeinReplayCheckpointEncodeWork
{
	TStrongObjectPtr<USeinReplayWriter> WriterRoot;
	FSeinWorldSnapshot Snapshot;
	FSeinWorldSnapshotReferenceGuard SnapshotGuard;

	explicit FSeinReplayCheckpointEncodeWork(USeinReplayWriter* Writer)
		: WriterRoot(Writer)
		, SnapshotGuard(Snapshot)
	{
	}

	~FSeinReplayCheckpointEncodeWork()
	{
		check(IsInGameThread());
	}
};

namespace
{
	int32 GetTicksPerTurnFromSettings()
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		if (!Settings || Settings->TurnRate <= 0)
		{
			return 1;
		}
		return FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
	}

	int32 GetInputDelayTurnsFromSettings()
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		return Settings && Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
	}

	int32 GetTurnBatchSizeFromSettings()
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		return FMath::Clamp(
			Settings ? Settings->ReplayTurnBatchSize : 64,
			1,
			SeinReplayJournalFormat::MaxTurnRecordsPerBatch);
	}

	int32 GetProgressIntervalTicks()
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		return FMath::Max(1, Settings ? Settings->SimulationTickRate : 30);
	}

	bool ValidateHeaderAgainstWorld(
		const FSeinReplayHeader& Header,
		const USeinWorldSubsystem& World,
		bool bRequireCurrentStartTick,
		FString& OutError)
	{
		if (Header.CommandProtocolDigest != World.GetCommandProtocolDigest()
			|| Header.ConfigFingerprint != World.GetConfigFingerprint()
			|| Header.MatchSettingsDigest != World.GetMatchSettingsDigest()
			|| !World.IsSimulationContentReady()
			|| Header.BootstrapReceipt.SimulationContentDigest
				!= World.GetSimulationContentDigest()
			|| Header.BootstrapReceipt != World.GetMatchBootstrapReceipt()
			|| Header.RandomSeed != World.GetSessionSeed()
			|| (bRequireCurrentStartTick
				&& Header.StartTick != World.GetCurrentTick()))
		{
			OutError = TEXT("header protocol, content, config, settings, seed, or start tick disagrees with the attached world");
			return false;
		}
		return true;
	}

	FSeinStructWireCatalogView GetFrozenReplayHeaderCatalog(
		const USeinWorldSubsystem* World)
	{
		return World
			? FSeinStructWireCatalogView{
				World->GetCommandAdditionalDynamicPayloadStructs(),
				World->GetCommandAdditionalWireNames()}
			: FSeinStructWireCatalogView{};
	}

}

void USeinReplayWriter::ResetForNewRecording()
{
	// Re-entry is rare and explicit. Finish any worker that still owns the old
	// journal path before forgetting its state, so the preserved partial has a
	// stable length when the new recording begins.
	WaitAndDiscardPendingCheckpointEncode();
	WaitAndDiscardPendingAppend();
	++RecordingGeneration;
	bRecording = false;
	bTickObservationFailed = false;
	bJournalObservationFailed = false;
	bHasInitialCheckpoint = false;
	bMaintenanceScheduled = false;
	bMaintenanceRunning = false;
	bFinalizing = false;
	RecordingHeader = FSeinReplayHeader();
	ReleaseResidentTurns();
	ActivePartialPath.Reset();
	FinalFilePath.Reset();
	RecordingWorld.Reset();
	PreviousFrameDigest.Invalidate();
	NextFrameSequence = 0;
	PersistedBytes = 0;
	ResidentBytes = 0;
	PeakResidentBytes = 0;
	PeakResidentTurnCount = 0;
	TotalRecordedTurnCount = 0;
	PersistedTurnCount = 0;
	FirstPersistedTurn = INDEX_NONE;
	LastPersistedTurn = INDEX_NONE;
	NextExpectedTurn = INDEX_NONE;
	LastObservedCompletedTick = 0;
	LastProgressTick = INDEX_NONE;
	LastCheckpointPersistedTurnCount = 0;
	PersistedCheckpointCount = 0;
	NextCheckpointRetryTick = 0;
	CheckpointRetryBackoffTicks = 0;
	bLoggedChronicCheckpointFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
	bFailNextBackgroundAppendForTests = false;
#endif
}

void USeinReplayWriter::StartRecording(const FSeinReplayHeader& Header)
{
	if (bRecording)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("ReplayWriter::StartRecording called while already recording; preserving partial %s and starting a new journal."),
			*ActivePartialPath);
	}
	ResetForNewRecording();

	FSeinReplayHeader CandidateHeader = Header;
	CandidateHeader.EndTick = 0;
	FSeinMatchSettings CanonicalSettings = CandidateHeader.SettingsSnapshot;
	FGuid SettingsDigest;
	FString Error;
	const UWorld* AttachedWorld = GetWorld();
	const USeinWorldSubsystem* WorldSub = AttachedWorld
		? AttachedWorld->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	FGameplayTag SettingsRejectionReason;
	const bool bCompatibleRuntime = AttachedWorld
		? SeinReplayCompatibility::ValidateCurrent(
			CandidateHeader, AttachedWorld, Error)
		: CandidateHeader.FrameworkVersion
				== SeinReplayCompatibility::GetFrameworkVersion()
			&& CandidateHeader.GameVersion
				== SeinReplayCompatibility::GetGameVersion();
	const bool bValidMatchSettings = !WorldSub
		|| WorldSub->ValidateMatchSettings(
			CandidateHeader.SettingsSnapshot, SettingsRejectionReason);
	const bool bMatchesWorld = !WorldSub
		|| ValidateHeaderAgainstWorld(
			CandidateHeader,
			*WorldSub,
			/*bRequireCurrentStartTick=*/true,
			Error);
	if (!CandidateHeader.CommandProtocolDigest.IsValid()
		|| !CandidateHeader.MatchSettingsDigest.IsValid()
		|| !CandidateHeader.BootstrapReceipt.IsValid()
		|| CandidateHeader.BootstrapReceipt.ContractDigest
			!= CandidateHeader.MatchSettingsDigest
		|| CandidateHeader.MapIdentifier.IsEmpty()
		|| !bCompatibleRuntime
		|| !bValidMatchSettings
		|| !bMatchesWorld
		|| CandidateHeader.StartTick != 0
		|| !SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSettings, SettingsDigest, nullptr)
		|| SettingsDigest != CandidateHeader.MatchSettingsDigest)
	{
		if (Error.IsEmpty() && SettingsRejectionReason.IsValid())
		{
			Error = SettingsRejectionReason.ToString();
		}
		if (Error.IsEmpty())
		{
			Error = TEXT("required identity, digest, or tick metadata is inconsistent");
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused recording with an incomplete or inconsistent compatibility header (%s)."),
			*Error);
		return;
	}

	CandidateHeader.SettingsSnapshot = MoveTemp(CanonicalSettings);
	CandidateHeader.Players.Sort([](
		const FSeinPlayerRegistration& A,
		const FSeinPlayerRegistration& B)
	{
		return A.PlayerID.Value < B.PlayerID.Value;
	});
	if (!SeinReplayCompatibility::ValidatePlayerManifest(
		CandidateHeader, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused inconsistent player metadata: %s."),
			*Error);
		return;
	}

	FSeinReplay HeaderDocument;
	HeaderDocument.Header = CandidateHeader;
	TArray<uint8> HeaderPayload;
	const auto FindSchema = [WorldSub](
		FGameplayTag Type,
		int32 Version,
		FSeinCommandSchemaDescriptor& Out)
	{
		return WorldSub
			? WorldSub->FindCommandSchema(Type, Version, Out)
			: FSeinCommandSchemaRegistry::FindSchema(Type, Version, Out);
	};
	if (!FSeinReplayWireCodec::Encode(
			HeaderDocument,
			GetFrozenReplayHeaderCatalog(WorldSub),
			FindSchema,
			HeaderPayload,
			Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused recording because its bounded header cannot be encoded: %s."),
			*Error);
		return;
	}

	const FString MapName = CandidateHeader.MapIdentifier.IsEmpty()
		? TEXT("UnknownMap")
		: FPackageName::GetShortName(CandidateHeader.MapIdentifier);
	const FString Stamp = FString::Printf(
		TEXT("%s_%lld_%s"),
		*CandidateHeader.RecordedAt.ToString(TEXT("%Y%m%d_%H%M%S")),
		static_cast<long long>(CandidateHeader.RecordedAt.GetTicks()),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Replays");
	FinalFilePath = Directory / FString::Printf(
		TEXT("%s_%s.seinreplay"), *MapName, *Stamp);
	ActivePartialPath = FinalFilePath + TEXT(".partial");

	TArray<uint8> PrefixBytes;
	SeinReplayJournalFormat::FPrefix Prefix;
	if (!SeinReplayJournalFormat::BuildPrefix(
			CandidateHeader.CommandProtocolDigest,
			CandidateHeader.MatchSettingsDigest,
			CandidateHeader.BootstrapReceipt,
			CandidateHeader.ConfigFingerprint,
			FGuid::NewGuid(),
			PrefixBytes,
			Prefix,
			Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: could not build the v9 prefix: %s."), *Error);
		ActivePartialPath.Reset();
		FinalFilePath.Reset();
		return;
	}

	TArray<uint8> HeaderFrameBytes;
	SeinReplayJournalFormat::FFrameHeader HeaderFrame;
	if (!SeinReplayJournalFormat::BuildFrame(
			SeinReplayJournalFormat::EFrameType::Header,
			/*Flags=*/0,
			/*Sequence=*/0,
			INDEX_NONE,
			INDEX_NONE,
			/*TimelineTick=*/0,
			Prefix.PrefixDigest,
			HeaderPayload,
			HeaderFrameBytes,
			HeaderFrame,
			Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: could not build the v9 Header frame: %s."),
			*Error);
		ActivePartialPath.Reset();
		FinalFilePath.Reset();
		return;
	}

	TArray<uint8> InitialBytes = MoveTemp(PrefixBytes);
	InitialBytes.Append(HeaderFrameBytes);
	if (static_cast<uint64>(InitialBytes.Num()) > GetMaximumFileBytes()
		|| !SeinReplayFileIO::CreateNew(
			ActivePartialPath, InitialBytes, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: could not create v9 partial journal %s: %s."),
			*ActivePartialPath,
			Error.IsEmpty() ? TEXT("recording file-size policy is too small") : *Error);
		ActivePartialPath.Reset();
		FinalFilePath.Reset();
		return;
	}

	RecordingHeader = MoveTemp(CandidateHeader);
	PreviousFrameDigest = HeaderFrame.CurrentDigest;
	NextFrameSequence = 1;
	PersistedBytes = InitialBytes.Num();
	NextExpectedTurn = GetInputDelayTurnsFromSettings();
	RecordingWorld = const_cast<UWorld*>(AttachedWorld);
	bRecording = true;

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayWriter: v9 recording opened at %s (seed=%lld map=%s)."),
		*ActivePartialPath,
		RecordingHeader.RandomSeed,
		*RecordingHeader.MapIdentifier);
}

void USeinReplayWriter::RecordTurn(
	int32 TurnId,
	const TArray<FSeinCommand>& Commands)
{
	if (!bRecording)
	{
		return;
	}
	if (Commands.Num() > SeinReplayFormat::MaxCommandsPerTurn)
	{
		FailRecording(FString::Printf(
			TEXT("turn %d has %d commands, exceeding the canonical turn cap %d"),
			TurnId,
			Commands.Num(),
			SeinReplayFormat::MaxCommandsPerTurn));
		return;
	}

	const UWorld* World = RecordingWorld.Get();
	const USeinWorldSubsystem* WorldSub = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	FSeinOpaqueCommandBatch Encoded;
	FString Error;
	const auto FindSchema = [WorldSub](
		FGameplayTag Type,
		int32 Version,
		FSeinCommandSchemaDescriptor& Out)
	{
		return WorldSub
			? WorldSub->FindCommandSchema(Type, Version, Out)
			: FSeinCommandSchemaRegistry::FindSchema(Type, Version, Out);
	};
	if (!FSeinNetCommandWireCodec::EncodeCommands(
			Commands,
			SeinReplayFormat::MaxCommandsPerTurn,
			FindSchema,
			Encoded,
			Error))
	{
		FailRecording(FString::Printf(
			TEXT("turn %d cannot be encoded by the canonical command codec: %s"),
			TurnId, *Error));
		return;
	}
	RecordEncodedTurn(TurnId, Encoded);
}

void USeinReplayWriter::RecordEncodedTurn(
	int32 TurnId,
	const FSeinOpaqueCommandBatch& OpaqueCommands)
{
	if (!bRecording)
	{
		return;
	}
	if (TurnId != NextExpectedTurn)
	{
		FailRecording(FString::Printf(
			TEXT("turn journal is not contiguous: received %d, expected %d"),
			TurnId, NextExpectedTurn));
		return;
	}
	if (OpaqueCommands.Bytes.IsEmpty()
		|| OpaqueCommands.Bytes.Num()
			> static_cast<int32>(FSeinOpaqueCommandBatch::MaxBytes))
	{
		FailRecording(FString::Printf(
			TEXT("turn %d has invalid opaque command bytes"), TurnId));
		return;
	}

	const UWorld* World = RecordingWorld.Get();
	const USeinWorldSubsystem* WorldSub = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	TArray<FSeinCommand> Commands;
	FString Error;
	const auto FindSchema = [WorldSub](
		FGameplayTag Type,
		int32 Version,
		FSeinCommandSchemaDescriptor& Out)
	{
		return WorldSub
			? WorldSub->FindCommandSchema(Type, Version, Out)
			: FSeinCommandSchemaRegistry::FindSchema(Type, Version, Out);
	};
	if (!FSeinNetCommandWireCodec::DecodeCommands(
			OpaqueCommands,
			SeinReplayFormat::MaxCommandsPerTurn,
			FindSchema,
			Commands,
			Error))
	{
		FailRecording(FString::Printf(
			TEXT("turn %d exact fan-out bytes fail canonical decode: %s"),
			TurnId, *Error));
		return;
	}

	FSeinReplayTurnRecord DecodedTurn;
	DecodedTurn.TurnId = TurnId;
	DecodedTurn.Commands = MoveTemp(Commands);
	if (!SeinReplayFormat::ValidateTurnEnvelope(
			RecordingHeader,
			DecodedTurn,
			GetTicksPerTurnFromSettings(),
			GetInputDelayTurnsFromSettings(),
			Error))
	{
		FailRecording(FString::Printf(
			TEXT("turn %d fails the replay envelope contract: %s"),
			TurnId, *Error));
		return;
	}
	for (const FSeinCommand& Command : DecodedTurn.Commands)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bValidStructure = WorldSub
			? WorldSub->ValidateCommandStructure(Command, &Schema)
				== ESeinCommandStructureResult::Valid
			: FSeinCommandSchemaRegistry::FindSchema(
				Command.CommandType, Command.SchemaVersion, Schema);
		if (!bValidStructure
			|| !SeinReplayFormat::ValidateIssuerForSchema(
				Command.IssuerKind, Schema.AuthorityScope, Error))
		{
			FailRecording(FString::Printf(
				TEXT("turn %d command structure/provenance is invalid: %s"),
				TurnId, *Error));
			return;
		}
		// Tripwire, not dead code: the server currently drops pause-control
		// BEFORE aggregation (USeinNetSubsystem's IsUnsupportedNetworkPauseCommand
		// gate), so this abort is unreachable today. It exists so that
		// installing the canonical pause lane without extending the replay
		// format fails loudly here instead of journaling turns v9 cannot
		// faithfully replay.
		if (Command.CommandType
				== SeinARTSTags::Command_Type_PauseMatchRequest
			|| (Schema.AllowedExecutionContexts
				& static_cast<int32>(
					ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0)
		{
			FailRecording(FString::Printf(
				TEXT("turn %d contains unsupported pause-control; v9 has no frozen-time control journal"),
				TurnId));
			return;
		}
	}

	const uint64 AddedBytes =
		static_cast<uint64>(OpaqueCommands.Bytes.Num());
	if ((PendingTurns.Num() >= GetMaximumResidentTurns()
			|| AddedBytes > GetMaximumResidentBytes() - ResidentBytes)
		&& bHasInitialCheckpoint)
	{
		RunDeferredMaintenance(/*bForce=*/true);
	}
	if (!bRecording)
	{
		return;
	}
	if (PendingTurns.Num() >= GetMaximumResidentTurns()
		|| ResidentBytes > GetMaximumResidentBytes()
		|| AddedBytes > GetMaximumResidentBytes() - ResidentBytes)
	{
		FailRecording(FString::Printf(
			TEXT("bounded resident turn queue is exhausted at turn %d"),
			TurnId));
		return;
	}

	FSeinReplayWriterPendingTurn& Pending =
		PendingTurns.AddDefaulted_GetRef();
	Pending.TurnId = TurnId;
	Pending.OpaqueCommands = OpaqueCommands;
	ResidentBytes += AddedBytes;
	PeakResidentBytes = FMath::Max(PeakResidentBytes, ResidentBytes);
	PeakResidentTurnCount = FMath::Max(
		PeakResidentTurnCount, PendingTurns.Num());
	++TotalRecordedTurnCount;
	if (NextExpectedTurn == MAX_int32)
	{
		FailRecording(TEXT("turn journal exhausted the int32 turn range"));
		return;
	}
	++NextExpectedTurn;
	ScheduleMaintenance();
}

void USeinReplayWriter::ObserveCompletedTick(int32 CompletedTick)
{
	if (!bRecording)
	{
		return;
	}
	const int64 Expected =
		static_cast<int64>(LastObservedCompletedTick) + 1;
	if (Expected > MAX_int32 || CompletedTick != static_cast<int32>(Expected))
	{
		FailRecording(FString::Printf(
			TEXT("non-contiguous completed tick %d (expected %lld)"),
			CompletedTick,
			static_cast<long long>(Expected)),
			/*bTickFailure=*/true);
		return;
	}
	LastObservedCompletedTick = CompletedTick;
	ScheduleMaintenance();
}

void USeinReplayWriter::ScheduleMaintenance()
{
	if (!bRecording || bMaintenanceScheduled)
	{
		return;
	}
	bMaintenanceScheduled = true;
	const uint64 ScheduledGeneration = RecordingGeneration;
	TWeakObjectPtr<USeinReplayWriter> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread,
		[WeakThis, ScheduledGeneration]()
		{
			USeinReplayWriter* Writer = WeakThis.Get();
			if (!Writer
				|| Writer->RecordingGeneration != ScheduledGeneration)
			{
				return;
			}
			Writer->RunScheduledMaintenancePass();
		});
}

void USeinReplayWriter::RunScheduledMaintenancePass()
{
	bMaintenanceScheduled = false;
	if (!bRecording)
	{
		return;
	}
	if (!ResolvePendingCheckpointEncode(
			/*bWait=*/false,
			/*bAppendSynchronously=*/false)
		|| PendingCheckpointEncodeFuture.IsValid())
	{
		return;
	}
	RunDeferredMaintenance(/*bForce=*/false);
	if (bRecording && IsPeriodicCheckpointDue())
	{
		CaptureCheckpointInternal(
			/*bRequired=*/false,
			/*bAllowBackgroundAppend=*/true);
	}
}

void USeinReplayWriter::RunDeferredMaintenance(bool bForce)
{
	if (!bRecording || bMaintenanceRunning || !bHasInitialCheckpoint)
	{
		return;
	}
	bMaintenanceRunning = true;
	if (!ResolvePendingCheckpointEncode(
			/*bWait=*/bForce,
			/*bAppendSynchronously=*/bForce))
	{
		bMaintenanceRunning = false;
		return;
	}
	if (PendingCheckpointEncodeFuture.IsValid())
	{
		// The checkpoint owns the next digest-chain position. Resident turns may
		// accumulate, but no later frame may overtake its captured frontier.
		bMaintenanceRunning = false;
		return;
	}
	if (!FlushEligibleTurns(bForce))
	{
		bMaintenanceRunning = false;
		return;
	}
	if (HasPendingAppend())
	{
		// One ordered append is now owned by the worker. Its completion queues
		// another maintenance pass; no later digest-chained frame may overtake it.
		bMaintenanceRunning = false;
		return;
	}

	const bool bProgressDue = bForce
		|| LastProgressTick == INDEX_NONE
		|| LastObservedCompletedTick - LastProgressTick
			>= GetProgressIntervalTicks();
	if (bProgressDue)
	{
		FString Error;
		if (CanPublishFrontier(LastObservedCompletedTick, Error))
		{
			AppendProgress(
				LastObservedCompletedTick,
				/*bForceDuplicate=*/bForce,
				/*bAsync=*/!bForce);
		}
		else if (bForce)
		{
			FailRecording(FString::Printf(
				TEXT("cannot publish the observed replay frontier: %s"),
				*Error));
		}
	}
	bMaintenanceRunning = false;
}

bool USeinReplayWriter::ResolvePendingCheckpointEncode(
	bool bWait,
	bool bAppendSynchronously)
{
	if (!PendingCheckpointEncodeFuture.IsValid())
	{
		return true;
	}
	if (!bWait && !PendingCheckpointEncodeFuture.IsReady())
	{
		return true;
	}
	if (bWait)
	{
		PendingCheckpointEncodeFuture.Wait();
	}

	FSeinReplayAsyncCheckpointEncodeResult Result =
		PendingCheckpointEncodeFuture.Consume();
#if WITH_DEV_AUTOMATION_TESTS
	ActiveCheckpointEncodeTestGate.Reset();
#endif
	PendingCheckpointEncodeGeneration = MAX_uint64;
	PendingCheckpointEncodeOperationId = MAX_uint64;
	// The worker borrows this object from the writer. Consume proves the callable
	// has returned, so dropping the sole owner destroys the GC guard here.
	PendingCheckpointEncodeWork.Reset();
	if (!Result.bSucceeded)
	{
		HandleCheckpointFailure(
			/*bRequired=*/false,
			FString::Printf(
				TEXT("checkpoint envelope encode failed: %s"),
				*Result.Error));
		return bRecording;
	}
	if (Result.SnapshotTick <= 0)
	{
		HandleCheckpointFailure(
			/*bRequired=*/false,
			TEXT("background checkpoint encode returned an invalid tick"));
		return bRecording;
	}

	const int32 EncodedCheckpointBytes = Result.Envelope.Num();
	const bool bAppended = AppendJournalFrame(
			static_cast<uint8>(
				SeinReplayJournalFormat::EFrameType::Checkpoint),
			INDEX_NONE,
			INDEX_NONE,
			Result.SnapshotTick,
			Result.Envelope,
			/*bAsync=*/!bAppendSynchronously);
	Result.Envelope.Reset();
	if (!bAppended)
	{
		return false;
	}
	if (bAppendSynchronously)
	{
		LastCheckpointPersistedTurnCount = PersistedTurnCount;
		++PersistedCheckpointCount;
		NextCheckpointRetryTick = 0;
		CheckpointRetryBackoffTicks = 0;
	}
	UE_LOG(LogSeinNet, Verbose,
		TEXT("ReplayWriter: %s encoded checkpoint at tick %d (%d bytes)."),
		bAppendSynchronously ? TEXT("appended") : TEXT("queued"),
		Result.SnapshotTick,
		EncodedCheckpointBytes);
	return true;
}

void USeinReplayWriter::WaitAndDiscardPendingCheckpointEncode()
{
#if WITH_DEV_AUTOMATION_TESTS
	ReleaseHeldCheckpointEncodeForTests();
#endif
	if (PendingCheckpointEncodeFuture.IsValid())
	{
		PendingCheckpointEncodeFuture.Wait();
		PendingCheckpointEncodeFuture.Consume();
	}
#if WITH_DEV_AUTOMATION_TESTS
	ActiveCheckpointEncodeTestGate.Reset();
#endif
	PendingCheckpointEncodeGeneration = MAX_uint64;
	PendingCheckpointEncodeOperationId = MAX_uint64;
	PendingCheckpointEncodeWork.Reset();
}

void USeinReplayWriter::DiscardCompletedCheckpointEncode(
	uint64 ExpectedGeneration,
	uint64 ExpectedOperationId)
{
	if (PendingCheckpointEncodeGeneration != ExpectedGeneration
		|| PendingCheckpointEncodeOperationId != ExpectedOperationId
		|| !PendingCheckpointEncodeFuture.IsValid())
	{
		return;
	}
	PendingCheckpointEncodeFuture.Wait();
	PendingCheckpointEncodeFuture.Consume();
#if WITH_DEV_AUTOMATION_TESTS
	ActiveCheckpointEncodeTestGate.Reset();
#endif
	PendingCheckpointEncodeGeneration = MAX_uint64;
	PendingCheckpointEncodeOperationId = MAX_uint64;
	PendingCheckpointEncodeWork.Reset();
}

bool USeinReplayWriter::ResolvePendingAppend(bool bWait)
{
	if (!HasPendingAppend())
	{
		return true;
	}
	if (!bWait && !PendingAppendFuture.IsReady())
	{
		return true;
	}
	if (bWait)
	{
#if WITH_DEV_AUTOMATION_TESTS
		if (ActiveBackgroundAppendTestGate)
		{
			ActiveBackgroundAppendTestGate->WriterWaitEntered->Trigger();
		}
#endif
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_WaitForBackgroundAppend);
		PendingAppendFuture.Wait();
	}

	FSeinReplayAsyncAppendResult Result = PendingAppendFuture.Consume();
	PendingAppendOperationId = MAX_uint64;
#if WITH_DEV_AUTOMATION_TESTS
	ActiveBackgroundAppendTestGate.Reset();
#endif
	if (!Result.bSucceeded)
	{
		PendingAppendKind = ESeinReplayAsyncAppendKind::None;
		FailRecording(FString::Printf(
			TEXT("replay background append failed: %s"),
			*Result.Error));
		return false;
	}

	PersistedBytes += PendingAppendByteCount;
	PreviousFrameDigest = PendingAppendDigest;
	++NextFrameSequence;

	if (PendingAppendKind == ESeinReplayAsyncAppendKind::TurnBatch)
	{
		if (PendingAppendTurnCount <= 0
			|| PendingTurns.Num() < PendingAppendTurnCount
			|| PendingTurns[0].TurnId != PendingAppendFirstTurn
			|| PendingTurns[PendingAppendTurnCount - 1].TurnId
				!= PendingAppendLastTurn
			|| PendingAppendResidentBytes > ResidentBytes)
		{
			PendingAppendKind = ESeinReplayAsyncAppendKind::None;
			FailRecording(TEXT("replay resident turn queue changed while its ordered append was in flight"));
			return false;
		}
		ResidentBytes -= PendingAppendResidentBytes;
		if (FirstPersistedTurn == INDEX_NONE)
		{
			FirstPersistedTurn = PendingAppendFirstTurn;
		}
		LastPersistedTurn = PendingAppendLastTurn;
		PersistedTurnCount += PendingAppendTurnCount;
		PendingTurns.RemoveAt(
			0, PendingAppendTurnCount, EAllowShrinking::No);
	}
	else if (PendingAppendKind == ESeinReplayAsyncAppendKind::Progress)
	{
		LastProgressTick = PendingAppendTimelineTick;
	}
	else if (PendingAppendKind == ESeinReplayAsyncAppendKind::Checkpoint)
	{
		if (PendingAppendCheckpointTurnCount < 0
			|| PendingAppendCheckpointTurnCount > PersistedTurnCount)
		{
			PendingAppendKind = ESeinReplayAsyncAppendKind::None;
			FailRecording(TEXT("replay checkpoint completion carried an invalid durable turn count"));
			return false;
		}
		LastCheckpointPersistedTurnCount =
			PendingAppendCheckpointTurnCount;
		++PersistedCheckpointCount;
		NextCheckpointRetryTick = 0;
		CheckpointRetryBackoffTicks = 0;
	}

	PendingAppendKind = ESeinReplayAsyncAppendKind::None;
	PendingAppendDigest.Invalidate();
	PendingAppendByteCount = 0;
	PendingAppendResidentBytes = 0;
	PendingAppendTurnCount = 0;
	PendingAppendFirstTurn = INDEX_NONE;
	PendingAppendLastTurn = INDEX_NONE;
	PendingAppendTimelineTick = INDEX_NONE;
	PendingAppendCheckpointTurnCount = INDEX_NONE;
	return true;
}

void USeinReplayWriter::WaitAndDiscardPendingAppend()
{
#if WITH_DEV_AUTOMATION_TESTS
	ReleaseHeldBackgroundAppendForTests();
#endif
	if (HasPendingAppend())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_WaitForBackgroundAppend);
		PendingAppendFuture.Wait();
		PendingAppendFuture.Consume();
	}
	PendingAppendOperationId = MAX_uint64;
	PendingAppendKind = ESeinReplayAsyncAppendKind::None;
	PendingAppendDigest.Invalidate();
	PendingAppendByteCount = 0;
	PendingAppendResidentBytes = 0;
	PendingAppendTurnCount = 0;
	PendingAppendFirstTurn = INDEX_NONE;
	PendingAppendLastTurn = INDEX_NONE;
	PendingAppendTimelineTick = INDEX_NONE;
	PendingAppendCheckpointTurnCount = INDEX_NONE;
#if WITH_DEV_AUTOMATION_TESTS
	ActiveBackgroundAppendTestGate.Reset();
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void USeinReplayWriter::QueueAppliedProgressForTests()
{
	RunDeferredMaintenance(/*bForce=*/false);
}

void USeinReplayWriter::RunScheduledMaintenanceForTests()
{
	RunScheduledMaintenancePass();
}

void USeinReplayWriter::FlushAppliedProgressForTests()
{
	RunDeferredMaintenance(/*bForce=*/true);
}

void USeinReplayWriter::ResolveCheckpointEncodeForTests()
{
	ResolvePendingCheckpointEncode(
		/*bWait=*/true,
		/*bAppendSynchronously=*/false);
}

void USeinReplayWriter::HoldNextBackgroundAppendForTests()
{
	check(IsInGameThread());
	check(!HasPendingAppend());
	check(!NextBackgroundAppendTestGate);
	check(!ActiveBackgroundAppendTestGate);
	NextBackgroundAppendTestGate = MakeShared<
		FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>();
}

void USeinReplayWriter::HoldNextCheckpointEncodeForTests()
{
	check(IsInGameThread());
	check(!NextCheckpointEncodeTestGate);
	NextCheckpointEncodeTestGate = MakeShared<
		FSeinReplayAsyncCheckpointEncodeTestGate, ESPMode::ThreadSafe>();
}

bool USeinReplayWriter::WaitForHeldCheckpointEncodeForTests(
	uint32 WaitTimeMilliseconds) const
{
	check(IsInGameThread());
	const TSharedPtr<
		FSeinReplayAsyncCheckpointEncodeTestGate,
		ESPMode::ThreadSafe> Gate = ActiveCheckpointEncodeTestGate;
	return Gate && Gate->WorkerEntered->Wait(WaitTimeMilliseconds);
}

void USeinReplayWriter::ReleaseHeldCheckpointEncodeForTests()
{
	check(IsInGameThread());
	if (NextCheckpointEncodeTestGate)
	{
		NextCheckpointEncodeTestGate->ReleaseWorker->Trigger();
		NextCheckpointEncodeTestGate.Reset();
	}
	if (ActiveCheckpointEncodeTestGate)
	{
		ActiveCheckpointEncodeTestGate->ReleaseWorker->Trigger();
	}
}

TFuture<double>
USeinReplayWriter::ReleaseHeldCheckpointEncodeAfterDelayForTests(
	uint32 DelayMilliseconds) const
{
	check(IsInGameThread());
	const TSharedPtr<
		FSeinReplayAsyncCheckpointEncodeTestGate,
		ESPMode::ThreadSafe> Gate = ActiveCheckpointEncodeTestGate;
	return Async(EAsyncExecution::Thread,
		[Gate, DelayMilliseconds]()
		{
			if (!Gate)
			{
				return 0.0;
			}
			FPlatformProcess::SleepNoStats(
				static_cast<float>(DelayMilliseconds) / 1000.0f);
			const double ReleasedAt = FPlatformTime::Seconds();
			Gate->ReleaseWorker->Trigger();
			return ReleasedAt;
		});
}

void USeinReplayWriter::HoldNextCheckpointAppendForTests()
{
	check(IsInGameThread());
	check(!NextCheckpointAppendTestGate);
	NextCheckpointAppendTestGate = MakeShared<
		FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>();
}

bool USeinReplayWriter::WaitForHeldBackgroundAppendForTests(
	uint32 WaitTimeMilliseconds) const
{
	check(IsInGameThread());
	const TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe> Gate =
		ActiveBackgroundAppendTestGate;
	return Gate && Gate->WorkerEntered->Wait(WaitTimeMilliseconds);
}

TFuture<bool>
USeinReplayWriter::ReleaseHeldBackgroundAppendAfterWriterWaitForTests(
	uint32 WaitTimeMilliseconds) const
{
	check(IsInGameThread());
	const TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe> Gate =
		ActiveBackgroundAppendTestGate;
	return Async(EAsyncExecution::Thread,
		[Gate, WaitTimeMilliseconds]()
		{
			if (!Gate)
			{
				return false;
			}
			const bool bWriterWaited =
				Gate->WriterWaitEntered->Wait(WaitTimeMilliseconds);
			Gate->ReleaseWorker->Trigger();
			return bWriterWaited;
		});
}

void USeinReplayWriter::ReleaseHeldBackgroundAppendForTests()
{
	check(IsInGameThread());
	if (NextBackgroundAppendTestGate)
	{
		NextBackgroundAppendTestGate->ReleaseWorker->Trigger();
		NextBackgroundAppendTestGate.Reset();
	}
	if (NextCheckpointAppendTestGate)
	{
		NextCheckpointAppendTestGate->ReleaseWorker->Trigger();
		NextCheckpointAppendTestGate.Reset();
	}
	if (ActiveBackgroundAppendTestGate)
	{
		ActiveBackgroundAppendTestGate->ReleaseWorker->Trigger();
	}
}

void USeinReplayWriter::AbortAndDrainBackgroundWorkForTests()
{
	check(IsInGameThread());
	bRecording = false;
	bMaintenanceScheduled = false;
	++RecordingGeneration;
	WaitAndDiscardPendingCheckpointEncode();
	WaitAndDiscardPendingAppend();
	ReleaseResidentTurns();
}
#endif

int32 USeinReplayWriter::GetEligiblePendingTurnCount() const
{
	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();
	int32 Count = 0;
	for (const FSeinReplayWriterPendingTurn& Pending : PendingTurns)
	{
		const int64 FirstTick =
			static_cast<int64>(Pending.TurnId) * TicksPerTurn;
		if (FirstTick > LastObservedCompletedTick)
		{
			break;
		}
		++Count;
	}
	return Count;
}

bool USeinReplayWriter::FlushEligibleTurns(bool bForce)
{
	if (!bRecording || !bHasInitialCheckpoint)
	{
		return bRecording;
	}
	if (!ResolvePendingAppend(/*bWait=*/bForce))
	{
		return false;
	}
	if (HasPendingAppend())
	{
		return true;
	}
	int32 EligibleCount = GetEligiblePendingTurnCount();
	if (EligibleCount == 0)
	{
		return true;
	}

	const bool bTimeDue = LastProgressTick == INDEX_NONE
		? LastObservedCompletedTick >= GetProgressIntervalTicks()
		: LastObservedCompletedTick - LastProgressTick
			>= GetProgressIntervalTicks();
	const int32 BatchLimit = GetTurnBatchSizeFromSettings();
	if (!bForce && !bTimeDue && EligibleCount < BatchLimit)
	{
		return true;
	}

	while (EligibleCount > 0 && bRecording)
	{
		const int32 CandidateCount = FMath::Min(EligibleCount, BatchLimit);
		TArray<SeinReplayJournalFormat::FTurnRecord> Records;
		Records.Reserve(CandidateCount);
		uint64 PayloadBudget = 4;
		int32 BatchCount = 0;
		for (; BatchCount < CandidateCount; ++BatchCount)
		{
			const FSeinReplayWriterPendingTurn& Pending =
				PendingTurns[BatchCount];
			const uint64 RecordBytes = 8ULL
				+ static_cast<uint64>(
					Pending.OpaqueCommands.Bytes.Num());
			if (PayloadBudget
				> SeinReplayJournalFormat::MaxTurnBatchPayloadBytes
					- RecordBytes)
			{
				break;
			}
			PayloadBudget += RecordBytes;
			SeinReplayJournalFormat::FTurnRecord& Record =
				Records.AddDefaulted_GetRef();
			Record.TurnId = Pending.TurnId;
			Record.OpaqueCommands = Pending.OpaqueCommands;
		}
		if (BatchCount == 0)
		{
			FailRecording(TEXT("one replay turn exceeds the bounded v9 TurnBatch payload"));
			return false;
		}

		TArray<uint8> Payload;
		FString Error;
		if (!SeinReplayJournalFormat::EncodeTurnBatch(
				Records, Payload, Error)
			|| !AppendJournalFrame(
				static_cast<uint8>(
					SeinReplayJournalFormat::EFrameType::TurnBatch),
				Records[0].TurnId,
				Records.Last().TurnId,
				LastObservedCompletedTick,
				Payload,
				/*bAsync=*/!bForce))
		{
			if (bRecording)
			{
				FailRecording(FString::Printf(
					TEXT("could not append a bounded TurnBatch: %s"),
					*Error));
			}
			return false;
		}
		if (!bForce)
		{
			// Commit/removal happens only after the ordered worker reports that the
			// complete frame is durably present at the expected offset.
			return true;
		}

		for (int32 Index = 0; Index < BatchCount; ++Index)
		{
			ResidentBytes -= static_cast<uint64>(
				PendingTurns[Index].OpaqueCommands.Bytes.Num());
		}
		if (FirstPersistedTurn == INDEX_NONE)
		{
			FirstPersistedTurn = Records[0].TurnId;
		}
		LastPersistedTurn = Records.Last().TurnId;
		PersistedTurnCount += BatchCount;
		PendingTurns.RemoveAt(
			0, BatchCount, EAllowShrinking::No);
		EligibleCount -= BatchCount;
	}
	return bRecording;
}

bool USeinReplayWriter::CanPublishFrontier(
	int32 EndTick,
	FString& OutError) const
{
	FSeinReplayHeader CoverageHeader = RecordingHeader;
	CoverageHeader.EndTick = EndTick;
	int32 FirstRequired = INDEX_NONE;
	int32 LastRequired = INDEX_NONE;
	if (!SeinReplayFormat::GetRequiredTurnRange(
			CoverageHeader,
			GetTicksPerTurnFromSettings(),
			GetInputDelayTurnsFromSettings(),
			FirstRequired,
			LastRequired,
			OutError))
	{
		return false;
	}
	const int64 RequiredCount = FirstRequired == INDEX_NONE
		? 0
		: static_cast<int64>(LastRequired) - FirstRequired + 1;
	if (RequiredCount != PersistedTurnCount
		|| (RequiredCount == 0
			&& (FirstPersistedTurn != INDEX_NONE
				|| LastPersistedTurn != INDEX_NONE))
		|| (RequiredCount > 0
			&& (FirstPersistedTurn != FirstRequired
				|| LastPersistedTurn != LastRequired)))
	{
		OutError = FString::Printf(
			TEXT("tick %d requires turns %d..%d (%lld), but durable journal has %d..%d (%d)"),
			EndTick,
			FirstRequired,
			LastRequired,
			static_cast<long long>(RequiredCount),
			FirstPersistedTurn,
			LastPersistedTurn,
			PersistedTurnCount);
		return false;
	}
	return true;
}

bool USeinReplayWriter::AppendProgress(
	int32 EndTick,
	bool bForceDuplicate,
	bool bAsync)
{
	if (!bForceDuplicate && EndTick == LastProgressTick)
	{
		return true;
	}
	if (LastProgressTick != INDEX_NONE && EndTick < LastProgressTick)
	{
		FailRecording(TEXT("replay Progress frontier moved backwards"));
		return false;
	}
	if (!AppendFrontierFrame(
		static_cast<uint8>(
			SeinReplayJournalFormat::EFrameType::Progress),
		EndTick,
		bAsync))
	{
		return false;
	}
	if (!bAsync)
	{
		LastProgressTick = EndTick;
	}
	return true;
}

bool USeinReplayWriter::AppendFrontierFrame(
	uint8 FrameType,
	int32 EndTick,
	bool bAsync)
{
	FString Error;
	if (!CanPublishFrontier(EndTick, Error))
	{
		FailRecording(FString::Printf(
			TEXT("replay frontier is incomplete: %s"), *Error));
		return false;
	}
	if (PersistedTurnCount < 0
		|| static_cast<uint64>(PersistedTurnCount) > MAX_uint32)
	{
		FailRecording(TEXT("replay frontier turn count exceeds v9 limits"));
		return false;
	}

	SeinReplayJournalFormat::FFrontier Frontier;
	Frontier.EndTick = EndTick;
	Frontier.FirstAppliedTurn = FirstPersistedTurn;
	Frontier.LastAppliedTurn = LastPersistedTurn;
	Frontier.AppliedTurnCount =
		static_cast<uint32>(PersistedTurnCount);
	TArray<uint8> Payload;
	if (!SeinReplayJournalFormat::EncodeFrontier(
			Frontier, Payload, Error))
	{
		FailRecording(FString::Printf(
			TEXT("could not encode replay frontier: %s"), *Error));
		return false;
	}
	return AppendJournalFrame(
		FrameType,
		FirstPersistedTurn,
		LastPersistedTurn,
		EndTick,
		Payload,
		bAsync);
}

bool USeinReplayWriter::AppendJournalFrame(
	uint8 FrameType,
	int32 FirstTurn,
	int32 LastTurn,
	int32 TimelineTick,
	TConstArrayView<uint8> Payload,
	bool bAsync)
{
	if (!bRecording || (bAsync && HasPendingAppend()))
	{
		return false;
	}
	const SeinReplayJournalFormat::EFrameType TypedFrame =
		static_cast<SeinReplayJournalFormat::EFrameType>(FrameType);
	const uint64 RequiredCapacity = bFinalizing
		? (TypedFrame == SeinReplayJournalFormat::EFrameType::Finalize
			? 1ULL : 2ULL)
		: 3ULL; // normal recording always reserves final Progress + Finalize
	if (RequiredCapacity > SeinReplayJournalFormat::MaxFrameCount
		|| NextFrameSequence
			> SeinReplayJournalFormat::MaxFrameCount - RequiredCapacity)
	{
		FailRecording(TEXT("replay frame-count policy is exhausted"));
		return false;
	}
	TArray<uint8> FrameBytes;
	SeinReplayJournalFormat::FFrameHeader Frame;
	FString Error;
	if (!SeinReplayJournalFormat::BuildFrame(
			TypedFrame,
			/*Flags=*/0,
			NextFrameSequence,
			FirstTurn,
			LastTurn,
			TimelineTick,
			PreviousFrameDigest,
			Payload,
			FrameBytes,
			Frame,
			Error))
	{
		FailRecording(FString::Printf(
			TEXT("could not build replay frame: %s"), *Error));
		return false;
	}
	const uint64 FrameByteCount =
		static_cast<uint64>(FrameBytes.Num());
	constexpr uint64 FrontierFrameBytes =
		static_cast<uint64>(SeinReplayJournalFormat::FrameHeaderBytes)
		+ static_cast<uint64>(
			SeinReplayJournalFormat::FrontierPayloadBytes);
	const uint64 RequiredTerminalBytes = bFinalizing
		? (TypedFrame == SeinReplayJournalFormat::EFrameType::Finalize
			? 0ULL : FrontierFrameBytes)
		: FrontierFrameBytes * 2ULL;
	const uint64 MaximumFileBytes = GetMaximumFileBytes();
	if (PersistedBytes > MaximumFileBytes
		|| RequiredTerminalBytes > MaximumFileBytes - PersistedBytes
		|| FrameByteCount
			> MaximumFileBytes - PersistedBytes - RequiredTerminalBytes)
	{
		FailRecording(TEXT("replay file-size policy is exhausted after reserving its terminal frontier"));
		return false;
	}
	if (bAsync)
	{
		switch (TypedFrame)
		{
		case SeinReplayJournalFormat::EFrameType::TurnBatch:
			PendingAppendKind = ESeinReplayAsyncAppendKind::TurnBatch;
			break;
		case SeinReplayJournalFormat::EFrameType::Progress:
			PendingAppendKind = ESeinReplayAsyncAppendKind::Progress;
			break;
		case SeinReplayJournalFormat::EFrameType::Checkpoint:
			PendingAppendKind = ESeinReplayAsyncAppendKind::Checkpoint;
			break;
		default:
			FailRecording(TEXT("replay frame type does not support background append"));
			return false;
		}
		PendingAppendDigest = Frame.CurrentDigest;
		PendingAppendByteCount = FrameByteCount;
		PendingAppendFirstTurn = FirstTurn;
		PendingAppendLastTurn = LastTurn;
		PendingAppendTimelineTick = TimelineTick;
		PendingAppendCheckpointTurnCount = PendingAppendKind
			== ESeinReplayAsyncAppendKind::Checkpoint
			? PersistedTurnCount
			: INDEX_NONE;
		PendingAppendTurnCount = PendingAppendKind
			== ESeinReplayAsyncAppendKind::TurnBatch
			? LastTurn - FirstTurn + 1
			: 0;
		PendingAppendResidentBytes = 0;
		for (int32 Index = 0; Index < PendingAppendTurnCount; ++Index)
		{
			if (!PendingTurns.IsValidIndex(Index))
			{
				PendingAppendKind = ESeinReplayAsyncAppendKind::None;
				FailRecording(TEXT("replay async TurnBatch exceeds the resident queue"));
				return false;
			}
			PendingAppendResidentBytes += static_cast<uint64>(
				PendingTurns[Index].OpaqueCommands.Bytes.Num());
		}

		const FString AppendPath = ActivePartialPath;
		const int64 ExpectedOffset = static_cast<int64>(PersistedBytes);
		const uint64 ScheduledGeneration = RecordingGeneration;
		check(NextAsyncOperationId != MAX_uint64);
		const uint64 ScheduledOperationId = NextAsyncOperationId++;
		PendingAppendOperationId = ScheduledOperationId;
#if WITH_DEV_AUTOMATION_TESTS
		const bool bForceBackgroundFailure =
			bFailNextBackgroundAppendForTests;
		bFailNextBackgroundAppendForTests = false;
		TSharedPtr<FSeinReplayAsyncAppendTestGate, ESPMode::ThreadSafe>
			BackgroundAppendTestGate;
		if (PendingAppendKind == ESeinReplayAsyncAppendKind::Checkpoint
			&& NextCheckpointAppendTestGate)
		{
			BackgroundAppendTestGate = MoveTemp(
				NextCheckpointAppendTestGate);
		}
		else
		{
			BackgroundAppendTestGate = MoveTemp(
				NextBackgroundAppendTestGate);
		}
		ActiveBackgroundAppendTestGate = BackgroundAppendTestGate;
#else
		constexpr bool bForceBackgroundFailure = false;
#endif
		TWeakObjectPtr<USeinReplayWriter> WeakThis(this);
		PendingAppendFuture = Async(
			EAsyncExecution::ThreadPool,
			[AppendPath,
			 ExpectedOffset,
			 FrameBytes = MoveTemp(FrameBytes),
			 WeakThis,
			 ScheduledGeneration,
			 ScheduledOperationId,
#if WITH_DEV_AUTOMATION_TESTS
			 BackgroundAppendTestGate,
#endif
			 bForceBackgroundFailure]() mutable
			{
				FSeinReplayAsyncAppendResult Result;
				if (bForceBackgroundFailure)
				{
					Result.Error = TEXT(
						"synthetic background append failure");
				}
				else
				{
					LLM_SCOPE_BYNAME(TEXT("SeinARTS/Replay/DurableAppend"));
					TRACE_CPUPROFILER_EVENT_SCOPE(
						Sein_Replay_BackgroundDurableAppend);
#if WITH_DEV_AUTOMATION_TESTS
					if (BackgroundAppendTestGate)
					{
						Result.bSucceeded = SeinReplayFileIO::
							AppendAtExpectedOffsetWithMidpointForTests(
								AppendPath,
								ExpectedOffset,
								FrameBytes,
								Result.Error,
								[BackgroundAppendTestGate](FString& GateError)
								{
									BackgroundAppendTestGate
										->WorkerEntered->Trigger();
									constexpr uint32
										TestGateTimeoutMilliseconds = 30000;
									if (!BackgroundAppendTestGate
										->ReleaseWorker->Wait(
											TestGateTimeoutMilliseconds))
									{
										GateError = TEXT(
											"background append test gate timed out");
										return false;
									}
									return true;
								});
					}
					else
#endif
					{
					Result.bSucceeded =
						SeinReplayFileIO::AppendAtExpectedOffset(
							AppendPath,
							ExpectedOffset,
							FrameBytes,
							Result.Error);
					}
				}
				AsyncTask(ENamedThreads::GameThread,
					[WeakThis, ScheduledGeneration, ScheduledOperationId]()
					{
						USeinReplayWriter* Writer = WeakThis.Get();
						if (Writer
							&& Writer->RecordingGeneration
								== ScheduledGeneration
							&& Writer->PendingAppendOperationId
								== ScheduledOperationId)
						{
							if (Writer->PendingAppendFuture.IsValid())
							{
								Writer->PendingAppendFuture.Wait();
							}
							Writer->ScheduleMaintenance();
						}
					});
				return Result;
			});
		return true;
	}

	bool bAppendSucceeded = false;
	{
		LLM_SCOPE_BYNAME(TEXT("SeinARTS/Replay/DurableAppend"));
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_SynchronousDurableAppend);
		bAppendSucceeded = SeinReplayFileIO::AppendAtExpectedOffset(
			ActivePartialPath,
			static_cast<int64>(PersistedBytes),
			FrameBytes,
			Error);
	}
	if (!bAppendSucceeded)
	{
		FailRecording(FString::Printf(
			TEXT("replay append failed: %s"), *Error));
		return false;
	}
	PersistedBytes += FrameByteCount;
	PreviousFrameDigest = Frame.CurrentDigest;
	++NextFrameSequence;
	return true;
}

bool USeinReplayWriter::CaptureCheckpoint(bool bRequired)
{
	return CaptureCheckpointInternal(
		bRequired,
		/*bAllowBackgroundAppend=*/false);
}

bool USeinReplayWriter::HandleCheckpointFailure(
	bool bRequired,
	const FString& Reason)
{
	if (bRequired)
	{
		FailRecording(FString::Printf(
			TEXT("required replay checkpoint failed: %s"), *Reason));
		return false;
	}

	const int32 BaseBackoff = GetProgressIntervalTicks();
	const int32 MaxBackoff = BaseBackoff > MAX_int32 / 64
		? MAX_int32 : BaseBackoff * 64;
	CheckpointRetryBackoffTicks = CheckpointRetryBackoffTicks <= 0
		? BaseBackoff
		: FMath::Min(
			MaxBackoff,
			CheckpointRetryBackoffTicks > MAX_int32 / 2
				? MAX_int32
				: CheckpointRetryBackoffTicks * 2);
	NextCheckpointRetryTick = LastObservedCompletedTick
		> MAX_int32 - CheckpointRetryBackoffTicks
		? MAX_int32
		: LastObservedCompletedTick + CheckpointRetryBackoffTicks;
	UE_LOG(LogSeinNet, Warning,
		TEXT("ReplayWriter: periodic checkpoint skipped; retry in %d tick(s): %s."),
		CheckpointRetryBackoffTicks, *Reason);
	// Recording survives checkpoint failure, but seek/recovery granularity
	// silently degrades to the last successful checkpoint. Escalate chronic
	// failure once so it cannot hide among routine retry warnings.
	if (CheckpointRetryBackoffTicks >= MaxBackoff
		&& !bLoggedChronicCheckpointFailure)
	{
		bLoggedChronicCheckpointFailure = true;
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: periodic checkpoints are chronically failing; this replay records turns but its seek/recovery granularity is frozen at the last successful checkpoint. Last reason: %s"),
			*Reason);
	}
	return false;
}

bool USeinReplayWriter::CaptureCheckpointInternal(
	bool bRequired,
	bool bAllowBackgroundAppend)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_CaptureCheckpoint);

	if (!bRecording)
	{
		return false;
	}
	const bool bInitial = !bHasInitialCheckpoint;
	if (bInitial && !bRequired)
	{
		bRequired = true;
	}
	const bool bBackgroundCheckpoint = bAllowBackgroundAppend
		&& !bRequired && !bInitial;

	UWorld* World = RecordingWorld.Get();
	USeinWorldSubsystem* WorldSub = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	auto RefuseCapture = [this, bRequired](const FString& Reason)
	{
		return HandleCheckpointFailure(bRequired, Reason);
	};
	if (!WorldSub)
	{
		return RefuseCapture(TEXT("no attached simulation world"));
	}

	if (!bInitial)
	{
		RunDeferredMaintenance(/*bForce=*/true);
		if (!bRecording)
		{
			return false;
		}
	}

	FSeinWorldSnapshot Snapshot;
	FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
	{
		LLM_SCOPE_BYNAME(TEXT("SeinARTS/Replay/Checkpoint/Snapshot"));
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_Checkpoint_CaptureSnapshot);
		WorldSub->CaptureSnapshot(Snapshot);
	}
	if (Snapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
	{
		return RefuseCapture(TEXT("Core refused quiescent snapshot capture"));
	}
	if ((bInitial && Snapshot.CurrentTick != 0)
		|| (!bInitial && Snapshot.CurrentTick <= 0))
	{
		return RefuseCapture(bInitial
			? TEXT("mandatory initial checkpoint is not tick zero")
			: TEXT("periodic checkpoint did not advance beyond tick zero"));
	}
	FString Error;
	if (!CanPublishFrontier(Snapshot.CurrentTick, Error))
	{
		return RefuseCapture(FString::Printf(
			TEXT("checkpoint frontier is not durable: %s"), *Error));
	}

	if (bBackgroundCheckpoint)
	{
		if (PendingCheckpointEncodeFuture.IsValid() || HasPendingAppend())
		{
			return RefuseCapture(
				TEXT("ordered checkpoint pipeline is already occupied"));
		}

		PendingCheckpointEncodeWork = MakeShared<
			FSeinReplayCheckpointEncodeWork, ESPMode::ThreadSafe>(this);
		PendingCheckpointEncodeWork->Snapshot = MoveTemp(Snapshot);
		// The writer is the sole owner and never resets it until this future has
		// completed. The worker borrows the stable address so final destruction
		// remains a game-thread operation.
		FSeinReplayCheckpointEncodeWork* const EncodeWork =
			PendingCheckpointEncodeWork.Get();
		const uint64 ScheduledGeneration = RecordingGeneration;
		check(NextAsyncOperationId != MAX_uint64);
		const uint64 ScheduledOperationId = NextAsyncOperationId++;
		PendingCheckpointEncodeGeneration = ScheduledGeneration;
		PendingCheckpointEncodeOperationId = ScheduledOperationId;
		TWeakObjectPtr<USeinReplayWriter> WeakThis(this);
#if WITH_DEV_AUTOMATION_TESTS
		TSharedPtr<
			FSeinReplayAsyncCheckpointEncodeTestGate,
			ESPMode::ThreadSafe> CheckpointEncodeTestGate =
				MoveTemp(NextCheckpointEncodeTestGate);
		ActiveCheckpointEncodeTestGate = CheckpointEncodeTestGate;
#endif
		PendingCheckpointEncodeFuture = Async(
			EAsyncExecution::ThreadPool,
			[EncodeWork,
			 WeakThis,
			 ScheduledGeneration,
			 ScheduledOperationId
#if WITH_DEV_AUTOMATION_TESTS
			 , CheckpointEncodeTestGate
#endif
			]() mutable
			{
				LLM_SCOPE_BYNAME(TEXT("SeinARTS/Replay/Checkpoint/Encode"));
				FSeinReplayAsyncCheckpointEncodeResult Result;
				Result.SnapshotTick = EncodeWork->Snapshot.CurrentTick;
				FSeinSnapshotEnvelopeMetadata Metadata;
#if WITH_DEV_AUTOMATION_TESTS
				if (CheckpointEncodeTestGate)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(
						Sein_Replay_Checkpoint_EncodeEnvelope);
					FGCScopeGuard GCScopeGuard;
					Result.bSucceeded = SeinSnapshotTransfer::
						EncodeCheckpointEnvelopeWithMidpointForTests(
							EncodeWork->Snapshot,
							Result.Envelope,
							Metadata,
							Result.Error,
							[CheckpointEncodeTestGate](FString& GateError)
							{
								CheckpointEncodeTestGate
									->WorkerEntered->Trigger();
								constexpr uint32
									TestGateTimeoutMilliseconds = 30000;
								if (!CheckpointEncodeTestGate
									->ReleaseWorker->Wait(
										TestGateTimeoutMilliseconds))
								{
									GateError = TEXT(
										"checkpoint encode test gate timed out");
									return false;
								}
								return true;
							});
				}
				else
#endif
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(
						Sein_Replay_Checkpoint_EncodeEnvelope);
					// Reflected serialization reads frozen UObject class/path metadata.
					// The work's snapshot guard pins references; this guard prevents GC
					// from mutating object reachability while the worker reads them.
					FGCScopeGuard GCScopeGuard;
					Result.bSucceeded =
						SeinSnapshotTransfer::EncodeCheckpointEnvelope(
							EncodeWork->Snapshot,
							Result.Envelope,
							Metadata,
							Result.Error);
				}
				if (Result.bSucceeded
					&& Metadata.SnapshotTick != Result.SnapshotTick)
				{
					Result.bSucceeded = false;
					Result.Error = TEXT(
						"checkpoint envelope tick disagrees with Core");
					Result.Envelope.Reset();
				}
				AsyncTask(ENamedThreads::GameThread,
					[WeakThis, ScheduledGeneration, ScheduledOperationId]()
					{
						USeinReplayWriter* Writer = WeakThis.Get();
						if (!Writer)
						{
							return;
						}
						if (Writer->RecordingGeneration
							== ScheduledGeneration
							&& Writer->PendingCheckpointEncodeOperationId
								== ScheduledOperationId)
						{
							if (Writer->PendingCheckpointEncodeFuture.IsValid())
							{
								Writer->PendingCheckpointEncodeFuture.Wait();
							}
							Writer->ScheduleMaintenance();
						}
						else
						{
							Writer->DiscardCompletedCheckpointEncode(
								ScheduledGeneration,
								ScheduledOperationId);
						}
					});
				return Result;
			});
		UE_LOG(LogSeinNet, Verbose,
			TEXT("ReplayWriter: encoding periodic checkpoint at tick %d in the background."),
			PendingCheckpointEncodeWork->Snapshot.CurrentTick);
		return true;
	}

	TArray<uint8> Envelope;
	FSeinSnapshotEnvelopeMetadata Metadata;
	bool bEnvelopeEncoded = false;
	{
		LLM_SCOPE_BYNAME(TEXT("SeinARTS/Replay/Checkpoint/Encode"));
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Replay_Checkpoint_EncodeEnvelope);
		bEnvelopeEncoded = SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Snapshot, Envelope, Metadata, Error);
	}
	if (!bEnvelopeEncoded)
	{
		return RefuseCapture(FString::Printf(
			TEXT("checkpoint envelope encode failed: %s"), *Error));
	}
	if (Metadata.SnapshotTick != Snapshot.CurrentTick)
	{
		return RefuseCapture(TEXT("checkpoint envelope tick disagrees with Core"));
	}
	if (!AppendJournalFrame(
			static_cast<uint8>(
				SeinReplayJournalFormat::EFrameType::Checkpoint),
			INDEX_NONE,
			INDEX_NONE,
			Snapshot.CurrentTick,
			Envelope,
			/*bAsync=*/false))
	{
		return false;
	}

	bHasInitialCheckpoint = true;
	LastCheckpointPersistedTurnCount = PersistedTurnCount;
	++PersistedCheckpointCount;
	NextCheckpointRetryTick = 0;
	CheckpointRetryBackoffTicks = 0;
	UE_LOG(LogSeinNet, Verbose,
		TEXT("ReplayWriter: appended checkpoint at tick %d (%d bytes)."),
		Snapshot.CurrentTick,
		Envelope.Num());
	return true;
}

bool USeinReplayWriter::IsPeriodicCheckpointDue() const
{
	if (!bRecording || !bHasInitialCheckpoint
		|| PendingCheckpointEncodeFuture.IsValid()
		|| HasPendingAppend()
		|| LastObservedCompletedTick < NextCheckpointRetryTick)
	{
		return false;
	}
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 Interval = FMath::Clamp(
		Settings ? Settings->ReplayCheckpointIntervalTurns : 3000,
		1,
		1000000);
	return PersistedTurnCount - LastCheckpointPersistedTurnCount >= Interval;
}

FString USeinReplayWriter::FinishRecording()
{
	if (!bRecording)
	{
		if (bTickObservationFailed)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayWriter::FinishRecording called but recording was aborted by a completed-tick observation gap; no-op."));
		}
		else if (bJournalObservationFailed)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayWriter::FinishRecording called but recording was aborted by an invalid or oversized journal; no-op."));
		}
		else
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("ReplayWriter::FinishRecording called while not recording; no-op."));
		}
		return FString();
	}

	FString Error;
	if (!bHasInitialCheckpoint
		&& !CaptureCheckpoint(/*bRequired=*/true))
	{
		return FString();
	}
	if (!bRecording)
	{
		return FString();
	}
	if (!ResolvePendingCheckpointEncode(
			/*bWait=*/true,
			/*bAppendSynchronously=*/true)
		|| !bRecording)
	{
		return FString();
	}
	// Finalization reserves exactly two frames below. Flush the applied tail
	// without first spending a third slot on ordinary periodic Progress.
	if (!FlushEligibleTurns(/*bForce=*/true) || !bRecording)
	{
		return FString();
	}
	if (!CanPublishFrontier(LastObservedCompletedTick, Error))
	{
		FailRecording(FString::Printf(
			TEXT("refusing incomplete final replay frontier: %s"), *Error));
		return FString();
	}

	if (const UWorld* World = RecordingWorld.Get())
	{
		if (const USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			if (!SeinReplayCompatibility::ValidateCurrent(
					RecordingHeader, World, Error)
				|| !ValidateHeaderAgainstWorld(
					RecordingHeader,
					*WorldSub,
					/*bRequireCurrentStartTick=*/false,
					Error))
			{
				FailRecording(FString::Printf(
					TEXT("runtime compatibility changed during recording: %s"),
					*Error));
				return FString();
			}
		}
	}

	bFinalizing = true;
	if (!AppendProgress(
			LastObservedCompletedTick,
			/*bForceDuplicate=*/true)
		|| !AppendFrontierFrame(
			static_cast<uint8>(
				SeinReplayJournalFormat::EFrameType::Finalize),
			LastObservedCompletedTick))
	{
		return FString();
	}

	const FString PublishedPath = FinalFilePath;
	if (!SeinReplayFileIO::PublishExistingAtomically(
			ActivePartialPath, PublishedPath, Error))
	{
		FailRecording(FString::Printf(
			TEXT("could not publish completed replay: %s"), *Error));
		return FString();
	}

	LastPublishedPath = PublishedPath;
	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayWriter: published v9 replay with %d applied turn(s) through tick %d (%llu bytes) -> %s"),
		PersistedTurnCount,
		LastObservedCompletedTick,
		static_cast<unsigned long long>(PersistedBytes),
		*PublishedPath);
	bRecording = false;
	bTickObservationFailed = false;
	bJournalObservationFailed = false;
	bMaintenanceScheduled = false;
	bFinalizing = false;
	++RecordingGeneration;
	ActivePartialPath.Reset();
	FinalFilePath.Reset();
	RecordingWorld.Reset();
	ReleaseResidentTurns();
	return PublishedPath;
}

void USeinReplayWriter::FailRecording(
	const FString& Reason,
	bool bTickFailure)
{
#if WITH_DEV_AUTOMATION_TESTS
	ReleaseHeldCheckpointEncodeForTests();
	ReleaseHeldBackgroundAppendForTests();
#endif
	WaitAndDiscardPendingCheckpointEncode();
	WaitAndDiscardPendingAppend();
	if (bTickFailure)
	{
		bTickObservationFailed = true;
	}
	else
	{
		bJournalObservationFailed = true;
	}
	bRecording = false;
	bMaintenanceScheduled = false;
	bFinalizing = false;
	++RecordingGeneration;
	ReleaseResidentTurns();
	UE_LOG(LogSeinNet, Error,
		TEXT("ReplayWriter: %s; recording stopped and the valid partial journal was preserved at %s."),
		*Reason,
		ActivePartialPath.IsEmpty() ? TEXT("<none>") : *ActivePartialPath);
}

void USeinReplayWriter::ReleaseResidentTurns()
{
	PendingTurns.Reset();
	ResidentBytes = 0;
}

uint64 USeinReplayWriter::GetMaximumFileBytes() const
{
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const uint64 MiB = static_cast<uint64>(FMath::Clamp(
		Settings ? Settings->ReplayMaxFileSizeMiB : 16384,
		64,
		65536));
	return FMath::Min(
		MiB * 1024ULL * 1024ULL,
		SeinReplayJournalFormat::MaxFileBytes);
}

int32 USeinReplayWriter::GetMaximumResidentTurns() const
{
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 TurnsPerSecond = Settings && Settings->TurnRate > 0
		? Settings->TurnRate
		: 10;
	return FMath::Max(
		8,
		GetInputDelayTurnsFromSettings() + TurnsPerSecond + 4);
}

uint64 USeinReplayWriter::GetMaximumResidentBytes() const
{
	return static_cast<uint64>(GetMaximumResidentTurns())
		* static_cast<uint64>(FSeinOpaqueCommandBatch::MaxBytes);
}
