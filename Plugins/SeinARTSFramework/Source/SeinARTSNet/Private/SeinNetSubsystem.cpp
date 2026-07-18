/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.cpp
 */

#include "SeinNetSubsystem.h"
#include "SeinARTSNet.h"
#include "SeinNetRelay.h"
#include "SeinReplayWriter.h"
#include "SeinReplayReader.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "AI/SeinAIController.h"
#include "AI/SeinNullAIController.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include "UObject/Class.h"

namespace
{
	// Valid peers submit only a few turns ahead (input delay plus at most a
	// small scheduling skew). A deliberately generous window keeps the wire
	// extensible while preventing malformed RPCs from allocating unbounded
	// per-turn maps. This is transport hygiene, not gameplay tuning.
	constexpr int32 GSeinMaxProtocolTurnLead = 256;
	constexpr int32 GSeinRetainedHistoryTurns = 256;

	bool AreCommandBatchesIdentical(
		const TArray<FSeinCommand>& A, const TArray<FSeinCommand>& B)
	{
		if (A.Num() != B.Num()) return false;
		const UScriptStruct* CommandStruct = FSeinCommand::StaticStruct();
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!CommandStruct->CompareScriptStruct(&A[Index], &B[Index], 0))
			{
				return false;
			}
		}
		return true;
	}
}

void USeinNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &USeinNetSubsystem::OnPostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &USeinNetSubsystem::OnLogout);

	// Hook world cleanup so we can reset lockstep state between worlds. This
	// subsystem is GameInstance-scoped — it survives map travel — but the
	// per-match lockstep state (turn buffers, completed turns, last-submitted
	// counter) is only valid within ONE simulation. If a sim runs in world A
	// (e.g. an auto-started ghost-sim in a menu map) and world B is then
	// loaded, the new sim should start from a clean lockstep state. Without
	// this hook, world B's fresh sim inherits world A's `LastSubmittedTurn`,
	// `ReceivedTurns`, etc., causing instant gate stalls (sim ticks to turn N
	// in world B but turn N's data is from world A's already-completed
	// pipeline). Relay tracking (`Relays`, `RelayToSlot`, `LocalRelay`) is
	// peer-identity, not lockstep state, and is preserved.
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &USeinNetSubsystem::OnWorldCleanup);

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem initialized."));
}

void USeinNetSubsystem::Deinitialize()
{
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);

	// Flush the replay log to disk on session teardown. PIE Stop = end of
	// match for our purposes, so we always write a file. If the writer isn't
	// recording (e.g. session never started), FinishRecording is a no-op.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		const FString WrittenPath = ReplayWriter->FinishRecording();
		if (!WrittenPath.IsEmpty())
		{
			UE_LOG(LogSeinNet, Log, TEXT("Deinitialize: replay flushed -> %s"), *WrittenPath);
		}
	}
	ReplayWriter = nullptr;

	if (ReplayReader && ReplayReader->IsPlaying())
	{
		ReplayReader->Stop();
	}
	ReplayReader = nullptr;

	ResetLockstepEpochState(GetWorld());

	Relays.Reset();
	LocalRelay.Reset();
	RelayToSlot.Reset();
	AcceptedConfigFingerprints.Reset();
	SlotLifecycle.Reset();
	SlotDroppedAtTime.Reset();
	StragglerCounts.Reset();
	TurnsCompletedCount = 0;
	bDesyncDetected = false;
	LocalPlayerID = FSeinPlayerID::Neutral();
	SessionSeed = 0;

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem deinitialized."));
	Super::Deinitialize();
}

void USeinNetSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World) return;

	// Only react to OUR GameInstance's world cleanup. PIE editor worlds for
	// other GIs (sub-PIE windows, asset browser previews, etc) shouldn't
	// touch our state.
	if (World->GetGameInstance() != GetGameInstance()) return;

	const bool bHadState =
		!ServerTurnBuffer.IsEmpty() ||
		!ReceivedTurns.IsEmpty() ||
		!PendingOutgoingCommands.IsEmpty() ||
		!PendingTurnSubmissions.IsEmpty() ||
		!CompletedTurns.IsEmpty() ||
		!ServerHashReports.IsEmpty() ||
		!PendingStateHashReports.IsEmpty() ||
		!AITakeoverControllers.IsEmpty() ||
		LastSubmittedTurn != -1 ||
		LastHashReportedTurn != -1;

	ResetLockstepEpochState(World);

	if (bHadState)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("OnWorldCleanup: reset lockstep state for world %s (sessionEnded=%d)."),
			*GetNameSafe(World), bSessionEnded ? 1 : 0);
	}
}

void USeinNetSubsystem::ReleaseWorldOwnedAI(UWorld* RetiringWorld)
{
	USeinWorldSubsystem* RetiringWorldSub =
		RetiringWorld ? RetiringWorld->GetSubsystem<USeinWorldSubsystem>() : nullptr;

	for (TPair<FSeinPlayerID, TObjectPtr<USeinAIController>>& Pair : AITakeoverControllers)
	{
		USeinAIController* Controller = Pair.Value.Get();
		if (!IsValid(Controller)) continue;

		USeinWorldSubsystem* OwnerSub = Controller->WorldSubsystem.Get();
		if (IsValid(RetiringWorldSub) && OwnerSub == RetiringWorldSub)
		{
			RetiringWorldSub->UnregisterAIController(Controller);
		}
		else if (IsValid(OwnerSub) && (!RetiringWorld || OwnerSub->GetWorld() == RetiringWorld))
		{
			// Cleanup ordering can make GetSubsystem unavailable before this
			// GameInstance delegate runs. Preserve the controller lifecycle and,
			// most importantly, break its strong back-reference to the old world.
			Controller->OnUnregistered();
			Controller->WorldSubsystem = nullptr;
		}
	}

	AITakeoverControllers.Reset();
	PendingAICommands.Reset();
}

void USeinNetSubsystem::ResetLockstepEpochState(UWorld* RetiringWorld)
{
	USeinWorldSubsystem* RetiringWorldSub =
		RetiringWorld ? RetiringWorld->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (RetiringWorldSub && CachedWorldSub.Get() == RetiringWorldSub)
	{
		RetiringWorldSub->TurnReadyResolver.Unbind();
		RetiringWorldSub->TurnConsumeNotifier.Unbind();
		RetiringWorldSub->AIEmitInterceptor.Unbind();
		if (TickCompletedHandle.IsValid())
		{
			RetiringWorldSub->OnSimTickCompleted.Remove(TickCompletedHandle);
		}
	}

	TickCompletedHandle.Reset();
	CachedWorldSub.Reset();
	ReleaseWorldOwnedAI(RetiringWorld);

	ServerTurnBuffer.Reset();
	CompletedTurns.Reset();
	CompletedTurnRejectionFloor = -1;
	ReceivedTurns.Reset();
	PendingOutgoingCommands.Reset();
	PendingTurnSubmissions.Reset();
	LastQueuedTurn = -1;
	LastSubmittedTurn = -1;
	bStartSessionRequested = false;
	bServerStartRequested = false;

	ServerHashReports.Reset();
	CompletedHashChecks.Reset();
	CompletedHashRejectionFloor = -1;
	PendingStateHashReports.Reset();
	LastHashQueuedTurn = -1;
	LastHashReportedTurn = -1;

	LastStalledTurn = -1;
	FirstStalledAtTime = 0.0;
	LastStallLogTime = 0.0;
	bStallLogEscalated = false;
	IncompleteTurnDiagnostics.Reset();
}

int32 USeinNetSubsystem::GetTicksPerTurn() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || Settings->TurnRate <= 0) return 1;
	return FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate);
}

int32 USeinNetSubsystem::GetInputDelayTurns() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return (Settings && Settings->InputDelayTurns > 0) ? Settings->InputDelayTurns : 3;
}

int32 USeinNetSubsystem::GetCurrentTurn() const
{
	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	return WorldSub ? FMath::Max(0, WorldSub->GetCurrentTick() / GetTicksPerTurn()) : 0;
}

bool USeinNetSubsystem::IsCommandTurnWithinProtocolWindow(int32 TurnId, const TCHAR* Context) const
{
	if (TurnId < 0)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("%s: rejecting negative TurnId=%d."), Context, TurnId);
		return false;
	}

	const int32 CurrentTurn = GetCurrentTurn();
	if (TurnId < CurrentTurn)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting stale TurnId=%d (current=%d)."), Context, TurnId, CurrentTurn);
		return false;
	}

	const int64 MaxAccepted = static_cast<int64>(CurrentTurn) + GetInputDelayTurns() + GSeinMaxProtocolTurnLead;
	if (static_cast<int64>(TurnId) > MaxAccepted)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting implausible future TurnId=%d (current=%d max=%lld)."),
			Context, TurnId, CurrentTurn, MaxAccepted);
		return false;
	}
	return true;
}

bool USeinNetSubsystem::IsHashTurnWithinProtocolWindow(int32 Turn, const TCHAR* Context) const
{
	if (Turn < 0)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("%s: rejecting negative hash turn=%d."), Context, Turn);
		return false;
	}

	const int32 CurrentTurn = GetCurrentTurn();
	const int32 OldestAccepted = FMath::Max(0, CurrentTurn - GSeinRetainedHistoryTurns);
	const int64 MaxAccepted = static_cast<int64>(CurrentTurn) + GSeinMaxProtocolTurnLead;
	if (Turn < OldestAccepted || static_cast<int64>(Turn) > MaxAccepted)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting hash turn=%d outside [%d,%lld] (current=%d)."),
			Context, Turn, OldestAccepted, MaxAccepted, CurrentTurn);
		return false;
	}
	return true;
}

void USeinNetSubsystem::PruneProtocolState(int32 ReferenceTurn)
{
	const int32 Cutoff = ReferenceTurn - GSeinRetainedHistoryTurns;
	if (Cutoff <= -1) return;

	CompletedTurnRejectionFloor = FMath::Max(CompletedTurnRejectionFloor, Cutoff);
	for (auto It = CompletedTurns.CreateIterator(); It; ++It)
	{
		if (*It <= CompletedTurnRejectionFloor) It.RemoveCurrent();
	}
	for (auto It = ServerTurnBuffer.CreateIterator(); It; ++It)
	{
		if (It.Key() <= CompletedTurnRejectionFloor)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Server] pruning incomplete obsolete turn buffer=%d."), It.Key());
			It.RemoveCurrent();
		}
	}
	for (auto It = IncompleteTurnDiagnostics.CreateIterator(); It; ++It)
	{
		if (It.Key() <= CompletedTurnRejectionFloor) It.RemoveCurrent();
	}
	for (auto It = ReceivedTurns.CreateIterator(); It; ++It)
	{
		if (It.Key() <= CompletedTurnRejectionFloor) It.RemoveCurrent();
	}

	CompletedHashRejectionFloor = FMath::Max(CompletedHashRejectionFloor, Cutoff);
	for (auto It = CompletedHashChecks.CreateIterator(); It; ++It)
	{
		if (*It <= CompletedHashRejectionFloor) It.RemoveCurrent();
	}
	for (auto It = ServerHashReports.CreateIterator(); It; ++It)
	{
		if (It.Key() <= CompletedHashRejectionFloor)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[DETERMINISM] pruning incomplete hash check for obsolete turn=%d."), It.Key());
			It.RemoveCurrent();
		}
	}
	for (int32 Index = PendingStateHashReports.Num() - 1; Index >= 0; --Index)
	{
		if (PendingStateHashReports[Index].Turn <= CompletedHashRejectionFloor)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[DETERMINISM] discarding locally pending obsolete hash turn=%d."),
				PendingStateHashReports[Index].Turn);
			PendingStateHashReports.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
}

void USeinNetSubsystem::GetExpectedCommandSlots(TArray<FSeinPlayerID>& OutSlots) const
{
	OutSlots.Reset();
	for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& Pair : RelayToSlot)
	{
		if (Pair.Key.IsValid() && Pair.Value.IsValid())
		{
			OutSlots.AddUnique(Pair.Value);
		}
	}
	OutSlots.Sort([](const FSeinPlayerID& A, const FSeinPlayerID& B)
	{
		return A.Value < B.Value;
	});
}

bool USeinNetSubsystem::AreExpectedCommandSlotsComplete(
	const TArray<FSeinPlayerID>& ExpectedSlots,
	const TMap<FSeinPlayerID, TArray<FSeinCommand>>& Submissions)
{
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		if (!Submissions.Contains(Slot)) return false;
	}
	return true;
}

bool USeinNetSubsystem::IsCommandSubmissionLifecycleAllowed(FSeinPlayerID Slot) const
{
	const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
	return Lifecycle && *Lifecycle == ESeinSlotLifecycle::Connected;
}

USeinNetSubsystem::EFirstAcceptResult USeinNetSubsystem::BufferCommandSubmissionFirstWins(
	int32 TurnId, FSeinPlayerID Slot, TArray<FSeinCommand>&& Commands)
{
	TMap<FSeinPlayerID, TArray<FSeinCommand>>& TurnBuffer = ServerTurnBuffer.FindOrAdd(TurnId);
	if (const TArray<FSeinCommand>* Existing = TurnBuffer.Find(Slot))
	{
		return AreCommandBatchesIdentical(*Existing, Commands)
			? EFirstAcceptResult::IdenticalDuplicate
			: EFirstAcceptResult::ConflictingDuplicate;
	}

	TurnBuffer.Add(Slot, MoveTemp(Commands));
	return EFirstAcceptResult::Accepted;
}

USeinNetSubsystem::EFirstAcceptResult USeinNetSubsystem::BufferHashReportFirstWins(
	int32 Turn, FSeinPlayerID Slot, int32 Hash)
{
	TMap<FSeinPlayerID, int32>& TurnBuffer = ServerHashReports.FindOrAdd(Turn);
	if (const int32* Existing = TurnBuffer.Find(Slot))
	{
		return *Existing == Hash
			? EFirstAcceptResult::IdenticalDuplicate
			: EFirstAcceptResult::ConflictingDuplicate;
	}

	TurnBuffer.Add(Slot, Hash);
	return EFirstAcceptResult::Accepted;
}

void USeinNetSubsystem::GetExpectedHashReporterSlots(TArray<FSeinPlayerID>& OutSlots) const
{
	OutSlots.Reset();
	for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& Pair : RelayToSlot)
	{
		if (!Pair.Key.IsValid() || !Pair.Value.IsValid()) continue;
		const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Pair.Value);
		if (!Lifecycle || *Lifecycle != ESeinSlotLifecycle::Connected) continue;
		OutSlots.AddUnique(Pair.Value);
	}
	OutSlots.Sort([](const FSeinPlayerID& A, const FSeinPlayerID& B)
	{
		return A.Value < B.Value;
	});
}

bool USeinNetSubsystem::AreExpectedHashReportsComplete(const TMap<FSeinPlayerID, int32>& Reports) const
{
	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedHashReporterSlots(ExpectedSlots);
	if (ExpectedSlots.IsEmpty()) return false;
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		if (!Reports.Contains(Slot)) return false;
	}
	return true;
}

bool USeinNetSubsystem::IsDeterminismGossipEnabled() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bDeterminismChecksEnabled;
}

bool USeinNetSubsystem::IsConfigParityCheckEnabled() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bConfigParityCheckEnabled;
}

int32 USeinNetSubsystem::GetDeterminismCheckIntervalTurns() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return (Settings && Settings->DeterminismCheckIntervalTurns > 0) ? Settings->DeterminismCheckIntervalTurns : 10;
}

bool USeinNetSubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer;
}

bool USeinNetSubsystem::IsDedicatedAuthority() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDedicatedAuthorityOverride.IsSet())
	{
		return TestDedicatedAuthorityOverride.GetValue();
	}
#endif
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

bool USeinNetSubsystem::IsNetworkingActive() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetNetMode() == NM_Standalone) return false;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bNetworkingEnabled;
}

void USeinNetSubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer) return;
	if (!IsServer()) return;

	UWorld* World = GameMode->GetWorld();
	if (!World) return;
	if (World->GetGameInstance() != GetGameInstance())
	{
		// Some other GI's GameMode (e.g. a sub-PIE world) — ignore.
		return;
	}

	// Phase 3a: relay spawn moved into ServerSpawnRelayForController, which
	// is invoked by ASeinGameMode::HandleStartingNewPlayer AFTER it binds
	// the controller's slot. This eliminates the dual-source-of-truth bug
	// where the old auto-spawn-here path independently sequenced slots
	// (NextSlotToAssign++) while GameMode independently picked from match
	// settings — they could disagree if connection order ≠ slot order.
	//
	// We still ensure the session seed is locked at first PostLogin so
	// that whenever the relay spawns, the seed is already stable.
	EnsureSessionSeed();

	UE_LOG(LogSeinNet, Verbose, TEXT("OnPostLogin: PC=%s noted (relay spawn deferred to GameMode binding flow)."),
		*GetNameSafe(NewPlayer));
}

void USeinNetSubsystem::ServerSpawnRelayForController(APlayerController* PC, FSeinPlayerID Slot)
{
	if (!IsServer()) return;
	if (!PC)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: null PC."));
		return;
	}
	if (!Slot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: invalid slot for PC %s — no relay spawned (legacy auto-assign path?)."),
			*GetNameSafe(PC));
		return;
	}

	UWorld* World = PC->GetWorld();
	if (!World) return;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->bNetworkingEnabled)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("ServerSpawnRelayForController: networking disabled — skipping."));
		return;
	}

	// SLOT COLLISION GUARD: refuse to bind `Slot` to this PC if a DIFFERENT PC
	// already owns a relay with that slot. Without this guard, GameMode bugs
	// that double-route two PCs to the same SeinPlayerStart (the visual spawn
	// bug + the [BUFFER INCOMPLETE missing=[3,3]] freeze bug) silently produce
	// two relays with AssignedPlayerID=N, the server's per-turn TMap<Slot,...>
	// buffer gets one entry per duplicated slot (overwritten), and the missing
	// slot (the one that should have been bound to the second PC) never
	// submits — gate stalls forever. Caller (GameMode) must fix its slot pick.
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		ASeinNetRelay* R = Wp.Get();
		if (!R) continue;
		if (R->GetOwner() == PC) continue; // same PC → handled by idempotence below
		const FSeinPlayerID* ExistingSlot = RelayToSlot.Find(R);
		if (ExistingSlot && *ExistingSlot == Slot)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[SLOT COLLISION] ServerSpawnRelayForController: slot %u is ALREADY bound to %s (relay %s). Refusing to bind %s to the same slot — would produce dual-binding and freeze the lockstep gate. GameMode bug: two controllers were routed to the same SeinPlayerStart. Investigate ChoosePlayerStart_Implementation and ClaimedSlots tracking."),
				Slot.Value, *GetNameSafe(R->GetOwner()), *GetNameSafe(R), *GetNameSafe(PC));
			return;
		}
	}

	// Idempotence: if this PC already has a relay (re-bind / seamless travel),
	// re-stamp it instead of spawning a duplicate. Replicates the new slot
	// value to the owning client via OnRep_AssignedPlayerID.
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* R = Wp.Get())
		{
			if (R->GetOwner() == PC)
			{
				const FSeinPlayerID* PrevSlot = RelayToSlot.Find(R);
				const uint8 PrevSlotValue = PrevSlot ? PrevSlot->Value : 0;
				if (PrevSlot && *PrevSlot != Slot)
				{
					int32 AcceptedFingerprint = 0;
					const bool bHadAcceptedFingerprint =
						AcceptedConfigFingerprints.RemoveAndCopyValue(*PrevSlot, AcceptedFingerprint);
					AcceptedConfigFingerprints.Remove(Slot);
					if (bHadAcceptedFingerprint)
					{
						// Acceptance belongs to this relay process, not the incidental
						// lobby slot number. Preserve it across a same-relay rebind.
						AcceptedConfigFingerprints.Add(Slot, AcceptedFingerprint);
					}
				}
				R->AssignedPlayerID = Slot;
				R->SessionSeed = SessionSeed;
				RelayToSlot.Add(R, Slot);
				UE_LOG(LogSeinNet, Log,
					TEXT("ServerSpawnRelayForController: existing relay %s re-stamped slot=%u (was %u) for %s"),
					*GetNameSafe(R), Slot.Value, PrevSlotValue, *GetNameSafe(PC));
				return;
			}
		}
	}

	// WYSIWYG. None/empty => the net relay is OFF: spawn nothing, so this controller gets no relay and
	// lockstep traffic can't flow. A set-but-unloadable class is a mistake, not an off-switch: fall
	// back to the shipped default with a logged error.
	if (Settings->RelayActorClass.IsNull())
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Net Relay"),
			TEXT("No relay is spawned, so lockstep networking can't send or receive commands."), /*bHighSeverity*/ true);
		return;
	}
	UClass* RelayClass = Settings->RelayActorClass.TryLoadClass<ASeinNetRelay>();
	if (!RelayClass)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("RelayActorClass '%s' could not be loaded — falling back to the shipped default."),
			*Settings->RelayActorClass.ToString());
		RelayClass = ASeinNetRelay::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// Deferred spawn so we can stamp AssignedPlayerID + SessionSeed BEFORE
	// BeginPlay runs (and before initial replication ships). On a Listen
	// Server, BeginPlay fires synchronously inside SpawnActor — if we stamp
	// after, RegisterRelay sees a zero slot and the host's LocalPlayerID
	// never latches (server-side has no OnRep callback).
	Params.bDeferConstruction = true;

	ASeinNetRelay* Relay = World->SpawnActor<ASeinNetRelay>(RelayClass, FTransform::Identity, Params);
	if (!Relay)
	{
		UE_LOG(LogSeinNet, Error, TEXT("ServerSpawnRelayForController: failed to spawn relay for %s slot=%u"),
			*GetNameSafe(PC), Slot.Value);
		return;
	}

	EnsureSessionSeed();
	Relay->AssignedPlayerID = Slot;
	Relay->SessionSeed = SessionSeed;
	// A genuinely new relay occupant must prove parity itself; never inherit
	// an earlier process's acceptance merely because the slot was reused.
	AcceptedConfigFingerprints.Remove(Slot);
	RelayToSlot.Add(Relay, Slot);

	// Drop-in/drop-out: mark this slot Connected. If it was previously
	// Dropped (a reconnect), this also clears the heartbeat-injection.
	// If it was AITakeover (reconnect after the grace period elapsed and
	// the framework auto-spawned an AI), tear down the AI so it doesn't
	// fight the now-live human player for command authorship.
	const ESeinSlotLifecycle* Prior = SlotLifecycle.Find(Slot);
	const bool bWasAITakeover = Prior && *Prior == ESeinSlotLifecycle::AITakeover;
	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Connected);
	SlotDroppedAtTime.Remove(Slot);
	if (bWasAITakeover)
	{
		TeardownAIForSlot(Slot);
	}

	Relay->FinishSpawning(FTransform::Identity);

	UE_LOG(LogSeinNet, Log, TEXT("ServerSpawnRelayForController: spawned %s for %s  slot=%u  seed=%lld  lifecycle=Connected"),
		*GetNameSafe(Relay), *GetNameSafe(PC), Slot.Value, SessionSeed);
}

void USeinNetSubsystem::EnsureSessionSeed()
{
	if (SessionSeed != 0) return;

	// Debug override: when settings.DebugFixedSessionSeed is nonzero, lock the
	// seed to that exact value so successive PIE Plays produce bit-identical
	// sim runs (any PRNG-driven variance disappears, leaving only true
	// determinism bugs visible). Production builds leave it at 0 and the
	// random path runs.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && Settings->DebugFixedSessionSeed != 0)
	{
		SessionSeed = Settings->DebugFixedSessionSeed;
		UE_LOG(LogSeinNet, Warning, TEXT("EnsureSessionSeed: using DebugFixedSessionSeed=%lld (DEBUG; clear in production)."), SessionSeed);
		return;
	}

	// Server-generated random seed combining wall clock + cycle counter. The
	// lockstep invariant only requires that ALL clients agree on the seed
	// before tick 0 — how it was generated is private to the host.
	const int64 NowCycles = (int64)FPlatformTime::Cycles64();
	const int64 NowTicks = FDateTime::UtcNow().GetTicks();
	SessionSeed = (NowCycles ^ NowTicks);
	if (SessionSeed == 0) SessionSeed = 1; // 0 reads as "uninitialized" elsewhere.
	UE_LOG(LogSeinNet, Log, TEXT("EnsureSessionSeed: generated SessionSeed=%lld"), SessionSeed);
}

void USeinNetSubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!Exiting || !IsServer()) return;

	// Drop-in/drop-out (Phase 4): instead of destroying the relay on logout,
	// mark the owning slot as Dropped + retain the relay actor. The server
	// will inject empty heartbeats on the slot's behalf so the gate doesn't
	// stall, and (after timeout) transition the slot to AITakeover. If the
	// player reconnects, the slot returns to Connected and the relay resumes
	// normally.
	//
	// PRIOR BEHAVIOR (pre-Phase-4): destroy relay + drop slot from RelayToSlot
	// so the gate ignores the leaving player. Worked but provided no
	// reconnect/AI-takeover affordance. Old code preserved as commentary at
	// the bottom for reference.

	const double NowSec = FPlatformTime::Seconds();
	bool bAnyMarkedDropped = false;
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		ASeinNetRelay* Relay = Wp.Get();
		if (!Relay || Relay->GetOwner() != Exiting) continue;

		const FSeinPlayerID Slot = Relay->AssignedPlayerID;
		if (!Slot.IsValid()) continue;

		SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Dropped);
		SlotDroppedAtTime.Add(Slot, NowSec);
		bAnyMarkedDropped = true;

		UE_LOG(LogSeinNet, Log,
			TEXT("OnLogout: slot %u marked DROPPED (owner=%s left). Server will inject heartbeats; AI takeover scheduled in %.1fs."),
			Slot.Value, *GetNameSafe(Exiting), GetDroppedToAITakeoverSeconds());
	}

	// Re-check open turns — the just-dropped slot might be the one we were
	// waiting on. Inject a heartbeat immediately for the most recent open
	// turn so the gate completes.
	if (bAnyMarkedDropped && !ServerTurnBuffer.IsEmpty())
	{
		TArray<int32> OpenTurns;
		ServerTurnBuffer.GetKeys(OpenTurns);
		for (int32 TurnId : OpenTurns)
		{
			InjectDroppedSlotHeartbeats(TurnId, /*bAllowAICommands=*/false);
			ServerCheckTurnComplete(TurnId);
		}
	}

	// Hash gossip compares live simulation peers, not occupied gameplay slots.
	// A dropped slot still receives command heartbeats from the server, but its
	// vanished process can no longer report hashes. Re-evaluate outstanding
	// checks against the remaining connected reporter set so they neither
	// wedge nor accumulate forever.
	if (bAnyMarkedDropped && !ServerHashReports.IsEmpty())
	{
		TArray<int32> PendingTurns;
		ServerHashReports.GetKeys(PendingTurns);
		for (const int32 Turn : PendingTurns)
		{
			const TMap<FSeinPlayerID, int32>* Reports = ServerHashReports.Find(Turn);
			if (Reports && AreExpectedHashReportsComplete(*Reports))
			{
				ServerCompareHashesForTurn(Turn);
			}
		}
	}

	// A pre-start parity request may have been waiting on this process. Once
	// its slot is no longer Connected it is no longer a start participant.
	if (bAnyMarkedDropped)
	{
		TryDispatchLockstepSessionStart();
	}
}

void USeinNetSubsystem::RegisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	Relays.AddUnique(Relay);

	// On the client side (and on the host's own PC), latch the local relay.
	// Identification: the relay's owner is a PlayerController whose IsLocalController() is true.
	if (APlayerController* PC = Cast<APlayerController>(Relay->GetOwner()))
	{
		if (PC->IsLocalController())
		{
			LocalRelay = Relay;
			UE_LOG(LogSeinNet, Log, TEXT("RegisterRelay: latched LOCAL relay %s (PC=%s)"),
				*GetNameSafe(Relay), *GetNameSafe(PC));

			// On a Listen Server, AssignedPlayerID + SessionSeed were stamped
			// before FinishSpawning (deferred-spawn path in OnPostLogin), so
			// they're valid here. OnRep_AssignedPlayerID does NOT fire on the
			// server, so without this eager latch the host's LocalPlayerID
			// would stay zero. On a remote client, AssignedPlayerID is still
			// neutral here (initial rep hasn't arrived yet) — OnRep latches it.
			if (Relay->AssignedPlayerID.IsValid())
			{
				NotifyLocalSlotAssigned(Relay, Relay->AssignedPlayerID, Relay->SessionSeed);
			}
		}
	}
}

void USeinNetSubsystem::UnregisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	const FSeinPlayerID RemovedSlot = RelayToSlot.FindRef(Relay);
	const ESeinSlotLifecycle* RemovedLifecycle = SlotLifecycle.Find(RemovedSlot);
	const bool bRemovedConnectedParticipant =
		RemovedSlot.IsValid() && RemovedLifecycle &&
		*RemovedLifecycle == ESeinSlotLifecycle::Connected;
	Relays.RemoveAllSwap([Relay](const TWeakObjectPtr<ASeinNetRelay>& Wp) { return Wp.Get() == Relay; });
	RelayToSlot.Remove(Relay);
	if (LocalRelay.Get() == Relay)
	{
		LocalRelay.Reset();
		// Note: keep LocalPlayerID + SessionSeed cached. A relay swap (e.g.
		// seamless travel) would re-stamp them; clearing here would briefly
		// flash a "no slot" state for any UI binding to GetLocalPlayerID.
	}

	// EndPlay can unregister a relay before GameMode's Logout delegate finds
	// its owner. Removing RelayToSlot has already shrunk the connected parity
	// set, so retry a deferred start now. Do not do this during world teardown:
	// that request belongs to the retiring epoch and ResetLockstepEpochState
	// will discard it. Acceptance/lifecycle are deliberately preserved for
	// same-match travel; a genuinely new relay occupant clears acceptance in
	// ServerSpawnRelayForController before it can satisfy the barrier.
	UWorld* RelayWorld = Relay->GetWorld();
	if (bRemovedConnectedParticipant && RelayWorld && !RelayWorld->bIsTearingDown)
	{
		TryDispatchLockstepSessionStart();
	}
}

USeinWorldSubsystem* USeinNetSubsystem::BindLockstepHooksForCurrentWorld()
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return nullptr;

	const bool bWorldChanged = CachedWorldSub.Get() != WorldSub;
	if (bWorldChanged)
	{
		if (USeinWorldSubsystem* Previous = CachedWorldSub.Get())
		{
			Previous->TurnReadyResolver.Unbind();
			Previous->TurnConsumeNotifier.Unbind();
			Previous->AIEmitInterceptor.Unbind();
			if (TickCompletedHandle.IsValid())
			{
				Previous->OnSimTickCompleted.Remove(TickCompletedHandle);
			}
		}
		TickCompletedHandle.Reset();
		CachedWorldSub = WorldSub;
	}

	TWeakObjectPtr<USeinNetSubsystem> WeakSelf(this);
	WorldSub->TurnReadyResolver.BindLambda([WeakSelf](int32 Turn)
	{
		USeinNetSubsystem* Self = WeakSelf.Get();
		return Self ? Self->ResolveTurnReady(Turn) : true;
	});
	WorldSub->TurnConsumeNotifier.BindLambda([WeakSelf](int32 Turn)
	{
		if (USeinNetSubsystem* Self = WeakSelf.Get())
		{
			Self->ConsumeTurn(Turn);
		}
	});
	WorldSub->AIEmitInterceptor.BindLambda([WeakSelf](FSeinPlayerID Slot, const FSeinCommand& Cmd) -> bool
	{
		USeinNetSubsystem* Self = WeakSelf.Get();
		return Self ? Self->HandleAIEmit(Slot, Cmd) : false;
	});

	if (!TickCompletedHandle.IsValid())
	{
		TickCompletedHandle = WorldSub->OnSimTickCompleted.AddUObject(this, &USeinNetSubsystem::OnSimTickCompleted);
	}

	if (bWorldChanged)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("BindLockstepHooksForCurrentWorld: bound %s (TicksPerTurn=%d InputDelay=%d)."),
			*GetNameSafe(WorldSub), GetTicksPerTurn(), GetInputDelayTurns());
	}
	return WorldSub;
}

bool USeinNetSubsystem::AreNetworkStartPrerequisitesReady(bool bHooksReady) const
{
	const bool bHasLocalParticipant =
		IsDedicatedAuthority() || (LocalRelay.IsValid() && LocalPlayerID.IsValid());
	return SessionSeed != 0 && bHooksReady && bHasLocalParticipant;
}

bool USeinNetSubsystem::AreConfigFingerprintsComplete(
	const TArray<FSeinPlayerID>& ExpectedSlots,
	const TMap<FSeinPlayerID, int32>& AcceptedFingerprints,
	int32 RequiredFingerprint)
{
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		const int32* Accepted = AcceptedFingerprints.Find(Slot);
		if (!Accepted || *Accepted != RequiredFingerprint)
		{
			return false;
		}
	}
	return true;
}

bool USeinNetSubsystem::IsConfigParityStartBarrierSatisfied(
	TArray<FSeinPlayerID>* OutMissingSlots) const
{
	if (OutMissingSlots) OutMissingSlots->Reset();
	if (!IsConfigParityCheckEnabled()) return true;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return false;
	const int32 RequiredFingerprint = Settings->ComputeConfigFingerprint();

	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedHashReporterSlots(ExpectedSlots);
	if (OutMissingSlots)
	{
		for (const FSeinPlayerID Slot : ExpectedSlots)
		{
			const int32* Accepted = AcceptedConfigFingerprints.Find(Slot);
			if (!Accepted || *Accepted != RequiredFingerprint)
			{
				OutMissingSlots->Add(Slot);
			}
		}
	}

	return AreConfigFingerprintsComplete(
		ExpectedSlots, AcceptedConfigFingerprints, RequiredFingerprint);
}

void USeinNetSubsystem::NotifyLocalSlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot, int64 Seed)
{
	if (!Relay) return;
	if (LocalRelay.Get() != Relay)
	{
		// OnRep can fire before RegisterRelay(BeginPlay) has run on the client.
		// Latch LocalRelay here too — RegisterRelay is idempotent on the
		// AddUnique path.
		LocalRelay = Relay;
	}
	const bool bSlotChanged = (LocalPlayerID != Slot);
	LocalPlayerID = Slot;
	SessionSeed = Seed;
	UE_LOG(LogSeinNet, Log, TEXT("NotifyLocalSlotAssigned: LocalPlayerID=%u  SessionSeed=%lld"),
		LocalPlayerID.Value, SessionSeed);

	// Fire local-slot delegates so lobby ViewModel + designer Widget BPs
	// don't have to poll. Only on actual changes — repeated calls with the
	// same slot are silent.
	if (bSlotChanged)
	{
		OnLocalSlotChanged.Broadcast(LocalPlayerID);
		OnLocalSlotChangedBP.Broadcast(LocalPlayerID);
	}

	// Bind before simulation start so the first tick is gated. This shared path
	// is also called directly by authority startup; dedicated servers never
	// receive a local-slot notification.
	BindLockstepHooksForCurrentWorld();

	// Every networked participant reports, regardless of its LOCAL parity
	// preference. Enforcement is a host policy: otherwise a client could turn
	// its own check off, send nothing, and bypass or indefinitely wedge the
	// host's pre-start barrier. The listen host's relay round-trips harmlessly.
	if (IsNetworkingActive() && LocalPlayerID.IsValid() && Relay)
	{
		if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
		{
			Relay->Server_ReportConfigFingerprint(Settings->ComputeConfigFingerprint());
		}
	}

	// Relay replacement/late assignment is the retry edge for locally frozen
	// work. Flush exact batches/checkpoints before honoring a deferred start.
	FlushPendingTurnSubmissions();
	FlushPendingStateHashReports();
	TryStartLocalSession();
}

void USeinNetSubsystem::StartLocalSession()
{
	bStartSessionRequested = true;
	TryStartLocalSession();
}

void USeinNetSubsystem::TryStartLocalSession()
{
	if (!bStartSessionRequested) return;

	UWorld* World = GetWorld();
	const bool bNetworked = IsNetworkingActive();
	if (bNetworked && IsServer())
	{
		EnsureSessionSeed();
	}
	USeinWorldSubsystem* WorldSub = bNetworked
		? BindLockstepHooksForCurrentWorld()
		: (World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr);
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("TryStartLocalSession: waiting for USeinWorldSubsystem."));
		return;
	}
	if (WorldSub->IsSimulationRunning())
	{
		bStartSessionRequested = false;
		UE_LOG(LogSeinNet, Verbose, TEXT("TryStartLocalSession: sim already running — request satisfied."));
		return;
	}

	if (bNetworked)
	{
		const bool bHooksReady =
			CachedWorldSub.Get() == WorldSub &&
			TickCompletedHandle.IsValid() &&
			WorldSub->TurnReadyResolver.IsBound() &&
			WorldSub->TurnConsumeNotifier.IsBound() &&
			WorldSub->AIEmitInterceptor.IsBound();
		if (!AreNetworkStartPrerequisitesReady(bHooksReady))
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("TryStartLocalSession: deferred (Dedicated=%d Relay=%d Slot=%u SeedReady=%d Hooks=%d)."),
				IsDedicatedAuthority() ? 1 : 0, LocalRelay.IsValid() ? 1 : 0,
				LocalPlayerID.Value, SessionSeed != 0 ? 1 : 0, bHooksReady ? 1 : 0);
			return;
		}
	}

	bStartSessionRequested = false;
	// Seed the deterministic PRNG with the session seed BEFORE the sim starts.
	// Every peer (host + clients) calls this with the same SessionSeed (delivered
	// via the relay's replicated SessionSeed property), so cross-peer rolls
	// produce identical results from tick 0. Without this, accuracy / damage /
	// any PRNG-driven sim code diverges across machines from the first call.
	if (SessionSeed != 0)
	{
		WorldSub->SeedSimRandom(SessionSeed);
	}

	UE_LOG(LogSeinNet, Log, TEXT("TryStartLocalSession: starting sim at tick 0 with gate engaged."));
	WorldSub->StartSimulation();

	// Pre-fire the first non-grace heartbeat (turn = InputDelayTurns).
	//
	// Why: configs with `InputDelayTurns × TicksPerTurn < 2` (only TPT=1 +
	// ID=1 today, but any future config that lands here) have zero grace
	// ticks before the first gated boundary — without seeding turn
	// InputDelayTurns here, the sim hits the gate at tick 1 with nothing
	// having been submitted yet, sim stalls indefinitely. For commoner
	// configs (TPT≥2 or ID≥2) OnSimTickCompleted handles it, but pre-firing
	// is harmless — server's per-slot map dedupes via Add().
	//
	// CRITICAL: only advance LastSubmittedTurn if the submission was
	// ACTUALLY sent. Pre-fire can be silently dropped if LocalRelay is null
	// at this exact moment (race: OnRep_AssignedPlayerID hasn't fired
	// before Client_StartSession arrives — possible because both are
	// replication-channel events delivered together, ordering is "usually
	// OnRep first" but not contractually guaranteed). If we advance
	// LastSubmittedTurn unconditionally, the catch-up loop in
	// OnSimTickCompleted sees the turn as "already submitted" and skips
	// it — server never gets that client's turn 2 → all peers stall
	// forever waiting for the missing slot. Caused intermittent freeze
	// at tick 5 (= last grace-period tick for TPT=3+ID=2).
	if (LocalPlayerID.IsValid() && bNetworked)
	{
		const int32 InputDelay = GetInputDelayTurns();
		if (LastQueuedTurn < InputDelay)
		{
			UE_LOG(LogSeinNet, Verbose, TEXT("TryStartLocalSession: queueing heartbeat for turn %d (gate seed)."), InputDelay);
			QueueTurnSubmissionsThrough(InputDelay, /*bAttachCurrentCommands=*/false);
			FlushPendingTurnSubmissions();
		}
	}
}

void USeinNetSubsystem::StartLockstepSession()
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("StartLockstepSession: callable only on server — ignored."));
		return;
	}

	bServerStartRequested = true;
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::TryDispatchLockstepSessionStart()
{
	if (!bServerStartRequested || !IsServer()) return;

	TArray<FSeinPlayerID> MissingSlots;
	if (!IsConfigParityStartBarrierSatisfied(&MissingSlots))
	{
		TArray<FString> MissingNames;
		MissingNames.Reserve(MissingSlots.Num());
		for (const FSeinPlayerID Slot : MissingSlots)
		{
			MissingNames.Add(FString::Printf(TEXT("%u"), Slot.Value));
		}
		UE_LOG(LogSeinNet, Log,
			TEXT("StartLockstepSession: deferred by config parity barrier; awaiting accepted fingerprint from connected slot(s) [%s]."),
			*FString::Join(MissingNames, TEXT(",")));
		return;
	}

	// Consume before fan-out. Any repeated request is explicitly idempotent
	// through StartLocalSession, while a later reconnect must not replay this
	// particular deferred request.
	bServerStartRequested = false;

	// A dedicated authority may have no local controller and therefore no
	// PostLogin-driven local relay path. Establish its seed and world hooks
	// explicitly before replay capture, client fan-out, or tick zero.
	EnsureSessionSeed();
	BindLockstepHooksForCurrentWorld();

	UE_LOG(LogSeinNet, Log, TEXT("StartLockstepSession: firing Client_StartSession on %d relay(s) and starting local sim."),
		Relays.Num());

	// Open the replay writer BEFORE fanning out so RecordTurn captures every
	// turn from the very first one. Header uses the framework's existing
	// FSeinReplayHeader (SeinReplayHeader.h) — RandomSeed = SessionSeed so
	// the deterministic PRNG seed is captured for replay re-runs.
	if (!ReplayWriter)
	{
		ReplayWriter = NewObject<USeinReplayWriter>(this);
	}
	if (ReplayWriter && !ReplayWriter->IsRecording())
	{
		FSeinReplayHeader Header;
		Header.FrameworkVersion = TEXT("0.1.0");
		Header.GameVersion      = TEXT("unset");
		Header.MapIdentifier    = GetWorld() ? FName(*GetWorld()->GetMapName()) : NAME_None;
		Header.RandomSeed       = SessionSeed;
		// SettingsSnapshot + Players left default for now — Phase 4b can
		// populate from match-flow once lobby is in. Reader compares against
		// current sim state and warns on mismatch.
		Header.StartTick        = 0;
		Header.EndTick          = 0; // updated on FinishRecording if needed
		Header.RecordedAt       = FDateTime::UtcNow();
		ReplayWriter->StartRecording(Header);
	}

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Target = Wp.Get())
		{
			Target->Client_StartSession();
		}
	}

	// Server's own sim. Local-call so the host doesn't depend on receiving
	// its own Client RPC (which it would, but explicit is safer + cheaper).
	StartLocalSession();
}

bool USeinNetSubsystem::ResolveTurnReady(int32 Turn)
{
	// Grace period: the first InputDelayTurns turns can never have submissions
	// (no input could have been issued before sim started), so unconditionally
	// pass them. This matches the heartbeat schedule below — first heartbeat
	// is for turn `0 + InputDelay`, fired after sim's first turn worth of
	// ticks complete, so turn `InputDelay` is the first gated turn.
	if (Turn < GetInputDelayTurns()) return true;

	const bool bReady = ReceivedTurns.Contains(Turn);
	if (!bReady)
	{
		// Per-tick stall noise — Verbose.
		UE_LOG(LogSeinNet, Verbose, TEXT("ResolveTurnReady: turn %d NOT ready — sim stalls."), Turn);

		// Persistent-stall escalation: most "not ready" hits are transient
		// pipeline blips (one peer's heartbeat is in flight). Only escalate
		// to Log level when the SAME turn has been stuck for ≥2 seconds —
		// that's a real stall, worth surfacing without verbose. Transient
		// blips stay at Verbose.
		const double NowSec = FPlatformTime::Seconds();
		if (Turn != LastStalledTurn)
		{
			// New unready turn — start the timer, log at Verbose.
			LastStalledTurn = Turn;
			FirstStalledAtTime = NowSec;
			LastStallLogTime = NowSec;
			bStallLogEscalated = false;
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[GATE STALL transient] turn=%d not ready (waiting for fan-out)."),
				Turn);
		}
		else
		{
			const double StalledFor = NowSec - FirstStalledAtTime;
			if (StalledFor >= 2.0)
			{
				// Persistent stall — log at Log level, but rate-limit to
				// once every 2 seconds so the log stays readable on a
				// long-frozen sim.
				if (!bStallLogEscalated || (NowSec - LastStallLogTime) >= 2.0)
				{
					UE_LOG(LogSeinNet, Log,
						TEXT("[GATE STALL persistent] turn=%d not ready for %.1fs  LocalSlot=%u  ReceivedTurns=%d entries  PendingOutgoing=%d  LastSubmittedTurn=%d. Sim is frozen — one peer's heartbeat dropped."),
						Turn, StalledFor, LocalPlayerID.Value, ReceivedTurns.Num(), PendingOutgoingCommands.Num(), LastSubmittedTurn);
					LastStallLogTime = NowSec;
					bStallLogEscalated = true;
				}
			}
		}
	}
	else
	{
		// Turn became ready — reset the persistence tracker so the next
		// transient blip starts fresh.
		if (Turn == LastStalledTurn)
		{
			LastStalledTurn = -1;
			FirstStalledAtTime = 0.0;
			bStallLogEscalated = false;
		}
	}
	return bReady;
}

void USeinNetSubsystem::ConsumeTurn(int32 Turn)
{
	TArray<FSeinCommand> Drained;
	if (!ReceivedTurns.RemoveAndCopyValue(Turn, Drained))
	{
		// Grace turn (no entry stored) or already drained — nothing to do.
		return;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return;

	for (const FSeinCommand& Cmd : Drained)
	{
		WorldSub->EnqueueCommand(Cmd);
	}
	UE_LOG(LogSeinNet, Verbose, TEXT("ConsumeTurn: drained turn %d (%d cmd(s)) into PendingCommands."),
		Turn, Drained.Num());
}

void USeinNetSubsystem::OnSimTickCompleted(int32 CompletedTick)
{
	if (!IsNetworkingActive()) return;

	const int32 TicksPerTurn = GetTicksPerTurn();
	const int32 NextTick = CompletedTick + 1;

	// Fire only on turn boundaries — i.e., the tick we just completed was the
	// last tick of a turn (so the next tick starts a new turn).
	if (NextTick % TicksPerTurn != 0) return;

	// Compute outgoing turn: commands accumulated during the turn we just
	// finished apply at `current_turn + InputDelay`. Idempotent guard against
	// multiple OnSimTickCompleted fires within the same boundary.
	const int32 JustFinishedTurn = CompletedTick / TicksPerTurn;
	const int32 InputDelay = GetInputDelayTurns();
	const int32 OutgoingTurn = JustFinishedTurn + InputDelay;

	// Only a process with a local gameplay slot authors the ordinary per-turn
	// submission and hash report. Dedicated authority still executes all
	// server maintenance below; it has no synthetic local relay or slot.
	if (LocalPlayerID.IsValid())
	{
		// Catch up any skipped non-grace turns with heartbeats, attaching user
		// commands only to the latest outgoing turn.
		if (OutgoingTurn > LastSubmittedTurn)
		{
			QueueTurnSubmissionsThrough(OutgoingTurn, /*bAttachCurrentCommands=*/true);
			FlushPendingTurnSubmissions();
		}

		// Authority hash participation without a local player identity is a
		// separate protocol decision (Gate A), so dedicated servers do not
		// synthesize a reporter here.
		MaybeSubmitStateHashCheck(JustFinishedTurn);
	}

	PruneProtocolState(JustFinishedTurn);

	// Drop-in/drop-out (Phase 4): server-only per-turn polling. Inject
	// heartbeats for dropped slots so the gate doesn't stall, then evaluate
	// whether any dropped slot has timed out into AI takeover. Cheap walks
	// over a small map; safe per-turn cost.
	if (IsServer())
	{
		// Heartbeats target the OUTGOING turn (= JustFinishedTurn + InputDelay).
		// That's the turn the next gate completion will need a slot from.
		InjectDroppedSlotHeartbeats(OutgoingTurn, /*bAllowAICommands=*/true);
		ServerCheckTurnComplete(OutgoingTurn);
		EvaluateDroppedSlots();
	}
}

void USeinNetSubsystem::QueueTurnSubmissionsThrough(int32 FinalTurn, bool bAttachCurrentCommands)
{
	const int32 InputDelay = GetInputDelayTurns();
	const int32 FirstTurn = FMath::Max3(LastQueuedTurn + 1, LastSubmittedTurn + 1, InputDelay);
	if (FirstTurn > FinalTurn) return;

	for (int32 Turn = FirstTurn; Turn <= FinalTurn; ++Turn)
	{
		FSeinPendingTurnSubmission& Pending = PendingTurnSubmissions.Emplace_GetRef();
		Pending.TurnId = Turn;
		if (bAttachCurrentCommands && Turn == FinalTurn)
		{
			Pending.Commands = MoveTemp(PendingOutgoingCommands);
			PendingOutgoingCommands.Reset();
		}
	}
	LastQueuedTurn = FinalTurn;
}

void USeinNetSubsystem::FlushPendingTurnSubmissions()
{
	while (!PendingTurnSubmissions.IsEmpty())
	{
		const FSeinPendingTurnSubmission& Pending = PendingTurnSubmissions[0];
		bool bSent = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (TestTurnSubmitOverride)
		{
			bSent = TestTurnSubmitOverride(Pending.TurnId, Pending.Commands);
		}
		else
#endif
		{
			bSent = SubmitLocalCommandsAtTurn(Pending.TurnId, Pending.Commands);
		}

		if (!bSent)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("FlushPendingTurnSubmissions: retaining turn=%d cmds=%d for retry."),
				Pending.TurnId, Pending.Commands.Num());
			return;
		}

		UE_LOG(LogSeinNet, Verbose,
			TEXT("FlushPendingTurnSubmissions: handed off turn=%d cmds=%d heartbeat=%d."),
			Pending.TurnId, Pending.Commands.Num(), Pending.Commands.IsEmpty() ? 1 : 0);
		LastSubmittedTurn = FMath::Max(LastSubmittedTurn, Pending.TurnId);
		PendingTurnSubmissions.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void USeinNetSubsystem::SubmitLocalCommand(const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	SubmitLocalCommands(Single);
}

void USeinNetSubsystem::SubmitLocalCommands(const TArray<FSeinCommand>& Commands)
{
	if (Commands.Num() == 0) return;

	// Phase 4 polish: fire the immediate-feedback delegate FIRST, before any
	// queueing or network routing. Designers can bind audio/visual cues here
	// to mask the InputDelayTurns roundtrip.
	for (const FSeinCommand& Cmd : Commands)
	{
		OnLocalCommandIssued.Broadcast(Cmd);
		OnLocalCommandIssuedBP.Broadcast(Cmd);
	}

	// Standalone path: bypass everything and drop straight into the local
	// sim's command buffer. Single-player is zero-network-overhead.
	if (!IsNetworkingActive())
	{
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommands [Standalone]: no USeinWorldSubsystem — dropping %d cmd(s)."), Commands.Num());
			return;
		}
		for (const FSeinCommand& Cmd : Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		return;
	}

	// Networked path (Phase 2b): accumulate in PendingOutgoingCommands. The
	// OnSimTickCompleted handler flushes them at the next turn boundary,
	// stamped with the correct outgoing TurnId. This guarantees a single
	// per-turn submission per client (commands + heartbeat in one packet),
	// which is what the server's gate needs to complete deterministically.
	PendingOutgoingCommands.Append(Commands);
	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommands: buffered %d cmd(s) (pending=%d) for next turn flush."),
		Commands.Num(), PendingOutgoingCommands.Num());
}

bool USeinNetSubsystem::SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	return SubmitLocalCommandsAtTurn(TurnId, Single);
}

bool USeinNetSubsystem::SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	// NOTE: empty `Commands` is intentionally allowed — the heartbeat path
	// relies on a per-turn submission even when no input was issued, so the
	// server's gate can complete for that turn. Skipping empties would stall
	// every peer.

	// Standalone / networking-disabled path: skip the relay entirely and
	// drop straight into the world subsystem's command buffer. Single-player
	// is zero-network-overhead; the lockstep wire is purely opt-in.
	if (!IsNetworkingActive())
	{
		if (Commands.Num() == 0) return true; // no heartbeat needed in Standalone (treat as "sent")
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommandsAtTurn: Standalone but no USeinWorldSubsystem — dropping %d cmd(s)."), Commands.Num());
			return false;
		}
		UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn [Standalone]: enqueuing %d cmd(s) directly to WorldSubsystem."), Commands.Num());
		for (const FSeinCommand& Cmd : Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		return true;
	}

	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("SubmitLocalCommandsAtTurn")))
	{
		return false;
	}

	ASeinNetRelay* Relay = LocalRelay.Get();
	if (!Relay)
	{
		// Promoted to Warning level (was already Warning) — but caller MUST
		// observe the false return and not advance any "last submitted"
		// tracking, otherwise the missed turn is lost forever. See
		// StartLocalSession's pre-fire guard.
		UE_LOG(LogSeinNet, Warning,
			TEXT("SubmitLocalCommandsAtTurn: no LocalRelay yet (replication pending?) — dropping %d cmd(s) for TurnId=%d. Caller MUST treat this as not-submitted."),
			Commands.Num(), TurnId);
		return false;
	}

	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn: TurnId=%d  Count=%d  Slot=%u  Relay=%s%s"),
		TurnId, Commands.Num(), LocalPlayerID.Value, *GetNameSafe(Relay),
		Commands.IsEmpty() ? TEXT("  [HEARTBEAT]") : TEXT(""));
	Relay->Server_SubmitCommands(TurnId, Commands);
	return true;
}

void USeinNetSubsystem::StampAuthoritativeCommandBatch(
	TArray<FSeinCommand>& Commands, FSeinPlayerID Slot, int32 TurnId) const
{
	const int32 AuthoritativeTick = TurnId * GetTicksPerTurn();
	for (FSeinCommand& Command : Commands)
	{
		Command.PlayerID = Slot;
		Command.Tick = AuthoritativeTick;
	}
}

bool USeinNetSubsystem::DrainPendingAICommandsForTurn(
	FSeinPlayerID Slot, int32 TurnId, TArray<FSeinCommand>& OutCommands)
{
	OutCommands.Reset();
	TArray<FSeinCommand>* Pending = PendingAICommands.Find(Slot);
	if (!Pending || Pending->IsEmpty()) return false;

	OutCommands = MoveTemp(*Pending);
	Pending->Reset();
	StampAuthoritativeCommandBatch(OutCommands, Slot, TurnId);
	return true;
}

bool USeinNetSubsystem::BuildDroppedSlotSubmission(
	FSeinPlayerID Slot, int32 TurnId, bool bAllowAICommands,
	TArray<FSeinCommand>& OutCommands)
{
	OutCommands.Reset();
	return bAllowAICommands &&
		DrainPendingAICommandsForTurn(Slot, TurnId, OutCommands);
}

void USeinNetSubsystem::ServerHandleSubmission(ASeinNetRelay* SourceRelay, int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (!IsServer() || !SourceRelay) return;

	const FSeinPlayerID* SlotPtr = RelayToSlot.Find(SourceRelay);
	if (!SlotPtr || !SlotPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Server] Submission from unmapped relay %s — rejecting %d cmd(s)."),
			*GetNameSafe(SourceRelay), Commands.Num());
		return;
	}
	const FSeinPlayerID Slot = *SlotPtr;
	if (!IsCommandSubmissionLifecycleAllowed(Slot))
	{
		const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejecting command submission from slot=%u turn=%d lifecycle=%d; only Connected slots may author relay commands."),
			Slot.Value, TurnId, Lifecycle ? static_cast<int32>(*Lifecycle) : INDEX_NONE);
		return;
	}

	PruneProtocolState(GetCurrentTurn());
	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("ServerHandleSubmission")))
	{
		return;
	}

	if (TurnId <= CompletedTurnRejectionFloor || CompletedTurns.Contains(TurnId))
	{
		// Late submission for an already-dispatched turn. Lockstep can't
		// retroactively splice these in — the turn already shipped. Drop +
		// log; Phase 4 adaptive-input-delay will preempt this by raising
		// the delay before stragglers happen.
		UE_LOG(LogSeinNet, Warning, TEXT("[Server] Late submission slot=%u  TurnId=%d (already dispatched) — dropped."),
			Slot.Value, TurnId);
		return;
	}

	// Stamp authoritative sender on each command. Caller's PlayerID is
	// untrusted — server overrides.
	TArray<FSeinCommand> Stamped = Commands;
	StampAuthoritativeCommandBatch(Stamped, Slot, TurnId);

	const EFirstAcceptResult InsertResult =
		BufferCommandSubmissionFirstWins(TurnId, Slot, MoveTemp(Stamped));
	if (InsertResult == EFirstAcceptResult::IdenticalDuplicate)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Server] identical duplicate submission slot=%u turn=%d — idempotent no-op."),
			Slot.Value, TurnId);
		return;
	}
	if (InsertResult == EFirstAcceptResult::ConflictingDuplicate)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] conflicting duplicate submission slot=%u turn=%d — rejected; first accepted batch is immutable."),
			Slot.Value, TurnId);
		return;
	}

	const TMap<FSeinPlayerID, TArray<FSeinCommand>>& BufferForTurn =
		ServerTurnBuffer.FindChecked(TurnId);

	UE_LOG(LogSeinNet, Verbose, TEXT("[Server] Buffered submission slot=%u  TurnId=%d  Count=%d  (have %d/%d slots)"),
		Slot.Value, TurnId, Commands.Num(), BufferForTurn.Num(), GetActiveSlotCount());

	ServerCheckTurnComplete(TurnId, Slot);
}

void USeinNetSubsystem::InjectDroppedSlotHeartbeats(
	int32 Turn, bool bAllowAICommands)
{
	if (!IsServer()) return;
	if (Turn <= CompletedTurnRejectionFloor || CompletedTurns.Contains(Turn)) return;

	TMap<FSeinPlayerID, TArray<FSeinCommand>>& BufferForTurn = ServerTurnBuffer.FindOrAdd(Turn);
	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedCommandSlots(ExpectedSlots);

	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		const ESeinSlotLifecycle* Status = SlotLifecycle.Find(Slot);
		if (!Status) continue;

		// Only inject for slots that aren't going to submit themselves.
		if (*Status != ESeinSlotLifecycle::Dropped &&
			*Status != ESeinSlotLifecycle::AITakeover) continue;

		if (!Slot.IsValid()) continue;
		if (BufferForTurn.Contains(Slot)) continue; // already submitted (race)

		// Drain any AI-emitted commands buffered for this slot since the
		// last turn boundary. If empty, this is a true heartbeat (no
		// commands this turn). Either way the slot's submission is now
		// complete for `Turn`, so the gate can finalize.
		TArray<FSeinCommand> AICommands;
		if (BuildDroppedSlotSubmission(
			Slot, Turn, bAllowAICommands, AICommands))
		{
			const int32 CommandCount = AICommands.Num();
			BufferForTurn.Add(Slot, MoveTemp(AICommands));

			UE_LOG(LogSeinNet, Verbose,
				TEXT("[Server] drained %d AI-emitted command(s) for slot=%u into turn=%d"),
				CommandCount, Slot.Value, Turn);
		}
		else
		{
			BufferForTurn.Add(Slot, TArray<FSeinCommand>());
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[Server] injected heartbeat on behalf of dropped/AI slot=%u for turn=%d"),
				Slot.Value, Turn);
		}
	}
}

bool USeinNetSubsystem::HandleAIEmit(FSeinPlayerID OwnedSlot, const FSeinCommand& Command)
{
	// Standalone / networking disabled: return false so the AI controller
	// falls back to direct local enqueue. There are no other peers to keep
	// in sync, so the lockstep wire isn't needed.
	if (!IsServer() || !IsNetworkingActive()) return false;

	if (!OwnedSlot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("HandleAIEmit: AI controller emitted with invalid OwnedPlayerID — dropping command."));
		return true; // we still claim ownership of the routing decision
	}

	// Buffer until the next turn boundary, where InjectDroppedSlotHeartbeats
	// drains us into the OutgoingTurn buffer slot for this player. Stamping
	// the turn here would race with what the heartbeat injector picks; we
	// let the injector own both turn assignment and authority stamping so an
	// AI-authored PlayerID/Tick can never leak into the canonical stream.
	PendingAICommands.FindOrAdd(OwnedSlot).Add(Command);
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] AI emit buffered: slot=%u  (pending=%d)"),
		OwnedSlot.Value, PendingAICommands[OwnedSlot].Num());
	return true;
}

void USeinNetSubsystem::EvaluateDroppedSlots()
{
	if (!IsServer()) return;
	const double NowSec = FPlatformTime::Seconds();

	for (auto It = SlotDroppedAtTime.CreateIterator(); It; ++It)
	{
		const FSeinPlayerID Slot = It.Key();
		const double DroppedAt = It.Value();

		ESeinSlotLifecycle* StatusPtr = SlotLifecycle.Find(Slot);
		if (!StatusPtr || *StatusPtr != ESeinSlotLifecycle::Dropped)
		{
			// Slot reconnected or already transitioned; clean up.
			It.RemoveCurrent();
			continue;
		}

		const double Elapsed = NowSec - DroppedAt;
		if (Elapsed < GetDroppedToAITakeoverSeconds()) continue;

		// Transition: Dropped → AITakeover. The framework flips the lifecycle
		// bit (server keeps injecting heartbeats so the gate doesn't stall)
		// and — when SlotDropPolicy is BasicAI — auto-instantiates the
		// configured AI controller class + registers it with the sim. Per
		// DESIGN §16 the AI internal reasoning runs on the host only; only
		// emitted commands cross the lockstep boundary.
		*StatusPtr = ESeinSlotLifecycle::AITakeover;
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u transition Dropped → AITakeover (was dropped %.1fs)."),
			Slot.Value, Elapsed);

		TryAutoRegisterAIForSlot(Slot);
		It.RemoveCurrent();
	}
}

double USeinNetSubsystem::GetDroppedToAITakeoverSeconds() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings ? Settings->DroppedToAITakeoverSeconds : 30.0;
}

void USeinNetSubsystem::TryAutoRegisterAIForSlot(FSeinPlayerID Slot)
{
	if (!IsServer() || !Slot.IsValid()) return;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return;

	// Policy gate. KeepUnitsAlive: just leave the slot in AITakeover — units
	// idle, no AI registered, the framework's empty-heartbeat injection
	// keeps the gate green. RemovePlayer: forfeit semantics — currently
	// behaves identically to KeepUnitsAlive at the AI layer (the unit-
	// teardown path is reserved for a follow-up; doing the destroy here
	// cleanly requires walking the slot's entities and we want that gated
	// on a deliberate API rather than a side effect of disconnect).
	if (Settings->SlotDropPolicy != ESeinSlotDropPolicy::BasicAI)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Drop] slot=%u AITakeover: SlotDropPolicy=%d, no AI auto-spawn."),
			Slot.Value, (int32)Settings->SlotDropPolicy);
		return;
	}

	// Already have a controller for this slot? (Edge case: simulate-disconnect
	// fired twice without reconnect in between.) Don't double-register.
	if (AITakeoverControllers.Contains(Slot))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Drop] slot=%u already has an AI controller registered; skipping re-register."),
			Slot.Value);
		return;
	}

	// Resolve the configured class. Empty path or failed load → fall back to
	// the framework-shipped null controller. Both branches log; designers
	// see in the log what class actually got picked.
	// WYSIWYG. None/empty => drop-takeover AI is OFF: register no controller, so the dropped slot's
	// units simply idle (the server's empty heartbeats keep the lockstep gate from stalling). A
	// set-but-unloadable class is a mistake, not an off-switch: fall back to the shipped null
	// controller with a logged warning.
	if (Settings->DefaultAIControllerClass.IsNull())
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Default AI Controller"),
			TEXT("A dropped player's slot gets no AI takeover; its units stay idle."), /*bHighSeverity*/ false);
		return;
	}
	UClass* AIClass = Settings->DefaultAIControllerClass.TryLoadClass<USeinAIController>();
	if (!AIClass)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u DefaultAIControllerClass='%s' failed to load — falling back to USeinNullAIController."),
			Slot.Value, *Settings->DefaultAIControllerClass.ToString());
		AIClass = USeinNullAIController::StaticClass();
	}

	// Need the world subsystem to register against.
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u AITakeover: no USeinWorldSubsystem — skipping AI auto-register."),
			Slot.Value);
		return;
	}

	// Outer the controller on the world subsystem so its lifetime is bound
	// to the same scope the registration array uses (matches how the
	// resolver pool entries are outered). NewObject also fires PostInit /
	// any UCLASS-level cdo-init the subclass needs.
	USeinAIController* Controller = NewObject<USeinAIController>(WorldSub, AIClass);
	if (!Controller)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Drop] slot=%u failed to NewObject<USeinAIController> from class %s."),
			Slot.Value, *GetNameSafe(AIClass));
		return;
	}

	WorldSub->RegisterAIController(Controller, Slot);
	AITakeoverControllers.Add(Slot, Controller);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] slot=%u registered AI controller %s (class=%s)."),
		Slot.Value, *GetNameSafe(Controller), *GetNameSafe(AIClass));
}

void USeinNetSubsystem::TeardownAIForSlot(FSeinPlayerID Slot)
{
	if (!IsServer() || !Slot.IsValid()) return;

	TObjectPtr<USeinAIController>* Found = AITakeoverControllers.Find(Slot);
	if (!Found) return;

	USeinAIController* Controller = Found->Get();
	if (Controller)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>())
			{
				WorldSub->UnregisterAIController(Controller);
			}
		}
		// MarkAsGarbage isn't strictly required — losing the strong ref via
		// the map removal below makes it eligible — but it makes the
		// teardown intent explicit + lets GC reclaim sooner if there's no
		// other refholder lingering on a designer subclass.
		Controller->MarkAsGarbage();
	}

	AITakeoverControllers.Remove(Slot);

	// Drop any AI-emitted commands buffered for this slot but not yet drained
	// into a turn — once the human reclaims the slot, the lockstep wire will
	// carry their submissions, and stale AI commands surfacing one tick later
	// would silently overwrite their first input.
	PendingAICommands.Remove(Slot);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] slot=%u AI controller torn down (slot reconnected or session ended)."),
		Slot.Value);
}

void USeinNetSubsystem::SimulateSlotDisconnect(FSeinPlayerID Slot)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SimulateSlotDisconnect: server-only — ignored on client."));
		return;
	}
	if (!Slot.IsValid()) return;

	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Dropped);
	SlotDroppedAtTime.Add(Slot, FPlatformTime::Seconds());

	UE_LOG(LogSeinNet, Warning,
		TEXT("[Drop] SimulateSlotDisconnect: slot %u marked DROPPED. Heartbeat injection active; AI takeover scheduled in %.1fs."),
		Slot.Value, GetDroppedToAITakeoverSeconds());

	// Inject heartbeats for any open turns so the gate doesn't stall.
	if (!ServerTurnBuffer.IsEmpty())
	{
		TArray<int32> OpenTurns;
		ServerTurnBuffer.GetKeys(OpenTurns);
		for (int32 T : OpenTurns)
		{
			InjectDroppedSlotHeartbeats(T, /*bAllowAICommands=*/false);
			ServerCheckTurnComplete(T);
		}
	}

	if (!ServerHashReports.IsEmpty())
	{
		TArray<int32> PendingTurns;
		ServerHashReports.GetKeys(PendingTurns);
		for (const int32 Turn : PendingTurns)
		{
			const TMap<FSeinPlayerID, int32>* Reports = ServerHashReports.Find(Turn);
			if (Reports && AreExpectedHashReportsComplete(*Reports))
			{
				ServerCompareHashesForTurn(Turn);
			}
		}
	}

	// The parity barrier is defined over connected relay processes. A
	// simulated drop shrinks that set just like a real logout.
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::SimulateSlotReconnect(FSeinPlayerID Slot)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SimulateSlotReconnect: server-only — ignored on client."));
		return;
	}
	if (!Slot.IsValid()) return;

	const ESeinSlotLifecycle* Prev = SlotLifecycle.Find(Slot);
	if (!Prev)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] SimulateSlotReconnect: slot %u has no lifecycle entry — was it ever connected?"),
			Slot.Value);
		return;
	}

	const bool bWasAITakeover = (*Prev == ESeinSlotLifecycle::AITakeover);
	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Connected);
	SlotDroppedAtTime.Remove(Slot);
	if (bWasAITakeover)
	{
		TeardownAIForSlot(Slot);
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] SimulateSlotReconnect: slot %u back to CONNECTED. NOTE: full snapshot+tail catch-up is a follow-up phase — slot resumes from current sim state, not from where it disconnected."),
		Slot.Value);
}

void USeinNetSubsystem::ServerCheckTurnComplete(
	int32 TurnId, FSeinPlayerID CompletingSubmitter)
{
	const TMap<FSeinPlayerID, TArray<FSeinCommand>>* BufferForTurn = ServerTurnBuffer.Find(TurnId);
	if (!BufferForTurn) return;

	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedCommandSlots(ExpectedSlots);
	int32 ReceivedExpectedCount = 0;
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		ReceivedExpectedCount += BufferForTurn->Contains(Slot) ? 1 : 0;
	}

	if (!AreExpectedCommandSlotsComplete(ExpectedSlots, *BufferForTurn))
	{
		// Persistent-incomplete escalation: most incompletes are transient
		// pipeline blips. State is per turn because future/open turns can be
		// examined in an interleaved order as RPCs and disconnect recovery run.
		const double NowSec = FPlatformTime::Seconds();
		FSeinIncompleteTurnDiagnostic* Diagnostic =
			IncompleteTurnDiagnostics.Find(TurnId);
		if (!Diagnostic)
		{
			FSeinIncompleteTurnDiagnostic& Added =
				IncompleteTurnDiagnostics.Add(TurnId);
			Added.FirstObservedAt = NowSec;
			Added.LastLoggedAt = NowSec;
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[BUFFER INCOMPLETE transient] turn=%d  have=%d/%d slots."),
				TurnId, ReceivedExpectedCount, ExpectedSlots.Num());
			return;
		}

		const double IncompleteFor = NowSec - Diagnostic->FirstObservedAt;
		if (IncompleteFor >= 2.0 &&
			(!Diagnostic->bEscalated || (NowSec - Diagnostic->LastLoggedAt) >= 2.0))
		{
			TArray<FString> Have, Missing;
			for (const FSeinPlayerID Slot : ExpectedSlots)
			{
				if (BufferForTurn->Contains(Slot))
				{
					Have.Add(FString::Printf(TEXT("%u"), Slot.Value));
				}
				else
				{
					Missing.Add(FString::Printf(TEXT("%u"), Slot.Value));
				}
			}
			UE_LOG(LogSeinNet, Log,
				TEXT("[BUFFER INCOMPLETE persistent] turn=%d incomplete for %.1fs  have=%d/%d slots [%s]  missing=[%s]. Server is holding — likely one peer's heartbeat dropped (LocalRelay race or disconnect)."),
				TurnId, IncompleteFor, ReceivedExpectedCount, ExpectedSlots.Num(),
				*FString::Join(Have, TEXT(",")),
				*FString::Join(Missing, TEXT(",")));
			Diagnostic->LastLoggedAt = NowSec;
			Diagnostic->bEscalated = true;
		}
		return;
	}
	// Assemble exactly the canonical expected slot IDs, already sorted by ID.
	// Extra/stale buffer keys can neither satisfy a missing live slot nor enter
	// the deterministic command stream.
	const TArray<FSeinPlayerID>& SortedSlots = ExpectedSlots;

	TArray<FSeinCommand> Assembled;
	for (const FSeinPlayerID& Slot : SortedSlots)
	{
		const TArray<FSeinCommand>& Sub = BufferForTurn->FindChecked(Slot);
		Assembled.Append(Sub);
	}

	// Per-turn chatter — Verbose. Routine completion isn't worth a log line
	// every ~100ms in a healthy session. Bump LogSeinNet to Verbose to see.
	UE_LOG(LogSeinNet, Verbose, TEXT("[Server] Turn complete: TurnId=%d  Slots=%d  TotalCmds=%d — fanning to %d relays."),
		TurnId, SortedSlots.Num(), Assembled.Num(), Relays.Num());

	FinalizeCompletedTurnDiagnostics(TurnId, CompletingSubmitter);

	// Capture the canonical assembled turn into the replay log BEFORE fan-out.
	// Recording at the assembly step (rather than per-client receive) gives us
	// one authoritative copy free of duplicates / ordering ambiguity.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		ReplayWriter->RecordTurn(TurnId, Assembled);
	}

	// Listen hosts receive through their owned relay. A dedicated authority has
	// no local relay/RPC loopback, so feed its gate from the same canonical
	// assembled payload before fanning out to remote peers.
	BufferAssembledTurnForDedicatedAuthority(TurnId, Assembled);

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Target = Wp.Get())
		{
			Target->Client_ReceiveTurn(TurnId, Assembled);
		}
	}

	ServerTurnBuffer.Remove(TurnId);
	CompletedTurns.Add(TurnId);
}

void USeinNetSubsystem::BufferAssembledTurnForDedicatedAuthority(
	int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (IsDedicatedAuthority())
	{
		BufferReceivedTurn(TurnId, Commands);
	}
}

void USeinNetSubsystem::BufferReceivedTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	ReceivedTurns.Add(TurnId, Commands);
	OnTurnReceived.Broadcast(TurnId, Commands);
}

void USeinNetSubsystem::ClientHandleTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	PruneProtocolState(GetCurrentTurn());
	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("ClientHandleTurn")))
	{
		return;
	}

	// Per-turn chatter — Verbose. See ServerCheckTurnComplete's note for why.
	UE_LOG(LogSeinNet, Verbose, TEXT("[Client] Receive turn: TurnId=%d Count=%d  (buffered for turn-boundary drain)"),
		TurnId, Commands.Num());

	// Phase 2b: store the assembled turn keyed by TurnId. The sim's gate
	// (ResolveTurnReady → ConsumeTurn) drains this map at the matching
	// turn boundary, guaranteeing every client applies turn N's commands
	// at the same sim tick (= N * TicksPerTurn). Empty turns still get an
	// entry so the gate sees them as "ready" instead of stalling.
	BufferReceivedTurn(TurnId, Commands);
}

USeinReplayReader* USeinNetSubsystem::GetOrCreateReplayReader()
{
	if (!ReplayReader)
	{
		ReplayReader = NewObject<USeinReplayReader>(this);
	}
	return ReplayReader;
}

void USeinNetSubsystem::FinalizeCompletedTurnDiagnostics(
	int32 TurnId, FSeinPlayerID CompletingSubmitter)
{
	FSeinIncompleteTurnDiagnostic Diagnostic;
	const bool bCompletedAfterIncomplete =
		IncompleteTurnDiagnostics.RemoveAndCopyValue(TurnId, Diagnostic);
	if (bCompletedAfterIncomplete)
	{
		if (Diagnostic.bEscalated)
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("[BUFFER INCOMPLETE persistent] turn=%d RESOLVED — completed after stall. Sim resuming."),
				TurnId);
		}
	}

	++TurnsCompletedCount;
	if (bCompletedAfterIncomplete && CompletingSubmitter.IsValid())
	{
		RecordStragglerIfApplicable(TurnId, CompletingSubmitter);
	}
}

void USeinNetSubsystem::RecordStragglerIfApplicable(int32 TurnId, FSeinPlayerID LastSubmittingSlot)
{
	if (!LastSubmittingSlot.IsValid()) return;
	int32& Count = StragglerCounts.FindOrAdd(LastSubmittingSlot);
	++Count;

	// Recommend bumping InputDelayTurns once a peer crosses 5% straggle rate
	// over a meaningful sample size. Rate-limit to once every 64 turns to
	// avoid log spam. Designers can manually raise InputDelayTurns in
	// settings; full automatic dynamic-delay adjustment is deferred (see the
	// header note next to StragglerCounts).
	const int32 RECOMMEND_INTERVAL_TURNS = 64;
	const int32 SAMPLE_THRESHOLD = 50;
	if (TurnsCompletedCount >= SAMPLE_THRESHOLD &&
		(TurnsCompletedCount % RECOMMEND_INTERVAL_TURNS) == 0)
	{
		const float Rate = static_cast<float>(Count) / static_cast<float>(TurnsCompletedCount);
		if (Rate > 0.05f)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[ADAPTIVE INPUT DELAY] slot=%u is consistently the last to submit (%d / %d turns, %.1f%%). Consider raising USeinARTSCoreSettings::InputDelayTurns to absorb this peer's latency. Today this is a manual change; automatic dynamic adjustment is a future polish item."),
				LastSubmittingSlot.Value, Count, TurnsCompletedCount, Rate * 100.0f);
		}
	}
}

// ============================================================================
// Determinism gossip (Phase 4)
// ============================================================================

void USeinNetSubsystem::MaybeSubmitStateHashCheck(int32 JustFinishedTurn)
{
	if (!IsDeterminismGossipEnabled()) return;
	if (!IsNetworkingActive()) return;
	if (!LocalPlayerID.IsValid()) return;

	const int32 Interval = GetDeterminismCheckIntervalTurns();
	if (Interval <= 0) return;

	// Cadence: every N turns, starting at turn 0 (which is grace anyway, so
	// no real check fires for it; first real check is turn `Interval`).
	if (JustFinishedTurn % Interval != 0) return;
	if (JustFinishedTurn <= LastHashReportedTurn || JustFinishedTurn <= LastHashQueuedTurn) return;

	// First time this client reports a hash — log at Log level so the user
	// sees gossip is live without needing Verbose. Subsequent reports stay
	// Verbose unless there's a desync.
	if (LastHashReportedTurn < 0)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[DETERMINISM] gossip active — reporting state hash every %d turn(s). Server compares + fires red on-screen alarm if peers diverge."),
			Interval);
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Error, TEXT("MaybeSubmitStateHashCheck: no USeinWorldSubsystem at checkpoint turn %d; hash cannot be captured."),
			JustFinishedTurn);
		return;
	}

	const int32 LocalHash = WorldSub->ComputeStateHash();
	EnqueueStateHashReport(JustFinishedTurn, LocalHash);
	FlushPendingStateHashReports();
}

void USeinNetSubsystem::EnqueueStateHashReport(int32 Turn, int32 Hash)
{
	if (Turn <= LastHashReportedTurn || Turn <= LastHashQueuedTurn) return;
	FSeinPendingStateHashReport& Pending = PendingStateHashReports.Emplace_GetRef();
	Pending.Turn = Turn;
	Pending.Hash = Hash;
	LastHashQueuedTurn = Turn;
}

void USeinNetSubsystem::FlushPendingStateHashReports()
{
	while (!PendingStateHashReports.IsEmpty())
	{
		const FSeinPendingStateHashReport& Pending = PendingStateHashReports[0];
		bool bSent = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (TestHashSubmitOverride)
		{
			bSent = TestHashSubmitOverride(Pending.Turn, Pending.Hash);
		}
		else
#endif
		{
			if (!IsNetworkingActive() || !LocalPlayerID.IsValid() ||
				!IsHashTurnWithinProtocolWindow(Pending.Turn, TEXT("FlushPendingStateHashReports")))
			{
				return;
			}

			if (ASeinNetRelay* Relay = LocalRelay.Get())
			{
				UE_LOG(LogSeinNet, Verbose,
					TEXT("[DETERMINISM] reporting hash=0x%08x for turn=%d slot=%u"),
					static_cast<uint32>(Pending.Hash), Pending.Turn, LocalPlayerID.Value);
				Relay->Server_ReportStateHash(Pending.Turn, Pending.Hash);
				bSent = true;
			}
		}

		if (!bSent)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("FlushPendingStateHashReports: retaining exact turn=%d hash=0x%08x for retry."),
				Pending.Turn, static_cast<uint32>(Pending.Hash));
			return;
		}

		LastHashReportedTurn = FMath::Max(LastHashReportedTurn, Pending.Turn);
		PendingStateHashReports.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void USeinNetSubsystem::ServerHandleConfigFingerprint(ASeinNetRelay* SourceRelay, int32 Fingerprint)
{
	if (!IsServer() || !SourceRelay) return;
	// Clients always report. The host alone chooses whether those reports are
	// enforced, so a client's local opt-out cannot bypass host policy.
	if (!IsConfigParityCheckEnabled()) return;

	const FSeinPlayerID* SlotPtr = RelayToSlot.Find(SourceRelay);
	if (!SlotPtr || !SlotPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[CONFIG] ServerHandleConfigFingerprint: unmapped relay %s — dropping fingerprint."),
			*GetNameSafe(SourceRelay));
		return;
	}
	const FSeinPlayerID Slot = *SlotPtr;
	const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
	if (!Lifecycle || *Lifecycle != ESeinSlotLifecycle::Connected)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[CONFIG] ignoring fingerprint from non-connected/unknown slot=%u."), Slot.Value);
		return;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 ServerFingerprint = Settings ? Settings->ComputeConfigFingerprint() : 0;

	if (Fingerprint == ServerFingerprint)
	{
		AcceptedConfigFingerprints.Add(Slot, Fingerprint);
		UE_LOG(LogSeinNet, Log,
			TEXT("[CONFIG] slot=%u config parity OK (fingerprint 0x%08x)."),
			Slot.Value, static_cast<uint32>(ServerFingerprint));
		TryDispatchLockstepSessionStart();
		return;
	}

	// Mismatch — the joining client's sim-affecting settings differ from the host's; it would desync
	// every tick. Reject it before the match starts, via the same disconnect + travel-to-menu path the
	// lobby kick uses. The disconnect drives the normal leave/logout cleanup (UnregisterRelay clears
	// RelayToSlot), so the slot frees for a correctly-configured re-join.
	UE_LOG(LogSeinNet, Error,
		TEXT("[CONFIG] slot=%u config MISMATCH (client 0x%08x vs host 0x%08x) — kicking."),
		Slot.Value, static_cast<uint32>(Fingerprint), static_cast<uint32>(ServerFingerprint));
	AcceptedConfigFingerprints.Remove(Slot);
	SourceRelay->Client_NotifyKicked(FString::Printf(
		TEXT("Config mismatch: your SeinARTS sim settings differ from the host's (fingerprint 0x%08x vs 0x%08x). Ensure both use the same DefaultGame.ini."),
		static_cast<uint32>(Fingerprint), static_cast<uint32>(ServerFingerprint)));
}

void USeinNetSubsystem::ServerHandleStateHashReport(ASeinNetRelay* SourceRelay, int32 Turn, int32 Hash)
{
	if (!IsServer() || !SourceRelay) return;

	const FSeinPlayerID* SlotPtr = RelayToSlot.Find(SourceRelay);
	if (!SlotPtr || !SlotPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] ServerHandleStateHashReport: unmapped relay %s — dropping hash for turn %d."),
			*GetNameSafe(SourceRelay), Turn);
		return;
	}
	const FSeinPlayerID Slot = *SlotPtr;
	const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
	if (!Lifecycle || *Lifecycle != ESeinSlotLifecycle::Connected)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] ignoring hash from non-connected/unknown slot=%u turn=%d."), Slot.Value, Turn);
		return;
	}

	PruneProtocolState(GetCurrentTurn());
	if (!IsHashTurnWithinProtocolWindow(Turn, TEXT("ServerHandleStateHashReport"))) return;

	if (Turn <= CompletedHashRejectionFloor || CompletedHashChecks.Contains(Turn))
	{
		// Late report for an already-compared turn. Drop silently — the
		// alarm (if any) already fanned out to all peers, no need to redo.
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] late hash report from slot=%u for already-compared turn=%d — dropped."),
			Slot.Value, Turn);
		return;
	}

	const EFirstAcceptResult InsertResult = BufferHashReportFirstWins(Turn, Slot, Hash);
	if (InsertResult == EFirstAcceptResult::IdenticalDuplicate)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] identical duplicate hash slot=%u turn=%d — idempotent no-op."),
			Slot.Value, Turn);
		return;
	}
	if (InsertResult == EFirstAcceptResult::ConflictingDuplicate)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[DETERMINISM] conflicting duplicate hash slot=%u turn=%d old checkpoint retained; new hash=0x%08x rejected."),
			Slot.Value, Turn, static_cast<uint32>(Hash));
		return;
	}

	const TMap<FSeinPlayerID, int32>& BufferForTurn = ServerHashReports.FindChecked(Turn);
	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedHashReporterSlots(ExpectedSlots);

	UE_LOG(LogSeinNet, Verbose,
		TEXT("[DETERMINISM] buffered hash report  slot=%u  Turn=%d  Hash=0x%08x  (have %d/%d slots)"),
		Slot.Value, Turn, static_cast<uint32>(Hash), BufferForTurn.Num(), ExpectedSlots.Num());

	if (AreExpectedHashReportsComplete(BufferForTurn))
	{
		ServerCompareHashesForTurn(Turn);
	}
}

void USeinNetSubsystem::ServerCompareHashesForTurn(int32 Turn)
{
	const TMap<FSeinPlayerID, int32>* Buffer = ServerHashReports.Find(Turn);
	if (!Buffer || Buffer->IsEmpty()) return;
	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedHashReporterSlots(ExpectedSlots);
	if (ExpectedSlots.IsEmpty() || !AreExpectedHashReportsComplete(*Buffer)) return;

	// Compare only live reporter processes. A dropped gameplay slot can still
	// have an earlier report buffered, but it is no longer a peer in this
	// checkpoint and must not satisfy or contaminate the live-peer comparison.
	const int32 Reference = Buffer->FindChecked(ExpectedSlots[0]);
	bool bAllAgree = true;
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		if (Buffer->FindChecked(Slot) != Reference)
		{
			bAllAgree = false;
			break;
		}
	}

	// Build per-slot summary array for either the agreement log OR the
	// fan-out payload — same shape, only the verbosity differs.
	TArray<FSeinSlotHashEntry> SortedHashes;
	SortedHashes.Reserve(ExpectedSlots.Num());
	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		SortedHashes.Emplace(Slot, Buffer->FindChecked(Slot));
	}
	SortedHashes.Sort([](const FSeinSlotHashEntry& A, const FSeinSlotHashEntry& B)
	{
		return A.Slot.Value < B.Slot.Value;
	});

	if (bAllAgree)
	{
		// Most check-turns are silent (Verbose). Promote every 5th to Log
		// level so the user has periodic visible confirmation that gossip
		// is working without spam — also serves as the "all green" signal
		// during long matches.
		const int32 Interval = GetDeterminismCheckIntervalTurns();
		const bool bPeriodicConfirm = (Interval > 0) && ((Turn / Interval) % 5 == 0);
		if (bPeriodicConfirm)
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("[DETERMINISM] turn=%d  %d/%d peers agree on hash 0x%08x — OK."),
				Turn, ExpectedSlots.Num(), ExpectedSlots.Num(), static_cast<uint32>(Reference));
		}
		else
		{
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[DETERMINISM] turn=%d  %d/%d peers agree on hash 0x%08x — OK."),
				Turn, ExpectedSlots.Num(), ExpectedSlots.Num(), static_cast<uint32>(Reference));
		}
	}
	else
	{
		// Build a diagnostic line listing each slot's hash for the log.
		FString Report;
		for (const FSeinSlotHashEntry& Entry : SortedHashes)
		{
			Report += FString::Printf(TEXT("[slot=%u hash=0x%08x] "), Entry.Slot.Value, static_cast<uint32>(Entry.Hash));
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("[DESYNC DETECTED] turn=%d — peer hashes diverge: %s. Fanning Client_NotifyDesync to %d relay(s) so every peer surfaces the on-screen alarm."),
			Turn, *Report, Relays.Num());

		// Fan to every relay (including host's own — RPC-loopback routes
		// to local process). Each peer's ClientHandleDesyncNotification
		// posts the red on-screen debug message + sets bDesyncDetected.
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
		{
			if (ASeinNetRelay* Target = Wp.Get())
			{
				Target->Client_NotifyDesync(Turn, SortedHashes);
			}
		}
	}

	ServerHashReports.Remove(Turn);
	CompletedHashChecks.Add(Turn);
}

void USeinNetSubsystem::ClientHandleDesyncNotification(int32 Turn, const TArray<FSeinSlotHashEntry>& PeerHashes)
{
	bDesyncDetected = true;

	// Build a one-line summary listing every peer's hash. Sort by slot for
	// readability — server already sorted, but be defensive.
	TArray<FSeinSlotHashEntry> Sorted = PeerHashes;
	Sorted.Sort([](const FSeinSlotHashEntry& A, const FSeinSlotHashEntry& B)
	{
		return A.Slot.Value < B.Slot.Value;
	});

	FString PeerSummary;
	for (const FSeinSlotHashEntry& Entry : Sorted)
	{
		const TCHAR* MarkLocal = (Entry.Slot == LocalPlayerID) ? TEXT("*") : TEXT("");
		PeerSummary += FString::Printf(TEXT("slot %u%s = 0x%08x  "), Entry.Slot.Value, MarkLocal, static_cast<uint32>(Entry.Hash));
	}

	UE_LOG(LogSeinNet, Error,
		TEXT("[DESYNC] localSlot=%u  turn=%d  Peers: %s  (* = this peer). Lockstep is broken — sim state has diverged. Capture replay/state dump now; bug repro is in this run."),
		LocalPlayerID.Value, Turn, *PeerSummary);

	// On-screen RED debug message. AddOnScreenDebugMessage with a stable Key
	// per-turn so successive desyncs don't all stack identically; we want
	// each unique (Turn) to surface once. -1 = persistent until cleared.
	if (GEngine)
	{
		const int32 KeyBase = 0x5E7DE57C; // arbitrary salt unique to Sein desync
		const uint64 KeyTurn = static_cast<uint64>(KeyBase) ^ static_cast<uint64>(Turn);
		const FString HeaderMsg = FString::Printf(
			TEXT("[SEINARTS DESYNC] turn=%d  localSlot=%u — sim state diverged across peers."),
			Turn, LocalPlayerID.Value);
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(KeyTurn & 0x7FFFFFFFull),
			30.0f, FColor::Red, HeaderMsg, /*bNewerOnTop=*/true,
			FVector2D(1.25f, 1.25f));

		// Also surface the per-slot summary as a second line so designers
		// can see who diverged at a glance without checking the log.
		const uint64 KeyTurnSummary = KeyTurn ^ 0x1ull;
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(KeyTurnSummary & 0x7FFFFFFFull),
			30.0f, FColor(255, 100, 100), FString::Printf(TEXT("  Peers: %s"), *PeerSummary),
			/*bNewerOnTop=*/true, FVector2D(1.0f, 1.0f));
	}
}
