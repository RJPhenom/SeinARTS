/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.h
 * @brief   Game-instance subsystem orchestrating the lockstep network layer.
 *
 * Responsibilities:
 *   - On the server, spawn an ASeinNetRelay per APlayerController as it joins
 *     (FGameModeEvents::GameModePostLoginEvent), destroy on logout. Owner =
 *     the PC, so the client legitimately owns its relay for ServerRPC.
 *   - Track all server-side relays + the one client-side local relay.
 *   - Provide SubmitLocalCommand() as the single client entry point.
 *   - Server-authoritative per-turn aggregation: buffer each slot's submission
 *     and dispatch the assembled turn only once every active slot has submitted
 *     for that TurnId (completeness gate), with deterministic command ordering
 *     before fan-out, seed distribution, replay capture, periodic state-hash
 *     desync gossip, and drop-in/out with AI takeover.
 *
 * Networking is gated on:
 *     USeinARTSCoreSettings::bNetworkingEnabled  AND
 *     World->GetNetMode() != NM_Standalone
 * If either is false the subsystem stays alive but every entry point is a
 * no-op — single-player builds are zero-overhead.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "SeinNetSubsystem.generated.h"

class ASeinNetRelay;
class APlayerController;
class AGameModeBase;
class AController;
class USeinReplayWriter;
class USeinReplayReader;
class USeinAIController;
class USeinWorldSubsystem;
struct FSeinSlotHashEntry;

/**
 * Slot lifecycle states for drop-in/drop-out (Phase 4).
 * Stored per-slot in `USeinNetSubsystem::SlotLifecycle`.
 *
 *   Connected  — owning PC is live, submitting commands normally.
 *   Dropped    — PC disconnected; server submits empty heartbeats on the
 *                slot's behalf so the lockstep gate doesn't stall. After
 *                a timeout (per match settings' ESeinHostDropAction) the
 *                slot transitions to AITakeover.
 *   AITakeover — slot is now driven by an AI controller (designer-authored).
 *                Sim continues; the original player is gone for good unless
 *                they reconnect (future phase).
 *   Reconnecting — a new connection matching this slot's stable ID has
 *                arrived; server is sending the catch-up snapshot+tail.
 *                (Reserved — full reconnect catch-up is a follow-up phase.)
 */
UENUM(BlueprintType)
enum class ESeinSlotLifecycle : uint8
{
	Connected,
	Dropped,
	AITakeover,
	Reconnecting,
};

UCLASS()
class SEINARTSNET_API USeinNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Player-input entry point. Stamps an automatically-computed TurnId
	 *  derived from the current sim tick + InputDelayTurns. Caller doesn't
	 *  need to think about turns — just hand it the command.
	 *
	 *  In Standalone (or when networking is disabled in settings) the command
	 *  bypasses the relay and goes directly into USeinWorldSubsystem's
	 *  command buffer — single-player is zero-overhead. In a networked
	 *  session, forwards to the local relay's Server_SubmitCommands; the
	 *  server's AssignedPlayerID overrides the command's PlayerID before
	 *  fan-out, so callers don't need to fill it correctly (server will). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SubmitLocalCommand(const FSeinCommand& Command);

	/** Batched variant of SubmitLocalCommand. */
	void SubmitLocalCommands(const TArray<FSeinCommand>& Commands);

	/** Explicit-turn variants. Used by the Sein.Net.TestPing console command
	 *  to drive deterministic turn IDs for the gate test, and by future
	 *  callers that already know the target turn. Player-input handlers should
	 *  use the no-TurnId overload instead.
	 *
	 *  Returns true iff the submission was actually sent (Standalone direct-
	 *  enqueue, OR networked submit through a valid LocalRelay). Returns false
	 *  if dropped — caller MUST NOT advance any "last submitted" tracking on
	 *  false, otherwise the missed turn never re-submits and the lockstep
	 *  gate stalls forever (subtle race: OnRep_AssignedPlayerID hasn't fired
	 *  before Client_StartSession arrives, LocalRelay is still null at the
	 *  pre-fire moment, the heartbeat is silently dropped). */
	bool SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command);
	bool SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Local player's slot, valid after the relay's AssignedPlayerID OnRep
	 *  fires (a few frames after PIE Play depending on replication latency).
	 *  Zero before that — callers should treat zero as "not yet assigned". */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network")
	FSeinPlayerID GetLocalPlayerID() const { return LocalPlayerID; }

	/** Session-wide deterministic seed. Server generates once at first
	 *  PostLogin; replicated to clients via the relay. Sim PRNG MUST be
	 *  initialized from this exact value before tick 0 or rolls diverge. */
	int64 GetSessionSeed() const { return SessionSeed; }

	/** Server-side: how many slots are currently occupied. Used by the
	 *  per-turn completeness gate.
	 *
	 *  Counts only entries whose weak relay pointer is still valid. During
	 *  seamless travel, old relays' `EndPlay` can run after their `GetWorld()`
	 *  returns null — `GetNetSubsystem()` then yields null and the cleanup
	 *  call (`UnregisterRelay`) never fires, leaving stale entries in
	 *  `RelayToSlot`. Without filtering, the gate waits for submissions from
	 *  ghost relays forever. */
	int32 GetActiveSlotCount() const
	{
		int32 Count = 0;
		for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& Pair : RelayToSlot)
		{
			if (Pair.Key.IsValid())
			{
				++Count;
			}
		}
		return Count;
	}

	/** Server-only: spawn the lockstep relay for a given PlayerController
	 *  with the slot already chosen by the match-flow / lobby authority
	 *  (typically `ASeinGameMode::HandleStartingNewPlayer` after it sets
	 *  `SeinPC->SeinPlayerID`). This REPLACES the old auto-spawn-on-
	 *  PostLogin path, which independently sequenced slots via
	 *  NextSlotToAssign and could disagree with GameMode's match-settings-
	 *  driven binding when controllers connected in non-slot order — a
	 *  silent dual-source-of-truth bug.
	 *
	 *  Idempotent: if the PC already has a relay (e.g. seamless travel,
	 *  re-bind), the existing relay is re-stamped with the new slot
	 *  instead of double-spawning. */
	void ServerSpawnRelayForController(APlayerController* PC, FSeinPlayerID Slot);

	/** Called by ASeinNetRelay::OnRep_AssignedPlayerID on the owning client
	 *  when the slot + seed arrive. Binds the lockstep gate hooks but does
	 *  NOT start the sim — sim start is gated on Client_StartSession (server)
	 *  / StartLockstepSession (manual / lobby trigger). */
	void NotifyLocalSlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot, int64 Seed);

	/** Server-only: kick off the lockstep session. Fires Client_StartSession
	 *  on every connected relay (so all peers' sims start simultaneously, at
	 *  tick 0, with the gate engaged from the first frame) and starts the
	 *  server's own sim. Called from `Sein.Net.StartMatch` (Phase 2b) or
	 *  from a future lobby flow (Phase 3). Idempotent — already-running
	 *  sims are left alone. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void StartLockstepSession();

	/** Local-only: start the sim if it isn't running. Called by
	 *  Client_StartSession on receiving clients, and by StartLockstepSession
	 *  on the host. Public so the console command can drive it directly. */
	void StartLocalSession();

	/** Server-only accessor: the replay writer instance, valid between
	 *  StartLockstepSession and the eventual FinishRecording flush. nullptr
	 *  on clients (only the server captures the canonical turn stream).
	 *  Public so Sein.Net.SaveReplay can drive a manual flush. */
	USeinReplayWriter* GetReplayWriter() const { return ReplayWriter; }

	/** Lazy-create the persistent replay reader instance. Same lifetime as
	 *  the GI subsystem — created on first access, cleared in Deinitialize.
	 *  `Sein.Net.LoadReplay` / `Sein.Net.StopReplay` console commands route
	 *  through this. Phase 4a. */
	USeinReplayReader* GetOrCreateReplayReader();

	// ============== Drop-in / drop-out (Phase 4) ==============

	/** Server-only: simulate a disconnect for `Slot` (used by `Sein.Net.SimulateDisconnect`
	 *  console command + future tests). Marks the slot as Dropped — the gate
	 *  won't stall on it because the server starts submitting empty heartbeats
	 *  on its behalf each turn. After `DroppedToAITakeoverSeconds` of continued
	 *  drop, transitions to AITakeover (designer's AI controller takes over). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SimulateSlotDisconnect(FSeinPlayerID Slot);

	/** Server-only: simulate the reconnect path. Marks the slot Connected
	 *  again. Full snapshot+tail catch-up flow is a follow-up phase — for now
	 *  this just clears the dropped flag so the original PC's relay can
	 *  resume submitting (if the PC is still around, e.g., from a stutter
	 *  rather than a true disconnect). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SimulateSlotReconnect(FSeinPlayerID Slot);

	/** Read-only accessor for diagnostic console commands. */
	const TMap<FSeinPlayerID, ESeinSlotLifecycle>& GetSlotLifecycle() const { return SlotLifecycle; }

	/** Relay lifecycle hooks called by ASeinNetRelay::BeginPlay/EndPlay on
	 *  whichever process the body runs in. Server tracks every relay; clients
	 *  remember which one is theirs. */
	void RegisterRelay(ASeinNetRelay* Relay);
	void UnregisterRelay(ASeinNetRelay* Relay);

	/** Server-side: a relay just received a client submission. Stamps the
	 *  authoritative source slot, buffers into the per-turn map, and dispatches
	 *  the assembled turn once every active slot has submitted for `TurnId`. */
	void ServerHandleSubmission(ASeinNetRelay* SourceRelay, int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Client-side: server delivered an assembled turn. Stores it keyed by
	 *  `TurnId` in ReceivedTurns; the sim's gate (ResolveTurnReady → ConsumeTurn)
	 *  drains it at the matching sim-tick boundary (= TurnId * TicksPerTurn). */
	void ClientHandleTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Server-side: a peer reported its state hash for a check turn. Buffer
	 *  per-peer hashes; once all active peers have reported, compare —
	 *  agreement is silent (Verbose log), divergence fans Client_NotifyDesync
	 *  to every relay so all peers get the red on-screen alarm with the
	 *  full per-slot hash table. */
	void ServerHandleStateHashReport(ASeinNetRelay* SourceRelay, int32 Turn, int32 Hash);

	/** Client-side: server told us a desync was detected at `Turn` and is
	 *  forwarding everyone's hashes so we can show the alarm with full
	 *  context. Logs at Error + posts a persistent red on-screen debug
	 *  message via GEngine->AddOnScreenDebugMessage. Optionally pauses the
	 *  local sim if `bPauseOnDesync` is set in plugin settings. */
	void ClientHandleDesyncNotification(int32 Turn, const TArray<FSeinSlotHashEntry>& PeerHashes);

	/** True iff the local sim has been flagged as desynced. Once set, stays
	 *  true until the user calls `Sein.Net.ClearDesync` or restarts PIE.
	 *  The lockstep gate consults this so designers can choose to halt the
	 *  sim on detection (default: continue ticking, just show alarm). */
	bool IsLocalDesyncDetected() const { return bDesyncDetected; }

	/** Phase 0 visibility hooks — bind from console exec or test code. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTurnReceived, int32 /*TurnId*/, const TArray<FSeinCommand>& /*Commands*/);
	FOnTurnReceived OnTurnReceived;

	/**
	 * Fires whenever the local-side `LocalPlayerID` value changes. Driven by
	 * the relay's `AssignedPlayerID` OnRep (latched in NotifyLocalSlotAssigned)
	 * and on session teardown (Deinitialize clears it back to Neutral).
	 *
	 * Replaces the lobby ViewModel's tick-based poll: bind here in the
	 * ViewModel's Initialize and the local-slot label refreshes the moment
	 * the relay arrives.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLocalSlotChanged, FSeinPlayerID /*NewSlot*/);
	FOnLocalSlotChanged OnLocalSlotChanged;

	/** BP-bindable variant. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalSlotChangedDynamic, FSeinPlayerID, NewSlot);
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Network")
	FOnLocalSlotChangedDynamic OnLocalSlotChangedBP;

	/**
	 * Phase 4 polish: immediate-feedback delegate. Fires SYNCHRONOUSLY on
	 * `SubmitLocalCommand` / `SubmitLocalCommands`, BEFORE the network
	 * roundtrip. Designer Widget BPs / audio hooks bind here to surface
	 * instant local feedback (selection ring flash, audio cue, ground
	 * marker) without waiting for the InputDelayTurns roundtrip — hides
	 * lockstep latency from the player.
	 *
	 * BP-bindable variant + matching BPFL `BindOnLocalCommandIssued` ships in
	 * SeinARTSFramework so designers can wire from a Widget BP graph.
	 *
	 * The command's TurnId is NOT yet set at this point — only the local PC
	 * has captured it; the server will stamp the authoritative turn during
	 * fan-out. Use the command's Type / TargetEntity / Position fields for
	 * feedback decisions, not Turn.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLocalCommandIssued, const FSeinCommand& /*Command*/);
	FOnLocalCommandIssued OnLocalCommandIssued;

	/** Dynamic (BP-bindable) variant of the same event. Fires alongside the
	 *  native delegate from `SubmitLocalCommand`. Designers bind from a
	 *  Widget BP / GameInstance event graph. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalCommandIssuedDynamic, const FSeinCommand&, Command);
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Network|Feedback")
	FOnLocalCommandIssuedDynamic OnLocalCommandIssuedBP;

	/** True if networking is enabled in settings AND the world has a real
	 *  NetDriver (NetMode != NM_Standalone). Public so callers can early-out
	 *  before constructing a payload. */
	bool IsNetworkingActive() const;

	/** Server-side accessor: every relay currently registered. Client-side:
	 *  contains only the local relay (or empty if not replicated yet). */
	const TArray<TWeakObjectPtr<ASeinNetRelay>>& GetRelays() const { return Relays; }

private:
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);

	/** FWorldDelegates::OnWorldCleanup handler. Resets per-match lockstep
	 *  state (`ServerTurnBuffer`, `ReceivedTurns`, `PendingOutgoingCommands`,
	 *  `CompletedTurns`, `LastSubmittedTurn`) when a world is torn down so
	 *  the next world's sim starts from a clean lockstep pipeline. Filtered
	 *  to only the world this GameInstance owns — cleanup of other worlds
	 *  (sub-PIE, etc.) is ignored. Relay/peer-identity state is preserved. */
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	/** True only on the server side. Cached at first relay-spawn / first
	 *  hook call; cheaper than re-querying NetMode every entry. */
	bool IsServer() const;

	/** Server-side: lazy-init SessionSeed on first call. Once set, never
	 *  changes for the life of this game-instance subsystem. */
	void EnsureSessionSeed();

	/** Server-side: check if every connected slot has submitted for `TurnId`;
	 *  if so, assemble + fan out via every relay's Client_ReceiveTurn. */
	void ServerCheckTurnComplete(int32 TurnId);

	/** Lockstep gate hook (Phase 2b). Bound on USeinWorldSubsystem when the
	 *  local slot is assigned. Returns true iff the assembled turn for `Turn`
	 *  is in ReceivedTurns, OR `Turn` is in the InputDelay-turns grace period
	 *  at session start (no submissions could exist for those). */
	bool ResolveTurnReady(int32 Turn);

	/** Lockstep drain hook (Phase 2b). Paired with ResolveTurnReady. Drains
	 *  `Turn`'s assembled commands into WorldSubsystem.PendingCommands and
	 *  removes the entry from ReceivedTurns. Called once per turn boundary. */
	void ConsumeTurn(int32 Turn);

	/** Subscribed to USeinWorldSubsystem::OnSimTickCompleted via
	 *  TickCompletedHandle. At every turn boundary, flushes pending outgoing
	 *  commands (or an empty heartbeat) to the server so the gate can
	 *  complete on every connected peer. */
	void OnSimTickCompleted(int32 CompletedTick);

	/** Read-helper: ticks-per-turn from settings, with sane fallback. */
	int32 GetTicksPerTurn() const;
	int32 GetInputDelayTurns() const;

	UPROPERTY()
	TArray<TWeakObjectPtr<ASeinNetRelay>> Relays;

	/** The local client's relay (also valid on the host, since the host has
	 *  its own PC + relay). Set when a relay registers itself whose owner
	 *  matches the local first PC. */
	TWeakObjectPtr<ASeinNetRelay> LocalRelay;

	/** Server-side: relay -> slot mapping. Source of truth for
	 *  authoritative "who sent this submission". */
	TMap<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID> RelayToSlot;

	/** Server-side: per-turn aggregation buffer. Inner map: PlayerID -> their
	 *  submitted commands for that turn. Once Inner.Num() == GetActiveSlotCount(),
	 *  the turn is fanned out and the entry purged. */
	TMap<int32, TMap<FSeinPlayerID, TArray<FSeinCommand>>> ServerTurnBuffer;

	/** Server-side: completed/dispatched turn IDs (so a stragglar's late
	 *  submission for an already-dispatched turn doesn't re-fan or stall). */
	TSet<int32> CompletedTurns;

	/** Local-client cache: this client's slot (replicated via relay). Zero
	 *  until OnRep_AssignedPlayerID fires. */
	FSeinPlayerID LocalPlayerID;

	/** Session-wide seed. On the server: generated once in EnsureSessionSeed
	 *  on first PostLogin. On clients: latched from the relay's replicated
	 *  property. */
	int64 SessionSeed = 0;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;

	/** Subscription to FWorldDelegates::OnWorldCleanup. Fires when any UWorld
	 *  is being torn down (PIE stop, map travel, seamless travel transition,
	 *  etc). We use it to reset lockstep state so the next world's sim starts
	 *  with a clean turn buffer and counter. The subsystem itself survives
	 *  the world cleanup (GameInstance scope), so this hook stays bound for
	 *  the GI's lifetime — cleared in Deinitialize. */
	FDelegateHandle WorldCleanupHandle;

	/** Subscription to USeinWorldSubsystem::OnSimTickCompleted, set in
	 *  NotifyLocalSlotAssigned (after the world subsystem exists), cleared
	 *  in Deinitialize. Drives the per-turn heartbeat flush. */
	FDelegateHandle TickCompletedHandle;

	/** Tracks WHICH WorldSubsystem the TickCompletedHandle is bound to.
	 *  Crucial for seamless travel: the GI-scoped NetSubsystem survives the
	 *  world swap, but the OLD WorldSubsystem (whose multicast TickCompletedHandle
	 *  was registered against) is destroyed. The handle ID becomes stale.
	 *  NotifyLocalSlotAssigned compares CachedWorldSub against the current
	 *  WorldSub — if they differ, it forces a rebind to the new WorldSub.
	 *  Without this, post-travel sims tick freely but never get
	 *  OnSimTickCompleted fired → no turn-boundary submissions → lockstep
	 *  gate stalls forever waiting for the next turn. */
	TWeakObjectPtr<USeinWorldSubsystem> CachedWorldSub;

	/** Client-side: assembled turns received from the server, awaiting
	 *  drainage at the matching sim-tick turn boundary. Keyed by turn ID. */
	TMap<int32, TArray<FSeinCommand>> ReceivedTurns;

	/** Client-side: locally captured commands buffered until the next turn
	 *  boundary, when they're flushed to the server (with the appropriate
	 *  outgoing TurnId stamp) along with an implicit heartbeat. */
	TArray<FSeinCommand> PendingOutgoingCommands;

	/** Client-side: highest TurnId we've already submitted for. Tracked so a
	 *  single-turn worth of OnSimTickCompleted ticks doesn't double-submit
	 *  the same heartbeat. -1 means "never submitted yet". */
	int32 LastSubmittedTurn = -1;

	/** Server-only: captures every dispatched turn for offline replay
	 *  reconstruction. Created lazily in StartLockstepSession, flushed in
	 *  Deinitialize (or via Sein.Net.SaveReplay). Null on clients. */
	UPROPERTY()
	TObjectPtr<USeinReplayWriter> ReplayWriter;

	/** Standalone-mode replay playback instance, created lazily by
	 *  `GetOrCreateReplayReader`. */
	UPROPERTY()
	TObjectPtr<USeinReplayReader> ReplayReader;

	/** Drop-in/drop-out (Phase 4): per-slot lifecycle state. Server-only.
	 *  Updated on PostLogin → Connected; on Logout → Dropped (relay kept
	 *  alive so the slot can resume); on AI-takeover transition; on
	 *  reconnect resolution. */
	TMap<FSeinPlayerID, ESeinSlotLifecycle> SlotLifecycle;

	/** Server-side: when a slot transitioned from Connected → Dropped, this
	 *  records the wall-clock timestamp. Used to fire the AI-takeover
	 *  transition after `DroppedToAITakeoverSeconds` of continuous drop. */
	TMap<FSeinPlayerID, double> SlotDroppedAtTime;

	/** Server-side: AI-emitted commands buffered until the next turn boundary,
	 *  keyed by the AI's owned slot. Populated by the AIEmitInterceptor
	 *  delegate (bound on USeinWorldSubsystem in NotifyLocalSlotAssigned) and
	 *  drained by InjectDroppedSlotHeartbeats when assembling the outgoing
	 *  turn — replaces the empty heartbeat for slots whose AI emitted this
	 *  tick. Without this, AI commands would call EnqueueCommand directly on
	 *  the host's sim and immediately desync from every client.
	 *
	 *  Cleared per-slot in TeardownAIForSlot (reconnect) and across-the-board
	 *  in Deinitialize. */
	TMap<FSeinPlayerID, TArray<FSeinCommand>> PendingAICommands;

	/** Interceptor body bound to USeinWorldSubsystem::AIEmitInterceptor. Returns
	 *  true iff the command was routed onto the lockstep wire (server +
	 *  networking active); false signals the AI controller should fall back
	 *  to direct local enqueue. */
	bool HandleAIEmit(FSeinPlayerID OwnedSlot, const FSeinCommand& Command);

	/** Server-side: per-slot AI controller instantiated on the
	 *  `Dropped → AITakeover` transition (when SlotDropPolicy is BasicAI).
	 *  Tracked here so reconnect can find + unregister the controller — the
	 *  WorldSubsystem holds a TObjectPtr to it for ticking, but we own the
	 *  per-slot lookup back. Empty map outside of an active dropped session.
	 *
	 *  Strong UPROPERTY ref so the controller stays alive even if the
	 *  WorldSubsystem's tracking array is briefly empty during edge cases
	 *  (subsystem shutdown ordering, level transitions). */
	UPROPERTY()
	TMap<FSeinPlayerID, TObjectPtr<USeinAIController>> AITakeoverControllers;

	/** Server-side helper: read DroppedToAITakeoverSeconds from plugin
	 *  settings with a sane fallback if settings aren't yet available. */
	double GetDroppedToAITakeoverSeconds() const;

	/** Server-side: instantiate + register the configured AI controller for
	 *  `Slot` on its Dropped → AITakeover transition. No-op if SlotDropPolicy
	 *  isn't BasicAI, or if a controller is already tracked for `Slot`.
	 *  Resolves `DefaultAIControllerClass` from settings (falls back to
	 *  USeinNullAIController if empty / failed to load). */
	void TryAutoRegisterAIForSlot(FSeinPlayerID Slot);

	/** Server-side: unregister + null any AI controller previously installed
	 *  for `Slot`. Called on reconnect (AITakeover → Connected). Safe to
	 *  call when no controller is tracked. */
	void TeardownAIForSlot(FSeinPlayerID Slot);

	/** Server-side: per-Dropped-slot heartbeat injection — called from
	 *  OnSimTickCompleted. For each slot in Dropped state, submit an empty
	 *  command list directly into the server's turn buffer so the gate can
	 *  complete without waiting for an unreachable peer. */
	void InjectDroppedSlotHeartbeats(int32 Turn);

	/** Server-side: poll dropped slots once per simulated turn boundary; if
	 *  any have been dropped longer than the timeout, transition them to
	 *  AITakeover. Logs the transition + fires future hooks for designer
	 *  AI controllers to claim the slot. */
	void EvaluateDroppedSlots();

	/** Rate-limit state for the GATE STALL diagnostic in ResolveTurnReady.
	 *  Tracks the currently-stalled turn + when it FIRST became unready —
	 *  used to escalate from Verbose (transient pipeline blip) to Log
	 *  (persistent ≥2s stall) without spamming on every turn boundary. */
	int32 LastStalledTurn = -1;
	double FirstStalledAtTime = 0.0;
	double LastStallLogTime = 0.0;
	bool bStallLogEscalated = false;

	/** Same persistence-tracking pattern for the server-side BUFFER
	 *  INCOMPLETE diagnostic. Most "incompletes" are transient (one peer's
	 *  packet arrives ms later than the others) — only persistent ones
	 *  are worth a Log-level line. */
	int32 LastIncompleteWarnedTurn = -1;
	double FirstIncompleteAtTime = 0.0;
	double LastIncompleteWarnTime = 0.0;
	bool bIncompleteLogEscalated = false;

	// ============== Determinism gossip (Phase 4) ==============

	/** Server-side: per-check-turn collected state hashes. Inner key is the
	 *  reporting peer's slot, value is their hash. When inner.Num() ==
	 *  GetActiveSlotCount() we run comparison + (on mismatch) fan-out.
	 *  Cleared per-turn after comparison. */
	TMap<int32 /*Turn*/, TMap<FSeinPlayerID, int32 /*Hash*/>> ServerHashReports;

	/** Server-side: turns that have already been compared, so a stragglar's
	 *  late report doesn't re-trigger the alarm. Pruned periodically. */
	TSet<int32> CompletedHashChecks;

	/** Local-side: set when ClientHandleDesyncNotification fires. Stays true
	 *  until manually cleared. Surfaces via IsLocalDesyncDetected() for
	 *  the gate to consult if the project wants halt-on-desync. */
	bool bDesyncDetected = false;

	/** Client-side: highest turn we've already submitted a state-hash report
	 *  for, so OnSimTickCompleted's check doesn't double-submit. */
	int32 LastHashReportedTurn = -1;

	/** Compute + submit the local sim's state hash if `Turn` is a check turn
	 *  per `DeterminismCheckIntervalTurns`. Called from OnSimTickCompleted at
	 *  every turn boundary; the cadence check is internal. */
	void MaybeSubmitStateHashCheck(int32 JustFinishedTurn);

	/** Server-only: run the comparison for `Turn` once all peers have
	 *  reported. On match, log Verbose + clear; on mismatch, fan
	 *  Client_NotifyDesync to every relay. */
	void ServerCompareHashesForTurn(int32 Turn);

	/** Read-helpers from settings. */
	bool IsDeterminismGossipEnabled() const;
	int32 GetDeterminismCheckIntervalTurns() const;

	// ============== Adaptive input delay observability (Phase 4 polish) ==============
	// MVP: observability-only. Tracks per-peer late-submission events and logs
	// a recommendation when stragglers are detected. Full automatic delay
	// raising is deferred — it requires a replicated dynamic-delay state and
	// careful synchronization across the grace period to avoid cross-peer
	// disagreement on outgoing-turn arithmetic. Today designers can manually
	// raise InputDelayTurns in settings + restart the session.

	/** Server-side: per-peer count of "this slot was last to submit for a
	 *  turn the server had to wait on" events. Surfaces in
	 *  Sein.Net.LatencyReport so designers can identify which peer's
	 *  connection is the bottleneck. */
	TMap<FSeinPlayerID, int32> StragglerCounts;

	/** Server-side: total number of completed turns observed since session
	 *  start. Denominator for straggle-rate calculation. */
	int32 TurnsCompletedCount = 0;

	/** Server-side helper: bump straggler count for the slot whose submission
	 *  pushed a turn into completion AFTER it had been logged as INCOMPLETE.
	 *  Called from ServerCheckTurnComplete on the completion path. */
	void RecordStragglerIfApplicable(int32 TurnId, FSeinPlayerID LastSubmittingSlot);

public:
	/** Server-only: read-only accessor for diagnostic console commands
	 *  (Sein.Net.LatencyReport). Returns the per-slot straggle count map. */
	const TMap<FSeinPlayerID, int32>& GetStragglerCounts() const { return StragglerCounts; }

	/** Server-only: total completed turns since session start (denominator
	 *  for straggle-rate calculation in the report). */
	int32 GetTurnsCompletedCount() const { return TurnsCompletedCount; }
};
