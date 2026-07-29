/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayWriter.cpp
 */

#include "SeinReplayWriter.h"
#include "SeinARTSNet.h"
#include "SeinReplayFormat.h"
#include "SeinReplayFileIO.h"
#include "SeinReplayWireCodec.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
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
				World->GetCommandAdditionalWireNames() }
			: FSeinStructWireCatalogView{};
	}
}

void USeinReplayWriter::StartRecording(const FSeinReplayHeader& Header)
{
	if (bRecording)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ReplayWriter::StartRecording called while already recording — discarding %d buffered turn(s) and resetting."),
			Buffer.Turns.Num());
	}

	bRecording = false;
	Buffer = FSeinReplay();
	Buffer.Header = Header;
	Buffer.Header.EndTick = 0;
	LastObservedCompletedTick = 0;
	bTickObservationFailed = false;
	bJournalObservationFailed = false;
	BaselineBodyBytes = 0;
	BaselineDecodedAllocationBytes = 0;
	BufferedBodyBytes = 0;
	BufferedDecodedAllocationBytes = 0;
	FSeinMatchSettings CanonicalSettings = Header.SettingsSnapshot;
	FGuid SettingsDigest;
	FString CompatibilityError;
	const UWorld* RecordingWorld = GetWorld();
	const USeinWorldSubsystem* RecordingWorldSub = RecordingWorld
		? RecordingWorld->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	FGameplayTag SettingsRejectionReason;
	const bool bCompatibleRuntime = RecordingWorld
		? SeinReplayCompatibility::ValidateCurrent(
			Header, RecordingWorld, CompatibilityError)
		: Header.FrameworkVersion
				== SeinReplayCompatibility::GetFrameworkVersion()
			&& Header.GameVersion == SeinReplayCompatibility::GetGameVersion();
	const bool bValidMatchSettings = !RecordingWorldSub
		|| RecordingWorldSub->ValidateMatchSettings(
			Header.SettingsSnapshot, SettingsRejectionReason);
	const bool bMatchesWorld = !RecordingWorldSub
		|| ValidateHeaderAgainstWorld(
			Header, *RecordingWorldSub,
			/*bRequireCurrentStartTick=*/true,
			CompatibilityError);
	if (!Header.CommandProtocolDigest.IsValid()
		|| !Header.MatchSettingsDigest.IsValid()
		|| !Header.BootstrapReceipt.IsValid()
		|| Header.BootstrapReceipt.ContractDigest
			!= Header.MatchSettingsDigest
		|| Header.MapIdentifier.IsEmpty()
		|| !bCompatibleRuntime
		|| !bValidMatchSettings
		|| !bMatchesWorld
		|| Header.StartTick != 0
		|| !SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSettings, SettingsDigest, nullptr)
		|| SettingsDigest != Header.MatchSettingsDigest)
	{
		bRecording = false;
		Buffer = FSeinReplay();
		FString HeaderError = MoveTemp(CompatibilityError);
		if (HeaderError.IsEmpty() && SettingsRejectionReason.IsValid())
		{
			HeaderError = SettingsRejectionReason.ToString();
		}
		if (HeaderError.IsEmpty())
		{
			HeaderError = TEXT("required identity, digest, or tick metadata is inconsistent");
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused recording with an incomplete or inconsistent compatibility header (%s)."),
			*HeaderError);
		return;
	}
	Buffer.Header.SettingsSnapshot = MoveTemp(CanonicalSettings);
	Buffer.Header.Players.Sort([](
		const FSeinPlayerRegistration& A,
		const FSeinPlayerRegistration& B)
	{
		return A.PlayerID.Value < B.PlayerID.Value;
	});
	if (!SeinReplayCompatibility::ValidatePlayerManifest(
		Buffer.Header, CompatibilityError))
	{
		Buffer = FSeinReplay();
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused inconsistent player metadata: %s."),
			*CompatibilityError);
		return;
	}
	TArray<uint8> EmptyBody;
	uint64 EmptyDecodedAllocationBytes = 0;
	if (!FSeinReplayWireCodec::Encode(
		Buffer,
		GetFrozenReplayHeaderCatalog(RecordingWorldSub),
		[](FGameplayTag, int32, FSeinCommandSchemaDescriptor&)
		{
			return false;
		},
		EmptyBody,
		CompatibilityError,
		&EmptyDecodedAllocationBytes))
	{
		Buffer = FSeinReplay();
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refused recording because its bounded header cannot be encoded: %s."),
			*CompatibilityError);
		return;
	}
	BaselineBodyBytes = static_cast<uint64>(EmptyBody.Num());
	BaselineDecodedAllocationBytes = EmptyDecodedAllocationBytes;
	BufferedBodyBytes = BaselineBodyBytes;
	BufferedDecodedAllocationBytes = BaselineDecodedAllocationBytes;
	bRecording = true;

	UE_LOG(LogSeinNet, Log, TEXT("ReplayWriter: recording started.  seed=%lld  map=%s"),
		Header.RandomSeed, *Header.MapIdentifier);
}

void USeinReplayWriter::RecordTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (!bRecording) return;
	if (Commands.Num() > SeinReplayFormat::MaxCommandsPerTurn)
	{
		bJournalObservationFailed = true;
		bRecording = false;
		Buffer = FSeinReplay();
		BaselineBodyBytes = 0;
		BaselineDecodedAllocationBytes = 0;
		BufferedBodyBytes = 0;
		BufferedDecodedAllocationBytes = 0;
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: turn %d has %d commands, exceeding the canonical turn cap %d."),
			TurnId, Commands.Num(), SeinReplayFormat::MaxCommandsPerTurn);
		return;
	}

	FSeinReplay SingleTurn;
	SingleTurn.Header = Buffer.Header;
	FSeinReplayTurnRecord& CandidateRecord =
		SingleTurn.Turns.AddDefaulted_GetRef();
	CandidateRecord.TurnId = TurnId;
	CandidateRecord.Commands = Commands;
	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	TArray<uint8> CandidateBody;
	uint64 CandidateDecodedAllocationBytes = 0;
	FString CandidateError;
	if (!FSeinReplayWireCodec::Encode(
		SingleTurn,
		GetFrozenReplayHeaderCatalog(WorldSub),
		[WorldSub](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return WorldSub
				? WorldSub->FindCommandSchema(Type, Version, Out)
				: FSeinCommandSchemaRegistry::FindSchema(Type, Version, Out);
		},
		CandidateBody,
		CandidateError,
		&CandidateDecodedAllocationBytes)
		|| static_cast<uint64>(CandidateBody.Num()) < BaselineBodyBytes
		|| CandidateDecodedAllocationBytes < BaselineDecodedAllocationBytes)
	{
		bJournalObservationFailed = true;
		bRecording = false;
		Buffer = FSeinReplay();
		BaselineBodyBytes = 0;
		BaselineDecodedAllocationBytes = 0;
		BufferedBodyBytes = 0;
		BufferedDecodedAllocationBytes = 0;
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: turn %d cannot be represented by the bounded replay codec; recording aborted: %s."),
			TurnId, *CandidateError);
		return;
	}
	const uint64 AddedBodyBytes =
		static_cast<uint64>(CandidateBody.Num()) - BaselineBodyBytes;
	const uint64 AddedDecodedAllocationBytes =
		CandidateDecodedAllocationBytes - BaselineDecodedAllocationBytes;
	if (AddedBodyBytes > SeinReplayFormat::MaxBodyBytes - BufferedBodyBytes
		|| AddedDecodedAllocationBytes
			> FSeinReplayWireCodec::MaxDecodedAllocationBytes
				- BufferedDecodedAllocationBytes)
	{
		bJournalObservationFailed = true;
		bRecording = false;
		Buffer = FSeinReplay();
		BaselineBodyBytes = 0;
		BaselineDecodedAllocationBytes = 0;
		BufferedBodyBytes = 0;
		BufferedDecodedAllocationBytes = 0;
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: cumulative bounded replay budget exhausted at turn %d; recording aborted before retaining the turn."),
			TurnId);
		return;
	}

	FSeinReplayTurnRecord& Record = Buffer.Turns.AddDefaulted_GetRef();
	Record.TurnId = TurnId;
	Record.Commands = Commands;
	BufferedBodyBytes += AddedBodyBytes;
	BufferedDecodedAllocationBytes += AddedDecodedAllocationBytes;
}

void USeinReplayWriter::ObserveCompletedTick(int32 CompletedTick)
{
	if (!bRecording) return;
	const int64 ExpectedTick = static_cast<int64>(LastObservedCompletedTick) + 1;
	if (ExpectedTick > MAX_int32 || CompletedTick != static_cast<int32>(ExpectedTick))
	{
		bTickObservationFailed = true;
		bRecording = false;
		Buffer = FSeinReplay();
		BaselineBodyBytes = 0;
		BaselineDecodedAllocationBytes = 0;
		BufferedBodyBytes = 0;
		BufferedDecodedAllocationBytes = 0;
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: non-contiguous completed tick %d (expected %lld); recording aborted and buffered journal released."),
			CompletedTick, static_cast<long long>(ExpectedTick));
		return;
	}
	LastObservedCompletedTick = CompletedTick;
	Buffer.Header.EndTick = CompletedTick;
}

FString USeinReplayWriter::FinishRecording()
{
	if (!bRecording)
	{
		if (bTickObservationFailed)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayWriter::FinishRecording called but recording was aborted by a completed-tick observation gap — no-op."));
		}
		else if (bJournalObservationFailed)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ReplayWriter::FinishRecording called but recording was aborted by an invalid or oversized journal — no-op."));
		}
		else
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("ReplayWriter::FinishRecording called while not recording — no-op."));
		}
		return FString();
	}
	if (bTickObservationFailed)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refusing to finalize after a completed-tick observation gap."));
		return FString();
	}
	if (bJournalObservationFailed)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refusing to finalize after an invalid turn journal observation."));
		return FString();
	}

	FSeinReplay Finalized = Buffer;
	Finalized.Header.EndTick = LastObservedCompletedTick;
	FString FormatError;
	if (const UWorld* CurrentWorld = GetWorld())
	{
		if (const USeinWorldSubsystem* CurrentWorldSub =
			CurrentWorld->GetSubsystem<USeinWorldSubsystem>())
		{
			if (!SeinReplayCompatibility::ValidateCurrent(
				Finalized.Header, CurrentWorld, FormatError)
				|| !ValidateHeaderAgainstWorld(
					Finalized.Header, *CurrentWorldSub,
					/*bRequireCurrentStartTick=*/false,
					FormatError))
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("ReplayWriter: runtime compatibility changed during recording: %s."),
					*FormatError);
				return FString();
			}
		}
	}
	if (!SeinReplayFormat::FinalizeRecordedJournal(
			Finalized.Header,
			Finalized.Turns,
			GetTicksPerTurnFromSettings(),
			GetInputDelayTurnsFromSettings(),
			FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: refusing an incomplete or malformed turn journal: %s"),
			*FormatError);
		return FString();
	}

	// Replay bytes are deterministic; filesystem names are not protocol state.
	// A GUID suffix prevents two recordings stamped by the same clock quantum
	// from colliding while the atomic publisher still refuses replacement.
	const FString MapStr = Finalized.Header.MapIdentifier.IsEmpty()
		? TEXT("UnknownMap")
		: FPackageName::GetShortName(Finalized.Header.MapIdentifier);
	const FString Stamp = FString::Printf(
		TEXT("%s_%lld_%s"),
		*Finalized.Header.RecordedAt.ToString(TEXT("%Y%m%d_%H%M%S")),
		static_cast<long long>(Finalized.Header.RecordedAt.GetTicks()),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString FileName = FString::Printf(TEXT("%s_%s.seinreplay"), *MapStr, *Stamp);
	const FString DirPath = FPaths::ProjectSavedDir() / TEXT("Replays");
	const FString FilePath = DirPath / FileName;

	// Serialize a fully bounded body. No replay field is decoded through
	// Unreal's generic reflected archive on the read side.
	TArray<uint8> Body;
	uint64 FinalDecodedAllocationBytes = 0;
	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!FSeinReplayWireCodec::Encode(
		Finalized,
		GetFrozenReplayHeaderCatalog(WorldSub),
		[WorldSub](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return WorldSub
				? WorldSub->FindCommandSchema(Type, Version, Out)
				: FSeinCommandSchemaRegistry::FindSchema(Type, Version, Out);
		},
		Body,
		FormatError,
		&FinalDecodedAllocationBytes))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: bounded-body serialization failed for %s: %s"),
			*FilePath, *FormatError);
		return FString();
	}
	// RecordTurn accounts every observed turn so it can reject the journal
	// before retaining data beyond either hard ceiling. Finalization is allowed
	// to remove only the canonical unapplied input-delay tail, so the finalized
	// encoding may be smaller than those conservative running totals. It must
	// never be larger: that would mean the incremental admission check missed
	// bytes or allocations that the final file actually retains.
	if (static_cast<uint64>(Body.Num()) > BufferedBodyBytes
		|| FinalDecodedAllocationBytes > BufferedDecodedAllocationBytes)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: final bounded encoding exceeds incremental journal accounting; refusing output."));
		return FString();
	}

	TArray<uint8> Prefix;
	if (!SeinReplayFormat::BuildPrefix(
			Finalized.Header.CommandProtocolDigest,
			Finalized.Header.MatchSettingsDigest,
			Finalized.Header.BootstrapReceipt,
			Finalized.Header.ConfigFingerprint,
			Body,
			Prefix,
			FormatError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: cannot emit v8 replay prefix: %s"), *FormatError);
		return FString();
	}

	TArray<uint8> FileBytes = MoveTemp(Prefix);
	FileBytes.Reserve(FileBytes.Num() + Body.Num());
	FileBytes.Append(Body);

	FString FileError;
	if (!SeinReplayFileIO::WriteNewAtomically(FilePath, FileBytes, FileError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ReplayWriter: FAILED to publish replay to %s: %s"),
			*FilePath, *FileError);
		return FString();
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayWriter: wrote %d applied turn(s) through inclusive tick %d, %d bytes -> %s"),
		Finalized.Turns.Num(), Finalized.Header.EndTick,
		FileBytes.Num(), *FilePath);

	// Drop the buffer to release memory now that it's persisted.
	bRecording = false;
	bTickObservationFailed = false;
	bJournalObservationFailed = false;
	BaselineBodyBytes = 0;
	BaselineDecodedAllocationBytes = 0;
	BufferedBodyBytes = 0;
	BufferedDecodedAllocationBytes = 0;
	LastObservedCompletedTick = 0;
	Buffer = FSeinReplay();
	return FilePath;
}
