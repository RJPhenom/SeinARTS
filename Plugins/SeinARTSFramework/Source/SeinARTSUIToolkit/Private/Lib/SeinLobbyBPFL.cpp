/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyBPFL.cpp
 */

#include "Lib/SeinLobbyBPFL.h"
#include "ViewModel/SeinLobbyViewModel.h"
#include "Core/SeinUISubsystem.h"
#include "SeinNetSubsystem.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetRelay.h"
#include "SeinLobbyState.h"
#include "Subsystems/SeinFactionService.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinMatchSettings.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/AssetManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	UWorld* ResolveWorld(const UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		return WorldContextObject->GetWorld();
	}

	/** Find the local relay (LocalController-owned) for sending a Server_* RPC.
	 *  Returns nullptr if not yet replicated — caller may want to retry next
	 *  frame (or report failure to the user). */
	ASeinNetRelay* ResolveLocalRelay(const USeinNetSubsystem* Net)
	{
		if (!Net) return nullptr;
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
		{
			ASeinNetRelay* R = Wp.Get();
			if (!R) continue;
			APlayerController* PC = Cast<APlayerController>(R->GetOwner());
			if (PC && PC->IsLocalController())
			{
				return R;
			}
		}
		return nullptr;
	}
}

USeinLobbyViewModel* USeinLobbyBPFL::SeinGetOrCreateLobbyViewModel(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return nullptr;

	USeinUISubsystem* UISub = World->GetSubsystem<USeinUISubsystem>();
	if (!UISub) return nullptr;

	return UISub->GetOrCreateLobbyViewModel();
}

bool USeinLobbyBPFL::SeinRequestSlotClaim(const UObject* WorldContextObject, int32 SlotIndex, FSeinFactionID FactionID)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	// Standalone fast-path: drop directly into the lobby subsystem.
	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby || !PC) return false;
		return Lobby->ServerHandleSlotClaim(PC, SlotIndex, FactionID);
	}

	// Networked: find the local relay + send Server_RequestSlotClaim. Same
	// path the `Sein.Net.Lobby.Claim` console command uses.
	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	if (!Net) return false;

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
	{
		ASeinNetRelay* R = Wp.Get();
		if (!R) continue;
		APlayerController* PC = Cast<APlayerController>(R->GetOwner());
		if (PC && PC->IsLocalController())
		{
			R->Server_RequestSlotClaim(SlotIndex, FactionID);
			return true;
		}
	}
	return false;
}

bool USeinLobbyBPFL::SeinRequestStartMatch(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_Client)
	{
		// Clients cannot start a match. Designers should hide / disable the
		// button on non-host clients via the view model's IsHost() check.
		return false;
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
	if (!Lobby) return false;

	// Travel requested whenever any gameplay map (runtime override OR plugin
	// setting fallback) is configured. Lobby's ResolveGameplayMap() handles
	// the priority; here we just check "is there any map at all."
	const bool bTravel = !Lobby->ResolveGameplayMap().IsNull();
	return Lobby->ServerStartMatch(bTravel);
}

namespace
{
	/** Cancel any in-flight connection attempt (UPendingNetGame). Without
	 *  this, calling Listen() or ClientTravel() while a previous JOIN is
	 *  still trying to connect crashes — the world is in a half-transition
	 *  state with both a NetDriver attempt and the new request fighting
	 *  for the world context. */
	void CancelPendingNetGame(UWorld* World)
	{
		if (!World || !GEngine) return;
		const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
		if (Context && Context->PendingNetGame)
		{
			// UEngine::CancelPending is protected; CancelAllPending is the
			// public surface (per UEngine API). Cancels every active
			// PendingNetGame across world contexts — fine for our case
			// where the local PC has one in-flight connection attempt.
			GEngine->CancelAllPending();
		}
	}
}

bool USeinLobbyBPFL::SeinRequestHostSession(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	// Already a server? No-op success — caller can treat as "now hosting".
	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
	{
		return true;
	}

	// Cancel any in-flight client connection (e.g. a stale JOIN attempt
	// to a non-existent server still spinning toward its 90s timeout).
	// Without this, Listen() on a world with a PendingNetGame crashes.
	CancelPendingNetGame(World);

	// Stand up a listen server in-place. UWorld::Listen creates the
	// GameNetDriver bound to the configured port without a map transition,
	// so the lobby UI already loaded in this map keeps running. The URL's
	// only flag is `?listen`; everything else (port, options) flows from
	// project network settings.
	FURL ListenURL(nullptr, TEXT("?listen"), TRAVEL_Absolute);
	const bool bOk = World->Listen(ListenURL);
	if (!bOk)
	{
		// Listen() returns false when a net driver couldn't be created
		// (port in use, networking disabled, etc.). Caller's UI should
		// surface a "Could not start host" message.
		return false;
	}

	// Backfill the host's relay. The local PC was created back when this world
	// was Standalone, so `HandleStartingNewPlayer` already ran for it and
	// assigned `SeinPlayerID` — but `Net->ServerSpawnRelayForController` was
	// short-circuited at that time by `IsServer()` returning false (NetMode
	// was Standalone, not yet ListenServer). Now that Listen() flipped the
	// NetMode, IsServer() is true and the spawn will proceed. Without this
	// re-call, the host has no relay → `Net->GetLocalPlayerID()` stays
	// Neutral → `IsInLobbySession()` returns false → the lobby panel stays
	// disabled even though the listen server is up.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
		{
			if (ASeinPlayerController* HostPC = Cast<ASeinPlayerController>(World->GetFirstPlayerController()))
			{
				if (HostPC->SeinPlayerID.IsValid())
				{
					Net->ServerSpawnRelayForController(HostPC, HostPC->SeinPlayerID);
				}
			}
		}
	}
	return true;
}

bool USeinLobbyBPFL::SeinRequestJoinSession(const UObject* WorldContextObject, const FString& ServerAddress)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;
	if (ServerAddress.IsEmpty()) return false;

	// Refuse to join while hosting — the listen server has clients of
	// its own, dropping it implicitly via ClientTravel is fragile and
	// can crash mid-handshake. Surface the error; caller's UI prompts
	// the user to disconnect first.
	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SeinRequestJoinSession: cannot join while hosting (NetMode=%d). Disconnect first."),
			(int32)Mode);
		return false;
	}

	// Cancel any in-flight prior connection so the new ClientTravel doesn't
	// conflict with a half-completed previous attempt.
	CancelPendingNetGame(World);

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return false;

	// ClientTravel handles the connection handshake to a remote server.
	// The remote's current map auto-loads on the local client. Equivalent
	// to `open <ip>` console command but BP-callable and explicit.
	PC->ClientTravel(ServerAddress, TRAVEL_Absolute);
	return true;
}

void USeinLobbyBPFL::SeinRequestLeaveLobby(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return;
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return;

	// Notify the server FIRST (via the relay's Server_RequestLeave RPC) so
	// the lobby can release the slot to fully Open instead of marking it
	// disconnected-but-reserved. Without this, OnLogout (fired after the
	// disconnect below) treats the leave as a network drop and reserves
	// the slot for `LobbyReconnectGraceSeconds`. Reliable RPCs are queued
	// in order, so the leave signal arrives before the disconnect tears
	// down the channel.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
		{
			if (ASeinNetRelay* R = ResolveLocalRelay(Net))
			{
				R->Server_RequestLeave();
			}
		}
	}

	// Cancel any in-flight PendingNetGame BEFORE disconnecting + opening the
	// menu map. The `disconnect` console command tears down established
	// NetConnections but does NOT cancel a PendingNetGame still in its
	// connection-attempt phase (the state JOIN leaves you in if the remote
	// host is unreachable). Without this, a JOIN-fails-then-LEAVE flow
	// runs OpenLevel(MainMenu) while the engine is still processing a
	// PendingNetGame — the new world loads but its UI / pawn possession
	// initialization gets dropped, leaving the player stuck in a viewport
	// with no menu and no input. Same helper HOST/JOIN already use to
	// prevent the analogous pre-existing-pending bug.
	CancelPendingNetGame(World);

	// ConsoleCommand("disconnect") is the cleanest cross-mode disconnect:
	// host kicks the listen-server, client drops its connection. Project
	// hooks main-menu return via UE's standard NetworkFailure / MapTravel
	// delegates if a custom return-to-menu path is needed.
	PC->ConsoleCommand(TEXT("disconnect"));

	// If the project ships a main-menu map in plugin settings, travel there
	// after the disconnect. UGameplayStatics::OpenLevel is safe post-
	// disconnect (no NetDriver required); resolves the soft world path to
	// a map name string for the engine.
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

bool USeinLobbyBPFL::SeinRequestSetReady(const UObject* WorldContextObject, bool bReady)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby || !PC) return false;
		return Lobby->ServerHandleSetReady(PC, bReady);
	}

	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	ASeinNetRelay* R = ResolveLocalRelay(Net);
	if (!R) return false;
	R->Server_RequestSetReady(bReady);
	return true;
}

bool USeinLobbyBPFL::SeinRequestSetTeam(const UObject* WorldContextObject, int32 SlotIndex, uint8 TeamID)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby) return false;
		return Lobby->ServerHandleSetTeam(PC, SlotIndex, TeamID);
	}

	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	ASeinNetRelay* R = ResolveLocalRelay(Net);
	if (!R) return false;
	R->Server_RequestSetTeam(SlotIndex, TeamID);
	return true;
}

bool USeinLobbyBPFL::SeinRequestSetSlotState(const UObject* WorldContextObject, int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby) return false;
		return Lobby->ServerHandleSetSlotState(PC, SlotIndex, NewState, AIProfile);
	}

	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	ASeinNetRelay* R = ResolveLocalRelay(Net);
	if (!R) return false;
	R->Server_RequestSetSlotState(SlotIndex, NewState, AIProfile);
	return true;
}

bool USeinLobbyBPFL::SeinRequestKickPlayer(const UObject* WorldContextObject, int32 SlotIndex)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby) return false;
		return Lobby->ServerHandleKickPlayer(PC, SlotIndex);
	}

	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	ASeinNetRelay* R = ResolveLocalRelay(Net);
	if (!R) return false;
	R->Server_RequestKickPlayer(SlotIndex);
	return true;
}

TArray<TSoftObjectPtr<USeinFaction>> USeinLobbyBPFL::SeinGetAvailableFactions(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return {};
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return {};
	USeinFactionService* FS = GI->GetSubsystem<USeinFactionService>();
	return FS ? FS->GetAvailableFactions() : TArray<TSoftObjectPtr<USeinFaction>>{};
}

bool USeinLobbyBPFL::SeinRequestSelectMap(const UObject* WorldContextObject, TSoftObjectPtr<UWorld> Map)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	// Standalone fast-path: drop directly into the lobby subsystem. Useful for
	// PIE-direct lobby UI iteration without standing up a listen server.
	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby || !PC) return false;
		return Lobby->ServerHandleSelectMap(PC, Map);
	}

	// Networked: route through the local relay's Server_RequestSelectMap.
	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	ASeinNetRelay* R = ResolveLocalRelay(Net);
	if (!R) return false;
	R->Server_RequestSelectMap(Map);
	return true;
}

TArray<FSeinLobbyMapEntry> USeinLobbyBPFL::SeinGetAvailableMaps()
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings ? Settings->AvailableMaps : TArray<FSeinLobbyMapEntry>{};
}

