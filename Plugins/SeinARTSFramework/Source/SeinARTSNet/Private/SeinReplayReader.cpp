/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayReader.cpp
 */

#include "SeinReplayReader.h"
#include "SeinARTSNet.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
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
}

bool USeinReplayReader::LoadFromFile(const FString& Path)
{
	bLoaded = false;
	NextTurnIndex = 0;
	Loaded = FSeinReplay();

	const FString Resolved = ResolveReplayPath(Path);

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Resolved))
	{
		UE_LOG(LogSeinNet, Error, TEXT("ReplayReader: failed to load file %s"), *Resolved);
		return false;
	}

	// Mirror the writer's archive setup: FObjectAndNameAsStringProxyArchive
	// over an FMemoryReader so FInstancedStruct / UObject path strings inside
	// FSeinCommand::Payload deserialize correctly.
	FMemoryReader MemReader(Bytes, /*bIsPersistent*/ true);
	FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadIfFindFails*/ true);
	FSeinReplay::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);

	if (Reader.IsError() || Reader.IsCriticalError())
	{
		UE_LOG(LogSeinNet, Error, TEXT("ReplayReader: deserialization error reading %s"), *Resolved);
		Loaded = FSeinReplay();
		return false;
	}

	bLoaded = true;

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader: loaded %s — turns=%d  seed=%lld  map=%s  framework=%s  game=%s  recorded=%s"),
		*Resolved, Loaded.Turns.Num(), Loaded.Header.RandomSeed,
		*Loaded.Header.MapIdentifier.ToString(),
		*Loaded.Header.FrameworkVersion, *Loaded.Header.GameVersion,
		*Loaded.Header.RecordedAt.ToString());

	// Warn on map mismatch — the user might be playing the file in the wrong
	// level, which usually means entity IDs don't line up and the replay is
	// effectively garbage. We don't block; designer might be intentionally
	// running cross-level for diagnostics.
	if (UWorld* World = GetWorld())
	{
		const FString CurrentMap = World->GetMapName();
		if (!Loaded.Header.MapIdentifier.IsNone() &&
			!CurrentMap.Contains(Loaded.Header.MapIdentifier.ToString()))
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("ReplayReader: header map=%s but current world=%s — playback may produce garbage state."),
				*Loaded.Header.MapIdentifier.ToString(), *CurrentMap);
		}
	}

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

	// Seed the deterministic PRNG from the recorded header. Required for
	// PRNG-driven sim code (combat accuracy rolls, tie-breakers) to reproduce
	// the original session's outcomes.
	WorldSub->SeedSimRandom(Loaded.Header.RandomSeed);

	// Start the sim if it isn't running. Replay drives forward via
	// HandleSimTick → EnqueueCommand at turn boundaries.
	if (!WorldSub->IsSimulationRunning())
	{
		WorldSub->StartSimulation();
	}

	NextTurnIndex = 0;
	bPlaying = true;
	SimTickHandle = WorldSub->OnSimTickCompleted.AddUObject(this, &USeinReplayReader::HandleSimTick);

	UE_LOG(LogSeinNet, Log,
		TEXT("ReplayReader::Play: started — %d turn(s) queued, TicksPerTurn=%d, seed=%lld"),
		Loaded.Turns.Num(), GetTicksPerTurnFromSettings(), Loaded.Header.RandomSeed);

	return true;
}

void USeinReplayReader::Stop()
{
	if (USeinWorldSubsystem* WorldSub = GetWorldSubsystem())
	{
		if (SimTickHandle.IsValid())
		{
			WorldSub->OnSimTickCompleted.Remove(SimTickHandle);
		}
	}
	SimTickHandle.Reset();

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
	if (!WorldSub) return;

	const int32 TicksPerTurn = GetTicksPerTurnFromSettings();

	// Drain every loaded turn whose first-tick is exactly the next sim tick
	// the sim is about to run. Turn N starts at sim tick N * TicksPerTurn.
	const int32 NextTickAboutToRun = CompletedTick + 1;
	while (NextTurnIndex < Loaded.Turns.Num())
	{
		const FSeinReplayTurnRecord& Record = Loaded.Turns[NextTurnIndex];
		const int32 TurnFirstTick = Record.TurnId * TicksPerTurn;
		if (TurnFirstTick > NextTickAboutToRun) break; // not yet
		// Enqueue every command in this turn for processing on the upcoming tick.
		for (const FSeinCommand& Cmd : Record.Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		UE_LOG(LogSeinNet, Verbose,
			TEXT("ReplayReader: drained turn %d (%d cmd(s)) for tick %d."),
			Record.TurnId, Record.Commands.Num(), NextTickAboutToRun);
		++NextTurnIndex;
	}

	if (NextTurnIndex >= Loaded.Turns.Num())
	{
		UE_LOG(LogSeinNet, Log, TEXT("ReplayReader: end of replay reached at tick %d. Stopping."), CompletedTick);
		Stop();
	}
}

USeinWorldSubsystem* USeinReplayReader::GetWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}
