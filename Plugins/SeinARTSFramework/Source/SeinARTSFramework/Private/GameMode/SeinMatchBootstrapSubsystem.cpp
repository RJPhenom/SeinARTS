/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapSubsystem.cpp
 */

#include "GameMode/SeinMatchBootstrapSubsystem.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinGameMode.h"
#include "Simulation/SeinSnapshotCameraProvider.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Types/Transform.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "SeinLobbySubsystem.h"

void USeinMatchBootstrapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the actor bridge to initialize first, then disable its
	// OnWorldBeginPlay auto-register. We're going to drive bootstrap order
	// explicitly so server and clients produce identical entity-ID sequences:
	//
	//   server:  GameMode pre-spawns slot entities (InitGame, before BeginPlay)
	//            → THEN our OnWorldBeginPlay calls bridge.RegisterAllPlacedActors
	//   client:  no GameMode → our OnWorldBeginPlay pre-spawns slot entities
	//            → THEN calls bridge.RegisterAllPlacedActors
	//
	// Either way, slot entities go in first (IDs 1..N), then placed actors
	// (sorted by name for further determinism) get IDs N+1..M. Lockstep stays
	// bit-identical from tick zero.
	Collection.InitializeDependency(USeinActorBridgeSubsystem::StaticClass());

	UWorld* World = GetWorld();
	if (USeinActorBridgeSubsystem* Bridge = World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr)
	{
		Bridge->SetAutoRegisterOnBeginPlay(false);
	}
}

void USeinMatchBootstrapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* Sub = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinMatchBootstrap: USeinWorldSubsystem missing."));
		return;
	}

	// On the server (incl. ListenServer + DedicatedServer) GameMode already
	// pre-spawned slot entities in InitGame. On clients (NetMode == NM_Client)
	// we mirror that work in the SAME order so entity IDs match.
	const ENetMode NetMode = InWorld.GetNetMode();
	if (NetMode == NM_Client)
	{
		// Phase 3d: register factions from plugin settings BEFORE pre-registering
		// players. Server already did this in ASeinGameMode::InitGame; this
		// mirror call ensures the client's Factions map has the SAME contents
		// in the SAME iteration order, so any RegisterPlayer call that follows
		// resolves the faction's ResourceKit identically on both sides.
		Sub->RegisterFactionsFromSettings();

		// Mirror the server's match-settings resolution chain:
		//   1. Lobby snapshot (lobby flow)
		//   2. Synthesize from level's SeinPlayerStarts (PIE-direct)
		// Both sides walking the same source produces matching entity IDs
		// during pre-registration (lockstep determinism).
		const FSeinMatchSettings* Snapshot = nullptr;
		FSeinMatchSettings Synthesized;
		if (UGameInstance* GI = InWorld.GetGameInstance())
		{
			if (USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
			{
				if (Lobby->HasPublishedSnapshot() && Lobby->GetPublishedSnapshot().Slots.Num() > 0)
				{
					Snapshot = &Lobby->GetPublishedSnapshot();
				}
			}
		}
		if (!Snapshot)
		{
			Synthesized = ASeinPlayerStart::SynthesizeMatchSettingsFromLevel(&InWorld);
			if (Synthesized.Slots.Num() > 0)
			{
				Snapshot = &Synthesized;
			}
		}
		if (Snapshot)
		{
			RunSlotPreRegistration(InWorld, *Sub, *Snapshot);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap: no lobby snapshot AND no SeinPlayerStarts on this level — skipping client pre-reg."));
		}
	}

	// Whether we just pre-spawned slot entities (client) or GameMode did it
	// earlier (server), placed actors get registered LAST so their entity IDs
	// always come after slot starts. Stable-sort by name (inside the bridge)
	// + skip-already-linked makes this deterministic across machines.
	if (USeinActorBridgeSubsystem* Bridge = InWorld.GetSubsystem<USeinActorBridgeSubsystem>())
	{
		Bridge->RegisterAllPlacedActors(InWorld);
	}

	// Bind snapshot capture/restore hooks so the local camera pose
	// round-trips with save-game files. Cycle-avoidance: SeinARTSCoreEntity
	// can't see ASeinCameraPawn directly (would close a module dependency
	// loop). The world subsystem broadcasts; we (Framework-side) bind.
	SnapshotCaptureHandle = Sub->OnCaptureSnapshotPostSim.AddUObject(this, &USeinMatchBootstrapSubsystem::HandleSnapshotCapture);
	SnapshotRestoreHandle = Sub->OnRestoreSnapshotPostSim.AddUObject(this, &USeinMatchBootstrapSubsystem::HandleSnapshotRestore);
}

void USeinMatchBootstrapSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sub = World->GetSubsystem<USeinWorldSubsystem>())
		{
			if (SnapshotCaptureHandle.IsValid()) Sub->OnCaptureSnapshotPostSim.Remove(SnapshotCaptureHandle);
			if (SnapshotRestoreHandle.IsValid()) Sub->OnRestoreSnapshotPostSim.Remove(SnapshotRestoreHandle);
		}
	}
	SnapshotCaptureHandle.Reset();
	SnapshotRestoreHandle.Reset();
	Super::Deinitialize();
}

namespace
{
	/** Walk likely UObjects on the local PC + the PC itself looking for one
	 *  that implements ISeinSnapshotCameraProvider. Returns the first match.
	 *  Provider-target order: pawn, view target, then PC itself. Designers
	 *  typically implement on the camera pawn; advanced setups (orbital cam
	 *  rigs, cinematic systems) may implement on a non-pawn view-target
	 *  actor instead. */
	UObject* FindCameraProvider(UWorld* World)
	{
		if (!World) return nullptr;
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) return nullptr;

		auto Implements = [](UObject* Obj) -> bool
		{
			return Obj && Obj->GetClass()->ImplementsInterface(USeinSnapshotCameraProvider::StaticClass());
		};

		if (APawn* Pawn = PC->GetPawn())
		{
			if (Implements(Pawn)) return Pawn;
		}
		if (AActor* ViewTarget = PC->GetViewTarget())
		{
			if (Implements(ViewTarget)) return ViewTarget;
		}
		if (Implements(PC)) return PC;
		return nullptr;
	}
}

void USeinMatchBootstrapSubsystem::HandleSnapshotCapture(FSeinWorldSnapshot& Snapshot)
{
	UObject* Provider = FindCameraProvider(GetWorld());
	if (!Provider) return; // No camera provider implements the interface — silent no-op (designer opted out).

	ISeinSnapshotCameraProvider::Execute_CaptureCameraState(Provider, Snapshot.CameraState);
}

void USeinMatchBootstrapSubsystem::HandleSnapshotRestore(const FSeinWorldSnapshot& Snapshot)
{
	UObject* Provider = FindCameraProvider(GetWorld());
	if (!Provider) return;

	ISeinSnapshotCameraProvider::Execute_RestoreCameraState(Provider, Snapshot.CameraState);
	UE_LOG(LogTemp, Log,
		TEXT("SeinMatchBootstrap: camera state restored via provider %s (impl class=%s)"),
		*Provider->GetName(), *Provider->GetClass()->GetName());
}

void USeinMatchBootstrapSubsystem::RunSlotPreRegistration(UWorld& InWorld, USeinWorldSubsystem& Sub, const FSeinMatchSettings& Settings)
{
	// Resolve the GameMode CDO once — used both for DefaultFactionID fallback
	// (must match server's value, NOT a hardcoded `FSeinFactionID(1)`) and for
	// the StartingResources copy below. Server's RegisterPlayerWithSim reads
	// these from `this` (a live GameMode); we read from the CDO since clients
	// have no GameMode instance.
	const ASeinGameMode* GMCDO = nullptr;
	{
		UClass* GMClass = nullptr;
		if (AWorldSettings* WS = InWorld.GetWorldSettings())
		{
			GMClass = WS->DefaultGameMode;
		}
		if (GMClass && GMClass->IsChildOf(ASeinGameMode::StaticClass()))
		{
			GMCDO = GMClass->GetDefaultObject<ASeinGameMode>();
		}
	}
	const FSeinFactionID DefaultFaction = GMCDO ? GMCDO->DefaultFactionID : FSeinFactionID(1);

	// SAME LOOP as SeinGameMode::PreRegisterMatchSlots. Iteration order over
	// Settings.Slots is deterministic (it's a TArray), and the PlayerStart
	// lookup is by SlotIndex match (not iteration order), so resulting
	// entity IDs are identical across server and every client.
	int32 NumSpawned = 0;
	for (const FSeinMatchSlot& Slot : Settings.Slots)
	{
		if (Slot.State != ESeinSlotState::Human && Slot.State != ESeinSlotState::AI)
		{
			continue;
		}
		if (Slot.SlotIndex <= 0)
		{
			continue;
		}

		const FSeinPlayerID SlotPlayerID(static_cast<uint8>(Slot.SlotIndex));
		// Use the actual GameMode CDO's DefaultFactionID (resolved above).
		// Hardcoding FSeinFactionID(1) here would diverge from server whenever
		// the project sets DefaultFactionID to anything else — silent state
		// divergence at registration time.
		const FSeinFactionID Faction = Slot.FactionID.IsValid() ? Slot.FactionID : DefaultFaction;

		Sub.RegisterPlayer(SlotPlayerID, Faction, Slot.TeamID);

		// Mirror the server's `RegisterPlayerWithSim` — copy StartingResources
		// from the GameMode CDO onto the just-registered player state.
		// Without this, server PlayerState has resources but client PlayerState
		// has none → state-hash mismatch from tick 1.
		if (GMCDO)
		{
			if (FSeinPlayerState* State = Sub.GetPlayerState(SlotPlayerID))
			{
				for (const TPair<FGameplayTag, FFixedPoint>& Pair : GMCDO->StartingResources)
				{
					State->SetResource(Pair.Key, Pair.Value);
				}
			}
		}

		// Find the SeinPlayerStart that targets this slot.
		ASeinPlayerStart* Start = nullptr;
		for (TActorIterator<ASeinPlayerStart> It(&InWorld); It; ++It)
		{
			if (It->PlayerSlot == Slot.SlotIndex)
			{
				Start = *It;
				break;
			}
		}

		if (!Start || !Start->SpawnEntity)
		{
			continue;
		}

		// Read the editor-baked snapshot for cross-arch determinism (matches
		// SeinGameMode::SpawnStartEntity exactly). Re-saving the level bakes
		// PlacedSimLocation so this branch is preferred; runtime FromFloat
		// is a fallback that loses determinism guarantees.
		FFixedTransform SpawnTransform;
		if (Start->bSimLocationBaked)
		{
			SpawnTransform.Location = Start->PlacedSimLocation;
		}
		else
		{
			const FVector Loc = Start->GetActorLocation();
			UE_LOG(LogTemp, Warning,
				TEXT("SeinMatchBootstrap: PlayerStart %s has stale PlacedSimLocation — falling back to runtime FromFloat at %s. NOT cross-arch deterministic until the level is re-saved."),
				*Start->GetName(), *Loc.ToString());
			SpawnTransform.Location = FFixedVector(
				FFixedPoint::FromFloat(Loc.X),
				FFixedPoint::FromFloat(Loc.Y),
				FFixedPoint::FromFloat(Loc.Z));
		}

		const FSeinEntityHandle Handle = Sub.SpawnEntity(Start->SpawnEntity, SpawnTransform, SlotPlayerID);
		if (Handle.IsValid())
		{
			++NumSpawned;
			UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap [Client]: spawned %s for %s"),
				*Start->SpawnEntity->GetName(), *SlotPlayerID.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SeinMatchBootstrap [Client]: failed to spawn start entity %s for %s"),
				*Start->SpawnEntity->GetName(), *SlotPlayerID.ToString());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap [Client]: pre-registered slots from match settings, spawned %d start entity(ies). Should match server's pre-reg log."),
		NumSpawned);
}
