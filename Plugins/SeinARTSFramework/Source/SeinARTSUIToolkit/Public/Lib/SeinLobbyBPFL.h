/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyBPFL.h
 * @brief   Blueprint surface for the lobby flow — verbs designer Widget BPs
 *          call from button-click events.
 *
 * Phase 3c. Designers extend `USeinUserWidget`, drop a `USeinLobbyViewModel`
 * (via `SeinGetOrCreateLobbyViewModel`) for read-side data, and call the
 * verbs here on click handlers. Framework owns the wiring; project owns the
 * UMG composition / styling.
 *
 * All verbs are world-context-aware so they work from any UMG event graph
 * without a hand-passed `World`. Each routes through the appropriate
 * subsystem (NetSubsystem for the relay-RPC paths, LobbySubsystem for
 * lobby-state reads, etc.) so no game code needs to know which subsystem
 * owns what.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"  // ESeinSlotState
#include "Data/SeinLobbyMapEntry.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "SeinLobbyBPFL.generated.h"

class USeinLobbyViewModel;
class USeinFaction;

UCLASS(meta = (DisplayName = "SeinARTS Lobby Library"))
class SEINARTSUITOOLKIT_API USeinLobbyBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ========== View-model accessor ==========

	/** Get (or lazily create + initialize) the lobby view model for the
	 *  current world. Cached on the UISubsystem so multiple widgets share
	 *  a single instance + change-event subscription. Designers grab this
	 *  in the lobby widget's Construct event and use it as their data
	 *  source for the slot panel. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Or Create Lobby View Model"))
	static USeinLobbyViewModel* SeinGetOrCreateLobbyViewModel(const UObject* WorldContextObject);

	// ========== Verbs ==========

	/** Request that the local player be moved to `SlotIndex` with the given
	 *  faction. Routes through the local relay's Server_RequestSlotClaim
	 *  RPC. Server validates + replicates the new lobby state to all peers
	 *  via the always-relevant `ASeinLobbyState` actor. Returns true if the
	 *  request was actually sent (false = no relay yet, retry next frame). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Slot Claim"))
	static bool SeinRequestSlotClaim(const UObject* WorldContextObject, int32 SlotIndex, FSeinFactionID FactionID);

	/** Host-only: snapshot the current lobby state into `FSeinMatchSettings`,
	 *  publish it on the GI as the next match's source-of-truth, then either
	 *  (a) start the lockstep session in-place if `bTravelToGameplayMap=false`,
	 *  or (b) `ServerTravel` to the configured gameplay map (set
	 *  `USeinLobbySubsystem::GameplayMap`) and let the new map's GameMode
	 *  consume the snapshot in InitGame.
	 *
	 *  Returns true if the request was accepted (host + at least one
	 *  Human-claimed slot), false if rejected. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Start Match"))
	static bool SeinRequestStartMatch(const UObject* WorldContextObject);

	/** Disconnect the local player from the session. Host: tears down the
	 *  listen server (every peer disconnects). Client: leaves the session.
	 *  After disconnect, if `USeinARTSCoreSettings::MainMenuMap` is set, the
	 *  local player travels to it. Otherwise the project handles routing
	 *  via UE's standard NetworkFailure delegates. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Leave Lobby"))
	static void SeinRequestLeaveLobby(const UObject* WorldContextObject);

	/** Convert the current world into a listen server in-place — no map
	 *  travel. Use this for the HOST button when the lobby UI is already
	 *  loaded in the current map and you just want to start accepting
	 *  client connections.
	 *
	 *  Returns true if a listen server is now active (newly started OR
	 *  already running). Returns false if the world rejected the URL or
	 *  is not in a startable state.
	 *
	 *  Contrast with `open <map>?listen` (which travels) and the JOIN
	 *  flow `open <IP>` (which client-travels to a remote server). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Host Session"))
	static bool SeinRequestHostSession(const UObject* WorldContextObject);

	/** Connect the local player to a remote listen server. This DOES travel
	 *  (client-travels to the remote's current map). Use for the JOIN
	 *  button. Returns true if the travel command was issued — actual
	 *  connection success comes asynchronously via UE's NetworkFailure /
	 *  ClientTravel delegates. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Join Session"))
	static bool SeinRequestJoinSession(const UObject* WorldContextObject, const FString& ServerAddress);

	/** Toggle the local player's slot ready-flag. Designer's host UI reads
	 *  per-slot `bReady` from the replicated lobby state and decides whether
	 *  to enable the Start Match button. Framework permits Start whenever
	 *  there's a Human-claimed slot. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Set Ready"))
	static bool SeinRequestSetReady(const UObject* WorldContextObject, bool bReady);

	/** Host-only: assign `SlotIndex` to `TeamID`. Framework treats team as
	 *  opaque — designer convention assigns meaning. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Set Team"))
	static bool SeinRequestSetTeam(const UObject* WorldContextObject, int32 SlotIndex, uint8 TeamID);

	/** Host-only: change a slot's state (Open/AI/Closed — Human transitions
	 *  go through Request Slot Claim). AIProfile only meaningful for AI. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Set Slot State"))
	static bool SeinRequestSetSlotState(const UObject* WorldContextObject, int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile);

	/** Host-only: kick whoever currently occupies `SlotIndex` and reset the
	 *  slot to Open. For Human-claimed slots, the bound PC is dropped via
	 *  `ClientReturnToMainMenuWithTextReason`. For AI/Closed slots, this is
	 *  equivalent to `Request Set Slot State (Open)`. Cannot be used on the
	 *  host's own slot (would tear down the listen server — use Leave Lobby
	 *  for that). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Kick Player"))
	static bool SeinRequestKickPlayer(const UObject* WorldContextObject, int32 SlotIndex);

	// ========== Faction service accessors ==========

	/** Every faction known to the GI's `USeinFactionService` (AssetRegistry-
	 *  discovered + runtime-registered). Lobby UI iterates this for the
	 *  faction picker. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Available Factions"))
	static TArray<TSoftObjectPtr<USeinFaction>> SeinGetAvailableFactions(const UObject* WorldContextObject);

	/** Host-only: change the lobby's selected gameplay map. Resizes lobby
	 *  slots to match the entry's `SlotCount`; rejects shrink if a claimed
	 *  slot would be lost. Pass a map from `SeinGetAvailableMaps`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Select Map"))
	static bool SeinRequestSelectMap(const UObject* WorldContextObject, TSoftObjectPtr<UWorld> Map);

	/** Plugin-settings playable maps list — drives the lobby's map combobox.
	 *  Designer populates `USeinARTSCoreSettings::AvailableMaps`; this
	 *  helper just exposes it to BP. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Lobby",
		meta = (DisplayName = "Get Available Maps"))
	static TArray<FSeinLobbyMapEntry> SeinGetAvailableMaps();

};
