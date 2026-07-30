/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetExec.cpp
 * @brief   Phase 1 console hooks for proving the lockstep wire end-to-end in PIE.
 *
 *   Sein.Net.TestPing [TurnId]   — local-PC submit of a single Ping command.
 *                                  Optional TurnId for testing the completeness
 *                                  gate (run on every window with the same
 *                                  TurnId to see one fan-out). With no arg,
 *                                  uses an auto-incrementing local counter.
 *
 *   Sein.Net.Status              — dump NetMode, networking-active flag,
 *                                  LocalPlayerID, SessionSeed, slot/relay counts.
 *
 * Drive these from the in-PIE console (~). Watch LogSeinNet at Verbose to see
 * each hop. Every PIE window is independent — fire on each window with the
 * same TurnId to demonstrate the completeness gate (server holds until every
 * connected slot has submitted).
 */

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

#include "SeinARTSNet.h"
#include "SeinNetSubsystem.h"
#include "SeinNetRelay.h"
#include "SeinReplayWriter.h"
#include "SeinReplayReader.h"
#include "SeinLobbySubsystem.h"
#include "SeinLobbyState.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinLobbyMapEntry.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinEntityPool.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinFaction.h"
#include "Subsystems/SeinFactionService.h"
#include "GameplayTagContainer.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "HAL/FileManager.h"
#include "Types/Vector.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"

namespace
{
	static int32 GTestPingTurnCounter = 0;

	void HandleTestPing(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: no World."));
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		if (!GI)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: no GameInstance."));
			return;
		}
		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		if (!Net)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: USeinNetSubsystem missing."));
			return;
		}

		// Resolve TurnId: explicit arg if given, else auto-incrementing local
		// counter. To exercise the completeness gate, run with the SAME TurnId
		// on every PIE window: server holds the assembly until every slot has
		// submitted, then fans out exactly once.
		int32 TurnId = 0;
		if (Args.Num() > 0)
		{
			TurnId = FCString::Atoi(*Args[0]);
		}
		else
		{
			TurnId = ++GTestPingTurnCounter;
		}

		// Build a Ping command stamped with the local slot. Server overwrites
		// PlayerID anyway from the source relay's AssignedPlayerID, but a
		// correct local stamp keeps the local log readable pre-network.
		const FSeinPlayerID Slot = Net->GetLocalPlayerID();
		const FFixedVector FakeLoc = FFixedVector();
		const FSeinCommand Cmd = FSeinCommand::MakePingCommand(Slot, FakeLoc);

		Ar.Logf(TEXT("[SeinNet] TestPing: TurnId=%d  LocalSlot=%u  Active=%d  submitting..."),
			TurnId, Slot.Value, (int32)Net->IsNetworkingActive());
		Net->SubmitLocalCommandAtTurn(TurnId, Cmd);
	}

	void HandleStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] Status: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;

		if (!Net)
		{
			Ar.Logf(TEXT("[SeinNet] Status: NetMode=%d  USeinNetSubsystem MISSING."), (int32)World->GetNetMode());
			return;
		}

		Ar.Logf(TEXT("[SeinNet] Status:"));
		Ar.Logf(TEXT("  NetMode             = %d"), (int32)World->GetNetMode());
		Ar.Logf(TEXT("  NetworkingActive    = %d"), (int32)Net->IsNetworkingActive());
		Ar.Logf(TEXT("  LocalPlayerID       = %u  (0 = not yet assigned)"), Net->GetLocalPlayerID().Value);
		Ar.Logf(TEXT("  SessionSeed         = %lld"), Net->GetSessionSeed());
		Ar.Logf(TEXT("  RegisteredRelays    = %d"), Net->GetRelays().Num());
		Ar.Logf(TEXT("  ActiveSlots(server) = %d"), Net->GetActiveSlotCount());
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdTestPing(
		TEXT("Sein.Net.TestPing"),
		TEXT("Sein.Net.TestPing [TurnId] — submit a Ping command for the given TurnId (default: auto-increment). Use the same TurnId on every window to test the completeness gate."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleTestPing));

	void HandleStartMatch(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] StartMatch: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] StartMatch: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Logf(TEXT("[SeinNet] StartMatch: must be run on the SERVER (NetMode=%d). Run on the host (Listen Server) window."), (int32)Mode);
			return;
		}

		// Phase 3b: snapshot the lobby BEFORE the lockstep session starts so
		// any future map-travel / GameMode resolve picks up the captured slot
		// manifest (faction picks, claims, AI policies). Lobby snapshot is a
		// no-op if no lobby actor exists; safe to call always.
		if (USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
		{
			Lobby->PublishMatchSettingsSnapshot();
			const FSeinMatchSettings& Snap = Lobby->GetPublishedSnapshot();
			Ar.Logf(TEXT("[SeinNet] StartMatch: captured lobby snapshot (%d slot(s))."), Snap.Slots.Num());
		}
		else
		{
			Ar.Log(TEXT("[SeinNet] StartMatch: USeinLobbySubsystem missing — proceeding without snapshot."));
		}

		Ar.Logf(TEXT("[SeinNet] StartMatch: triggering lockstep session start across %d relay(s)."), Net->GetRelays().Num());
		Net->StartLockstepSession();
	}

	// ============== Lobby commands (Phase 3b) ==============

	const TCHAR* SlotStateToString(ESeinSlotState S)
	{
		switch (S)
		{
		case ESeinSlotState::Open:   return TEXT("Open");
		case ESeinSlotState::Human:  return TEXT("Human");
		case ESeinSlotState::AI:     return TEXT("AI");
		case ESeinSlotState::Closed: return TEXT("Closed");
		default:                     return TEXT("?");
		}
	}

	void HandleLobbyStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Status: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinLobbySubsystem* Lobby = GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
		if (!Lobby) { Ar.Log(TEXT("[SeinLobby] Status: USeinLobbySubsystem missing.")); return; }

		ASeinLobbyState* Actor = Lobby->GetLobbyState();
		if (!Actor)
		{
			Ar.Logf(TEXT("[SeinLobby] No lobby actor yet (NetMode=%d). On clients this means initial replication hasn't arrived."),
				(int32)World->GetNetMode());
			return;
		}

		Ar.Logf(TEXT("[SeinLobby] Lobby state (%d slot(s), NetMode=%d, SnapshotPublished=%d):"),
			Actor->Slots.Num(), (int32)World->GetNetMode(), (int32)Lobby->HasPublishedSnapshot());
		for (const FSeinLobbySlotState& Slot : Actor->Slots)
		{
			Ar.Logf(TEXT("  slot=%d  state=%s  bClaimed=%d  bDisc=%d  bReady=%d  ClaimedBy=%u  Faction=%u  Team=%u  Name=\"%s\"  Addr=\"%s\""),
				Slot.SlotIndex,
				SlotStateToString(Slot.State),
				(int32)Slot.bClaimed,
				(int32)Slot.bDisconnected,
				(int32)Slot.bReady,
				Slot.ClaimedBy.Value,
				Slot.FactionID.Value,
				Slot.TeamID,
				*Slot.DisplayName.ToString(),
				*Slot.RemoteAddress);
		}
	}

	void HandleLobbyClaim(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Claim: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinLobby] Claim: usage: Sein.Net.Lobby.Claim <SlotIndex> [FactionID]"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		const int32 Faction = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
		if (Slot <= 0 || Slot > 255)
		{
			Ar.Logf(TEXT("[SeinLobby] Claim: SlotIndex must be 1..255 (got %d)."), Slot);
			return;
		}
		if (Faction < 0 || Faction > 255)
		{
			Ar.Logf(TEXT("[SeinLobby] Claim: FactionID must be 0..255 (got %d)."), Faction);
			return;
		}

		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinLobby] Claim: USeinNetSubsystem missing.")); return; }

		// Standalone fast-path: drop the call directly into the lobby subsystem
		// using whichever local PC is available.
		if (World->GetNetMode() == NM_Standalone)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			APlayerController* PC = World->GetFirstPlayerController();
			if (!Lobby || !PC)
			{
				Ar.Log(TEXT("[SeinLobby] Claim [Standalone]: no Lobby or PC."));
				return;
			}
			const bool bOk = Lobby->ServerHandleSlotClaim(PC, Slot, FSeinFactionID(static_cast<uint8>(Faction)));
			Ar.Logf(TEXT("[SeinLobby] Claim [Standalone]: slot=%d faction=%d  result=%s"),
				Slot, Faction, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		// Networked: route via the local relay → Server_RequestSlotClaim.
		ASeinNetRelay* Relay = nullptr;
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
		{
			ASeinNetRelay* R = Wp.Get();
			if (!R) continue;
			APlayerController* PC = Cast<APlayerController>(R->GetOwner());
			if (PC && PC->IsLocalController())
			{
				Relay = R;
				break;
			}
		}
		if (!Relay)
		{
			Ar.Log(TEXT("[SeinLobby] Claim: no local relay yet (PostLogin not reached?)."));
			return;
		}

		Ar.Logf(TEXT("[SeinLobby] Claim: requesting slot=%d faction=%d via relay %s"),
			Slot, Faction, *GetNameSafe(Relay));
		Relay->Server_RequestSlotClaim(Slot, FSeinFactionID(static_cast<uint8>(Faction)));
	}

	void HandleLobbyInit(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Init: no World.")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer && Mode != NM_Standalone)
		{
			Ar.Logf(TEXT("[SeinLobby] Init: SERVER ONLY (NetMode=%d)."), (int32)Mode);
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		USeinLobbySubsystem* Lobby = GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
		if (!Lobby) { Ar.Log(TEXT("[SeinLobby] Init: USeinLobbySubsystem missing.")); return; }

		const int32 SlotCount = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		Lobby->InitializeLobby(SlotCount);
		Ar.Logf(TEXT("[SeinLobby] Init: re-seeded with %d Open slot(s) (0 = use settings default)."),
			SlotCount);
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyStatus(
		TEXT("Sein.Net.Lobby.Status"),
		TEXT("Dump current lobby slot state. Run on each PIE window: server is authoritative, clients mirror via replication."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyStatus));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyClaim(
		TEXT("Sein.Net.Lobby.Claim"),
		TEXT("Sein.Net.Lobby.Claim <SlotIndex> [FactionID] — request the given slot for the local player. Faction defaults to 0 (unset). Server validates."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyClaim));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyInit(
		TEXT("Sein.Net.Lobby.Init"),
		TEXT("SERVER ONLY. Sein.Net.Lobby.Init [SlotCount] — re-seed the lobby with N Open slots (default = preset's count, then settings.MaxPlayers). Wipes existing claims."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyInit));

	// ============== Lobby verbs (Phase 3c) ==============
	//
	// All command handlers below share the same shape: resolve the local PC
	// in standalone mode (drop directly into the subsystem) OR find the local
	// relay and send the matching Server_Request* RPC. The lobby subsystem
	// owns validation; these are just terminals.

	void HandleLobbySetReady(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] SetReady: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinLobby] SetReady: usage: Sein.Net.Lobby.SetReady <0|1>"));
			return;
		}
		const bool bReady = (FCString::Atoi(*Args[0]) != 0);

		UGameInstance* GI = World->GetGameInstance();
		if (!GI) { Ar.Log(TEXT("[SeinLobby] SetReady: no GI.")); return; }

		if (World->GetNetMode() == NM_Standalone)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			APlayerController* PC = World->GetFirstPlayerController();
			if (!Lobby || !PC) { Ar.Log(TEXT("[SeinLobby] SetReady: no Lobby/PC.")); return; }
			const bool bOk = Lobby->ServerHandleSetReady(PC, bReady);
			Ar.Logf(TEXT("[SeinLobby] SetReady [Standalone]: bReady=%d  result=%s"),
				(int32)bReady, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		ASeinNetRelay* Relay = nullptr;
		if (Net)
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (!R) continue;
				APlayerController* PC = Cast<APlayerController>(R->GetOwner());
				if (PC && PC->IsLocalController()) { Relay = R; break; }
			}
		}
		if (!Relay) { Ar.Log(TEXT("[SeinLobby] SetReady: no local relay yet.")); return; }
		Relay->Server_RequestSetReady(bReady);
		Ar.Logf(TEXT("[SeinLobby] SetReady: routed via relay  bReady=%d"), (int32)bReady);
	}

	void HandleLobbySetTeam(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] SetTeam: no World.")); return; }
		if (Args.Num() < 2)
		{
			Ar.Log(TEXT("[SeinLobby] SetTeam: usage: Sein.Net.Lobby.SetTeam <SlotIndex> <TeamID>"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		const int32 Team = FCString::Atoi(*Args[1]);
		if (Slot <= 0 || Team < 0 || Team > 255)
		{
			Ar.Logf(TEXT("[SeinLobby] SetTeam: SlotIndex>0, TeamID 0..255 (got slot=%d team=%d)."), Slot, Team);
			return;
		}

		UGameInstance* GI = World->GetGameInstance();
		if (!GI) return;

		if (World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			if (!Lobby) return;
			APlayerController* PC = World->GetFirstPlayerController();
			const bool bOk = Lobby->ServerHandleSetTeam(PC, Slot, static_cast<uint8>(Team));
			Ar.Logf(TEXT("[SeinLobby] SetTeam [Server]: slot=%d team=%d  result=%s"),
				Slot, Team, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		ASeinNetRelay* Relay = nullptr;
		if (Net)
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (!R) continue;
				APlayerController* PC = Cast<APlayerController>(R->GetOwner());
				if (PC && PC->IsLocalController()) { Relay = R; break; }
			}
		}
		if (!Relay) { Ar.Log(TEXT("[SeinLobby] SetTeam: no local relay yet.")); return; }
		Relay->Server_RequestSetTeam(Slot, static_cast<uint8>(Team));
		Ar.Logf(TEXT("[SeinLobby] SetTeam: routed via relay  slot=%d team=%d"), Slot, Team);
	}

	void HandleLobbySetSlotState(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] SetSlotState: no World.")); return; }
		if (Args.Num() < 2)
		{
			Ar.Log(TEXT("[SeinLobby] SetSlotState: usage: Sein.Net.Lobby.SetSlotState <SlotIndex> <Open|Human|AI|Closed> [AIProfileTag]"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		ESeinSlotState NewState = ESeinSlotState::Open;
		const FString& StateArg = Args[1];
		if (StateArg.Equals(TEXT("Open"), ESearchCase::IgnoreCase))   NewState = ESeinSlotState::Open;
		else if (StateArg.Equals(TEXT("Human"), ESearchCase::IgnoreCase)) NewState = ESeinSlotState::Human;
		else if (StateArg.Equals(TEXT("AI"), ESearchCase::IgnoreCase))     NewState = ESeinSlotState::AI;
		else if (StateArg.Equals(TEXT("Closed"), ESearchCase::IgnoreCase)) NewState = ESeinSlotState::Closed;
		else
		{
			Ar.Logf(TEXT("[SeinLobby] SetSlotState: unknown state '%s'. Use Open / Human / AI / Closed."), *StateArg);
			return;
		}
		FGameplayTag AIProfile;
		if (Args.Num() >= 3)
		{
			AIProfile = FGameplayTag::RequestGameplayTag(FName(*Args[2]), /*ErrorIfNotFound=*/false);
		}

		UGameInstance* GI = World->GetGameInstance();
		if (!GI) return;

		if (World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			if (!Lobby) return;
			APlayerController* PC = World->GetFirstPlayerController();
			const bool bOk = Lobby->ServerHandleSetSlotState(PC, Slot, NewState, AIProfile);
			Ar.Logf(TEXT("[SeinLobby] SetSlotState [Server]: slot=%d state=%s  result=%s"),
				Slot, *StateArg, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		ASeinNetRelay* Relay = nullptr;
		if (Net)
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (!R) continue;
				APlayerController* PC = Cast<APlayerController>(R->GetOwner());
				if (PC && PC->IsLocalController()) { Relay = R; break; }
			}
		}
		if (!Relay) { Ar.Log(TEXT("[SeinLobby] SetSlotState: no local relay yet.")); return; }
		Relay->Server_RequestSetSlotState(Slot, NewState, AIProfile);
		Ar.Logf(TEXT("[SeinLobby] SetSlotState: routed via relay  slot=%d state=%s"), Slot, *StateArg);
	}

	void HandleLobbySelectMap(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] SelectMap: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinLobby] SelectMap: usage: Sein.Net.Lobby.SelectMap <Index|PackageName>"));
			Ar.Log(TEXT("    Index: 0-based row from PluginSettings AvailableMaps."));
			Ar.Log(TEXT("    PackageName: e.g. /Game/Maps/LVL_1v1 (matched against AvailableMaps)."));
			return;
		}

		// Resolve the candidate map. Accept either:
		//   - a numeric index into PluginSettings::AvailableMaps (convenience), or
		//   - a string that matches an entry's package path / asset name.
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings || Settings->AvailableMaps.IsEmpty())
		{
			Ar.Log(TEXT("[SeinLobby] SelectMap: PluginSettings AvailableMaps is empty — configure it under Project Settings → SeinARTS → Network|Lobby."));
			return;
		}

		TSoftObjectPtr<UWorld> Candidate;
		const FString& Arg = Args[0];

		bool bIsIndex = !Arg.IsEmpty();
		for (TCHAR Ch : Arg) { if (!FChar::IsDigit(Ch)) { bIsIndex = false; break; } }

		if (bIsIndex)
		{
			const int32 Index = FCString::Atoi(*Arg);
			if (!Settings->AvailableMaps.IsValidIndex(Index))
			{
				Ar.Logf(TEXT("[SeinLobby] SelectMap: index %d out of range (0..%d)."),
					Index, Settings->AvailableMaps.Num() - 1);
				return;
			}
			Candidate = Settings->AvailableMaps[Index].Map;
		}
		else
		{
			for (const FSeinLobbyMapEntry& Entry : Settings->AvailableMaps)
			{
				const FString Path = Entry.Map.ToSoftObjectPath().ToString();
				if (Path.Contains(Arg))
				{
					Candidate = Entry.Map;
					break;
				}
			}
			if (Candidate.IsNull())
			{
				Ar.Logf(TEXT("[SeinLobby] SelectMap: no AvailableMaps entry matches '%s'."), *Arg);
				return;
			}
		}

		UGameInstance* GI = World->GetGameInstance();
		if (!GI) return;

		if (World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			if (!Lobby) return;
			APlayerController* PC = World->GetFirstPlayerController();
			const bool bOk = Lobby->ServerHandleSelectMap(PC, Candidate);
			Ar.Logf(TEXT("[SeinLobby] SelectMap [Server]: map=%s  result=%s"),
				*Candidate.ToSoftObjectPath().ToString(), bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		ASeinNetRelay* Relay = nullptr;
		if (Net)
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (!R) continue;
				APlayerController* PC = Cast<APlayerController>(R->GetOwner());
				if (PC && PC->IsLocalController()) { Relay = R; break; }
			}
		}
		if (!Relay) { Ar.Log(TEXT("[SeinLobby] SelectMap: no local relay yet.")); return; }
		Relay->Server_RequestSelectMap(Candidate);
		Ar.Logf(TEXT("[SeinLobby] SelectMap: routed via relay  map=%s"),
			*Candidate.ToSoftObjectPath().ToString());
	}

	void HandleLobbyListMaps(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings || Settings->AvailableMaps.IsEmpty())
		{
			Ar.Log(TEXT("[SeinLobby] ListMaps: PluginSettings AvailableMaps is empty."));
			return;
		}
		Ar.Logf(TEXT("[SeinLobby] ListMaps: %d entry(ies)."), Settings->AvailableMaps.Num());
		for (int32 i = 0; i < Settings->AvailableMaps.Num(); ++i)
		{
			const FSeinLobbyMapEntry& E = Settings->AvailableMaps[i];
			Ar.Logf(TEXT("  [%d] '%s'  Slots=%d  Path=%s"),
				i, *E.DisplayName.ToString(), E.SlotCount,
				*E.Map.ToSoftObjectPath().ToString());
		}
	}

	void HandleLobbyStartMatch(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] StartMatch: no World.")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer && Mode != NM_Standalone)
		{
			Ar.Logf(TEXT("[SeinLobby] StartMatch: must be run on the SERVER (NetMode=%d)."), (int32)Mode);
			return;
		}

		UGameInstance* GI = World->GetGameInstance();
		USeinLobbySubsystem* Lobby = GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
		if (!Lobby) { Ar.Log(TEXT("[SeinLobby] StartMatch: USeinLobbySubsystem missing.")); return; }

		// Mirror the BPFL StartMatch verb: travel to the configured gameplay
		// map if one is set, else start lockstep in-place. This is the path
		// dedicated-server admins need to kick a match off the menu lobby —
		// `Sein.Net.StartMatch` is in-place-only and would start lockstep in
		// the lobby map.
		const bool bTravel = !Lobby->ResolveGameplayMap().IsNull();
		const bool bOk = Lobby->ServerStartMatch(bTravel);
		Ar.Logf(TEXT("[SeinLobby] StartMatch: travel=%s  result=%s"),
			bTravel ? TEXT("true") : TEXT("false"),
			bOk ? TEXT("OK") : TEXT("REJECTED (no Human-claimed slot, or travel requested but no GameplayMap configured)"));
	}

	void HandleLobbyKick(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Kick: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinLobby] Kick: usage: Sein.Net.Lobby.Kick <SlotIndex>"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		if (Slot <= 0)
		{
			Ar.Logf(TEXT("[SeinLobby] Kick: SlotIndex>0 (got %d)."), Slot);
			return;
		}

		UGameInstance* GI = World->GetGameInstance();
		if (!GI) return;

		if (World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			if (!Lobby) return;
			APlayerController* PC = World->GetFirstPlayerController();
			const bool bOk = Lobby->ServerHandleKickPlayer(PC, Slot);
			Ar.Logf(TEXT("[SeinLobby] Kick [Server]: slot=%d  result=%s"),
				Slot, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		ASeinNetRelay* Relay = nullptr;
		if (Net)
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (!R) continue;
				APlayerController* PC = Cast<APlayerController>(R->GetOwner());
				if (PC && PC->IsLocalController()) { Relay = R; break; }
			}
		}
		if (!Relay) { Ar.Log(TEXT("[SeinLobby] Kick: no local relay yet.")); return; }
		Relay->Server_RequestKickPlayer(Slot);
		Ar.Logf(TEXT("[SeinLobby] Kick: routed via relay  slot=%d"), Slot);
	}

	void HandleLobbyListFactions(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) return;
		UGameInstance* GI = World->GetGameInstance();
		USeinFactionService* FS = GI ? GI->GetSubsystem<USeinFactionService>() : nullptr;
		if (!FS) { Ar.Log(TEXT("[SeinLobby] ListFactions: no FactionService.")); return; }

		const TArray<TSoftObjectPtr<USeinFaction>> All = FS->GetAvailableFactions();
		Ar.Logf(TEXT("[SeinLobby] ListFactions: %d faction(s) discovered."), All.Num());
		for (const TSoftObjectPtr<USeinFaction>& Soft : All)
		{
			USeinFaction* F = Soft.LoadSynchronous();
			if (F)
			{
				Ar.Logf(TEXT("  - ID=%u  Name='%s'  Path=%s"),
					F->FactionID.Value, *F->FactionName.ToString(), *Soft.ToSoftObjectPath().ToString());
			}
			else
			{
				Ar.Logf(TEXT("  - <unloaded>  Path=%s"), *Soft.ToSoftObjectPath().ToString());
			}
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbySetReady(
		TEXT("Sein.Net.Lobby.SetReady"),
		TEXT("Sein.Net.Lobby.SetReady <0|1> — toggle the local player's slot ready-flag."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbySetReady));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbySetTeam(
		TEXT("Sein.Net.Lobby.SetTeam"),
		TEXT("Sein.Net.Lobby.SetTeam <SlotIndex> <TeamID> — host-only: assign team to slot."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbySetTeam));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbySetSlotState(
		TEXT("Sein.Net.Lobby.SetSlotState"),
		TEXT("Sein.Net.Lobby.SetSlotState <SlotIndex> <Open|Human|AI|Closed> [AIProfileTag] — host-only: change slot occupancy. Cannot rip out a Human-claimed slot."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbySetSlotState));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyListFactions(
		TEXT("Sein.Net.Lobby.ListFactions"),
		TEXT("Dump all factions known to the GI's USeinFactionService (AssetRegistry-discovered + runtime-registered)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyListFactions));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbySelectMap(
		TEXT("Sein.Net.Lobby.SelectMap"),
		TEXT("Sein.Net.Lobby.SelectMap <Index|PackageNameFragment> — host-only: change the lobby's selected gameplay map. Resizes slots to the entry's SlotCount; rejects if a claim would be lost."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbySelectMap));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyListMaps(
		TEXT("Sein.Net.Lobby.ListMaps"),
		TEXT("Dump every entry in PluginSettings AvailableMaps with its SlotCount and asset path."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyListMaps));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyKick(
		TEXT("Sein.Net.Lobby.Kick"),
		TEXT("Sein.Net.Lobby.Kick <SlotIndex> — host-only: kick whoever occupies the slot and reset to Open. Drops Human-claimed PCs to main menu; AI/Closed slots just open. Cannot kick the host's own slot."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyKick));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyStartMatch(
		TEXT("Sein.Net.Lobby.StartMatch"),
		TEXT("SERVER ONLY. Full lobby start: snapshot the lobby + ServerTravel to the configured gameplay map (or start lockstep in-place if no gameplay map is set). Mirrors the BPFL START button. Use this from the dedicated-server console; `Sein.Net.StartMatch` is the in-place-only variant."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyStartMatch));

	// ============== Determinism gossip / desync ==============

	void HandleSimulateDesync(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateDesync: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateDesync: USeinNetSubsystem missing.")); return; }

		// Build a fake world-root table where every peer disagrees, so the alarm
		// path exercises end-to-end without actually corrupting the sim.
		// Every participant gets a distinct 128-bit root.
		const int32 FakeTurn = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 999;

		TArray<FSeinParticipantWorldRootEntry> Fake;
		const auto& Relays = Net->GetRelays();
		uint32 Counter = 0xDEAD0000u;
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
		{
			if (ASeinNetRelay* R = Wp.Get())
			{
				Fake.Emplace(
					R->AssignedParticipantID,
					FGuid(0xDEAD0000u, 0xBEEF0000u, 0xCAFE0000u, Counter++));
			}
		}
		if (Fake.IsEmpty())
		{
			Fake.Emplace(
				Net->GetLocalParticipantID(),
				FGuid(0xDEADBEEFu, 1, 2, 3));
			Fake.Emplace(
				FSeinNetworkParticipantID(FGuid(1, 2, 3, 4)),
				FGuid(0xCAFEBABEu, 5, 6, 7));
		}

		Ar.Logf(TEXT("[SeinNet] SimulateDesync: triggering local alarm path with %d fake peer world roots (Turn=%d)."),
			Fake.Num(), FakeTurn);
		Net->ClientHandleDesyncNotification(
			Net->GetActiveProtocolContext(), FakeTurn, Fake);
	}

	void HandleClearDesync(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] ClearDesync: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] ClearDesync: USeinNetSubsystem missing.")); return; }

		// We can't directly write Net->bDesyncDetected (it's private). Use
		// GEngine to clear all on-screen messages — designer can use this
		// to silence the red alarm without restarting PIE. The flag stays
		// set internally; restart PIE to fully clear.
		if (GEngine)
		{
			GEngine->ClearOnScreenDebugMessages();
		}
		Ar.Log(TEXT("[SeinNet] ClearDesync: cleared on-screen alarm (note: bDesyncDetected flag remains set in subsystem until PIE restart)."));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateDesync(
		TEXT("Sein.Net.SimulateDesync"),
		TEXT("Sein.Net.SimulateDesync [Turn] — fire the LOCAL desync alarm with fake per-peer world roots for testing the red on-screen UI. Does not actually desync the sim. Default Turn=999."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateDesync));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdClearDesync(
		TEXT("Sein.Net.ClearDesync"),
		TEXT("Clear the red on-screen desync alarm message. Internal bDesyncDetected flag stays set until PIE restart."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleClearDesync));

	// ============== Replay reader (Phase 4a) ==============

	void HandleLoadReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LoadReplay: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: usage: Sein.Net.LoadReplay <FileNameOrPath>  (resolves bare filenames against Saved/Replays/)."));
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] LoadReplay: USeinNetSubsystem missing.")); return; }

		USeinReplayReader* Reader = Net->GetOrCreateReplayReader();
		if (!Reader) { Ar.Log(TEXT("[SeinNet] LoadReplay: failed to create reader.")); return; }
		if (Reader->IsPlaying())
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: a replay is already playing — call Sein.Net.StopReplay first."));
			return;
		}

		const FString& Path = Args[0];
		Ar.Logf(TEXT("[SeinNet] LoadReplay: loading %s..."), *Path);
		if (!Reader->LoadFromFile(Path))
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: failed (see log for details)."));
			return;
		}

		const FSeinReplayHeader& H = Reader->GetHeader();
		Ar.Logf(TEXT("[SeinNet] LoadReplay: loaded %d turn(s)  seed=%lld  map=%s  recorded=%s"),
			Reader->GetTurnCount(), H.RandomSeed, *H.MapIdentifier, *H.RecordedAt.ToString());

		if (Reader->Play())
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: playback started. Use Sein.Net.StopReplay to halt."));
		}
		else
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: load OK but Play() rejected — check log (likely networked world; replay needs Standalone)."));
		}
	}

	void HandleStopReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] StopReplay: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] StopReplay: USeinNetSubsystem missing.")); return; }
		USeinReplayReader* Reader = Net->GetOrCreateReplayReader();
		if (!Reader || !Reader->IsPlaying())
		{
			Ar.Log(TEXT("[SeinNet] StopReplay: no replay currently playing."));
			return;
		}
		Reader->Stop();
		Ar.Log(TEXT("[SeinNet] StopReplay: halted."));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLoadReplay(
		TEXT("Sein.Net.LoadReplay"),
		TEXT("Sein.Net.LoadReplay <FileNameOrPath> — load + play a .seinreplay file. Bare filenames resolve against Saved/Replays/. Standalone-mode only (close any multiplayer session first)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLoadReplay));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStopReplay(
		TEXT("Sein.Net.StopReplay"),
		TEXT("Halt the currently-playing replay (sim continues from current state)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStopReplay));

	// ============== World snapshot ==============

	void HandleDumpSnapshot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] DumpSnapshot: no World.")); return; }
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] DumpSnapshot: USeinWorldSubsystem missing.")); return; }

		FSeinWorldSnapshot Snap;
		FSeinWorldSnapshotReferenceGuard SnapGCGuard(Snap);
		WorldSub->CaptureSnapshot(Snap);
		if (Snap.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
		{
			Ar.Log(TEXT("[SeinNet] DumpSnapshot: capture refused; no checkpoint was written."));
			return;
		}

		// Trusted-local developer format only. Production multiplayer,
		// campaign, cloud-save, and replay checkpoints require the future
		// bounded/versioned envelope instead of this raw UObject archive.
		TArray<uint8> Buf;
		FMemoryWriter MemWriter(Buf, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadIfFindFails*/ false);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Writer, &Snap, nullptr);
		if (Writer.IsError() || Writer.IsCriticalError()
			|| MemWriter.IsError() || MemWriter.IsCriticalError()
			|| MemWriter.Tell() != Buf.Num())
		{
			Ar.Log(TEXT("[SeinNet] DumpSnapshot: serialization error; no checkpoint was written."));
			return;
		}

		const FString FileName = Args.Num() > 0
			? Args[0]
			: FString::Printf(TEXT("Snapshot_%s_tick%d.seinsnapshot"),
				*Snap.MapIdentifier.ToString(),
				Snap.CurrentTick);
		const FString DirPath = FPaths::ProjectSavedDir() / TEXT("Snapshots");
		const FString FullPath = FPaths::IsRelative(FileName) ? (DirPath / FileName) : FileName;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree*/ true);

		if (FFileHelper::SaveArrayToFile(Buf, *FullPath))
		{
			Ar.Logf(TEXT("[SeinNet] DumpSnapshot: %d bytes -> %s"), Buf.Num(), *FullPath);
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] DumpSnapshot: write FAILED to %s"), *FullPath);
		}
	}

	void HandleLoadSnapshot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LoadSnapshot: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinNet] LoadSnapshot: usage: Sein.Net.LoadSnapshot <FileNameOrPath>"));
			return;
		}
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] LoadSnapshot: USeinWorldSubsystem missing.")); return; }

		// Snapshot LOAD is a single-peer state rewind. Doing this in a live
		// networked session unilaterally rolls back THIS peer's sim tick while
		// every other peer continues forward — the lockstep gate stalls
		// permanently because the local turn cursor goes backward but the
		// network-layer state doesn't (server has already dispatched turns
		// 100..N, local sim now wants turn 67 again, those turns never re-fire).
		//
		// Live resync instead requires an authenticated coordinator-selected
		// source, bounded checkpoint transfer, and exact command-tail catch-up
		// for the affected peer(s) while healthy peers continue. That
		// drop-in/drop-out infrastructure is not yet built.
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_Standalone)
		{
			Ar.Logf(TEXT("[SeinNet] LoadSnapshot: refused — world is networked (NetMode=%d). Single-peer snapshot rewind would break lockstep across the other peers. Test snapshot dump/load in a Standalone (single-window) PIE session. Multi-peer snapshot resync is a follow-up phase (drop-in/drop-out catch-up RPC)."),
				(int32)Mode);
			return;
		}

		// Resolve bare filenames against Saved/Snapshots/.
		FString Path = Args[0];
		if (!FPaths::FileExists(Path))
		{
			const FString Resolved = FPaths::ProjectSavedDir() / TEXT("Snapshots") / Path;
			if (FPaths::FileExists(Resolved)) Path = Resolved;
			else if (!Path.EndsWith(TEXT(".seinsnapshot")))
			{
				const FString WithExt = FPaths::ProjectSavedDir() / TEXT("Snapshots") / (Path + TEXT(".seinsnapshot"));
				if (FPaths::FileExists(WithExt)) Path = WithExt;
			}
		}

		TArray<uint8> Buf;
		if (!FFileHelper::LoadFileToArray(Buf, *Path))
		{
			Ar.Logf(TEXT("[SeinNet] LoadSnapshot: failed to read %s"), *Path);
			return;
		}

		// This archive may allocate and resolve object paths while decoding.
		// Treat the file as local developer input; never reuse this path for an
		// untrusted network, campaign import, cloud, or replay artifact.
		FSeinWorldSnapshot Snap;
		FSeinWorldSnapshotReferenceGuard SnapGCGuard(Snap);
		FMemoryReader MemReader(Buf, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadIfFindFails*/ true);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Reader, &Snap, nullptr);
		if (Reader.IsError() || Reader.IsCriticalError()
			|| MemReader.IsError() || MemReader.IsCriticalError()
			|| MemReader.Tell() != Buf.Num())
		{
			Ar.Log(TEXT("[SeinNet] LoadSnapshot: deserialization error or trailing file data."));
			return;
		}

		Ar.Logf(TEXT("[SeinNet] LoadSnapshot: header tick=%d  seed=%lld  map=%s  recorded=%s  entities=%d  componentBlobs=%d"),
			Snap.CurrentTick, Snap.SessionSeed, *Snap.MapIdentifier.ToString(),
			*Snap.CapturedAt.ToString(), Snap.Entities.Num(), Snap.ComponentStorageBlobs.Num());

		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI
			? GI->GetSubsystem<USeinNetSubsystem>()
			: nullptr;
		if (!Net)
		{
			Ar.Log(TEXT("[SeinNet] LoadSnapshot: USeinNetSubsystem missing; no trusted local-load adapter is available."));
			return;
		}

		// This standalone console command is an explicit local-operator trust
		// decision. Multiplayer resync will claim the same Core capability only
		// after its coordinator/session envelope has been authenticated and
		// bounded before decoding.
		FSeinSnapshotRestoreAuthorityHandle RestoreAuthority;
		FString RestoreAuthorityError;
		if (!WorldSub->ClaimSnapshotRestoreAuthority(
				TEXT("SeinARTS.Net.LocalSnapshotLoad"),
				Net,
				RestoreAuthority,
				RestoreAuthorityError))
		{
			Ar.Logf(TEXT("[SeinNet] LoadSnapshot: restore authority rejected (%s)."),
				*RestoreAuthorityError);
			return;
		}

		const bool bOk = WorldSub->RestoreSnapshot(
			MoveTemp(RestoreAuthority),
			Snap,
			FSeinSnapshotRestoreOptions(
				ESeinSnapshotLocalStateRestorePolicy::RestoreCaptured,
				ESeinSnapshotResumePolicy::ResumeImmediately));
		Ar.Logf(TEXT("[SeinNet] LoadSnapshot: restore result = %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdDumpSnapshot(
		TEXT("Sein.Net.DumpSnapshot"),
		TEXT("Sein.Net.DumpSnapshot [FileName] — capture current sim state to Saved/Snapshots/<FileName>. Default filename = Snapshot_<Map>_tick<N>.seinsnapshot."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleDumpSnapshot));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLoadSnapshot(
		TEXT("Sein.Net.LoadSnapshot"),
		TEXT("Sein.Net.LoadSnapshot <FileNameOrPath> — restore a compatible standalone sim checkpoint. Active continuations require an exact locally frozen codec."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLoadSnapshot));

	// ============== Drop-in / drop-out (Phase 4) ==============

	const TCHAR* SlotLifecycleToString(ESeinSlotLifecycle S)
	{
		switch (S)
		{
		case ESeinSlotLifecycle::Connected:    return TEXT("Connected");
		case ESeinSlotLifecycle::Dropped:      return TEXT("Dropped");
		case ESeinSlotLifecycle::AITakeover:   return TEXT("AITakeover");
		case ESeinSlotLifecycle::Reconnecting: return TEXT("Reconnecting");
		default:                               return TEXT("?");
		}
	}

	void HandleSimulateDisconnect(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: no World.")); return; }
		if (Args.Num() < 1) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: usage: Sein.Net.SimulateDisconnect <SlotIndex>")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: SERVER ONLY.")); return; }

		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: USeinNetSubsystem missing.")); return; }

		const int32 SlotInt = FCString::Atoi(*Args[0]);
		if (SlotInt <= 0 || SlotInt > 255) { Ar.Logf(TEXT("[SeinNet] SimulateDisconnect: invalid slot %d"), SlotInt); return; }
		Net->SimulateSlotDisconnect(FSeinPlayerID(static_cast<uint8>(SlotInt)));
		Ar.Logf(TEXT("[SeinNet] SimulateDisconnect: slot %d marked DROPPED."), SlotInt);
	}

	void HandleSimulateReconnect(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: no World.")); return; }
		if (Args.Num() < 1) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: usage: Sein.Net.SimulateReconnect <SlotIndex>")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: SERVER ONLY.")); return; }

		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: USeinNetSubsystem missing.")); return; }

		const int32 SlotInt = FCString::Atoi(*Args[0]);
		if (SlotInt <= 0 || SlotInt > 255) { Ar.Logf(TEXT("[SeinNet] SimulateReconnect: invalid slot %d"), SlotInt); return; }
		Net->SimulateSlotReconnect(FSeinPlayerID(static_cast<uint8>(SlotInt)));
		Ar.Logf(TEXT("[SeinNet] SimulateReconnect: slot %d back to CONNECTED."), SlotInt);
	}

	void HandleSlotStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SlotStatus: no World.")); return; }
		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SlotStatus: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Log(TEXT("[SeinNet] SlotStatus: SERVER ONLY (lifecycle is server-side state)."));
			return;
		}

		const TMap<FSeinPlayerID, ESeinSlotLifecycle>& Map = Net->GetSlotLifecycle();
		Ar.Logf(TEXT("[SeinNet] Slot lifecycle (%d slot(s)):"), Map.Num());
		for (const auto& Pair : Map)
		{
			Ar.Logf(TEXT("  slot=%u  status=%s"), Pair.Key.Value, SlotLifecycleToString(Pair.Value));
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateDisconnect(
		TEXT("Sein.Net.SimulateDisconnect"),
		TEXT("SERVER ONLY. Sein.Net.SimulateDisconnect <SlotIndex> — mark a slot as Dropped. Server starts injecting heartbeats so the gate doesn't stall; AI-takeover transition fires after the configured timeout."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateDisconnect));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateReconnect(
		TEXT("Sein.Net.SimulateReconnect"),
		TEXT("SERVER ONLY. Sein.Net.SimulateReconnect <SlotIndex> — mark a slot back to Connected WITHOUT catch-up (slot resumes from current sim state). For the full checkpoint+tail resync, run Sein.Net.RequestResync on the affected CLIENT instead."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateReconnect));

	void HandleRequestResync(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] RequestResync: no World.")); return; }
		USeinNetSubsystem* Net = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] RequestResync: USeinNetSubsystem missing.")); return; }

		FString Error;
		if (Net->RequestResync(Error))
		{
			Ar.Log(TEXT("[SeinNet] RequestResync: checkpoint+tail resync requested from the coordinator."));
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] RequestResync: refused — %s"), *Error);
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdRequestResync(
		TEXT("Sein.Net.RequestResync"),
		TEXT("CLIENT (owning peer). Request a full authenticated checkpoint+command-tail resync from the coordinator: the slot is withheld from authorship, a fresh boundary checkpoint transfers in bounded chunks, the retained turn tail replays through the normal gate, and authorship resumes only after an exact canonical-root handshake."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleRequestResync));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSlotStatus(
		TEXT("Sein.Net.SlotStatus"),
		TEXT("SERVER ONLY. Dump per-slot lifecycle state (Connected / Dropped / AITakeover / Reconnecting)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSlotStatus));

	// ============== Latency / straggler report (Phase 4 polish) ==============

	void HandleLatencyReport(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LatencyReport: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] LatencyReport: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Log(TEXT("[SeinNet] LatencyReport: SERVER ONLY — run on the host."));
			return;
		}

		const int32 Total = Net->GetTurnsCompletedCount();
		const TMap<FSeinPlayerID, int32>& Counts = Net->GetStragglerCounts();
		Ar.Logf(TEXT("[SeinNet] Latency report (TotalTurnsCompleted=%d):"), Total);
		if (Counts.IsEmpty() || Total == 0)
		{
			Ar.Log(TEXT("  No straggle events recorded — every peer is keeping up."));
			return;
		}
		for (const TPair<FSeinPlayerID, int32>& Pair : Counts)
		{
			const float Rate = static_cast<float>(Pair.Value) / static_cast<float>(Total);
			Ar.Logf(TEXT("  slot=%u  late-submit count=%d  rate=%.2f%%  %s"),
				Pair.Key.Value, Pair.Value, Rate * 100.0f,
				Rate > 0.05f ? TEXT("← consider raising InputDelayTurns to absorb this peer's latency") : TEXT(""));
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLatencyReport(
		TEXT("Sein.Net.LatencyReport"),
		TEXT("SERVER ONLY. Dump per-peer straggle counts so you can see which connection is the slowest. >5% straggle rate suggests bumping InputDelayTurns."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLatencyReport));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStatus(
		TEXT("Sein.Net.Status"),
		TEXT("Dump SeinARTS networking status: NetMode, active, LocalPlayerID, SessionSeed, slot/relay counts."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStatus));

	void HandleSaveReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SaveReplay: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] SaveReplay: USeinNetSubsystem missing.")); return; }

		USeinReplayWriter* Writer = Net->GetReplayWriter();
		if (!Writer)
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: no ReplayWriter (server-only — run on the host)."));
			return;
		}
		if (!Writer->IsRecording())
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: writer is not recording (StartMatch hasn't been called yet?)."));
			return;
		}

		const int32 BufferedTurns = Writer->GetBufferedTurnCount();
		const FString Path = Writer->FinishRecording();
		if (Path.IsEmpty())
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: write failed — see log."));
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] SaveReplay: wrote %d turn(s) -> %s"), BufferedTurns, *Path);
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStartMatch(
		TEXT("Sein.Net.StartMatch"),
		TEXT("SERVER ONLY. Start lockstep bootstrap: gather identical tick-zero receipts from every frozen sim peer, authorize them, then launch tick 0."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStartMatch));

	void HandleDumpState(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] DumpState: no World.")); return; }
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		USeinNetSubsystem* Net = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] DumpState: no USeinWorldSubsystem.")); return; }

		const int32 Tick = WorldSub->GetCurrentTick();
		const int32 LegacyPartialHash = WorldSub->ComputeStateHash();
		FGuid CanonicalRoot;
		FString CanonicalRootError;
		const bool bHasCanonicalRoot =
			WorldSub->ComputeCanonicalStateRoot(
				CanonicalRoot, CanonicalRootError);
		const int32 ActiveEntities = WorldSub->GetEntityPool().GetActiveCount();
		const int64 Seed = Net ? Net->GetSessionSeed() : 0;
		const FSeinPlayerID LocalSlot = Net ? Net->GetLocalPlayerID() : FSeinPlayerID::Neutral();

		Ar.Logf(TEXT("[SeinNet] DumpState  Tick=%d  LegacyPartialStateHash=0x%08x  ActiveEntities=%d  Seed=%lld  LocalSlot=%u  NetMode=%d"),
			Tick, static_cast<uint32>(LegacyPartialHash), ActiveEntities,
			Seed, LocalSlot.Value, static_cast<int32>(World->GetNetMode()));
		if (bHasCanonicalRoot)
		{
			Ar.Logf(TEXT("[SeinNet] CanonicalStateRoot=%s"),
				*CanonicalRoot.ToString(EGuidFormats::Digits));
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] CanonicalStateRoot unavailable: %s"),
				*CanonicalRootError);
		}

		// Server-only: dump the slot↔relay binding so cross-run comparisons can
		// confirm Player 1 = SeinPlayerController_0, etc.
		if (Net && (World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer))
		{
			Ar.Logf(TEXT("[SeinNet] Slot bindings (server view, %d slot(s)):"), Net->GetActiveSlotCount());
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				if (ASeinNetRelay* R = Wp.Get())
				{
					Ar.Logf(TEXT("  slot=%u  relay=%s  owner=%s"),
						R->AssignedPlayerID.Value, *GetNameSafe(R), *GetNameSafe(R->GetOwner()));
				}
			}
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSaveReplay(
		TEXT("Sein.Net.SaveReplay"),
		TEXT("SERVER ONLY. Manually flush the in-memory replay buffer to Saved/Replays/. Otherwise auto-flushes on session teardown (PIE Stop)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSaveReplay));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdDumpState(
		TEXT("Sein.Net.DumpState"),
		TEXT("Dump current sim diagnostics: tick, legacy partial 32-bit hash, entity count, session seed, and local slot. The legacy hash is not the canonical world root and cannot prove lockstep agreement."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleDumpState));
}
