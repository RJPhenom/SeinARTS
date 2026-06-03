/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyState.h
 * @brief   Replicated lobby state actor + per-slot state struct.
 *
 * Phase 3b lobby data layer. The lobby exists between session start and
 * `Sein.Net.StartMatch` — players claim a slot + pick a faction before the
 * lockstep session begins. On match-start, USeinLobbySubsystem snapshots the
 * lobby into an `FSeinMatchSettings` and stores it for the game mode to pick
 * up (overrides ASeinWorldSettings).
 *
 * Design:
 *  - One ASeinLobbyState exists per session (server spawns at first PostLogin,
 *    bAlwaysRelevant = true so every client sees it).
 *  - Per-slot state lives in a replicated TArray<FSeinLobbySlotState>.
 *  - Mutation is server-only (validated in USeinLobbySubsystem); clients
 *    receive updates via OnRep_Slots and mirror to UI. Phase 3c wires the
 *    lobby-Widget BPs to this actor's replicated state.
 *
 * Coexistence with current single-map PIE flow:
 *  - On the initial map InitGame, no lobby snapshot exists yet → game mode
 *    falls back to ASeinWorldSettings (existing behavior preserved).
 *  - Lobby actor + claims happen post-PostLogin; claim state is observable but
 *    doesn't retroactively rewrite the in-flight match.
 *  - `Sein.Net.StartMatch` snapshots the current lobby; future Phase 3c map
 *    travel surfaces the snapshot in the new map's InitGame chain.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h" // for ESeinSlotState reuse
#include "GameplayTagContainer.h"
#include "GameFramework/PlayerState.h" // FUniqueNetIdRepl
#include "UObject/SoftObjectPtr.h"
#include "SeinLobbyState.generated.h"

class APlayerController;
class USeinLobbySubsystem;

/**
 * Per-slot lobby state (replicated). Mirrors `FSeinMatchSlot` (Data/SeinMatchSettings.h)
 * but adds a `bClaimed` flag + `ClaimedBy` controller-id so clients can tell
 * which slot belongs to whom before match start.
 *
 * `ClaimedBy` is the FSeinPlayerID assigned to the controller occupying the
 * slot; equal to the slot's eventual sim-side identity once the match begins.
 * Zero (Neutral) means the slot is free.
 */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinLobbySlotState
{
	GENERATED_BODY()

	/** 1..MaxPlayers. 0 = invalid sentinel. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	int32 SlotIndex = 0;

	/** Open / Human / AI / Closed — same enum as FSeinMatchSlot::State. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	ESeinSlotState State = ESeinSlotState::Open;

	/** Faction picked by the claiming player (or AI default). Zero = unset. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FSeinFactionID FactionID;

	/** Team grouping; 0 = no team. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	uint8 TeamID = 0;

	/** Display name (lobby UI). For Open: "Open"; Human: player name; AI: "AI - Hard". */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FText DisplayName;

	/** True iff a controller (or AI policy) currently owns this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	bool bClaimed = false;

	/** Slot's reserved player identity. Equal to FSeinPlayerID(SlotIndex) once
	 *  claimed (current convention: slot N → PlayerID N). Phase 3c could
	 *  decouple this if remapping slots becomes a feature, but today's slot
	 *  binding (Phase 3a) ties them together. Zero when bClaimed = false. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FSeinPlayerID ClaimedBy;

	/** Optional AI personality tag (DESIGN §16); only meaningful when State == AI. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FGameplayTag AIProfile;

	/** Player-toggled readiness. Drives the Start Match gate when the active
	 *  preset's `bRequireAllReady` is true. AI / Closed slots are treated as
	 *  implicitly ready. Reset to false when the slot's claim flips. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	bool bReady = false;

	/** Set when the slot's claimant disconnects but the lobby is still in
	 *  the reconnect grace period. `bClaimed` stays true (the slot is
	 *  reserved); UI distinguishes "Open" from "Disconnected — reserved for
	 *  player X" via this flag. After grace expires, the slot reverts to a
	 *  fully-Open state and this flag clears. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	bool bDisconnected = false;

	/** UE network ID of the slot's most recent claimant. Persists across
	 *  disconnect (paired with `bDisconnected`) so a reconnecting PC with a
	 *  matching `UniqueId` can be re-bound to the same slot. Zero/invalid
	 *  on never-claimed slots. PIE listen-server windows often share a
	 *  fake UniqueId — reconnect testing must happen in cooked builds. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FUniqueNetIdRepl LastClaimantNetID;

	/** Remote address ("IP:Port") of the slot's claimant. Populated on
	 *  PostLogin / cross-slot claim from `NetConnection->LowLevelGetRemoteAddress`.
	 *  Empty for the listen-server's local host PC (no NetConnection) and for
	 *  Standalone (no networking). UI can display alongside `DisplayName` to
	 *  differentiate Human players when no real player name is available
	 *  (PIE multi-window testing). Persists across the disconnect grace
	 *  window so reconnect-aware UIs can show "(was 127.0.0.1)". Cleared
	 *  on full slot release. Real player names will arrive via OSS (EOS /
	 *  Steamworks) → `PlayerState::PlayerName` → `DisplayName`; this field
	 *  remains as the testing fallback + diagnostic identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Lobby")
	FString RemoteAddress;
};

/**
 * Replicated lobby state actor. Server spawns one at first PostLogin;
 * bAlwaysRelevant = true ensures every connected client receives the slot
 * array via OnRep_Slots. Mutation is server-only and routed through
 * USeinLobbySubsystem (validates + commits).
 *
 * UI binding (Phase 3c): client widgets bind to OnLobbyStateChanged.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Lobby State"))
class SEINARTSNET_API ASeinLobbyState : public AActor
{
	GENERATED_BODY()

public:
	ASeinLobbyState();

	/** Replicated per-slot state. Server is the single writer; clients mirror
	 *  via OnRep_Slots. */
	UPROPERTY(ReplicatedUsing = OnRep_Slots, BlueprintReadOnly, Category = "SeinARTS|Lobby")
	TArray<FSeinLobbySlotState> Slots;

	UFUNCTION()
	void OnRep_Slots();

	/** Lobby's currently-selected gameplay map (host's pick from the map
	 *  dropdown). Drives:
	 *   - Slot count (lobby resizes to the entry's `SlotCount` on selection)
	 *   - StartMatch ServerTravel destination
	 *  Replicated so every client's UI shows the same selection. */
	UPROPERTY(ReplicatedUsing = OnRep_SelectedMap, BlueprintReadOnly, Category = "SeinARTS|Lobby")
	TSoftObjectPtr<UWorld> SelectedMap;

	UFUNCTION()
	void OnRep_SelectedMap();

	/** Read-only accessor: copy of the current slot array. Safe to call from BP. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Lobby")
	const TArray<FSeinLobbySlotState>& GetSlots() const { return Slots; }

	/** Lookup by slot index (1-based). Returns nullptr if not present. */
	const FSeinLobbySlotState* FindSlot(int32 SlotIndex) const;
	FSeinLobbySlotState* FindSlotMutable(int32 SlotIndex);

	/** Multicast delegate fired on every replicated slot update (client-side
	 *  via OnRep_Slots, server-side via the subsystem after every commit).
	 *  Phase 3c lobby widgets bind here to refresh the slot panel. */
	DECLARE_MULTICAST_DELEGATE(FOnLobbyStateChanged);
	FOnLobbyStateChanged OnLobbyStateChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Resolve the GI-level lobby manager. nullptr only if GI is mid-teardown. */
	USeinLobbySubsystem* GetLobbySubsystem() const;
};
