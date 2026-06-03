/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyViewModel.h
 * @brief   ViewModel exposing the replicated lobby state to designer Widget BPs.
 *
 * Phase 3c lobby UI surface. Mirrors the established `USeinPlayerViewModel`
 * pattern: a UObject wrapper around the underlying replicated lobby actor,
 * with BP-friendly accessors + a multicast `OnLobbyChanged` event fired on
 * RepNotify. Designers extend `USeinUserWidget` and bind to this view model
 * to author the actual lobby Widget BPs (`WBP_Lobby`, `WBP_LobbySlotTile`,
 * `WBP_FactionPicker`) without touching framework C++.
 *
 * Responsibilities:
 *  - Resolve the GI-level `USeinLobbySubsystem` and its replicated
 *    `ASeinLobbyState` actor (waits for replication on clients).
 *  - Expose `Slots`, `LocalSlotIndex`, `bIsHost`, `bCanStartMatch` to BP.
 *  - Fire `OnLobbyChanged` when the actor's `OnLobbyStateChanged` fires
 *    (replication or server-side commit).
 *
 * The verbs (claim slot, start match, leave) live on `USeinLobbyBPFL`, not
 * here — view model is read-only by convention.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinLobbyMapEntry.h"
#include "SeinLobbyState.h"
#include "SeinLobbyViewModel.generated.h"

class USeinLobbySubsystem;
class ASeinLobbyState;
class APlayerController;
class UWorld;
class USeinFaction;
class USeinNetSubsystem;

/** Broadcast when the lobby's replicated state changes (RepNotify or server commit). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyChanged);

/** Broadcast when the local player's slot binding changes (relay
 *  AssignedPlayerID OnRep, or initial binding latch). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalSlotChanged, FSeinPlayerID, NewLocalSlot);

UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinLobbyViewModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Lifecycle ==========

	/** Initialize against a world. Resolves the lobby subsystem + binds to the
	 *  replicated lobby actor's change delegate. Call from the owning widget's
	 *  Construct, or use `USeinLobbyBPFL::SeinGetOrCreateLobbyViewModel`
	 *  to get a cached singleton via `USeinUISubsystem`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Lobby")
	void Initialize(UWorld* InWorld);

	/** Tear down delegate bindings. Called on world end. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Lobby")
	void Shutdown();

	// ========== Reads ==========

	/** Snapshot of the replicated lobby slot array. Updated whenever the
	 *  underlying actor's `OnLobbyStateChanged` fires. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	const TArray<FSeinLobbySlotState>& GetSlots() const { return CachedSlots; }

	/** Find a slot by its index (1-based). Returns false if not present. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool TryGetSlot(int32 SlotIndex, FSeinLobbySlotState& OutSlot) const;

	/** The local player's claimed slot. Reads live from `USeinNetSubsystem`
	 *  rather than a cached value — eliminates the binding-timing race
	 *  where a fresh ViewModel (post-map-travel) might miss the
	 *  OnLocalSlotChanged broadcast if `bSlotChanged=false`. Returns Neutral
	 *  until the relay registers. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	FSeinPlayerID GetLocalSlot() const;

	/** True iff `SlotIndex` (1-based) is the local player's claimed slot.
	 *  Convenience wrapper around `GetLocalSlot()` so slot-tile widgets can
	 *  ask "is this me?" without breaking the FSeinPlayerID byte. False when
	 *  the local player has no slot yet (relay not registered) or
	 *  `SlotIndex <= 0`. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsLocalSlot(int32 SlotIndex) const;

	/** True iff this client is the listen-server / dedicated-server host. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsHost() const;

	/** True iff a network session is active and the local PC has a lobby
	 *  slot assigned — i.e. the lobby panel is meaningful to interact with.
	 *  False on PIE Standalone (no network), or before HOST/JOIN, or
	 *  briefly during connection before the relay registers.
	 *
	 *  Use this in BP to gate the lobby panel's enabled state — show greyed-
	 *  out / "Press HOST or JOIN to start" placeholder until this is true. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsInLobbySession() const;

	/** True iff the host can issue Start Match (today: at least one
	 *  Human-claimed slot). Designers override by subclassing the BPFL or
	 *  binding to `OnLobbyChanged` and computing their own gate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool CanStartMatch() const;

	/** True if a snapshot has been published (server only — false on clients). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool HasPublishedMatchSnapshot() const;

	/** Local PC's slot ready-flag (false until the local relay arrives). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsLocalReady() const;

	/** Count of Human-claimed slots whose `bReady` is true. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	int32 GetReadyCount() const;

	/** Count of Human-claimed slots (including disconnected/reserved). UI uses
	 *  this with `GetReadyCount` for "3/4 ready" displays. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	int32 GetClaimedCount() const;

	/** Faction list for the picker — all factions discovered by the
	 *  USeinFactionService. Designer who wants per-match-type faction
	 *  filtering reads from their own rule struct in
	 *  `FSeinMatchSettings::Extensions` and filters the list themselves. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	TArray<TSoftObjectPtr<USeinFaction>> GetAvailableFactionsForPicker() const;

	// ========== Map dropdown ==========

	/** The lobby's currently-selected gameplay map (replicated). Returns null
	 *  until the lobby actor has replicated, or if no map has been selected. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	TSoftObjectPtr<UWorld> GetSelectedMap() const;

	/** Designer-authored map dropdown entries from
	 *  `USeinARTSCoreSettings::AvailableMaps`. Lobby UI populates the map
	 *  combobox from this list; entries carry display name, slot count, and
	 *  thumbnail. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	TArray<FSeinLobbyMapEntry> GetAvailableMaps() const;

	/** True iff selecting `Candidate` would NOT lose any currently-claimed
	 *  slot. Equivalent to "can the host pick this map right now?". UI uses
	 *  this to grey out / disable map options that would be rejected by
	 *  `ServerHandleSelectMap`. Cheap — runs entirely on the cached slot
	 *  array (no asset loads). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsMapShrinkSafe(const FSeinLobbyMapEntry& Candidate) const;

	/** Look up the lobby's currently-selected map in
	 *  `USeinARTSCoreSettings::AvailableMaps` and return its full entry
	 *  (slot count, team count, thumbnail, display name). Returns false
	 *  if no map is selected, or if the selection isn't in `AvailableMaps`
	 *  (shouldn't happen in normal flow — `ServerHandleSelectMap` only
	 *  accepts entries from the configured list). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool TryGetCurrentMapEntry(FSeinLobbyMapEntry& OutEntry) const;

	/** TeamCount of the currently-selected map, for driving per-slot team
	 *  picker comboboxes (populate values 1..N). Returns 2 (1v1 default)
	 *  when no map is selected or the selection isn't in `AvailableMaps`. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	int32 GetCurrentMapTeamCount() const;

	// ========== Events ==========

	/** Fires when the lobby's replicated slot array changes. Bind from BP
	 *  to refresh the slot panel. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Lobby")
	FOnLobbyChanged OnLobbyChanged;

	/** Fires when the local player's slot binding changes. Bind to update
	 *  faction-picker selection state, "ready" button enable state, etc. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Lobby")
	FOnLocalSlotChanged OnLocalSlotChanged;

private:
	/** Resolve subsystem from the cached world. nullptr after Shutdown or if
	 *  GI is mid-teardown. */
	USeinLobbySubsystem* GetLobbySubsystem() const;

	/** Resolve the GI's net subsystem (for the OnLocalSlotChanged delegate). */
	USeinNetSubsystem* GetNetSubsystem() const;

	/** Pull the latest snapshot from the lobby actor + fire `OnLobbyChanged`. */
	void RefreshFromActor();

	/** Hook bound to `ASeinLobbyState::OnLobbyStateChanged`. */
	void HandleLobbyStateChanged();

	/** Hook bound to `USeinNetSubsystem::OnLocalSlotChanged`. Replaces the
	 *  pre-3c tick-based poll. */
	void HandleNetLocalSlotChanged(FSeinPlayerID NewSlot);

	UPROPERTY()
	TWeakObjectPtr<UWorld> CachedWorld;

	UPROPERTY()
	TWeakObjectPtr<ASeinLobbyState> BoundActor;

	UPROPERTY()
	TArray<FSeinLobbySlotState> CachedSlots;

	UPROPERTY()
	FSeinPlayerID LocalSlotID;

	FDelegateHandle LobbyChangedHandle;

	/** Bound on Net subsystem's OnLocalSlotChanged. Cleared in Shutdown. */
	FDelegateHandle NetLocalSlotChangedHandle;

	/** Late-bind ticker for the lobby actor: clients may not have the
	 *  replicated actor yet at Initialize time. Polls once per second until
	 *  the actor arrives, then unbinds itself. */
	FTSTicker::FDelegateHandle LobbyActorPollHandle;
};
