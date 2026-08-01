/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.cpp
 */

#include "SeinReplayReader.h"
#include "SeinARTSNet.h"
#include "SeinReplayFormat.h"
#include "SeinReplayJournalFormat.h"
#include "SeinReplayFileIO.h"
#include "SeinReplayWireCodec.h"
#include "SeinNetCommandWireCodec.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
	const FName GSeinReplayBootstrapAuthorityID(
		TEXT("SeinARTS.Replay.Playback"));
	const FName GSeinReplayCheckpointAuthorityID(
		TEXT("SeinARTS.Replay.CheckpointPlayback"));
	constexpr uint64 MaxJournalIndexBytes = 128ULL * 1024ULL * 1024ULL;

	FString ResolveReplayPath(const FString& Input)
	{
		if (FPaths::FileExists(Input)) return Input;

		// Bare filename: resolve against Saved/Replays/.
		const FString Resolved = FPaths::ProjectSavedDir() / TEXT("Replays") / Input;
		if (FPaths::FileExists(Resolved)) return Resolved;

		// Try with .seinreplay extension if the user omitted it.
		if (!Input.EndsWith(TEXT(".seinreplay")))
		{
			const FString WithExt = FPaths::ProjectSavedDir() / TEXT("Replays") / (Input + TEXT(".seinreplay"));
			if (FPaths::FileExists(WithExt)) return WithExt;
		}

		return Input; // return unresolved; caller will FFileHelper::Load fail visibly
	}

	bool IsV9Magic(TConstArrayView<uint8> Bytes)
	{
		return Bytes.Num() == UE_ARRAY_COUNT(SeinReplayJournalFormat::Magic)
			&& FMemory::Memcmp(
				Bytes.GetData(),
				SeinReplayJournalFormat::Magic,
				UE_ARRAY_COUNT(SeinReplayJournalFormat::Magic)) == 0;
	}

	bool IsTrustedLocalJournalPath(const FString& InputPath)
	{
		FString ReplayRoot = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Replays"));
		FString Candidate = FPaths::ConvertRelativePathToFull(InputPath);
		FPaths::NormalizeDirectoryName(ReplayRoot);
		FPaths::NormalizeFilename(Candidate);
		FPaths::CollapseRelativeDirectories(ReplayRoot);
		FPaths::CollapseRelativeDirectories(Candidate);
		return FPaths::IsUnderDirectory(Candidate, ReplayRoot)
			&& (Candidate.EndsWith(
					TEXT(".seinreplay"), ESearchCase::IgnoreCase)
				|| Candidate.EndsWith(
					TEXT(".seinreplay.partial"), ESearchCase::IgnoreCase));
	}

	bool IsPartialJournalPath(const FString& Path)
	{
		return Path.EndsWith(
			TEXT(".seinreplay.partial"), ESearchCase::IgnoreCase);
	}

	int32 GetTicksPerTurnFromSettings()
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings || Settings->TurnRate <= 0) return 1;
		return FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate);
	}

	int32 GetInputDelayTurnsFromSettings()
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		return (Settings && Settings->InputDelayTurns > 0)
			? Settings->InputDelayTurns
			: 3;
	}

	bool ComputeMatchSettingsDigest(
		FSeinMatchSettings& Settings,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinDeterministicValueDigestError Error;
		if (SeinCanonicalizeAndDigestMatchSettings(Settings, OutDigest, &Error)
			&& OutDigest.IsValid())
		{
			return true;
		}
		OutError = FString::Printf(
			TEXT("settings digest failed (field=%s error=%s)"),
			*Error.FieldPath, *Error.Message);
		return false;
	}
}

void USeinReplayReader::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

bool USeinReplayReader::LoadFromFile(const FString& Path)
{
	if (bPlaying || SimTickHandle.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: refused to replace the loaded replay while playback is active; call Stop first."));
		return false;
	}

	const FString Resolved = ResolveReplayPath(Path);
	int64 FileSize = 0;
	FString FileError;
	if (!SeinReplayFileIO::QueryBoundedSize(
			Resolved,
			UE_ARRAY_COUNT(SeinReplayJournalFormat::Magic),
			static_cast<int64>(SeinReplayJournalFormat::MaxFileBytes),
			FileSize,
			FileError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected file size or open: %s (%s)"),
			*Resolved, *FileError);
		return false;
	}
	TArray<uint8> MagicBytes;
	if (!SeinReplayFileIO::ReadRange(
			Resolved,
			0,
			UE_ARRAY_COUNT(SeinReplayJournalFormat::Magic),
			static_cast<int64>(SeinReplayJournalFormat::MaxFileBytes),
			MagicBytes,
			FileError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: could not inspect replay magic: %s (%s)"),
			*Resolved, *FileError);
		return false;
	}
	if (IsV9Magic(MagicBytes))
	{
		// Checkpoint payloads use the trusted-local snapshot decoder, whose
		// object/name archive may load referenced packages when resolution misses.
		// Keep that package-loading trust boundary inside Saved/Replays explicitly.
		if (!IsTrustedLocalJournalPath(Resolved))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayReader: v9 checkpoint journals are trusted-local artifacts and must reside under Saved/Replays: %s"),
				*Resolved);
			return false;
		}
		return LoadV9FromResolvedPath(Resolved, FileSize);
	}

	// Decode into a candidate so a failed replacement leaves the previously
	// validated replay available. The live cursor changes only on full success.
	FSeinReplay Candidate;

	const int64 MaxFileBytes =
		SeinReplayFormat::PrefixBytes
		+ static_cast<int64>(SeinReplayFormat::MaxBodyBytes);
	TArray<uint8> Bytes;
	if (!SeinReplayFileIO::ReadBounded(
			Resolved,
			SeinReplayFormat::PrefixBytes,
			MaxFileBytes,
			Bytes,
			FileError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected file size or read before allocation: %s (%s)"),
			*Resolved, *FileError);
		return false;
	}

	SeinReplayFormat::FPrefix Prefix;
	FString FormatError;
	if (!SeinReplayFormat::ParsePrefix(Bytes, Prefix, FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected %s before body deserialization: %s"),
			*Resolved, *FormatError);
		return false;
	}

	// Compatibility gates execute before the bounded body is touched.
	const USeinWorldSubsystem* WorldSub = GetWorldSubsystem();
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: cannot validate %s without a world simulation subsystem."),
			*Resolved);
		return false;
	}
	if (Prefix.CommandProtocolDigest != WorldSub->GetCommandProtocolDigest())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected %s before body deserialization: command-protocol digest mismatch."),
			*Resolved);
		return false;
	}
	if (!WorldSub->IsSimulationContentReady()
		|| Prefix.BootstrapReceipt.SimulationContentDigest
			!= WorldSub->GetSimulationContentDigest())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected %s before body deserialization: simulation-content digest mismatch."),
			*Resolved);
		return false;
	}
	if (Prefix.ConfigFingerprint != WorldSub->GetConfigFingerprint())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected %s before body deserialization: config fingerprint mismatch (file=0x%08x local=0x%08x)."),
			*Resolved,
			static_cast<uint32>(Prefix.ConfigFingerprint),
			static_cast<uint32>(WorldSub->GetConfigFingerprint()));
		return false;
	}

	const TArrayView<const uint8> BodyView(
		Bytes.GetData() + SeinReplayFormat::PrefixBytes,
		static_cast<int32>(Prefix.BodyBytes));
	if (!FSeinReplayWireCodec::Decode(
		BodyView,
		{
			WorldSub->GetCommandAdditionalDynamicPayloadStructs(),
			WorldSub->GetCommandAdditionalWireNames()
		},
		[WorldSub](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return WorldSub->FindCommandSchema(Type, Version, Out);
		},
		Candidate,
		FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: bounded-body decode rejected %s: %s"),
			*Resolved, *FormatError);
		return false;
	}
	if (Candidate.Header.CommandProtocolDigest != Prefix.CommandProtocolDigest
		|| Candidate.Header.MatchSettingsDigest != Prefix.MatchSettingsDigest
		|| Candidate.Header.BootstrapReceipt != Prefix.BootstrapReceipt
		|| Candidate.Header.ConfigFingerprint != Prefix.ConfigFingerprint)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: bounded header disagrees with the validated v8 prefix in %s."),
			*Resolved);
		return false;
	}
	if (!SeinReplayCompatibility::ValidateCurrent(
		Candidate.Header, GetWorld(), FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: incompatible replay %s: %s."),
			*Resolved, *FormatError);
		return false;
	}
	if (Candidate.Header.MapIdentifier.IsEmpty())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: replay %s does not declare a map identity."), *Resolved);
		return false;
	}
	FGuid SnapshotDigest;
	if (!ComputeMatchSettingsDigest(
			Candidate.Header.SettingsSnapshot, SnapshotDigest, FormatError)
		|| SnapshotDigest != Prefix.MatchSettingsDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: settings snapshot does not match the prefix digest in %s (%s)."),
			*Resolved, *FormatError);
		return false;
	}
	FGameplayTag SettingsRejectionReason;
	if (!WorldSub->ValidateMatchSettings(
		Candidate.Header.SettingsSnapshot, SettingsRejectionReason))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: settings snapshot fails the current match contract in %s (%s)."),
			*Resolved, *SettingsRejectionReason.ToString());
		return false;
	}
	if (!SeinReplayCompatibility::ValidatePlayerManifest(
		Candidate.Header, FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: player metadata fails the current replay contract in %s (%s)."),
			*Resolved, *FormatError);
		return false;
	}
	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();
	const int32 EarliestRecordableTurn = GetInputDelayTurnsFromSettings();
	if (!SeinReplayFormat::ValidateJournal(
			Candidate.Header,
			Candidate.Turns,
			TicksPerTurn,
			EarliestRecordableTurn,
			FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: invalid exact turn journal in %s: %s."),
			*Resolved, *FormatError);
		return false;
	}
	for (const FSeinReplayTurnRecord& Turn : Candidate.Turns)
	{
		for (const FSeinCommand& Command : Turn.Commands)
		{
			FSeinCommandSchemaDescriptor Schema;
			if (WorldSub->ValidateCommandStructure(Command, &Schema)
				!= ESeinCommandStructureResult::Valid
				|| !SeinReplayFormat::ValidateIssuerForSchema(
					Command.IssuerKind, Schema.AuthorityScope, FormatError))
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("ReplayReader: command schema/provenance validation failed in %s at turn=%d: %s."),
					*Resolved, Turn.TurnId, *FormatError);
				return false;
			}
			if (Command.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest
				|| (Schema.AllowedExecutionContexts
					& static_cast<int32>(
						ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0)
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("ReplayReader: replay %s contains unsupported pause-control at turn=%d; v8 has no frozen-time control journal."),
					*Resolved, Turn.TurnId);
				return false;
			}
		}
	}

	ResetJournalLoadedState();
	Loaded = MoveTemp(Candidate);
	NextTurnIndex = 0;
	bLoaded = true;

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader: loaded %s — turns=%d  ticks=0..%d  seed=%lld  map=%s  framework=%s  game=%s  recorded=%s"),
		*Resolved, Loaded.Turns.Num(), Loaded.Header.EndTick,
		Loaded.Header.RandomSeed,
		*Loaded.Header.MapIdentifier,
		*Loaded.Header.FrameworkVersion, *Loaded.Header.GameVersion,
		*Loaded.Header.RecordedAt.ToString());

	return true;
}

void USeinReplayReader::ResetJournalLoadedState()
{
	bLoadedV9 = false;
	bLoadedJournalPartial = false;
	bOwnsJournalTurnGate = false;
	bJournalCatchUpActive = false;
	bJournalFailureScheduled = false;
	LoadedJournalPath.Reset();
	LoadedJournalFileSize = 0;
	LoadedJournalTurnCount = 0;
	LoadedJournalEarliestTurn = 0;
	NextJournalTurnOrdinal = 0;
	NextJournalFrameIndex = 0;
	ResidentJournalFrameIndex = INDEX_NONE;
	ResidentJournalRecordIndex = 0;
	JournalSeekTargetTick = 0;
	PendingJournalFailureReason.Reset();
	LoadedJournalTurnFrames.Reset();
	LoadedJournalCheckpoints.Reset();
	LoadedJournalDurableFrame = FIndexedJournalFrame();
	LoadedJournalBootstrapCheckpoint = FSeinSnapshotBootstrapCheckpoint();
	ResidentJournalTurns.Reset();
}

bool USeinReplayReader::ValidateDecodedTurn(
	const FSeinReplayHeader& Header,
	const FSeinReplayTurnRecord& Turn,
	USeinWorldSubsystem* WorldSub,
	FString& OutError) const
{
	check(WorldSub);
	if (!SeinReplayFormat::ValidateTurnEnvelope(
			Header,
			Turn,
			GetTicksPerTurnFromSettings(),
			GetInputDelayTurnsFromSettings(),
			OutError))
	{
		return false;
	}
	for (const FSeinCommand& Command : Turn.Commands)
	{
		FSeinCommandSchemaDescriptor Schema;
		if (WorldSub->ValidateCommandStructure(Command, &Schema)
				!= ESeinCommandStructureResult::Valid
			|| !SeinReplayFormat::ValidateIssuerForSchema(
				Command.IssuerKind, Schema.AuthorityScope, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("command structure or provenance is invalid");
			}
			return false;
		}
		if (Command.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest
			|| (Schema.AllowedExecutionContexts
				& static_cast<int32>(
					ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0)
		{
			OutError = TEXT("replay contains unsupported frozen-time pause control");
			return false;
		}
	}
	return true;
}

bool USeinReplayReader::LoadV9FromResolvedPath(
	const FString& ResolvedPath,
	int64 FileSize)
{
	using namespace SeinReplayJournalFormat;

	auto Reject = [&ResolvedPath](const FString& Reason)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader: rejected v9 journal %s: %s"),
			*ResolvedPath, *Reason);
		return false;
	};
	if (FileSize < PrefixBytes
		|| static_cast<uint64>(FileSize) > MaxFileBytes)
	{
		return Reject(TEXT("file size is outside the v9 journal bounds"));
	}

	USeinWorldSubsystem* WorldSub = GetWorldSubsystem();
	if (!WorldSub)
	{
		return Reject(TEXT("no world simulation subsystem is available"));
	}

	TArray<uint8> PrefixBytesBuffer;
	FString Error;
	if (!SeinReplayFileIO::ReadRange(
			ResolvedPath,
			0,
			PrefixBytes,
			static_cast<int64>(MaxFileBytes),
			PrefixBytesBuffer,
			Error))
	{
		return Reject(Error);
	}
	FPrefix Prefix;
	if (!ParsePrefix(PrefixBytesBuffer, Prefix, Error))
	{
		return Reject(Error);
	}

	// These compatibility identities are available without touching a frame
	// payload. In particular, no trusted-local checkpoint decoder runs until
	// the current executable has admitted this prefix.
	if (Prefix.CommandProtocolDigest != WorldSub->GetCommandProtocolDigest())
	{
		return Reject(TEXT("command-protocol digest mismatch before frame decode"));
	}
	if (!WorldSub->IsSimulationContentReady()
		|| Prefix.BootstrapReceipt.SimulationContentDigest
			!= WorldSub->GetSimulationContentDigest())
	{
		return Reject(TEXT("simulation-content digest mismatch before frame decode"));
	}
	if (Prefix.ConfigFingerprint != WorldSub->GetConfigFingerprint())
	{
		return Reject(TEXT("config fingerprint mismatch before frame decode"));
	}

	FSeinReplay Candidate;
	TArray<FIndexedJournalFrame> CandidateTurnFrames;
	TArray<FIndexedJournalFrame> CandidateCheckpoints;
	FIndexedJournalFrame CandidateDurableFrame;
	TOptional<FSeinSnapshotBootstrapCheckpoint> CandidateBootstrapCheckpoint;
	int32 ScannedTurnCount = 0;
	int32 DurableTurnCount = 0;
	int32 DurableEndTick = INDEX_NONE;
	int32 PreviousTurn = INDEX_NONE;
	int32 LastFrameTimelineTick = 0;
	uint64 ExpectedSequence = 0;
	FGuid ExpectedPreviousDigest = Prefix.PrefixDigest;
	bool bSawHeader = false;
	bool bSawInitialCheckpoint = false;
	bool bSawFinalize = false;
	const bool bPartial = IsPartialJournalPath(ResolvedPath);
	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();
	const int32 EarliestTurn = GetInputDelayTurnsFromSettings();

	auto ValidateCoverage = [
		&Candidate,
		TicksPerTurn,
		EarliestTurn](
			int32 EndTick,
			int32 AvailableTurnCount,
			const FFrontier* Frontier,
			FString& OutError)
	{
		FSeinReplayHeader CoverageHeader = Candidate.Header;
		CoverageHeader.EndTick = EndTick;
		int32 FirstRequired = INDEX_NONE;
		int32 LastRequired = INDEX_NONE;
		if (!SeinReplayFormat::GetRequiredTurnRange(
				CoverageHeader,
				TicksPerTurn,
				EarliestTurn,
				FirstRequired,
				LastRequired,
				OutError))
		{
			return false;
		}
		const int64 RequiredCount = FirstRequired == INDEX_NONE
			? 0
			: static_cast<int64>(LastRequired) - FirstRequired + 1;
		if (RequiredCount != AvailableTurnCount)
		{
			OutError = FString::Printf(
				TEXT("frontier tick %d requires %lld contiguous turns but %d precede its frame"),
				EndTick,
				static_cast<long long>(RequiredCount),
				AvailableTurnCount);
			return false;
		}
		if (Frontier
			&& (Frontier->FirstAppliedTurn != FirstRequired
				|| Frontier->LastAppliedTurn != LastRequired
				|| Frontier->AppliedTurnCount
					!= static_cast<uint32>(RequiredCount)))
		{
			OutError = TEXT("frontier summary disagrees with its exact executable turn range");
			return false;
		}
		return true;
	};

	auto ValidateCheckpointSnapshot = [
		&Candidate,
		&Prefix,
		&CandidateBootstrapCheckpoint](
			const FFrameHeader& Frame,
			TConstArrayView<uint8> Payload,
			FString& OutError)
	{
		FSeinWorldSnapshot Snapshot;
		FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
		FSeinSnapshotEnvelopeMetadata Metadata;
		if (!SeinSnapshotTransfer::DecodeCheckpointEnvelope(
				Payload, Snapshot, Metadata, OutError))
		{
			return false;
		}
		if (Frame.FirstTurn != INDEX_NONE || Frame.LastTurn != INDEX_NONE
			|| Snapshot.CurrentTick != Frame.TimelineTick
			|| Metadata.SnapshotTick != Frame.TimelineTick
			|| Snapshot.CommandProtocolDigest != Prefix.CommandProtocolDigest
			|| Snapshot.SimulationContentDigest
				!= Prefix.BootstrapReceipt.SimulationContentDigest
			|| Snapshot.MatchSettingsDigest != Prefix.MatchSettingsDigest
			|| Snapshot.ConfigFingerprint != Prefix.ConfigFingerprint
			|| Snapshot.BootstrapCheckpoint.Receipt != Prefix.BootstrapReceipt
			|| !Snapshot.BootstrapCheckpoint.IsValidConsumedCheckpoint()
			|| Snapshot.SessionSeed != Candidate.Header.RandomSeed
			|| Snapshot.FrameworkVersion != Candidate.Header.FrameworkVersion
			|| Snapshot.GameVersion != Candidate.Header.GameVersion
			|| Snapshot.MapIdentifier.ToString()
				!= Candidate.Header.MapIdentifier
			|| Snapshot.bSimPaused || Snapshot.bSimPausedHard
			|| !Snapshot.PendingStandalonePauseControlCommands.IsEmpty())
		{
			OutError = TEXT("checkpoint snapshot metadata does not cross-bind to the journal header/prefix");
			return false;
		}
		if (CandidateBootstrapCheckpoint.IsSet()
			&& !FSeinSnapshotBootstrapCheckpoint::StaticStruct()
				->CompareScriptStruct(
					&CandidateBootstrapCheckpoint.GetValue(),
					&Snapshot.BootstrapCheckpoint,
					PPF_None))
		{
			OutError = TEXT("checkpoint changes the mandatory tick-zero bootstrap identity");
			return false;
		}
		if (!CandidateBootstrapCheckpoint.IsSet())
		{
			CandidateBootstrapCheckpoint = Snapshot.BootstrapCheckpoint;
		}
		return true;
	};

	int64 Offset = PrefixBytes;
	while (Offset < FileSize)
	{
		if (ExpectedSequence >= MaxFrameCount)
		{
			return Reject(TEXT("journal frame count exceeds the bounded scan/index budget"));
		}
		if (bSawFinalize)
		{
			return Reject(TEXT("bytes or frames follow the terminal Finalize frame"));
		}
		const int64 Remaining = FileSize - Offset;
		if (Remaining < FrameHeaderBytes)
		{
			if (bPartial)
			{
				break; // sole tolerated case: torn final frame header
			}
			return Reject(TEXT("final frame header is truncated"));
		}

		TArray<uint8> HeaderBytes;
		if (!SeinReplayFileIO::ReadRange(
				ResolvedPath,
				Offset,
				FrameHeaderBytes,
				static_cast<int64>(MaxFileBytes),
				HeaderBytes,
				Error))
		{
			return Reject(Error);
		}
		FFrameHeader Frame;
		if (!ParseFrameHeader(HeaderBytes, Frame, Error))
		{
			return Reject(Error); // a complete but invalid header is never recovery
		}
		if (Frame.Sequence != ExpectedSequence
			|| Frame.PreviousDigest != ExpectedPreviousDigest)
		{
			return Reject(TEXT("frame sequence or predecessor digest breaks the journal chain"));
		}
		const int64 PayloadOffset = Offset + FrameHeaderBytes;
		if (static_cast<int64>(Frame.PayloadBytes) > FileSize - PayloadOffset)
		{
			if (bPartial)
			{
				break; // sole tolerated case: torn final frame payload
			}
			return Reject(TEXT("final frame payload is truncated"));
		}

		TArray<uint8> Payload;
		if (!SeinReplayFileIO::ReadRange(
				ResolvedPath,
				PayloadOffset,
				Frame.PayloadBytes,
				static_cast<int64>(MaxFileBytes),
				Payload,
				Error))
		{
			return Reject(Error);
		}
		if (!ValidateFrame(Frame, Payload, Error))
		{
			return Reject(Error);
		}
		if (ExpectedSequence > 0
			&& Frame.TimelineTick < LastFrameTimelineTick)
		{
			return Reject(TEXT("frame timeline ticks move backwards"));
		}

		FIndexedJournalFrame Descriptor;
		Descriptor.FileOffset = Offset;
		Descriptor.Sequence = Frame.Sequence;
		Descriptor.Type = static_cast<uint8>(Frame.Type);
		Descriptor.Flags = Frame.Flags;
		Descriptor.FirstTurn = Frame.FirstTurn;
		Descriptor.LastTurn = Frame.LastTurn;
		Descriptor.TimelineTick = Frame.TimelineTick;
		Descriptor.PayloadBytes = Frame.PayloadBytes;
		Descriptor.PreviousDigest = Frame.PreviousDigest;
		Descriptor.CurrentDigest = Frame.CurrentDigest;
		Descriptor.FirstRecordOrdinal = ScannedTurnCount;

		switch (Frame.Type)
		{
		case EFrameType::Header:
		{
			if (bSawHeader || ExpectedSequence != 0)
			{
				return Reject(TEXT("Header must be the first and only Header frame"));
			}
			FSeinReplay HeaderRecord;
			if (!FSeinReplayWireCodec::Decode(
					Payload,
					{
						WorldSub->GetCommandAdditionalDynamicPayloadStructs(),
						WorldSub->GetCommandAdditionalWireNames()
					},
					[WorldSub](FGameplayTag Type, int32 Version,
						FSeinCommandSchemaDescriptor& Out)
					{
						return WorldSub->FindCommandSchema(Type, Version, Out);
					},
					HeaderRecord,
					Error))
			{
				return Reject(FString::Printf(
					TEXT("Header payload decode failed: %s"), *Error));
			}
			if (!HeaderRecord.Turns.IsEmpty()
				|| HeaderRecord.Header.StartTick != 0
				|| HeaderRecord.Header.EndTick != 0
				|| HeaderRecord.Header.CommandProtocolDigest
					!= Prefix.CommandProtocolDigest
				|| HeaderRecord.Header.MatchSettingsDigest
					!= Prefix.MatchSettingsDigest
				|| HeaderRecord.Header.BootstrapReceipt
					!= Prefix.BootstrapReceipt
				|| HeaderRecord.Header.ConfigFingerprint
					!= Prefix.ConfigFingerprint)
			{
				return Reject(TEXT("Header payload disagrees with the v9 prefix or is not a tick-zero empty journal header"));
			}
			Candidate.Header = MoveTemp(HeaderRecord.Header);
			if (!SeinReplayCompatibility::ValidateCurrent(
					Candidate.Header, GetWorld(), Error)
				|| Candidate.Header.MapIdentifier.IsEmpty())
			{
				return Reject(Error.IsEmpty()
					? TEXT("Header has no map identity") : Error);
			}
			FGuid SnapshotDigest;
			if (!ComputeMatchSettingsDigest(
					Candidate.Header.SettingsSnapshot,
					SnapshotDigest,
					Error)
				|| SnapshotDigest != Prefix.MatchSettingsDigest)
			{
				return Reject(TEXT("Header settings snapshot digest mismatch"));
			}
			FGameplayTag SettingsRejectionReason;
			if (!WorldSub->ValidateMatchSettings(
					Candidate.Header.SettingsSnapshot,
					SettingsRejectionReason))
			{
				return Reject(FString::Printf(
					TEXT("Header settings fail the current match contract (%s)"),
					*SettingsRejectionReason.ToString()));
			}
			if (!SeinReplayCompatibility::ValidatePlayerManifest(
					Candidate.Header, Error))
			{
				return Reject(Error);
			}
			bSawHeader = true;
			break;
		}
		case EFrameType::Checkpoint:
		{
			if (!bSawHeader || (ExpectedSequence == 1
				&& Frame.TimelineTick != 0)
				|| (ExpectedSequence > 1 && !bSawInitialCheckpoint))
			{
				return Reject(TEXT("the second frame must be the mandatory tick-zero Checkpoint"));
			}
			if (!ValidateCheckpointSnapshot(Frame, Payload, Error)
				|| !ValidateCoverage(
					Frame.TimelineTick,
					ScannedTurnCount,
					nullptr,
					Error))
			{
				return Reject(Error);
			}
			if (DurableEndTick != INDEX_NONE
				&& Frame.TimelineTick < DurableEndTick)
			{
				return Reject(TEXT("checkpoint frontier moves backwards"));
			}
			const uint64 NextIndexedCount = static_cast<uint64>(
				CandidateTurnFrames.Num() + CandidateCheckpoints.Num()) + 1ULL;
			if (NextIndexedCount
				> MaxJournalIndexBytes / sizeof(FIndexedJournalFrame))
			{
				return Reject(TEXT("journal checkpoint index exceeds its bounded memory budget"));
			}
			CandidateCheckpoints.Add(Descriptor);
			bSawInitialCheckpoint = true;
			DurableEndTick = Frame.TimelineTick;
			DurableTurnCount = ScannedTurnCount;
			CandidateDurableFrame = Descriptor;
			break;
		}
		case EFrameType::TurnBatch:
		{
			if (!bSawInitialCheckpoint)
			{
				return Reject(TEXT("TurnBatch precedes the mandatory tick-zero Checkpoint"));
			}
			TArray<FTurnRecord> Records;
			if (!DecodeTurnBatch(Payload, Records, Error))
			{
				return Reject(Error);
			}
			const int32 ExpectedFirstTurn = PreviousTurn == INDEX_NONE
				? EarliestTurn
				: PreviousTurn + 1;
			if (Records.IsEmpty() || Records[0].TurnId != ExpectedFirstTurn
				|| static_cast<int64>(Records.Last().TurnId) * TicksPerTurn
					> Frame.TimelineTick)
			{
				return Reject(TEXT("TurnBatch is not the next contiguous applied range for its timeline tick"));
			}
			Descriptor.FirstRecordOrdinal = ScannedTurnCount;
			Descriptor.RecordCount = Records.Num();
			for (const FTurnRecord& OpaqueRecord : Records)
			{
				FSeinReplayTurnRecord Decoded;
				Decoded.TurnId = OpaqueRecord.TurnId;
				if (!FSeinNetCommandWireCodec::DecodeCommands(
						OpaqueRecord.OpaqueCommands,
						SeinReplayFormat::MaxCommandsPerTurn,
						[WorldSub](FGameplayTag Type, int32 Version,
							FSeinCommandSchemaDescriptor& Out)
						{
							return WorldSub->FindCommandSchema(Type, Version, Out);
						},
						Decoded.Commands,
						Error)
					|| !ValidateDecodedTurn(
						Candidate.Header, Decoded, WorldSub, Error))
				{
					return Reject(FString::Printf(
						TEXT("turn %d command decode/validation failed: %s"),
						OpaqueRecord.TurnId, *Error));
				}
			}
			if (Records.Num() > MAX_int32 - ScannedTurnCount)
			{
				return Reject(TEXT("journal turn count exceeds int32 indexing"));
			}
			ScannedTurnCount += Records.Num();
			PreviousTurn = Records.Last().TurnId;
			const uint64 NextIndexedCount = static_cast<uint64>(
				CandidateTurnFrames.Num() + CandidateCheckpoints.Num()) + 1ULL;
			if (NextIndexedCount
				> MaxJournalIndexBytes / sizeof(FIndexedJournalFrame))
			{
				return Reject(TEXT("journal turn index exceeds its bounded memory budget"));
			}
			CandidateTurnFrames.Add(Descriptor);
			break;
		}
		case EFrameType::Progress:
		case EFrameType::Finalize:
		{
			if (!bSawInitialCheckpoint)
			{
				return Reject(TEXT("frontier frame precedes the mandatory tick-zero Checkpoint"));
			}
			FFrontier Frontier;
			if (!DecodeFrontier(Payload, Frontier, Error)
				|| !ValidateCoverage(
					Frontier.EndTick,
					ScannedTurnCount,
					&Frontier,
					Error))
			{
				return Reject(Error);
			}
			if (DurableEndTick != INDEX_NONE
				&& Frontier.EndTick < DurableEndTick)
			{
				return Reject(TEXT("durable replay frontier moves backwards"));
			}
			DurableEndTick = Frontier.EndTick;
			DurableTurnCount = ScannedTurnCount;
			CandidateDurableFrame = Descriptor;
			if (Frame.Type == EFrameType::Finalize)
			{
				bSawFinalize = true;
			}
			break;
		}
		default:
			return Reject(TEXT("unknown frame type"));
		}

		LastFrameTimelineTick = Frame.TimelineTick;
		ExpectedPreviousDigest = Frame.CurrentDigest;
		++ExpectedSequence;
		Offset = PayloadOffset + Frame.PayloadBytes;
	}

	if (!bSawHeader || !bSawInitialCheckpoint || DurableEndTick == INDEX_NONE
		|| !CandidateBootstrapCheckpoint.IsSet()
		|| !CandidateDurableFrame.CurrentDigest.IsValid())
	{
		return Reject(TEXT("journal lacks its Header, mandatory tick-zero Checkpoint, or durable frontier"));
	}
	if (!bPartial && !bSawFinalize)
	{
		return Reject(TEXT("completed .seinreplay file has no terminal Finalize frame"));
	}
	if (bSawFinalize && Offset != FileSize)
	{
		return Reject(TEXT("Finalize is not the exact final frame"));
	}

	// Range reads deliberately reopen closed handles so playback can lazily
	// revalidate chunks. Before publishing the candidate index, prove the path
	// still names the same-sized journal with the same integrity-checked prefix.
	// Runtime chunk reads then revalidate each descriptor/digest again.
	int64 RecheckedFileSize = 0;
	TArray<uint8> RecheckedPrefixBytes;
	FPrefix RecheckedPrefix;
	if (!SeinReplayFileIO::QueryBoundedSize(
			ResolvedPath,
			PrefixBytes,
			static_cast<int64>(MaxFileBytes),
			RecheckedFileSize,
			Error)
		|| RecheckedFileSize != FileSize
		|| !SeinReplayFileIO::ReadRange(
			ResolvedPath,
			0,
			PrefixBytes,
			static_cast<int64>(MaxFileBytes),
			RecheckedPrefixBytes,
			Error)
		|| !ParsePrefix(RecheckedPrefixBytes, RecheckedPrefix, Error)
		|| RecheckedPrefix.PrefixDigest != Prefix.PrefixDigest)
	{
		return Reject(Error.IsEmpty()
			? TEXT("journal changed while its bounded index was being built")
			: Error);
	}
	TArray<uint8> RecheckedDurableHeaderBytes;
	FFrameHeader RecheckedDurableHeader;
	if (!SeinReplayFileIO::ReadRange(
			ResolvedPath,
			CandidateDurableFrame.FileOffset,
			FrameHeaderBytes,
			static_cast<int64>(MaxFileBytes),
			RecheckedDurableHeaderBytes,
			Error)
		|| !ParseFrameHeader(
			RecheckedDurableHeaderBytes, RecheckedDurableHeader, Error)
		|| RecheckedDurableHeader.Sequence
			!= CandidateDurableFrame.Sequence
		|| static_cast<uint8>(RecheckedDurableHeader.Type)
			!= CandidateDurableFrame.Type
		|| RecheckedDurableHeader.Flags != CandidateDurableFrame.Flags
		|| RecheckedDurableHeader.FirstTurn
			!= CandidateDurableFrame.FirstTurn
		|| RecheckedDurableHeader.LastTurn != CandidateDurableFrame.LastTurn
		|| RecheckedDurableHeader.TimelineTick
			!= CandidateDurableFrame.TimelineTick
		|| RecheckedDurableHeader.PayloadBytes
			!= CandidateDurableFrame.PayloadBytes
		|| RecheckedDurableHeader.PreviousDigest
			!= CandidateDurableFrame.PreviousDigest
		|| RecheckedDurableHeader.CurrentDigest
			!= CandidateDurableFrame.CurrentDigest)
	{
		return Reject(Error.IsEmpty()
			? TEXT("durable frontier changed while its bounded index was being built")
			: Error);
	}

	TArray<FIndexedJournalFrame> ExecutableTurnFrames;
	for (const FIndexedJournalFrame& Frame : CandidateTurnFrames)
	{
		if (Frame.FirstRecordOrdinal >= DurableTurnCount)
		{
			break;
		}
		if (Frame.FirstRecordOrdinal + Frame.RecordCount > DurableTurnCount)
		{
			return Reject(TEXT("durable frontier splits a TurnBatch frame"));
		}
		ExecutableTurnFrames.Add(Frame);
	}
	Candidate.Header.EndTick = DurableEndTick;
	Candidate.Turns.Reset();

	ResetJournalLoadedState();
	Loaded = MoveTemp(Candidate);
	LoadedJournalPath = ResolvedPath;
	LoadedJournalFileSize = FileSize;
	LoadedJournalTurnCount = DurableTurnCount;
	LoadedJournalEarliestTurn = EarliestTurn;
	LoadedJournalTurnFrames = MoveTemp(ExecutableTurnFrames);
	LoadedJournalCheckpoints = MoveTemp(CandidateCheckpoints);
	LoadedJournalDurableFrame = CandidateDurableFrame;
	LoadedJournalBootstrapCheckpoint =
		MoveTemp(CandidateBootstrapCheckpoint.GetValue());
	bLoadedV9 = true;
	bLoadedJournalPartial = bPartial && !bSawFinalize;
	bLoaded = true;
	NextTurnIndex = 0;

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader: indexed %s v9 journal %s — turns=%d frames=%d checkpoints=%d ticks=0..%d seed=%lld"),
		bLoadedJournalPartial ? TEXT("recoverable partial") : TEXT("finalized"),
		*ResolvedPath,
		LoadedJournalTurnCount,
		LoadedJournalTurnFrames.Num(),
		LoadedJournalCheckpoints.Num(),
		Loaded.Header.EndTick,
		Loaded.Header.RandomSeed);
	return true;
}

bool USeinReplayReader::ReadIndexedFramePayload(
	const FIndexedJournalFrame& Descriptor,
	TArray<uint8>& OutPayload,
	FString& OutError) const
{
	using namespace SeinReplayJournalFormat;
	int64 CurrentSize = 0;
	if (!SeinReplayFileIO::QueryBoundedSize(
			LoadedJournalPath,
			PrefixBytes,
			static_cast<int64>(MaxFileBytes),
			CurrentSize,
			OutError)
		|| CurrentSize != LoadedJournalFileSize)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("journal file size changed after validation");
		}
		return false;
	}
	TArray<uint8> HeaderBytes;
	if (!SeinReplayFileIO::ReadRange(
			LoadedJournalPath,
			Descriptor.FileOffset,
			FrameHeaderBytes,
			static_cast<int64>(MaxFileBytes),
			HeaderBytes,
			OutError))
	{
		return false;
	}
	FFrameHeader Header;
	if (!ParseFrameHeader(HeaderBytes, Header, OutError)
		|| Header.Sequence != Descriptor.Sequence
		|| static_cast<uint8>(Header.Type) != Descriptor.Type
		|| Header.Flags != Descriptor.Flags
		|| Header.FirstTurn != Descriptor.FirstTurn
		|| Header.LastTurn != Descriptor.LastTurn
		|| Header.TimelineTick != Descriptor.TimelineTick
		|| Header.PayloadBytes != Descriptor.PayloadBytes
		|| Header.PreviousDigest != Descriptor.PreviousDigest
		|| Header.CurrentDigest != Descriptor.CurrentDigest)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("indexed frame header changed after validation");
		}
		return false;
	}
	TArray<uint8> Payload;
	if (!SeinReplayFileIO::ReadRange(
			LoadedJournalPath,
			Descriptor.FileOffset + FrameHeaderBytes,
			Descriptor.PayloadBytes,
			static_cast<int64>(MaxFileBytes),
			Payload,
			OutError)
		|| !ValidateFrame(Header, Payload, OutError))
	{
		return false;
	}
	OutPayload = MoveTemp(Payload);
	return true;
}

bool USeinReplayReader::ReadAndDecodeCheckpoint(
	const FIndexedJournalFrame& Descriptor,
	FSeinWorldSnapshot& OutSnapshot,
	FString& OutError) const
{
	if (Descriptor.Type != static_cast<uint8>(
			SeinReplayJournalFormat::EFrameType::Checkpoint))
	{
		OutError = TEXT("indexed frame is not a checkpoint");
		return false;
	}
	TArray<uint8> Payload;
	if (!ReadIndexedFramePayload(Descriptor, Payload, OutError))
	{
		return false;
	}
	FSeinWorldSnapshot Candidate;
	FSeinWorldSnapshotReferenceGuard CandidateGuard(Candidate);
	FSeinSnapshotEnvelopeMetadata Metadata;
	if (!SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			Payload, Candidate, Metadata, OutError))
	{
		return false;
	}
	if (Candidate.CurrentTick != Descriptor.TimelineTick
		|| Candidate.CurrentTick < 0
		|| Candidate.CurrentTick > Loaded.Header.EndTick
		|| Candidate.CommandProtocolDigest
			!= Loaded.Header.CommandProtocolDigest
		|| Candidate.SimulationContentDigest
			!= Loaded.Header.BootstrapReceipt.SimulationContentDigest
		|| Candidate.MatchSettingsDigest != Loaded.Header.MatchSettingsDigest
		|| Candidate.ConfigFingerprint != Loaded.Header.ConfigFingerprint
		|| Candidate.BootstrapCheckpoint.Receipt
			!= Loaded.Header.BootstrapReceipt
		|| !Candidate.BootstrapCheckpoint.IsValidConsumedCheckpoint()
		|| !FSeinSnapshotBootstrapCheckpoint::StaticStruct()
			->CompareScriptStruct(
				&LoadedJournalBootstrapCheckpoint,
				&Candidate.BootstrapCheckpoint,
				PPF_None)
		|| Candidate.SessionSeed != Loaded.Header.RandomSeed
		|| Candidate.FrameworkVersion != Loaded.Header.FrameworkVersion
		|| Candidate.GameVersion != Loaded.Header.GameVersion
		|| Candidate.MapIdentifier.ToString() != Loaded.Header.MapIdentifier
		|| Metadata.SnapshotTick != Descriptor.TimelineTick
		|| Metadata.CommandProtocolDigest
			!= Loaded.Header.CommandProtocolDigest
		|| Metadata.CompatibilityDigest
			!= Loaded.Header.BootstrapReceipt.StateContractDigest
		|| Candidate.bSimPaused || Candidate.bSimPausedHard
		|| !Candidate.PendingStandalonePauseControlCommands.IsEmpty())
	{
		OutError = TEXT("checkpoint no longer cross-binds to the loaded executable replay envelope");
		return false;
	}
	OutSnapshot = MoveTemp(Candidate);
	return true;
}

bool USeinReplayReader::RevalidateLoadedJournalFrontier(
	FString& OutError) const
{
	using namespace SeinReplayJournalFormat;
	if (LoadedJournalDurableFrame.FirstRecordOrdinal
		!= LoadedJournalTurnCount)
	{
		OutError = TEXT("durable frontier turn ordinal changed after validation");
		return false;
	}
	const EFrameType Type = static_cast<EFrameType>(
		LoadedJournalDurableFrame.Type);
	if (Type == EFrameType::Checkpoint)
	{
		FSeinWorldSnapshot Snapshot;
		FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
		if (!ReadAndDecodeCheckpoint(
				LoadedJournalDurableFrame, Snapshot, OutError))
		{
			return false;
		}
		if (Snapshot.CurrentTick != Loaded.Header.EndTick)
		{
			OutError = TEXT("durable checkpoint tick changed after validation");
			return false;
		}
		return true;
	}
	if (Type != EFrameType::Progress && Type != EFrameType::Finalize)
	{
		OutError = TEXT("loaded durable frontier has an unsupported frame type");
		return false;
	}
	TArray<uint8> Payload;
	FFrontier Frontier;
	if (!ReadIndexedFramePayload(
			LoadedJournalDurableFrame, Payload, OutError)
		|| !DecodeFrontier(Payload, Frontier, OutError))
	{
		return false;
	}
	int32 ExpectedFirst = INDEX_NONE;
	int32 ExpectedLast = INDEX_NONE;
	if (!SeinReplayFormat::GetRequiredTurnRange(
			Loaded.Header,
			GetTicksPerTurnFromSettings(),
			GetInputDelayTurnsFromSettings(),
			ExpectedFirst,
			ExpectedLast,
			OutError))
	{
		return false;
	}
	const bool bMatches = Frontier.EndTick == Loaded.Header.EndTick
		&& Frontier.FirstAppliedTurn == ExpectedFirst
		&& Frontier.LastAppliedTurn == ExpectedLast
		&& Frontier.AppliedTurnCount
			== static_cast<uint32>(LoadedJournalTurnCount);
	if (!bMatches)
	{
		OutError = TEXT("durable frontier changed after validation");
	}
	return bMatches;
}

bool USeinReplayReader::LoadResidentTurnFrame(
	int32 FrameIndex,
	USeinWorldSubsystem* WorldSub,
	FString& OutError)
{
	if (!LoadedJournalTurnFrames.IsValidIndex(FrameIndex))
	{
		OutError = TEXT("turn cursor has no indexed frame");
		return false;
	}
	const FIndexedJournalFrame& Descriptor =
		LoadedJournalTurnFrames[FrameIndex];
	TArray<uint8> Payload;
	if (!ReadIndexedFramePayload(Descriptor, Payload, OutError))
	{
		return false;
	}
	TArray<SeinReplayJournalFormat::FTurnRecord> OpaqueRecords;
	if (!SeinReplayJournalFormat::DecodeTurnBatch(
			Payload, OpaqueRecords, OutError)
		|| OpaqueRecords.Num() != Descriptor.RecordCount
		|| OpaqueRecords.IsEmpty()
		|| OpaqueRecords[0].TurnId != Descriptor.FirstTurn
		|| OpaqueRecords.Last().TurnId != Descriptor.LastTurn)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("indexed TurnBatch descriptor disagrees with its payload");
		}
		return false;
	}
	TArray<FSeinReplayTurnRecord> Candidate;
	Candidate.Reserve(OpaqueRecords.Num());
	for (const SeinReplayJournalFormat::FTurnRecord& Opaque : OpaqueRecords)
	{
		FSeinReplayTurnRecord& Decoded = Candidate.AddDefaulted_GetRef();
		Decoded.TurnId = Opaque.TurnId;
		if (!FSeinNetCommandWireCodec::DecodeCommands(
				Opaque.OpaqueCommands,
				SeinReplayFormat::MaxCommandsPerTurn,
				[WorldSub](FGameplayTag Type, int32 Version,
					FSeinCommandSchemaDescriptor& Out)
				{
					return WorldSub->FindCommandSchema(Type, Version, Out);
				},
				Decoded.Commands,
				OutError)
			|| !ValidateDecodedTurn(
				Loaded.Header, Decoded, WorldSub, OutError))
		{
			return false;
		}
	}
	const int32 StartRecord =
		NextJournalTurnOrdinal - Descriptor.FirstRecordOrdinal;
	if (StartRecord < 0 || StartRecord >= Candidate.Num())
	{
		OutError = TEXT("turn cursor does not fall inside the indexed TurnBatch");
		return false;
	}
	ResidentJournalTurns = MoveTemp(Candidate);
	ResidentJournalFrameIndex = FrameIndex;
	ResidentJournalRecordIndex = StartRecord;
	return true;
}

void USeinReplayReader::ResetJournalPlaybackCursor(int32 CheckpointTick)
{
	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();
	const int32 FirstFutureTurn = FMath::Max(
		LoadedJournalEarliestTurn,
		CheckpointTick / TicksPerTurn + 1);
	NextJournalTurnOrdinal = FMath::Clamp(
		FirstFutureTurn - LoadedJournalEarliestTurn,
		0,
		LoadedJournalTurnCount);
	NextTurnIndex = NextJournalTurnOrdinal;
	NextJournalFrameIndex = LoadedJournalTurnFrames.Num();
	for (int32 Index = 0; Index < LoadedJournalTurnFrames.Num(); ++Index)
	{
		const FIndexedJournalFrame& Frame = LoadedJournalTurnFrames[Index];
		if (NextJournalTurnOrdinal
			< Frame.FirstRecordOrdinal + Frame.RecordCount)
		{
			NextJournalFrameIndex = Index;
			break;
		}
	}
	ResidentJournalTurns.Reset();
	ResidentJournalFrameIndex = INDEX_NONE;
	ResidentJournalRecordIndex = 0;
}

bool USeinReplayReader::HandleJournalTurnReady(int32 Turn)
{
	if (!bPlaying || !bLoadedV9)
	{
		return false;
	}
	if (Turn < LoadedJournalEarliestTurn)
	{
		return true; // input-delay grace turns have no assembled records
	}
	const int32 ExpectedTurn =
		LoadedJournalEarliestTurn + NextJournalTurnOrdinal;
	if (NextJournalTurnOrdinal >= LoadedJournalTurnCount
		|| Turn != ExpectedTurn)
	{
		ScheduleJournalPlaybackFailure(FString::Printf(
			TEXT("turn gate requested %d but indexed cursor expects %d"),
			Turn, ExpectedTurn));
		return false;
	}
	USeinWorldSubsystem* WorldSub = BoundWorldSubsystem.Get();
	if (!WorldSub)
	{
		ScheduleJournalPlaybackFailure(TEXT("playback world was destroyed"));
		return false;
	}
	FString Error;
	if (ResidentJournalFrameIndex != NextJournalFrameIndex
		&& !LoadResidentTurnFrame(
			NextJournalFrameIndex, WorldSub, Error))
	{
		ScheduleJournalPlaybackFailure(Error);
		return false;
	}
	if (!ResidentJournalTurns.IsValidIndex(ResidentJournalRecordIndex)
		|| ResidentJournalTurns[ResidentJournalRecordIndex].TurnId != Turn)
	{
		ScheduleJournalPlaybackFailure(
			TEXT("resident TurnBatch cursor disagrees with the requested turn"));
		return false;
	}
	return true;
}

void USeinReplayReader::HandleJournalTurnConsume(int32 Turn)
{
	if (Turn < LoadedJournalEarliestTurn)
	{
		return;
	}
	USeinWorldSubsystem* WorldSub = BoundWorldSubsystem.Get();
	if (!WorldSub
		|| !ResidentJournalTurns.IsValidIndex(ResidentJournalRecordIndex)
		|| ResidentJournalTurns[ResidentJournalRecordIndex].TurnId != Turn)
	{
		ScheduleJournalPlaybackFailure(
			TEXT("turn consume followed without a matching integrity-checked ready record"));
		return;
	}
	for (const FSeinCommand& Command :
		ResidentJournalTurns[ResidentJournalRecordIndex].Commands)
	{
		WorldSub->EnqueueCommand(Command);
	}
	++NextJournalTurnOrdinal;
	NextTurnIndex = NextJournalTurnOrdinal;
	++ResidentJournalRecordIndex;
	if (ResidentJournalRecordIndex >= ResidentJournalTurns.Num())
	{
		ResidentJournalTurns.Reset();
		ResidentJournalFrameIndex = INDEX_NONE;
		ResidentJournalRecordIndex = 0;
		++NextJournalFrameIndex;
	}
}

void USeinReplayReader::ScheduleJournalPlaybackFailure(const FString& Reason)
{
	if (bJournalFailureScheduled)
	{
		return;
	}
	bJournalFailureScheduled = true;
	PendingJournalFailureReason = Reason.IsEmpty()
		? TEXT("journal frame revalidation failed") : Reason;
	const uint64 ScheduledGeneration = PlaybackGeneration;
	TWeakObjectPtr<USeinReplayReader> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, ScheduledGeneration]()
	{
		if (USeinReplayReader* Reader = WeakThis.Get())
		{
			Reader->HandleJournalPlaybackFailure(ScheduledGeneration);
		}
	});
}

void USeinReplayReader::HandleJournalPlaybackFailure(
	uint64 ExpectedPlaybackGeneration)
{
	if (ExpectedPlaybackGeneration != PlaybackGeneration)
	{
		return;
	}
	bJournalFailureScheduled = false;
	if (bPlaying)
	{
		const FString Reason = PendingJournalFailureReason;
		HaltPlayback(BoundWorldSubsystem.Get(), *Reason);
	}
}

bool USeinReplayReader::Play()
{
	return PlayFromTick(0);
}

bool USeinReplayReader::PlayFromTick(int32 TargetTick)
{
	if (bLoadedV9)
	{
		return PlayV9(TargetTick);
	}
	if (TargetTick != 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: frozen v8 files support tick-zero playback only."));
		return false;
	}
	return PlayV8();
}

bool USeinReplayReader::PlayV8()
{
	if (!bLoaded)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayReader::Play: nothing loaded — call LoadFromFile first."));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayReader::Play: no World."));
		return false;
	}

	// Refuse to clobber a live networked session.
	if (World->GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: world is networked (NetMode=%d). Replay playback runs only in Standalone — close the multiplayer session first."),
			(int32)World->GetNetMode());
		return false;
	}

	USeinWorldSubsystem* WorldSub = GetWorldSubsystem();
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayReader::Play: USeinWorldSubsystem missing."));
		return false;
	}
	FString PlaybackStateError;
	if (!SeinReplayFormat::ValidatePlaybackStartState(
			WorldSub->GetMatchState(),
			WorldSub->IsSimulationRunning(),
			WorldSub->GetCurrentTick(),
			PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: %s."), *PlaybackStateError);
		return false;
	}
	if (WorldSub->GetCommandProtocolDigest() != Loaded.Header.CommandProtocolDigest
		|| !WorldSub->IsSimulationContentReady()
		|| WorldSub->GetSimulationContentDigest()
			!= Loaded.Header.BootstrapReceipt.SimulationContentDigest
		|| WorldSub->GetConfigFingerprint() != Loaded.Header.ConfigFingerprint)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: local compatibility changed after load — refusing playback."));
		return false;
	}
	if (!SeinReplayCompatibility::ValidateCurrent(
		Loaded.Header, World, PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: runtime compatibility changed after load: %s."),
			*PlaybackStateError);
		return false;
	}
	if (WorldSub->GetMatchBootstrapState()
		!= ESeinMatchBootstrapState::Awaiting)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: playback requires an unclaimed bootstrap world (state=%d)."),
			static_cast<int32>(WorldSub->GetMatchBootstrapState()));
		return false;
	}

	if (!WorldSub->BeginReplayExclusiveCommandIngress(PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: could not acquire exclusive replay ingress: %s."),
			*PlaybackStateError);
		return false;
	}
	bOwnsExternalCommandIngress = true;

	FGuid AuthorizationContextDigest;
	if (!SeinReplayFormat::ComputeBootstrapAuthorizationContextDigest(
			Loaded.Header, AuthorizationContextDigest, PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: invalid bootstrap authorization envelope: %s."),
			*PlaybackStateError);
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		return false;
	}
	FSeinMatchBootstrapAuthorityHandle ClaimedAuthority;
	if (!WorldSub->ClaimMatchBootstrapAuthority(
			GSeinReplayBootstrapAuthorityID,
			this,
			ClaimedAuthority,
			PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: bootstrap authority claim failed: %s."),
			*PlaybackStateError);
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		return false;
	}
	BootstrapAuthority = ClaimedAuthority;

	// The seed is part of tick-zero state and must be installed before the
	// shared materializer seals its canonical receipt.
	if (!WorldSub->SeedSimRandom(
			BootstrapAuthority,
			Loaded.Header.RandomSeed,
			PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: bootstrap session seeding failed: %s."),
			*PlaybackStateError);
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		return false;
	}
	FSeinMatchBootstrapReceipt LocalReceipt;
	if (!WorldSub->EnsureMatchBootstrapLocallyReady(
			BootstrapAuthority,
			Loaded.Header.SettingsSnapshot,
			AuthorizationContextDigest,
			LocalReceipt,
			PlaybackStateError)
		|| LocalReceipt != Loaded.Header.BootstrapReceipt)
	{
		if (PlaybackStateError.IsEmpty())
		{
			PlaybackStateError =
				TEXT("locally materialized tick-zero receipt differs from the recording");
			FString FailureError;
			WorldSub->FailMatchBootstrap(
				BootstrapAuthority, PlaybackStateError, FailureError);
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: bootstrap materialization failed: %s."),
			*PlaybackStateError);
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		return false;
	}
	if (!WorldSub->AuthorizeMatchBootstrap(
			BootstrapAuthority,
			Loaded.Header.BootstrapReceipt,
			AuthorizationContextDigest,
			PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: bootstrap self-authorization failed: %s."),
			*PlaybackStateError);
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		return false;
	}

	NextTurnIndex = 0;
	if (Loaded.Header.EndTick == 0)
	{
		// Authorization owns a dormant scheduler reservation. Consume the
		// no-fail launch transition, then explicitly release it so a terminal
		// tick-zero replay does not leak bootstrap or ticker ownership.
		if (!WorldSub->LaunchAuthorizedMatchBootstrap(
				BootstrapAuthority, PlaybackStateError))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayReader::Play: tick-0 simulation launch transition failed: %s."),
				*PlaybackStateError);
			FString FailureError;
			WorldSub->FailMatchBootstrap(
				BootstrapAuthority, PlaybackStateError, FailureError);
			WorldSub->EndReplayExclusiveCommandIngress();
			bOwnsExternalCommandIngress = false;
			BootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();
			return false;
		}
		WorldSub->StopSimulation();
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		BootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();
		UE_LOG(LogSeinNet, Log,
			TEXT("ReplayReader::Play: tick-0 replay established; simulation remains stopped."));
		return true;
	}

	++PlaybackGeneration;
	bPlaying = true;
	BoundWorldSubsystem = WorldSub;
	SimTickHandle = WorldSub->ReplayCommandBoundaryNotifier.AddUObject(
		this, &USeinReplayReader::HandleSimTick);

	// The pristine-state gate guarantees this is the first simulation run.
	if (!WorldSub->LaunchAuthorizedMatchBootstrap(
			BootstrapAuthority, PlaybackStateError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::Play: simulation refused to start: %s."),
			*PlaybackStateError);
		FString FailureError;
		WorldSub->FailMatchBootstrap(
			BootstrapAuthority, PlaybackStateError, FailureError);
		Stop();
		return false;
	}

	// Launch only arms the already-reserved dormant ticker; it cannot execute on
	// this stack. Prime turn 1 after Consumed so even replay's canonical ingress
	// obeys the same launched-world boundary as transport and local drafts.
	if (!DrainTurnsForUpcomingTick(WorldSub, /*UpcomingTick=*/1))
	{
		HaltPlayback(WorldSub, TEXT("initial turn journal is already past due"));
		return false;
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader::Play: started — %d applied turn(s), EndTick=%d, TicksPerTurn=%d, seed=%lld"),
		Loaded.Turns.Num(), Loaded.Header.EndTick,
		GetTicksPerTurnFromSettings(), Loaded.Header.RandomSeed);

	return true;
}

bool USeinReplayReader::PlayV9(int32 TargetTick)
{
	if (!bLoaded || !bLoadedV9)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("ReplayReader::PlayFromTick: no v9 journal is loaded."));
		return false;
	}
	if (TargetTick < 0 || TargetTick > Loaded.Header.EndTick)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: target %d is outside 0..%d."),
			TargetTick, Loaded.Header.EndTick);
		return false;
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: checkpoint playback requires a Standalone world."));
		return false;
	}
	USeinWorldSubsystem* WorldSub = GetWorldSubsystem();
	if (!WorldSub)
	{
		return false;
	}
	FString Error;
	if (!SeinReplayFormat::ValidatePlaybackStartState(
			WorldSub->GetMatchState(),
			WorldSub->IsSimulationRunning(),
			WorldSub->GetCurrentTick(),
			Error)
		|| WorldSub->GetMatchBootstrapState()
			!= ESeinMatchBootstrapState::Awaiting)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: requires a pristine unclaimed world (%s)."),
			*Error);
		return false;
	}
	if (WorldSub->GetCommandProtocolDigest()
			!= Loaded.Header.CommandProtocolDigest
		|| !WorldSub->IsSimulationContentReady()
		|| WorldSub->GetSimulationContentDigest()
			!= Loaded.Header.BootstrapReceipt.SimulationContentDigest
		|| WorldSub->GetConfigFingerprint()
			!= Loaded.Header.ConfigFingerprint
		|| !SeinReplayCompatibility::ValidateCurrent(
			Loaded.Header, World, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: compatibility changed after load (%s)."),
			*Error);
		return false;
	}
	if (WorldSub->TurnReadyResolver.IsBound()
		|| WorldSub->TurnConsumeNotifier.IsBound())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: another topology adapter owns the turn gate."));
		return false;
	}
	if (!RevalidateLoadedJournalFrontier(Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: durable frontier revalidation failed: %s."),
			*Error);
		return false;
	}

	int32 CheckpointIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LoadedJournalCheckpoints.Num(); ++Index)
	{
		if (LoadedJournalCheckpoints[Index].TimelineTick <= TargetTick)
		{
			CheckpointIndex = Index;
		}
		else
		{
			break;
		}
	}
	if (CheckpointIndex == INDEX_NONE)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: no checkpoint precedes target %d."),
			TargetTick);
		return false;
	}
	const FIndexedJournalFrame& CheckpointDescriptor =
		LoadedJournalCheckpoints[CheckpointIndex];
	FSeinWorldSnapshot Checkpoint;
	FSeinWorldSnapshotReferenceGuard CheckpointGuard(Checkpoint);
	if (!ReadAndDecodeCheckpoint(CheckpointDescriptor, Checkpoint, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: checkpoint revalidation failed: %s."),
			*Error);
		return false;
	}

	FSeinSnapshotRestoreAuthorityHandle RestoreAuthority;
	if (!WorldSub->ClaimSnapshotRestoreAuthority(
			GSeinReplayCheckpointAuthorityID,
			this,
			RestoreAuthority,
			Error)
		|| !WorldSub->RestoreSnapshot(
			MoveTemp(RestoreAuthority),
			Checkpoint,
			FSeinSnapshotRestoreOptions(
				ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
				ESeinSnapshotResumePolicy::RemainStopped)))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: checkpoint adoption failed: %s."),
			*Error);
		return false;
	}
	if (Checkpoint.CurrentTick == Loaded.Header.EndTick)
	{
		// RemainStopped reserves a dormant ticker before snapshot commit. A
		// terminal replay has no later playback session to own/release it.
		WorldSub->StopSimulation();
		UE_LOG(LogSeinNet, Log,
			TEXT("ReplayReader::PlayFromTick: restored terminal checkpoint tick %d; simulation remains stopped."),
			Checkpoint.CurrentTick);
		return true;
	}

	// A continuation checkpoint may legitimately retain deterministic commands
	// authored during its final tick. The v8 helper rejects any pending queue
	// because it starts from pristine tick zero; v9 preserves that authoritative
	// snapshot lane while still requiring exclusive replay ownership.
	if (WorldSub->TurnReadyResolver.IsBound()
		|| WorldSub->TurnConsumeNotifier.IsBound()
		|| WorldSub->bReplayOwnsExternalCommandIngress
		|| WorldSub->PendingReplayCommands.Num() != 0
		|| !WorldSub->PendingStandalonePauseControlCommands.IsEmpty())
	{
		WorldSub->StopSimulation();
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: restored world cannot grant exclusive replay ingress or turn-gate ownership."));
		return false;
	}
	WorldSub->bReplayOwnsExternalCommandIngress = true;
	bOwnsExternalCommandIngress = true;

	ResetJournalPlaybackCursor(Checkpoint.CurrentTick);
	WorldSub->TurnReadyResolver.BindUObject(
		this, &USeinReplayReader::HandleJournalTurnReady);
	WorldSub->TurnConsumeNotifier.BindUObject(
		this, &USeinReplayReader::HandleJournalTurnConsume);
	bOwnsJournalTurnGate = true;
	++PlaybackGeneration;
	bPlaying = true;
	BoundWorldSubsystem = WorldSub;
	SimTickHandle = WorldSub->ReplayCommandBoundaryNotifier.AddUObject(
		this, &USeinReplayReader::HandleSimTick);
	JournalSeekTargetTick = TargetTick;
	if (TargetTick > Checkpoint.CurrentTick)
	{
		if (!WorldSub->BeginResyncCatchUpWindow(Error))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayReader::PlayFromTick: could not open checkpoint catch-up: %s."),
				*Error);
			WorldSub->StopSimulation();
			Stop();
			return false;
		}
		bJournalCatchUpActive = true;
	}
	if (!WorldSub->StartSimulation())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayReader::PlayFromTick: restored simulation refused to start."));
		WorldSub->StopSimulation();
		Stop();
		return false;
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader::PlayFromTick: restored checkpoint %d and started toward target %d (EndTick=%d, indexed turns=%d)."),
		Checkpoint.CurrentTick,
		TargetTick,
		Loaded.Header.EndTick,
		LoadedJournalTurnCount);
	return true;
}

void USeinReplayReader::Stop()
{
	++PlaybackGeneration;
	USeinWorldSubsystem* WorldSub = BoundWorldSubsystem.Get();
	if (!WorldSub && !SimTickHandle.IsValid())
	{
		WorldSub = GetWorldSubsystem();
	}
	if (WorldSub)
	{
		if (SimTickHandle.IsValid())
		{
			WorldSub->ReplayCommandBoundaryNotifier.Remove(SimTickHandle);
		}
		if (bOwnsJournalTurnGate)
		{
			WorldSub->TurnReadyResolver.Unbind();
			WorldSub->TurnConsumeNotifier.Unbind();
			bOwnsJournalTurnGate = false;
		}
		if (bJournalCatchUpActive)
		{
			WorldSub->EndResyncCatchUpWindow();
			bJournalCatchUpActive = false;
		}
		if (bOwnsExternalCommandIngress)
		{
			WorldSub->EndReplayExclusiveCommandIngress();
			bOwnsExternalCommandIngress = false;
		}
	}
	else
	{
		bOwnsExternalCommandIngress = false;
		bOwnsJournalTurnGate = false;
		bJournalCatchUpActive = false;
	}
	SimTickHandle.Reset();
	BoundWorldSubsystem.Reset();
	BootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();

	if (bPlaying)
	{
		UE_LOG(LogSeinNet, Log, TEXT("ReplayReader::Stop: stopped at turn-cursor %d/%d."),
			NextTurnIndex, GetTurnCount());
	}
	bPlaying = false;
	bJournalFailureScheduled = false;
	PendingJournalFailureReason.Reset();
	ResidentJournalTurns.Reset();
	ResidentJournalFrameIndex = INDEX_NONE;
	ResidentJournalRecordIndex = 0;
}

void USeinReplayReader::HandleSimTick(int32 CompletedTick)
{
	if (!bPlaying) return;
	if (!bLoaded) return;

	USeinWorldSubsystem* WorldSub = BoundWorldSubsystem.Get();
	if (!WorldSub)
	{
		Stop();
		return;
	}
	if (bLoadedV9)
	{
		if (CompletedTick > Loaded.Header.EndTick)
		{
			HaltPlayback(WorldSub, TEXT("simulation advanced beyond journal EndTick"));
			return;
		}
		if (bJournalCatchUpActive && CompletedTick >= JournalSeekTargetTick)
		{
			WorldSub->EndResyncCatchUpWindow();
			bJournalCatchUpActive = false;
		}
		if (CompletedTick == Loaded.Header.EndTick)
		{
			if (NextJournalTurnOrdinal != LoadedJournalTurnCount)
			{
				HaltPlayback(WorldSub, TEXT("journal reached EndTick with undrained turns"));
				return;
			}
			HaltPlayback(WorldSub, TEXT("inclusive journal EndTick reached"));
		}
		return;
	}
	if (CompletedTick > Loaded.Header.EndTick)
	{
		HaltPlayback(WorldSub, TEXT("simulation advanced beyond replay EndTick"));
		return;
	}
	if (CompletedTick == Loaded.Header.EndTick)
	{
		if (NextTurnIndex != Loaded.Turns.Num())
		{
			HaltPlayback(WorldSub, TEXT("replay reached EndTick with undrained turns"));
			return;
		}
		HaltPlayback(WorldSub, TEXT("inclusive EndTick reached"));
		return;
	}

	// Drain only the exact turn starting on the next sim tick. A past-due
	// record is a protocol failure, never an invitation to apply it late.
	const int64 NextTickAboutToRun = static_cast<int64>(CompletedTick) + 1;
	if (!DrainTurnsForUpcomingTick(WorldSub, NextTickAboutToRun))
	{
		HaltPlayback(WorldSub, TEXT("turn journal became past due"));
	}
}

bool USeinReplayReader::DrainTurnsForUpcomingTick(
	USeinWorldSubsystem* WorldSub,
	int64 UpcomingTick)
{
	check(WorldSub);
	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();
	while (NextTurnIndex < Loaded.Turns.Num())
	{
		const FSeinReplayTurnRecord& Record = Loaded.Turns[NextTurnIndex];
		const int64 TurnFirstTick =
			static_cast<int64>(Record.TurnId) * static_cast<int64>(TicksPerTurn);
		if (TurnFirstTick > UpcomingTick) break;
		if (TurnFirstTick < UpcomingTick)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayReader: turn %d belongs to tick %lld but next tick is %lld."),
				Record.TurnId,
				static_cast<long long>(TurnFirstTick),
				static_cast<long long>(UpcomingTick));
			return false;
		}
		// Enqueue every command in this turn for processing on the upcoming tick.
		for (const FSeinCommand& Cmd : Record.Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		UE_LOG(LogSeinNet, Verbose,
			TEXT("ReplayReader: drained turn %d (%d cmd(s)) for tick %lld."),
			Record.TurnId, Record.Commands.Num(),
			static_cast<long long>(UpcomingTick));
		++NextTurnIndex;
	}
	return true;
}

void USeinReplayReader::HaltPlayback(
	USeinWorldSubsystem* WorldSub,
	const TCHAR* Reason)
{
	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader: halting at tick %d/%d (%s), turn-cursor %d/%d."),
		WorldSub ? WorldSub->GetCurrentTick() : INDEX_NONE,
		Loaded.Header.EndTick,
		Reason ? Reason : TEXT("unspecified"),
		NextTurnIndex,
		GetTurnCount());
	if (WorldSub)
	{
		WorldSub->StopSimulation();
		if (BootstrapAuthority.IsValid()
			&& WorldSub->GetMatchBootstrapState()
				== ESeinMatchBootstrapState::Authorized)
		{
			FString FailureError;
			WorldSub->FailMatchBootstrap(
				BootstrapAuthority,
				Reason ? Reason : TEXT("Replay bootstrap halted before launch."),
				FailureError);
		}
	}
	// Avoid the generic explicit-abort log; the line above preserves the exact
	// natural-completion/failure reason.
	bPlaying = false;
	Stop();
}

USeinWorldSubsystem* USeinReplayReader::GetWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}
