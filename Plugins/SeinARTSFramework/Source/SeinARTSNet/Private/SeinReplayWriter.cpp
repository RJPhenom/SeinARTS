/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayWriter.cpp
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
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

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
	NextCheckpointRetryTick = 0;
	CheckpointRetryBackoffTicks = 0;
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
			Writer->bMaintenanceScheduled = false;
			if (!Writer->bRecording)
			{
				return;
			}
			Writer->RunDeferredMaintenance(/*bForce=*/false);
			if (Writer->bRecording
				&& Writer->IsPeriodicCheckpointDue())
			{
				Writer->CaptureCheckpoint(/*bRequired=*/false);
			}
		});
}

void USeinReplayWriter::RunDeferredMaintenance(bool bForce)
{
	if (!bRecording || bMaintenanceRunning || !bHasInitialCheckpoint)
	{
		return;
	}
	bMaintenanceRunning = true;
	if (!FlushEligibleTurns(bForce))
	{
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
				/*bForceDuplicate=*/bForce);
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

#if WITH_DEV_AUTOMATION_TESTS
void USeinReplayWriter::FlushAppliedProgressForTests()
{
	RunDeferredMaintenance(/*bForce=*/true);
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
				Payload))
		{
			if (bRecording)
			{
				FailRecording(FString::Printf(
					TEXT("could not append a bounded TurnBatch: %s"),
					*Error));
			}
			return false;
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
	bool bForceDuplicate)
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
		EndTick))
	{
		return false;
	}
	LastProgressTick = EndTick;
	return true;
}

bool USeinReplayWriter::AppendFrontierFrame(
	uint8 FrameType,
	int32 EndTick)
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
		Payload);
}

bool USeinReplayWriter::AppendJournalFrame(
	uint8 FrameType,
	int32 FirstTurn,
	int32 LastTurn,
	int32 TimelineTick,
	TConstArrayView<uint8> Payload)
{
	if (!bRecording)
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
	if (!SeinReplayFileIO::AppendAtExpectedOffset(
			ActivePartialPath,
			static_cast<int64>(PersistedBytes),
			FrameBytes,
			Error))
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
	if (!bRecording)
	{
		return false;
	}
	const bool bInitial = !bHasInitialCheckpoint;
	if (bInitial && !bRequired)
	{
		bRequired = true;
	}

	UWorld* World = RecordingWorld.Get();
	USeinWorldSubsystem* WorldSub = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	auto RefuseCapture = [this, bRequired](const FString& Reason)
	{
		if (bRequired)
		{
			FailRecording(FString::Printf(
				TEXT("required replay checkpoint failed: %s"), *Reason));
		}
		else
		{
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
				: LastObservedCompletedTick
					+ CheckpointRetryBackoffTicks;
			UE_LOG(LogSeinNet, Warning,
				TEXT("ReplayWriter: periodic checkpoint skipped; retry in %d tick(s): %s."),
				CheckpointRetryBackoffTicks, *Reason);
		}
		return false;
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
	WorldSub->CaptureSnapshot(Snapshot);
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

	TArray<uint8> Envelope;
	FSeinSnapshotEnvelopeMetadata Metadata;
	if (!SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Snapshot, Envelope, Metadata, Error))
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
			Envelope))
	{
		return false;
	}

	bHasInitialCheckpoint = true;
	LastCheckpointPersistedTurnCount = PersistedTurnCount;
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
