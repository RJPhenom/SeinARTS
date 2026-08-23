/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbySubsystem.h
 * @brief   Game-instance subsystem managing pre-match lobby state.
 *
 * Phase 3b. Server-authoritative lobby that lets connecting players claim a
 * slot + pick a faction before the lockstep session starts. Coexists with
 * USeinNetSubsystem (which owns the lockstep wire); both live at the GI
 * scope so they survive map travel between lobby + gameplay maps.
 *
 * Responsibilities:
 *  - On the server, spawn one ASeinLobbyState actor per session and seed it
 *    with default slots (count from USeinARTSCoreSettings::MaxPlayers, or
 *    an explicit InitializeLobby call from a future lobby Widget BP).
 *  - Hook FGameModeEvents::PostLogin: assign joining controllers to the next
 *    free Open slot (current convention: slot N → PlayerID N), mark Claimed.
 *  - Hook FGameModeEvents::Logout: release the leaving controller's slot.
 *  - Receive ServerHandleSlotClaim from ASeinNetRelay::Server_RequestSlotClaim
 *    (clients changing slot / faction mid-lobby).
 *  - Build an FSeinMatchSettings snapshot on demand (called from
 *    Sein.Net.StartMatch and the future map-travel handoff).
 *  - Publish the snapshot so ASeinGameMode::ResolveMatchSettingsForWorld can
 *    read it as the highest-priority match-settings source.
 *
 * Direct gameplay-map PIE flow:
 *  - ASeinGameMode::InitGame runs BEFORE any controller has connected, so the
 *    subsystem seeds claimable lobby seats from the level-authored manifest.
 *  - Players connect and claim only the seats Unreal requested for PIE.
 *  - Match start publishes the final Human/AI/Open/Closed roster and commits
 *    it into the current game mode before lockstep bootstrap dispatch.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "SeinLobbyState.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPtr.h"
#include "SeinLobbySubsystem.generated.h"

class APlayerController;
class AController;
class AGameModeBase;
class ASeinNetRelay;
class USeinFactionService;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FSeinMatchSettingsLaunchCommitted,
	const FSeinMatchSettings&);

UCLASS(Config = Game)
class SEINARTSNET_API USeinLobbySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Idempotently detach every callback and release runtime state before the
	 *  owning module's code can unload. Deinitialize delegates to this path. */
	void ReleaseModuleOwnedStateForModuleUnload();

	/**
	 * Optional gameplay map to ServerTravel to when StartLockstepSession is
	 * called via `USeinLobbyBPFL::SeinRequestStartMatch` with travel enabled.
	 * Empty = no travel (host runs the match in the current map). Configure
	 * per-project via Project Settings → SeinARTS, or set at runtime from a
	 * lobby Widget BP via `Set Gameplay Map`.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Lobby",
		meta = (DisplayName = "Gameplay Map"))
	TSoftObjectPtr<UWorld> GameplayMap;

	// ========== Server API ==========

	/** Server-only: (re)initialize the lobby with `SlotCount` Open slots. Idempotent
	 *  in the sense that re-initializing wipes existing claims — call only on
	 *  fresh sessions. If `SlotCount` <= 0, defaults to the active preset's
	 *  slot count, then to `USeinARTSCoreSettings::MaxPlayers`. */
	void InitializeLobby(int32 SlotCount = 0);

	/** Server-only: set the slot count `EnsureLobbyActor` should use when
	 *  lazy-spawning the lobby actor. Called from `ASeinGameMode::InitGame`
	 *  with `ResolvedMatchSettings.Slots.Num()` so the lobby in a gameplay
	 *  map reflects the level's actual slot count (synthesized from the
	 *  level's SeinPlayerStarts or imported from the published lobby
	 *  snapshot), NOT the lobby map's default (AvailableMaps[0]).
	 *
	 *  Zero (default) means "no override — use AvailableMaps[0] / MaxPlayers
	 *  fallbacks." Once set, persists until cleared or overwritten. */
	void SetSlotCountOverride(int32 N) { SlotCountOverride = FMath::Max(0, N); }

	/** Server-only: seed a direct gameplay-map lobby from the canonical level
	 *  manifest. Authored Human slots become claimable Open seats while their
	 *  faction/team defaults, AI seats, Closed seats, and extensions remain
	 *  intact. This makes manual and automatic direct-PIE starts publish the
	 *  same contract; published lobby-travel snapshots do not use this path. */
	void SetDirectMatchSettingsDefaults(const FSeinMatchSettings& Settings);

#if WITH_DEV_AUTOMATION_TESTS
	/** Projects one direct-map manifest slot without requiring a PIE world. */
	static FSeinLobbySlotState BuildDirectMatchSlotForTests(
		const FSeinMatchSlot& Slot);
	/** Rebuilds one committed slot for a destination world without a lobby actor. */
	static FSeinLobbySlotState BuildCommittedMatchSlotForTests(
		const FSeinMatchSlot& Slot);
	static bool IsOpenSlotAvailableForTests(
		const FSeinLobbySlotState& Slot,
		bool bLaunchCommitted);
	const FSeinMatchSettings& GetDirectMatchSettingsDefaultsForTests() const
	{
		return DirectMatchSettingsDefaults;
	}
#endif

	/** Server-only: route a client's claim request from the relay. Validates
	 *  slot existence + occupancy + faction validity (via FactionService),
	 *  commits to the actor, then re-replicates. Returns true on accept. */
	bool ServerHandleSlotClaim(APlayerController* PC, int32 SlotIndex, FSeinFactionID Faction);

	/** Server-only: toggle the requesting PC's slot's `bReady` flag. Called
	 *  by anyone with a Human-claimed slot (not host-restricted). */
	bool ServerHandleSetReady(APlayerController* PC, bool bReady);

	/** Server-only: change a slot's team (host-only). The framework treats
	 *  TeamID as opaque; designer convention assigns meaning. */
	bool ServerHandleSetTeam(APlayerController* HostPC, int32 SlotIndex, uint8 TeamID);

	/** Server-only: change a slot's state — open/close, add/remove AI bot.
	 *  Host-only. AIProfile only honored when NewState == AI; ignored otherwise.
	 *  Cannot be used to claim a slot for a Human — that's
	 *  `ServerHandleSlotClaim`'s job. */
	bool ServerHandleSetSlotState(APlayerController* HostPC, int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile);

	/** Server-only: voluntarily release the PC's currently-claimed slot back
	 *  to Open. Distinguishes "I'm leaving on purpose" from "my connection
	 *  dropped" — the latter goes through `OnLogout` which marks the slot
	 *  disconnected-but-reserved for `LobbyReconnectGraceSeconds`. Voluntary
	 *  leaves skip the grace period; the slot becomes immediately claimable
	 *  again. Returns true iff a slot was actually released. */
	bool ServerHandleLeave(APlayerController* PC);

	/** Server-only: kick whoever currently occupies `SlotIndex` and reset the
	 *  slot to Open. Host-only.
	 *
	 *  - Human-claimed slot: drops the bound PC's NetConnection via
	 *    `ClientReturnToMainMenuWithTextReason`, removes them from the
	 *    `ControllerToSlot` map, and opens the slot.
	 *  - AI / Closed slot: equivalent to `ServerHandleSetSlotState(Open)` —
	 *    no PC to kick, just opens.
	 *  - Open slot: no-op success.
	 *  - Cannot kick the host's own slot (would tear down the listen server).
	 *
	 *  Returns true iff the operation completed (slot is now Open). */
	bool ServerHandleKickPlayer(APlayerController* HostPC, int32 SlotIndex);

	/** Server-only: change the lobby's selected gameplay map. Host-only.
	 *  Validates the map exists in `USeinARTSCoreSettings::AvailableMaps`,
	 *  resizes the lobby's slot array to match the entry's `SlotCount`.
	 *
	 *  Reject policy on shrink: if any currently-claimed slot would be lost
	 *  by the resize (slot index > new count AND `bClaimed`), the switch is
	 *  rejected. Host must open those slots first. UI should grey out
	 *  smaller-map options when a claim would be lost — see
	 *  `USeinLobbyViewModel::IsMapShrinkSafe`. */
	bool ServerHandleSelectMap(APlayerController* HostPC, TSoftObjectPtr<UWorld> Map);

	/** True iff PC is the listen-server / dedicated-server / standalone host.
	 *  Used by the host-only ServerHandle* validators. */
	bool IsHostController(APlayerController* PC) const;

	/** Server-only fallback capacity check used when no external connection
	 *  admission authorizer owns exact-seat assignment.
	 *  Returns true (accept) if either:
	 *   - The lobby actor doesn't exist yet and no roster has committed, OR
	 *   - There's at least one free Open slot, OR
	 *   - There's a disconnected-reserved slot whose `LastClaimantNetID`
	 *     matches `UniqueId` (reconnecting player).
	 *  Once the roster commits, anonymous Open-slot admission is closed;
	 *  exact external admission may still rebind a published Human seat.
	 *  PreLogin's `ErrorMessage` should be set to a
	 *  human-readable refusal string when this returns false — the connecting
	 *  client gets a clean "server is full" error rather than connecting and
	 *  becoming a phantom PC with no slot binding. */
	bool CanAcceptConnection(const FUniqueNetIdRepl& UniqueId) const;

	/** Fail-closed availability check for an externally authorized exact slot. */
	bool CanAcceptConnectionAtSlot(
		FSeinPlayerID Slot,
		const APlayerController* Controller = nullptr) const;

	/** Server-only: build a fully-populated FSeinMatchSettings from the current
	 *  lobby state. Called by Sein.Net.StartMatch and (in Phase 3c) the
	 *  map-travel handoff. PC names / AI profiles flow through unchanged.
	 *
	 *  CapturedSettingsOut is overwritten with the snapshot. Returns true iff
	 *  the lobby has at least one Human or AI slot — empty lobby is treated as
	 *  "fall back to WorldSettings" by callers. */
	bool BuildMatchSettingsSnapshot(FSeinMatchSettings& CapturedSettingsOut) const;

	/** Server-only: take a snapshot now and store it as the GI-scoped match
	 *  settings override. Game mode's ResolveMatchSettingsForWorld picks this
	 *  up first. Called by Sein.Net.StartMatch. */
	void PublishMatchSettingsSnapshot();

	/** Server-only: full match-start flow used by `USeinLobbyBPFL::SeinRequestStartMatch`.
	 *  Publishes the snapshot, then either:
	 *   - if `bTravelToGameplayMap=true` AND `GameplayMap` is set, calls
	 *     `World::ServerTravel(GameplayMap)` (the new map's GameMode picks
	 *     up the snapshot in InitGame), OR
	 *   - standalone invokes CoreEntity's Framework-owned bootstrap launcher
	 *     in-place; networked lobby-derived starts require travel so the final
	 *     roster is installed before any peer registers simulation entities.
	 *
	 *  Returns true on accept, false if rejected (no Human-claimed slot,
	 *  invalid travel map, or a networked in-place request). */
	bool ServerStartMatch(bool bTravelToGameplayMap);

	// ========== Read API (server + client) ==========

	/** True iff a snapshot has been published (PublishMatchSettingsSnapshot
	 *  has been called at least once this GI lifetime). */
	bool HasPublishedSnapshot() const { return bSnapshotPublished; }

	/** Snapshot accessor. Empty (default-constructed) until
	 *  PublishMatchSettingsSnapshot fires. */
	const FSeinMatchSettings& GetPublishedSnapshot() const { return PublishedSnapshot; }

	/** Native notification emitted once a published snapshot has passed the
	 *  launch preparation gate and becomes the live routing contract. */
	FSeinMatchSettingsLaunchCommitted& OnMatchSettingsLaunchCommitted()
	{
		return MatchSettingsLaunchCommitted;
	}

	/** Net/session launchers call this only after preparation succeeds. */
	void ConfirmPublishedMatchSettingsLaunch();

	/** Removes a prepared direct-start snapshot only when launch has not been
	 *  committed. Used by editor auto-start so a transient preparation failure
	 *  can retry instead of permanently suppressing itself. */
	bool DiscardUncommittedPreparedMatchSettingsSnapshot();

	/** Install the server-authored, final snapshot into this GameInstance.
	 *  Used by the pre-travel lockstep bootstrap on clients so destination
	 *  world initialization sees the same slot/extension manifest as authority. */
	bool InstallPreparedMatchSettingsSnapshot(const FSeinMatchSettings& Snapshot);

	/** Forget the current session's lobby contract and slot bindings. Called
	 *  when the local process leaves a session (explicit leave, kick) BEFORE it
	 *  travels to the menu: the GameInstance-scoped subsystem outlives the
	 *  world, and a stale published snapshot would otherwise present a dead
	 *  roster in the menu and could drive the menu world's standalone
	 *  bootstrap. Does not touch module-lifetime delegate bindings. */
	void ResetForLocalSessionExit();

	/** Current replicated lobby actor; null on the client until first
	 *  replication arrives, null on the server until InitializeLobby spawns it. */
	ASeinLobbyState* GetLobbyState() const { return LobbyStateActor.Get(); }

	// ========== Replication callbacks (from ASeinLobbyState) ==========

	void NotifyLobbyStateActorBeginPlay(ASeinLobbyState* Actor);
	void NotifyLobbyStateActorEndPlay(ASeinLobbyState* Actor);

	/** Resolve the GI's faction service. Loads the configured class lazily
	 *  if it differs from the framework default (ShouldCreateSubsystem
	 *  ensures only one instance exists). Returns null only if the GI is
	 *  mid-teardown. */
	USeinFactionService* GetFactionService() const;

	/** Resolve the snapshot's base `FSeinMatchSettings` value. Returns the
	 *  plugin-settings `MatchSettingsStructure` (designer-authored Slots
	 *  manifest + Extensions array) on which the lobby overlays its
	 *  replicated slot state during snapshot build. */
	FSeinMatchSettings ResolveBaseSettings() const;

	/** Resolve the gameplay map to travel to on StartMatch:
	 *   1. USeinLobbySubsystem::GameplayMap (runtime override)
	 *   2. USeinARTSCoreSettings::DefaultGameplayMap (plugin setting)
	 *   3. null (start in-place; no travel) */
	TSoftObjectPtr<UWorld> ResolveGameplayMap() const;

private:
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);
	void ExecutePreparedServerTravel(FString MapURL);

	bool IsServer() const;

	/** Server-only: ensure the ASeinLobbyState actor exists in the current
	 *  world. Spawns lazily on first need; idempotent. */
	void EnsureLobbyActor();

	/** Server-only: pick the lowest-index Open slot not already claimed AND
	 *  not currently in disconnected/reserved state. Returns 0 if none free. */
	int32 PickNextFreeSlot() const;

	/** Server-only: try to find a disconnected slot whose `LastClaimantNetID`
	 *  matches the joining PC's UniqueId. Returns the slot index, or 0 if no
	 *  match — caller falls through to PickNextFreeSlot. */
	int32 FindDisconnectedSlotForPC(APlayerController* PC) const;

	/** Server-only: commit a slot's state and broadcast the change. */
	void CommitSlotState(int32 SlotIndex, const FSeinLobbySlotState& NewState);

	/** Server-only: schedule a grace-timer that reverts a disconnected slot
	 *  back to fully-Open after `LobbyReconnectGraceSeconds` if no reclaim
	 *  has happened by then. */
	void ScheduleSlotReclaim(int32 SlotIndex);

	/** Server-only: cancel any pending reclaim timer for `SlotIndex`. Called
	 *  on successful reconnect-rebind. */
	void CancelSlotReclaim(int32 SlotIndex);

	/** Server-only: revert `SlotIndex` from disconnected/reserved to fully
	 *  Open. Called by the grace-timer expiry. */
	void ExpireDisconnectedSlot(int32 SlotIndex);

	/** Project an FSeinLobbySlotState into the runtime FSeinMatchSlot used
	 *  by the snapshot. Drops lobby-only fields (bReady, bDisconnected, etc). */
	static FSeinMatchSlot ProjectLobbySlotToMatchSlot(const FSeinLobbySlotState& In);
	static FSeinLobbySlotState BuildDirectMatchSlot(const FSeinMatchSlot& In);
	static FSeinLobbySlotState BuildCommittedMatchSlot(const FSeinMatchSlot& In);
	static bool IsOpenSlotAvailable(
		const FSeinLobbySlotState& Slot,
		bool bLaunchCommitted);

	UPROPERTY()
	TWeakObjectPtr<ASeinLobbyState> LobbyStateActor;

	/** Server-side: PC ↔ slot mapping. Drives Logout-side release. */
	TMap<TWeakObjectPtr<APlayerController>, int32> ControllerToSlot;

	/** Server-side: pending grace-timers for disconnected slots, keyed by
	 *  slot index. Cancelled on successful reconnect. */
	TMap<int32, FTSTicker::FDelegateHandle> PendingReclaimTimers;

	/** Cached published snapshot. Updated on PublishMatchSettingsSnapshot. */
	FSeinMatchSettings PublishedSnapshot;
	bool bSnapshotPublished = false;
	bool bPublishedSnapshotLaunchCommitted = false;
	FSeinMatchSettingsLaunchCommitted MatchSettingsLaunchCommitted;
	bool bTravelScheduled = false;
	FTimerHandle PendingTravelTimerHandle;
	TWeakObjectPtr<UWorld> PendingTravelTimerWorld;

	/** GameMode-pushed slot count used by `EnsureLobbyActor` when lazy-spawning
	 *  the lobby actor in a world. Set via `SetSlotCountOverride`. Zero means
	 *  no override — fall back to AvailableMaps[0]/MaxPlayers (lobby-map
	 *  default). Non-zero means use this exact count (gameplay-map case). */
	int32 SlotCountOverride = 0;

	/** Canonical direct-map defaults staged before the first PostLogin. */
	FSeinMatchSettings DirectMatchSettingsDefaults;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
	bool bModuleOwnedStateReleased = false;
};
