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
	DOREPLIFETIME(ASeinNetRelay, SessionSeed);
}

void ASeinNetRelay::OnRep_AssignedPlayerID()
{
	UE_LOG(LogSeinNet, Log,
		TEXT("[Client] OnRep_AssignedPlayerID: slot=%u  seed=%lld  Owner=%s"),
		AssignedPlayerID.Value, SessionSeed, *GetNameSafe(GetOwner()));

	// Latch into the subsystem so SubmitLocalCommand and gameplay code can read
	// it. Note: this OnRep fires only on the owning client (relay is owner-only).
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->NotifyLocalSlotAssigned(this, AssignedPlayerID, SessionSeed);
	}
}

void ASeinNetRelay::OnRep_SessionSeed()
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] OnRep_SessionSeed: slot=%u  seed=%lld  Owner=%s"),
		AssignedPlayerID.Value, SessionSeed, *GetNameSafe(GetOwner()));

	// Replication does not guarantee the two property notifications arrive in
	// a particular order. If the slot is already known, re-run the idempotent
	// readiness/binding path now that the seed is available.
	if (AssignedPlayerID.IsValid())
	{
		if (USeinNetSubsystem* Net = GetNetSubsystem())
		{
			Net->NotifyLocalSlotAssigned(this, AssignedPlayerID, SessionSeed);
		}
	}
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

void ASeinNetRelay::Server_SubmitCommands_Implementation(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv submission  TurnId=%d  Count=%d  FromSlot=%u  Owner=%s"),
		TurnId, Commands.Num(), AssignedPlayerID.Value, *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleSubmission(this, TurnId, Commands);
	}
}

void ASeinNetRelay::Client_ReceiveTurn_Implementation(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] Recv turn  TurnId=%d  Count=%d  Owner=%s"),
		TurnId, Commands.Num(), *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleTurn(TurnId, Commands);
	}
}

void ASeinNetRelay::Client_StartSession_Implementation()
{
	UE_LOG(LogSeinNet, Log, TEXT("[Client] Client_StartSession received  Owner=%s"), *GetNameSafe(GetOwner()));
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->StartLocalSession();
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

void ASeinNetRelay::Server_ReportStateHash_Implementation(int32 Turn, int32 Hash)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv state-hash report  Turn=%d  Hash=0x%08x  FromSlot=%u"),
		Turn, static_cast<uint32>(Hash), AssignedPlayerID.Value);

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleStateHashReport(this, Turn, Hash);
	}
}

void ASeinNetRelay::Server_ReportConfigFingerprint_Implementation(int32 Fingerprint)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv config fingerprint 0x%08x  FromSlot=%u"),
		static_cast<uint32>(Fingerprint), AssignedPlayerID.Value);

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleConfigFingerprint(this, Fingerprint);
	}
}

void ASeinNetRelay::Client_NotifyDesync_Implementation(int32 Turn, const TArray<FSeinSlotHashEntry>& PeerHashes)
{
	UE_LOG(LogSeinNet, Error,
		TEXT("[Client] Recv DESYNC alarm  Turn=%d  PeerCount=%d"),
		Turn, PeerHashes.Num());

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleDesyncNotification(Turn, PeerHashes);
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
