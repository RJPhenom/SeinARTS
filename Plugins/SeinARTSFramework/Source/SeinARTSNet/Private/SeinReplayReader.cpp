/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.cpp
 */

#include "SeinReplayReader.h"
#include "SeinARTSNet.h"
#include "SeinReplayFormat.h"
#include "SeinReplayFileIO.h"
#include "SeinReplayWireCodec.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
	const FName GSeinReplayBootstrapAuthorityID(
		TEXT("SeinARTS.Replay.Playback"));

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

	// Decode into a candidate so a failed replacement leaves the previously
	// validated replay available. The live cursor changes only on full success.
	FSeinReplay Candidate;

	const FString Resolved = ResolveReplayPath(Path);
	const int64 MaxFileBytes =
		SeinReplayFormat::PrefixBytes
		+ static_cast<int64>(SeinReplayFormat::MaxBodyBytes);
	TArray<uint8> Bytes;
	FString FileError;
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

bool USeinReplayReader::Play()
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
		WorldSub->EndReplayExclusiveCommandIngress();
		bOwnsExternalCommandIngress = false;
		UE_LOG(LogSeinNet, Log,
			TEXT("ReplayReader::Play: tick-0 replay established; simulation remains stopped."));
		return true;
	}

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

void USeinReplayReader::Stop()
{
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
		if (bOwnsExternalCommandIngress)
		{
			WorldSub->EndReplayExclusiveCommandIngress();
			bOwnsExternalCommandIngress = false;
		}
	}
	else
	{
		bOwnsExternalCommandIngress = false;
	}
	SimTickHandle.Reset();
	BoundWorldSubsystem.Reset();
	BootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();

	if (bPlaying)
	{
		UE_LOG(LogSeinNet, Log, TEXT("ReplayReader::Stop: stopped at turn-cursor %d/%d."),
			NextTurnIndex, Loaded.Turns.Num());
	}
	bPlaying = false;
}

void USeinReplayReader::HandleSimTick(int32 CompletedTick)
{
	if (!bPlaying) return;
	if (!bLoaded) return;

	USeinWorldSubsystem* WorldSub = GetWorldSubsystem();
	if (!WorldSub)
	{
		Stop();
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
		Loaded.Turns.Num());
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
