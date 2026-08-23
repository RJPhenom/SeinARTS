/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetRelay.cpp
 */

#include "SeinNetRelay.h"
#include "SeinARTSNet.h"
#include "SeinNetSubsystem.h"
#include "SeinLobbySubsystem.h"
#include "Settings/PluginSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

ASeinNetRelay::ASeinNetRelay()
{
	// Replicated so its RPCs route through the NetDriver. NetLoadOnClient = false
	// because we always spawn dynamically per-connection, never as a level actor.
	bReplicates = true;
	bNetLoadOnClient = false;

	// Replicate only to the owning client (the PC this relay is for).
	// Per-PC relays are not relevant to other clients — fan-out happens by
	// the server iterating ALL relays and calling each one's ClientRPC, which
	// reaches that relay's owning client. No always-relevant flag needed.
	bOnlyRelevantToOwner = true;

	// No tick — RPC-driven only.
	PrimaryActorTick.bCanEverTick = false;
	SetActorTickEnabled(false);
}

void ASeinNetRelay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASeinNetRelay, AssignedPlayerID);
	DOREPLIFETIME(ASeinNetRelay, AssignedParticipantID);
	DOREPLIFETIME(ASeinNetRelay, ProtocolContext);
	DOREPLIFETIME(ASeinNetRelay, SessionSeed);
}

void ASeinNetRelay::OnRep_AssignedPlayerID()
{
	UE_LOG(LogSeinNet, Log,
		TEXT("[Client] OnRep_AssignedPlayerID: slot=%u  seed=%lld  Owner=%s"),
		AssignedPlayerID.Value, SessionSeed, *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->NotifyLocalLobbySlotAssigned(this, AssignedPlayerID);
	}
	OnRep_ProtocolAssignment();
}

void ASeinNetRelay::OnRep_SessionSeed()
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] OnRep_SessionSeed: slot=%u  seed=%lld  Owner=%s"),
		AssignedPlayerID.Value, SessionSeed, *GetNameSafe(GetOwner()));

	// Replication does not guarantee the two property notifications arrive in
	// a particular order. If the slot is already known, re-run the idempotent
	// readiness/binding path now that the seed is available.
	OnRep_ProtocolAssignment();
}

void ASeinNetRelay::OnRep_ProtocolAssignment()
{
	// Identity properties remain useful for inspection and relay replacement,
	// but match activation comes through the atomic bootstrap RPC so a client
	// can never combine a new context with stale settings from another match.
}

void ASeinNetRelay::BeginPlay()
{
	Super::BeginPlay();

	const ENetMode NetMode = GetNetMode();
	const ENetRole LocalRole = GetLocalRole();
	UE_LOG(LogSeinNet, Verbose,
		TEXT("ASeinNetRelay::BeginPlay  NetMode=%d  LocalRole=%d  Owner=%s"),
		(int32)NetMode, (int32)LocalRole, *GetNameSafe(GetOwner()));

	// Register with the GI subsystem on whichever process this body is running in.
	// On the server, the relay was spawned by the subsystem so it's already tracked
	// — but we re-register to handle seamless travel / late binding paths.
	// On the client, this is the relay's first appearance and we need the
	// subsystem to learn its local relay handle.
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->RegisterRelay(this);
	}
}

void ASeinNetRelay::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->UnregisterRelay(this);
	}
	Super::EndPlay(Reason);
}

USeinNetSubsystem* ASeinNetRelay::GetNetSubsystem() const
{
	const UWorld* World = GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<USeinNetSubsystem>();
}

void ASeinNetRelay::Server_SubmitCommands_Implementation(
	const FSeinProtocolContext& Context,
	int32 TurnId,
	const FSeinOpaqueCommandBatch& OpaqueDrafts)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv opaque submission  TurnId=%d  Bytes=%d  FromSlot=%u  Owner=%s"),
		TurnId, OpaqueDrafts.Bytes.Num(), AssignedPlayerID.Value, *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleSubmission(this, Context, TurnId, OpaqueDrafts);
	}
}

void ASeinNetRelay::Client_ReceiveTurn_Implementation(
	const FSeinProtocolContext& Context,
	int32 TurnId,
	const FSeinOpaqueCommandBatch& OpaqueCommands)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] Recv opaque turn  TurnId=%d  Bytes=%d  Owner=%s"),
		TurnId, OpaqueCommands.Bytes.Num(), *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleTurn(Context, TurnId, OpaqueCommands);
	}
}

void ASeinNetRelay::Client_RequestMatchBootstrapReceipt_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleBootstrapReceiptRequest(Context);
	}
}

void ASeinNetRelay::Server_ReportMatchBootstrapReceipt_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleBootstrapReceipt(this, Context, Receipt);
	}
}

void ASeinNetRelay::Server_ReportMatchBootstrapFailure_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleBootstrapFailure(this, Context);
	}
}

void ASeinNetRelay::Client_AuthorizeMatchBootstrap_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleBootstrapAuthorization(Context, Receipt);
	}
}

void ASeinNetRelay::Server_ReportMatchBootstrapAuthorizedReady_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleBootstrapAuthorizedReady(this, Context, Receipt);
	}
}

void ASeinNetRelay::Client_LaunchMatchBootstrap_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleBootstrapLaunch(Context, Receipt);
	}
}

void ASeinNetRelay::Client_FailMatchBootstrap_Implementation(
	const FSeinProtocolContext& Context,
	const FString& Reason)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleBootstrapFailure(Context, Reason);
	}
}

void ASeinNetRelay::Client_PrepareMatchBootstrap_Implementation(
	FSeinPlayerID Slot,
	FSeinNetworkParticipantID ParticipantID,
	const FSeinProtocolContext& Context,
	int64 Seed,
	bool bSimulates,
	bool bAllowCurrentWorldActivation,
	const TArray<FSeinParticipantBinding>& ParticipantBindings,
	const FSeinMatchSettings& MatchSettings)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->NotifyLocalProtocolAssigned(
			this,
			Slot,
			ParticipantID,
			Context,
			Seed,
			bSimulates,
			ParticipantBindings,
			MatchSettings,
			bAllowCurrentWorldActivation
				? ESeinPreparedWorldActivation::AllowCurrentWorld
				: ESeinPreparedWorldActivation::RequiresWorldTransition);
	}
}

void ASeinNetRelay::Client_CancelPreparedMatchTravel_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandlePreparedMatchTravelCancelled(Context);
	}
}

// ============== Lobby RPCs (Phase 3b/c) ==============
//
// All lobby RPCs share the same plumbing: resolve the owning PC, resolve the
// GI's USeinLobbySubsystem, route to the matching ServerHandle*. The lobby
// subsystem owns validation (host-only, slot existence, faction validity,
// etc.) — relay just authenticates the source.

namespace
{
	USeinLobbySubsystem* ResolveLobbySubsystem(AActor* Actor)
	{
		if (!Actor) return nullptr;
		UWorld* World = Actor->GetWorld();
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
	}
}

void ASeinNetRelay::Server_RequestSlotClaim_Implementation(int32 SlotIndex, FSeinFactionID Faction)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv slot claim  Slot=%d  Faction=%u  FromOwner=%s"),
		SlotIndex, Faction.Value, *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	if (!OwnerPC)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] Server_RequestSlotClaim: relay has no owning PC — rejecting."));
		return;
	}

	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!Lobby)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] Server_RequestSlotClaim: USeinLobbySubsystem missing — rejecting."));
		return;
	}

	Lobby->ServerHandleSlotClaim(OwnerPC, SlotIndex, Faction);
}

void ASeinNetRelay::Server_RequestSetReady_Implementation(bool bReady)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv set-ready  bReady=%d  FromOwner=%s"),
		(int32)bReady, *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleSetReady(OwnerPC, bReady);
}

void ASeinNetRelay::Server_RequestSetTeam_Implementation(int32 SlotIndex, uint8 TeamID)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv set-team  Slot=%d  TeamID=%u  FromOwner=%s"),
		SlotIndex, TeamID, *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleSetTeam(OwnerPC, SlotIndex, TeamID);
}

void ASeinNetRelay::Server_RequestSetSlotState_Implementation(int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv set-slot-state  Slot=%d  NewState=%d  AIProfile=%s  FromOwner=%s"),
		SlotIndex, (int32)NewState, *AIProfile.ToString(), *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleSetSlotState(OwnerPC, SlotIndex, NewState, AIProfile);
}

void ASeinNetRelay::Server_RequestSelectMap_Implementation(const TSoftObjectPtr<UWorld>& Map)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv select-map  Map=%s  FromOwner=%s"),
		*Map.ToSoftObjectPath().ToString(), *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleSelectMap(OwnerPC, Map);
}

void ASeinNetRelay::Server_RequestKickPlayer_Implementation(int32 SlotIndex)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv kick-player  Slot=%d  FromOwner=%s"),
		SlotIndex, *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleKickPlayer(OwnerPC, SlotIndex);
}

void ASeinNetRelay::Server_RequestLeave_Implementation()
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv leave  FromOwner=%s"), *GetNameSafe(GetOwner()));

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	USeinLobbySubsystem* Lobby = ResolveLobbySubsystem(this);
	if (!OwnerPC || !Lobby) return;

	Lobby->ServerHandleLeave(OwnerPC);
}

void ASeinNetRelay::Server_ReportWorldStateRoot_Implementation(
	const FSeinProtocolContext& Context,
	int32 Turn,
	FGuid WorldRoot)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Coordinator] Recv world-state-root report  Turn=%d  Root=%s  FromSlot=%u"),
		Turn, *WorldRoot.ToString(EGuidFormats::Digits), AssignedPlayerID.Value);

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleWorldStateRootReport(
			this, Context, Turn, WorldRoot);
	}
}

void ASeinNetRelay::Server_RequestResync_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncRequest(this, Context);
	}
}

void ASeinNetRelay::Client_BeginCheckpointTransfer_Implementation(
	const FSeinProtocolContext& Context,
	int32 TransferId,
	int32 CheckpointTurn,
	int32 TotalChunks,
	int64 TotalBytes,
	int64 UncompressedBytes,
	bool bCompressed)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleBeginCheckpointTransfer(
			Context, TransferId, CheckpointTurn, TotalChunks, TotalBytes,
			UncompressedBytes, bCompressed);
	}
}

void ASeinNetRelay::Client_ReceiveCheckpointChunk_Implementation(
	const FSeinProtocolContext& Context,
	int32 TransferId,
	int32 ChunkIndex,
	const TArray<uint8>& Bytes)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleCheckpointChunk(
			Context, TransferId, ChunkIndex, Bytes);
	}
}

void ASeinNetRelay::Server_AcknowledgeCheckpointChunk_Implementation(
	const FSeinProtocolContext& Context,
	int32 TransferId,
	int32 NextChunkIndex)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncChunkAcknowledgement(
			this, Context, TransferId, NextChunkIndex);
	}
}

void ASeinNetRelay::Client_EndCheckpointTransfer_Implementation(
	const FSeinProtocolContext& Context,
	int32 TransferId)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleEndCheckpointTransfer(Context, TransferId);
	}
}

void ASeinNetRelay::Server_RequestResyncTail_Implementation(
	const FSeinProtocolContext& Context,
	int32 FromTurn)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncTailRequest(this, Context, FromTurn);
	}
}

void ASeinNetRelay::Client_NotifyResyncTailComplete_Implementation(
	const FSeinProtocolContext& Context,
	int32 LastTailTurn)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleResyncTailComplete(Context, LastTailTurn);
	}
}

void ASeinNetRelay::Server_AbortResync_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncAbort(this, Context);
	}
}

void ASeinNetRelay::Server_ReportResyncReady_Implementation(
	const FSeinProtocolContext& Context)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncReady(this, Context);
	}
}

void ASeinNetRelay::Client_NotifyResyncActivationCheck_Implementation(
	const FSeinProtocolContext& Context,
	int32 CheckTurn)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleResyncActivationCheck(Context, CheckTurn);
	}
}

void ASeinNetRelay::Server_ReportResyncActivationRoot_Implementation(
	const FSeinProtocolContext& Context,
	int32 CheckTurn,
	FGuid WorldRoot)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleResyncActivationRoot(
			this, Context, CheckTurn, WorldRoot);
	}
}

void ASeinNetRelay::Client_NotifyResyncActivation_Implementation(
	const FSeinProtocolContext& Context,
	bool bActivated,
	int32 FirstAuthoredTurn,
	const FString& Reason)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleResyncActivation(
			Context, bActivated, FirstAuthoredTurn, Reason);
	}
}

void ASeinNetRelay::Server_ReportDeterminismSessionFailure_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinDeterminismSessionFailure& Failure)
{
	UE_LOG(LogSeinNet, Error,
		TEXT("[Coordinator] Recv determinism-session failure  Turn=%d  Kind=%d  FromSlot=%u"),
		Failure.Turn,
		static_cast<int32>(Failure.Kind),
		AssignedPlayerID.Value);

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleDeterminismSessionFailure(
			this, Context, Failure);
	}
}

void ASeinNetRelay::Server_ReportConfigFingerprint_Implementation(
	const FSeinProtocolContext& Context,
	int32 Fingerprint,
	FGuid CommandProtocolDigest,
	FGuid MatchSettingsDigest,
	FGuid SimulationContentDigest)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv config fingerprint 0x%08x  FromSlot=%u"),
		static_cast<uint32>(Fingerprint), AssignedPlayerID.Value);

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleConfigFingerprint(
			this,
			Context,
			Fingerprint,
			CommandProtocolDigest,
			MatchSettingsDigest,
			SimulationContentDigest);
	}
}

void ASeinNetRelay::Client_NotifyDesync_Implementation(
	const FSeinProtocolContext& Context,
	int32 Turn,
	const TArray<FSeinParticipantWorldRootEntry>& PeerRoots)
{
	UE_LOG(LogSeinNet, Error,
		TEXT("[Client] Recv DESYNC alarm  Turn=%d  PeerCount=%d"),
		Turn, PeerRoots.Num());

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleDesyncNotification(Context, Turn, PeerRoots);
	}
}

void ASeinNetRelay::Client_NotifyDeterminismSessionFailure_Implementation(
	const FSeinProtocolContext& Context,
	const FSeinDeterminismSessionFailure& Failure)
{
	UE_LOG(LogSeinNet, Error,
		TEXT("[Client] Recv determinism-session failure  Turn=%d  Kind=%d  Participant=%s"),
		Failure.Turn,
		static_cast<int32>(Failure.Kind),
		*Failure.ParticipantID.ToCanonicalString());

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->HandleAuthoritativeDeterminismSessionFailure(
			Context, Failure);
	}
}

void ASeinNetRelay::Client_NotifyKicked_Implementation(const FString& Reason)
{
	UE_LOG(LogSeinNet, Log,
		TEXT("[Client] Kicked from lobby: %s  Owner=%s"),
		*Reason, *GetNameSafe(GetOwner()));

	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC) return;

	// Mirror SeinRequestLeaveLobby: disconnect from the server, then travel
	// to the project's MainMenuMap. UE's built-in `ClientReturnToMainMenuWithTextReason`
	// triggers `LocalPlayer->ReturnToMainMenu()` which falls back to the
	// project's GameDefaultMap if no menu map is configured at the engine
	// level — that's how kicked players end up alone in the gameplay map.
	// Going through the same path as the explicit Leave button guarantees
	// consistent end-up-at-menu behavior.
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (USeinLobbySubsystem* Lobby =
			GameInstance->GetSubsystem<USeinLobbySubsystem>())
		{
			// The session is over for this process: drop the lobby contract
			// and slot bindings before the menu world initializes, so it
			// neither shows a dead roster nor bootstraps from stale settings.
			Lobby->ResetForLocalSessionExit();
		}
	}
	PC->ConsoleCommand(TEXT("disconnect"));

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && !Settings->MainMenuMap.IsNull())
	{
		const FString MapName = Settings->MainMenuMap.ToSoftObjectPath().GetLongPackageName();
		if (!MapName.IsEmpty())
		{
			UGameplayStatics::OpenLevel(World, FName(*MapName));
		}
	}
}
