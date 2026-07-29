/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.h
 * @brief   Game-instance subsystem orchestrating the lockstep network layer.
 *
 * Responsibilities:
 *   - On the server, spawn an ASeinNetRelay per APlayerController as it joins
 *     (FGameModeEvents::GameModePostLoginEvent) and retain slot identity across
 *     disconnect/reconnect. Owner = the PC, so the client legitimately owns
 *     its relay for ServerRPC.
 *   - Track all server-side relays + the one client-side local relay.
 *   - Provide SubmitLocalCommand() as the single client entry point.
 *   - Server-authoritative per-turn aggregation: buffer each slot's submission
 *     and dispatch the assembled turn only once every active slot has submitted
 *     for that TurnId (completeness gate), with deterministic command ordering
 *     before fan-out, seed distribution, replay capture, periodic world-root
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
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinMatchSettings.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinNetProtocolTypes.h"
#include "SeinBootstrapConsensus.h"
#include "SeinTurnAggregator.h"
#include "SeinNetSubsystem.generated.h"

class ASeinNetRelay;
class APlayerController;
class AGameModeBase;
class AController;
class USeinReplayWriter;
class USeinReplayReader;
class USeinAIController;
class USeinWorldSubsystem;
class UNetDriver;
struct FSeinParticipantWorldRootEntry;
struct FSeinCommandSchemaDescriptor;
struct FSeinNetSubsystemTestAccess;
namespace ETravelFailure { enum Type : int; }
namespace ENetworkFailure { enum Type : int; }

/** Optional native outbound adapter for P2P or non-UE transports. Returning
 *  false retains the exact report until the adapter explicitly retries it. */
DECLARE_DELEGATE_RetVal_TwoParams(
	bool,
	FSeinDeterminismSessionFailureSubmitter,
	const FSeinProtocolContext&,
	const FSeinDeterminismSessionFailure&);

/** A frozen local submission. Once a turn boundary assigns commands to a
 *  turn, retries must preserve that exact batch rather than merging later
 *  input into it. */
struct FSeinPendingTurnSubmission
{
	int32 TurnId = INDEX_NONE;
	TArray<FSeinCommandSubmissionDraft> Drafts;
};

/** Exact additive cost of one command inside an opaque batch. The fixed
 *  batch header is charged once when a turn prefix is selected. */
struct FSeinQueuedCommandCost
{
	int32 VariableWireBytes = 0;
	uint64 VariableCanonicalCostBytes = 0;
};

/** Bounded local-input backlog. Costs are captured from the same frozen
 *  codec/schema contract used at handoff, so selecting a prefix is linear. */
struct FSeinOutgoingDraftBacklog
{
	TArray<FSeinCommandSubmissionDraft> Drafts;
	TArray<FSeinQueuedCommandCost> Costs;
	int64 VariableWireBytes = 0;
	uint64 VariableCanonicalCostBytes = 0;

	bool IsEmpty() const { return Drafts.IsEmpty(); }
	int32 Num() const { return Drafts.Num(); }
	void Reset()
	{
		Drafts.Reset();
		Costs.Reset();
		VariableWireBytes = 0;
		VariableCanonicalCostBytes = 0;
	}
};

/** Per-author server-AI backlog. The source is consumed only after the
 *  selected prefix is accepted by the turn aggregator. */
struct FSeinAICommandBacklog
{
	TArray<FSeinCommand> Commands;
	TArray<FSeinQueuedCommandCost> Costs;
	int64 VariableWireBytes = 0;
	uint64 VariableCanonicalCostBytes = 0;

	bool IsEmpty() const { return Commands.IsEmpty(); }
	int32 Num() const { return Commands.Num(); }
};

/** A canonical world-state root captured at its exact checkpoint. A failed
 *  local send must
 *  retry this value; recomputing after the sim advances would report a
 *  different state under the old turn number. */
struct FSeinPendingWorldStateRootReport
{
	int32 Turn = INDEX_NONE;
	FGuid WorldRoot;
};

/** Non-authoritative, wall-clock-only diagnostics for one incomplete server
 *  turn. Kept per turn so interleaved network arrivals retain attribution. */
struct FSeinIncompleteTurnDiagnostic
{
	double FirstObservedAt = 0.0;
	double LastLoggedAt = 0.0;
	bool bEscalated = false;
};

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

enum class ESeinPreparedWorldActivation : uint8
{
	RequiresWorldTransition,
	AllowCurrentWorld,
};

/** Destination assignment retained by the game-instance without touching the
 * currently loaded world's active lockstep epoch. */
struct FSeinPendingLocalProtocolAssignment
{
	TWeakObjectPtr<ASeinNetRelay> Relay;
	FSeinPlayerID Slot;
	FSeinNetworkParticipantID ParticipantID;
	FSeinProtocolContext Context;
	int64 Seed = 0;
	bool bSimulates = false;
	FSeinMatchSettings MatchSettings;
	TWeakObjectPtr<UWorld> SourceWorld;
	ESeinPreparedWorldActivation Activation =
		ESeinPreparedWorldActivation::RequiresWorldTransition;

	bool IsSet() const { return Context.IsValid(); }
	void Reset() { *this = FSeinPendingLocalProtocolAssignment(); }
};

/** Coordinator-owned prepared epoch. It becomes active only after the loaded
 * world proves the destination digest. */
struct FSeinPendingAuthorityProtocolState
{
	FSeinProtocolContext Context;
	FSeinMatchSettings MatchSettings;
	int64 Seed = 0;
	TArray<FSeinParticipantBinding> ParticipantBindings;
	TMap<FSeinPlayerID, FSeinNetworkParticipantID> SlotToParticipant;
	FSeinNetworkParticipantID CoordinatorParticipantID;
	TMap<FSeinPlayerID, ESeinSlotLifecycle> SlotLifecycle;
	int32 FrozenMaxCommandsPerSubmission = 0;
	ESeinMatchTravelIntent Intent = ESeinMatchTravelIntent::NewMatch;
	TWeakObjectPtr<UWorld> SourceWorld;
	ESeinPreparedWorldActivation Activation =
		ESeinPreparedWorldActivation::RequiresWorldTransition;

	bool IsSet() const { return Context.IsValid(); }
	void Reset() { *this = FSeinPendingAuthorityProtocolState(); }
};

UCLASS()
class SEINARTSNET_API USeinNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Idempotently detach every callback and release runtime state before the
	 *  owning module's code can unload. Deinitialize delegates to this path. */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** Player-input entry point. Stamps an automatically-computed TurnId
	 *  derived from the current sim tick + InputDelayTurns. Caller doesn't
	 *  need to think about turns — just hand it the command.
	 *
	 *  In Standalone (or when networking is disabled in settings) the command
	 *  bypasses the relay and goes directly into USeinWorldSubsystem's
	 *  command buffer — single-player is zero-overhead. In a networked
	 *  session it buffers an untrusted draft for the next turn; authenticated
	 *  ingress replaces PlayerID, IssuerKind, and Tick before aggregation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SubmitLocalCommand(const FSeinCommand& Command);
	/** Topology-adapter entry used by USeinWorldSubsystem. Draft provenance is
	 *  ignored; match administration is only a request proved at ingress. */
	void SubmitLocalCommandDraft(
		const FSeinCommand& Draft,
		bool bRequestMatchAdministration);

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
	 *  gate stalls forever (for example, if relay assignment races tick-zero
	 *  authorization and the first heartbeat cannot be handed off). */
	bool SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command);
	bool SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Local player's slot, valid after the relay's AssignedPlayerID OnRep
	 *  fires (a few frames after PIE Play depending on replication latency).
	 *  Zero before that — callers should treat zero as "not yet assigned". */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network")
	FSeinPlayerID GetLocalPlayerID() const { return LocalPlayerID; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID GetLocalParticipantID() const { return LocalParticipantID; }

	const FSeinProtocolContext& GetActiveProtocolContext() const { return ActiveProtocolContext; }
	const TArray<FSeinParticipantBinding>& GetParticipantBindings() const { return ParticipantBindings; }
	const FSeinMatchSettings& GetActiveMatchSettings() const { return ActiveMatchSettings; }
	bool HasActiveMatchSettings() const { return bHasActiveMatchSettings; }

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
	int32 GetActiveSlotCount() const { return TurnAggregator.GetExpectedAuthors().Num(); }

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

	/** Update the local gameplay-slot cache for lobby/UI use. Protocol identity
	 *  is established separately when a prepared match assignment arrives. */
	void NotifyLocalLobbySlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot);

	/** Called by ASeinNetRelay replication on the owning client when its slot
	 *  and seed arrive. Latches local identity, ensures the current world's
	 *  lockstep hooks are bound, and retries deferred work. */
	void NotifyLocalProtocolAssigned(
		ASeinNetRelay* Relay,
		FSeinPlayerID Slot,
		FSeinNetworkParticipantID ParticipantID,
		const FSeinProtocolContext& Context,
		int64 Seed,
		bool bSimulates,
		const FSeinMatchSettings& MatchSettings,
		ESeinPreparedWorldActivation Activation =
			ESeinPreparedWorldActivation::RequiresWorldTransition);

	/**
	 * Establish durable match identity before in-place start or travel.
	 * Repeated preparation of the same pending intent is idempotent.
	 */
	bool PrepareMatchTravel(
		ESeinMatchTravelIntent Intent,
		FName DestinationWorldPackage,
		ESeinPreparedWorldActivation Activation =
			ESeinPreparedWorldActivation::RequiresWorldTransition);
	/** Cancel an unactivated prepared travel without disturbing the source epoch. */
	void AbortPreparedMatchTravel(const FString& Reason);

	/** Coordinator-only start request. Compatibility is checked first, then
	 *  every frozen simulating participant must report one identical tick-zero
	 *  materialization receipt before authorization is fanned out. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void StartLockstepSession();

	/** Standalone-only start/resume request. Network launch is coordinator-only. */
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
	void ServerHandleSubmission(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		int32 TurnId,
		const FSeinOpaqueCommandBatch& OpaqueDrafts);

	/** Client-side: server delivered an assembled turn. Stores it keyed by
	 *  `TurnId` in ReceivedTurns; the sim's gate (ResolveTurnReady → ConsumeTurn)
	 *  drains it at the matching sim-tick boundary (= TurnId * TicksPerTurn). */
	void ClientHandleTurn(
		const FSeinProtocolContext& Context,
		int32 TurnId,
		const FSeinOpaqueCommandBatch& OpaqueCommands);

	/** Coordinator requested local materialization under an exact context. */
	void ClientHandleBootstrapReceiptRequest(const FSeinProtocolContext& Context);

	/** Authenticated relay delivered a peer's sealed local receipt. */
	void ServerHandleBootstrapReceipt(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Authenticated relay reported terminal local materialization failure. */
	void ServerHandleBootstrapFailure(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context);

	/** Coordinator authorized this peer's exact local receipt. */
	void ClientHandleBootstrapAuthorization(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	void ServerHandleBootstrapAuthorizedReady(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	void ClientHandleBootstrapLaunch(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Coordinator terminated the frozen bootstrap attempt. */
	void ClientHandleBootstrapFailure(
		const FSeinProtocolContext& Context,
		const FString& Reason);
	void ClientHandlePreparedMatchTravelCancelled(
		const FSeinProtocolContext& Context);

	/** Coordinator-side: a peer reported its world-state root for a check
	 *  turn. Buffer per-peer roots; once all active peers have reported, compare —
	 *  agreement is silent (Verbose log), divergence fans Client_NotifyDesync
	 *  to every relay so all peers get the red on-screen alarm with the
	 *  full participant-root table. */
	void ServerHandleWorldStateRootReport(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		int32 Turn,
		FGuid WorldRoot);

	/** Shipped relay-adapter ingress. The relay supplies authenticated
	 *  participant provenance; the payload never chooses its identity. */
	void ServerHandleDeterminismSessionFailure(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		const FSeinDeterminismSessionFailure& Failure);

	/**
	 * Topology-neutral coordinator ingress for a trusted transport adapter.
	 * Dedicated, listen-host, P2P, and custom transports converge here after
	 * authenticating the reporter identity.
	 */
	bool HandleAuthenticatedDeterminismSessionFailure(
		const FSeinProtocolContext& Context,
		FSeinNetworkParticipantID AuthenticatedParticipantID,
		const FSeinDeterminismSessionFailure& Failure);

	/** Topology-neutral receiving side for a coordinator-issued terminal
	 *  determinism-health result. Custom transports call this exact seam. */
	void HandleAuthoritativeDeterminismSessionFailure(
		const FSeinProtocolContext& Context,
		const FSeinDeterminismSessionFailure& Failure);

	/** Server-side compatibility acknowledgement. Command, match-settings, and
	 *  simulation-content parity are mandatory; config parity is host policy. */
	void ServerHandleConfigFingerprint(
		ASeinNetRelay* SourceRelay,
		const FSeinProtocolContext& Context,
		int32 Fingerprint,
		FGuid CommandProtocolDigest,
		FGuid MatchSettingsDigest,
		FGuid SimulationContentDigest);

	/** Peer-side: coordinator told us a desync was detected at `Turn` and is
	 *  forwarding everyone's world roots so we can show the alarm with full
	 *  context. Logs at Error + posts a persistent red on-screen debug
	 *  message via GEngine->AddOnScreenDebugMessage. Optionally pauses the
	 *  local sim if `bPauseOnDesync` is set in plugin settings. */
	void ClientHandleDesyncNotification(
		const FSeinProtocolContext& Context,
		int32 Turn,
		const TArray<FSeinParticipantWorldRootEntry>& PeerRoots);

	/** True iff the local sim has been flagged as desynced. Once set, stays
	 *  true until the user calls `Sein.Net.ClearDesync` or restarts PIE.
	 *  The lockstep gate consults this so designers can choose to halt the
	 *  sim on detection (default: continue ticking, just show alarm). */
	bool IsLocalDesyncDetected() const { return bDesyncDetected; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network|Determinism")
	bool HasDeterminismSessionFailure() const
	{
		return DeterminismSessionFailure.IsValid();
	}

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network|Determinism")
	FSeinDeterminismSessionFailure GetDeterminismSessionFailure() const
	{
		return DeterminismSessionFailure;
	}

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network|Determinism")
	bool IsDeterminismSessionFailureAuthoritative() const
	{
		return bDeterminismSessionFailureAuthoritative;
	}

	/** Installs the single outbound terminal-report adapter used by a custom
	 *  transport. The built-in UE relay remains the fallback when unbound. */
	void SetDeterminismSessionFailureSubmitter(
		FSeinDeterminismSessionFailureSubmitter Submitter);
	void ClearDeterminismSessionFailureSubmitter();

	/** Explicit retry edge for a custom adapter that previously returned false.
	 *  Returns true once no local terminal report remains pending. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network|Determinism")
	bool RetryPendingDeterminismSessionFailureReport();

	/** Identity-based coordinator check; deliberately independent of UE net
	 *  mode so a custom peer-authority adapter can use the same contract. */
	bool IsLocalProtocolCoordinator() const
	{
		return ActiveProtocolContext.IsValid()
			&& LocalParticipantID.IsValid()
			&& ActiveProtocolContext.CoordinatorParticipantID
				== LocalParticipantID;
	}

	/** Native observer seam for custom topology adapters and systems code. */
	DECLARE_MULTICAST_DELEGATE_OneParam(
		FOnDeterminismSessionFailure,
		const FSeinDeterminismSessionFailure& /*Failure*/);
	FOnDeterminismSessionFailure OnDeterminismSessionFailure;

	/** Blueprint event for designer-authored failure UI and match flow. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnDeterminismSessionFailureDynamic,
		const FSeinDeterminismSessionFailure&,
		Failure);
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Network|Determinism")
	FOnDeterminismSessionFailureDynamic OnDeterminismSessionFailureBP;

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
	friend struct FSeinNetSubsystemTestAccess;
	enum class EFirstAcceptResult : uint8
	{
		Accepted,
		IdenticalDuplicate,
		ConflictingDuplicate,
	};

	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);

	/** FWorldDelegates::OnWorldCleanup handler. Resets state belonging to the
	 *  retiring tick-zero/turn-number epoch while preserving match identity
	 *  whose seamless-travel semantics are still a deliberate design gate. */
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void OnTravelFailure(
		UWorld* World,
		ETravelFailure::Type FailureType,
		const FString& ErrorString);
	void OnNetworkFailure(
		UWorld* World,
		UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType,
		const FString& ErrorString);
	bool OwnsFailureWorld(const UWorld* World) const;
	void CancelPendingLocalTravelFailure(const FString& Reason);

	/** Clear every command/world-root value whose identity is scoped to one fresh
	 *  simulation world. Does not decide whether seed, relay/slot identity,
	 *  replay, desync alarm, or drop lifecycle survive travel. */
	void ResetLockstepEpochState(UWorld* RetiringWorld);
	void ResetMatchState(UWorld* RetiringWorld);

	/** Release Net-owned AI takeover objects tied to RetiringWorld and discard
	 *  commands they authored for its obsolete turn epoch. */
	void ReleaseWorldOwnedAI(UWorld* RetiringWorld);

	/** True on listen and dedicated authority worlds. */
	bool IsServer() const;
	bool IsDedicatedAuthority() const;

	/** Server-side: lazy-init SessionSeed on first call. Once set, never
	 *  changes for the life of this game-instance subsystem. */
	void EnsureSessionSeed();
	bool BuildCanonicalParticipantManifest(
		const FSeinMatchInstanceID& MatchInstanceID,
		TArray<FSeinParticipantBinding>& OutBindings,
		TMap<FSeinPlayerID, FSeinNetworkParticipantID>& OutSlotToParticipant,
		FSeinNetworkParticipantID& OutCoordinatorParticipantID,
		TMap<FSeinPlayerID, ESeinSlotLifecycle>& OutSlotLifecycle);
	bool ConfigureTurnAggregator();
	bool ConfigureBootstrapConsensus();
	bool ResolveLocalCommandProtocolDigest(FGuid& OutDigest) const;
	bool ResolveLocalSimulationContentDigest(FGuid& OutDigest) const;
	void ApplyProtocolAssignmentToRelays();
	bool TryPromotePendingAuthorityProtocolState();
	bool TryPromotePendingLocalProtocolAssignment();
	void SchedulePendingProtocolPromotion();
	bool TickPendingProtocolPromotion(float DeltaSeconds);
	void CancelPendingProtocolPromotion();
	void TryRearmPreparedDestinationStart();
	bool IsCurrentProtocolContext(
		const FSeinProtocolContext& MessageContext,
		const TCHAR* Operation) const;
	const FSeinParticipantBinding* FindParticipantBinding(
		FSeinNetworkParticipantID ParticipantID) const;
	FSeinNetworkParticipantID FindParticipantForSlot(FSeinPlayerID Slot) const;
	bool IsParticipantConnected(FSeinNetworkParticipantID ParticipantID) const;
	bool AreRequiredStartParticipantsBound(
		TArray<FSeinNetworkParticipantID>* OutMissingParticipants = nullptr) const;
	bool IsCurrentWorldPreparedDestination(FString* OutError = nullptr) const;
	static bool IsPreparedWorldActivationEligible(
		const UWorld* CurrentWorld,
		const UWorld* SourceWorld,
		ESeinPreparedWorldActivation Activation,
		const FGuid& LoadedWorldDigest,
		const FGuid& DestinationWorldDigest);

	/** Bind the gate, turn-boundary, and authority-AI hooks to the current
	 *  simulation world independently of local-player relay assignment. */
	USeinWorldSubsystem* BindLockstepHooksForCurrentWorld();
	bool AreNetworkStartPrerequisitesReady(bool bHooksReady) const;
	void TryDispatchLockstepSessionStart();
	bool IsLocalSimulatingParticipant() const;
	bool TryMaterializeLocalBootstrapReceipt(
		const FSeinProtocolContext& Context,
		FSeinMatchBootstrapReceipt& OutReceipt,
		FString& OutError);
	void SubmitLocalBootstrapReceipt(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);
	void SubmitLocalBootstrapAuthorizedReady(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);
	void ScheduleBootstrapMaterializerRetry(
		const FSeinProtocolContext& Context);
	bool TickBootstrapMaterializerRetry(float DeltaSeconds);
	void CancelBootstrapMaterializerRetry();
	void SubmitBootstrapReceiptForParticipant(
		FSeinNetworkParticipantID ParticipantID,
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);
	void SubmitBootstrapAuthorizedReadyForParticipant(
		FSeinNetworkParticipantID ParticipantID,
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);
	void DispatchBootstrapReceiptRequests();
	void DispatchBootstrapAuthorization();
	void DispatchBootstrapLaunch();
	void TryAuthorizeLocalBootstrap();
	void TryLaunchLocalBootstrap();
	void StartBootstrapCoordinatorTimeout();
	bool TickBootstrapCoordinatorTimeout(float DeltaSeconds);
	void CancelBootstrapCoordinatorTimeout();
	void FailBootstrapSession(const FString& Reason, bool bNotifyPeers);
	void FailLocalBootstrapAfterCommit(const FString& Reason);
	void ReportLocalBootstrapFailure(const FSeinProtocolContext& Context);
	bool IsConfigParityStartBarrierSatisfied(
		TArray<FSeinNetworkParticipantID>* OutMissingParticipants = nullptr) const;
	static bool AreConfigFingerprintsComplete(
		const TArray<FSeinNetworkParticipantID>& ExpectedParticipants,
		const TMap<FSeinNetworkParticipantID, int32>& AcceptedFingerprints,
		int32 RequiredFingerprint);

	/** Server-side: check if every connected slot has submitted for `TurnId`;
	 *  if so, assemble + fan out via every relay's Client_ReceiveTurn. */
	void ServerCheckTurnComplete(
		int32 TurnId, FSeinPlayerID CompletingSubmitter = FSeinPlayerID());
	bool FreezeAuthorSubmissionPolicy(const TCHAR* Operation);
	int32 GetFrozenExpectedAuthorCount() const;
	bool ValidateAuthorSubmissionBudget(
		int32 CommandCount,
		const FSeinOpaqueCommandBatch& EncodedBatch,
		uint64 CanonicalCostBytes,
		FString& OutError) const;
	bool PreflightCanonicalTurnBatch(
		TConstArrayView<FSeinCommand> Commands,
		FString& OutError) const;
	void GetExpectedCommandSlots(TArray<FSeinPlayerID>& OutSlots) const;
	bool IsCommandSubmissionLifecycleAllowed(FSeinPlayerID Slot) const;
	bool IsAICommandSubmissionAllowed(
		FSeinPlayerID Slot,
		FString* OutError = nullptr) const;
	EFirstAcceptResult BufferWorldStateRootReportFirstWins(
		int32 Turn,
		FSeinNetworkParticipantID ParticipantID,
		const FGuid& WorldRoot);
	void StampAuthoritativeCommandBatch(
		TArray<FSeinCommand>& Commands,
		FSeinPlayerID Slot,
		bool bGrantMatchAdministration,
		int32 TurnId) const;
	bool FindFrozenCommandSchema(
		FGameplayTag CommandType,
		int32 SchemaVersion,
		FSeinCommandSchemaDescriptor& OutSchema) const;
	static bool IsUnsupportedNetworkPauseCommand(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor* Schema);
	bool MeasureOutgoingDraftCost(
		const FSeinCommandSubmissionDraft& Draft,
		FSeinQueuedCommandCost& OutCost,
		FString& OutError) const;
	bool MeasureAICommandCost(
		const FSeinCommand& Command,
		FSeinQueuedCommandCost& OutCost,
		FString& OutError) const;
	bool TryBufferOutgoingDraft(const FSeinCommandSubmissionDraft& Draft);
	void FreezeLargestOutgoingDraftPrefix(
		int32 TurnId, TArray<FSeinCommandSubmissionDraft>& OutDrafts);
	bool TryBufferAICommand(FSeinPlayerID Slot, const FSeinCommand& Command);
	void ConsumeAICommandPrefix(FSeinPlayerID Slot, int32 Count);
	bool PeekPendingAICommandsForTurn(
		FSeinPlayerID Slot, int32 TurnId, TArray<FSeinCommand>& OutCommands);
	bool BuildDroppedSlotSubmission(
		FSeinPlayerID Slot, int32 TurnId, bool bAllowAICommands,
		TArray<FSeinCommand>& OutCommands);

	/** Lockstep gate hook (Phase 2b). Bound on the current WorldSubsystem before
	 *  simulation start. Returns true iff the assembled turn for `Turn`
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

	/** Execute a standalone start/resume request. Network launch never enters. */
	void TryStartLocalSession();

	/** Put the canonical turn directly into a dedicated authority's gate
	 *  buffer; it has no owning relay through which to receive a client RPC. */
	void BufferAssembledTurnForDedicatedAuthority(int32 TurnId, const TArray<FSeinCommand>& Commands);
	void BufferReceivedTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Freeze every not-yet-queued turn through FinalTurn. Catch-up turns are
	 *  heartbeats; when bAttachCurrentCommands is true the final turn receives
	 *  the current command accumulator. */
	void QueueTurnSubmissionsThrough(int32 FinalTurn, bool bAttachCurrentCommands);

	/** Submit frozen turns in order. Stops at the first local-send failure and
	 *  leaves that entry intact for relay-assignment/replacement retry. */
	void FlushPendingTurnSubmissions();
	bool SubmitLocalDraftsAtTurn(
		int32 TurnId,
		const TArray<FSeinCommandSubmissionDraft>& Drafts);

	/** Submit exact captured world-state-root checkpoints in order, retaining the
	 *  first locally-unsent entry. */
	void FlushPendingWorldStateRootReports();

	/** Submit the exact local terminal report, retaining it until a topology
	 *  adapter accepts the handoff. */
	void FlushPendingDeterminismSessionFailure();

	/** Enqueue one valid exact root checkpoint unless already queued/reported. */
	void EnqueueWorldStateRootReport(int32 Turn, const FGuid& WorldRoot);

	/** Read-helper: ticks-per-turn from settings, with sane fallback. */
	int32 GetTicksPerTurn() const;
	int32 GetInputDelayTurns() const;
	int32 GetCurrentTurn() const;
	bool IsCommandTurnWithinProtocolWindow(int32 TurnId, const TCHAR* Context) const;
	bool IsDeterminismEvidenceTurnWithinProtocolWindow(
		int32 Turn, const TCHAR* Context) const;
	void PruneProtocolState(int32 ReferenceTurn);
	void GetExpectedWorldRootReporterParticipants(
		TArray<FSeinNetworkParticipantID>& OutParticipants) const;
	bool AreExpectedWorldRootReportsComplete(
		const TMap<FSeinNetworkParticipantID, FGuid>& Reports) const;
	void ServerHandleWorldStateRootReportForParticipant(
		FSeinNetworkParticipantID ParticipantID,
		int32 Turn,
		const FGuid& WorldRoot);
	bool IsDueWorldStateRootCheckpoint(int32 Turn) const;
	void ReportLocalWorldStateRootCaptureFailure(int32 CheckpointTurn);
	void ReportLocalDeterminismSessionFailure(
		const FSeinDeterminismSessionFailure& Failure);
	void HandleExecutionTopologyInvalidated(const FString& Reason);
	void EnterDeterminismSessionFailure(
		const FSeinDeterminismSessionFailure& Failure,
		bool bAuthoritative,
		bool bNotifyPeers);
	void ApplyDueAuthenticatedDeterminismSessionFailuresThrough(
		int32 ThroughTurn);
	void ExpireIncompleteWorldStateRootCheckpointsThrough(int32 Cutoff);

	UPROPERTY()
	TArray<TWeakObjectPtr<ASeinNetRelay>> Relays;

	/** The local client's relay (also valid on the host, since the host has
	 *  its own PC + relay). Set when a relay registers itself whose owner
	 *  matches the local first PC. */
	TWeakObjectPtr<ASeinNetRelay> LocalRelay;

	FSeinDeterminismSessionFailureSubmitter
		DeterminismSessionFailureSubmitter;

	/** Server-side: relay -> slot mapping. Source of truth for
	 *  authoritative "who sent this submission". */
	TMap<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID> RelayToSlot;
	TMap<TWeakObjectPtr<ASeinNetRelay>, FSeinNetworkParticipantID> RelayToParticipant;

	/** Durable match manifest. Slot identity and process identity are distinct. */
	TArray<FSeinParticipantBinding> ParticipantBindings;
	TMap<FSeinPlayerID, FSeinNetworkParticipantID> SlotToParticipant;
	FSeinNetworkParticipantID CoordinatorParticipantID;
	FSeinProtocolContext ActiveProtocolContext;
	FSeinMatchSettings ActiveMatchSettings;
	bool bHasActiveMatchSettings = false;
	FSeinPendingAuthorityProtocolState PendingAuthorityProtocolState;
	FSeinPendingLocalProtocolAssignment PendingLocalProtocolAssignment;
	FTSTicker::FDelegateHandle PendingProtocolPromotionHandle;
	/** CDO value captured once for a match. Epoch continuation preserves it;
	 *  only ResetMatchState clears it. */
	int32 FrozenMaxCommandsPerSubmission = 0;
	/** Frozen set of participants that simulate this epoch. It never shrinks
	 *  during bootstrap; a dedicated coordinator is included only if its
	 *  manifest binding actually simulates. */
	TSet<FSeinNetworkParticipantID> RequiredStartParticipants;
	FSeinBootstrapConsensus BootstrapConsensus;
	TOptional<FSeinMatchBootstrapReceipt> LocalBootstrapReceipt;
	TOptional<FSeinProtocolContext> PendingLocalBootstrapReceiptReportContext;
	TOptional<FSeinProtocolContext>
		PendingLocalBootstrapAuthorizedReadyReportContext;
	TOptional<FSeinProtocolContext> DeferredBootstrapReceiptRequestContext;
	FTSTicker::FDelegateHandle BootstrapMaterializerRetryHandle;
	int32 BootstrapMaterializerRetryAttempts = 0;
	TOptional<FSeinProtocolContext> PendingBootstrapAuthorizationContext;
	TOptional<FSeinMatchBootstrapReceipt> PendingBootstrapAuthorizationReceipt;
	TOptional<FSeinProtocolContext> PendingBootstrapLaunchContext;
	TOptional<FSeinMatchBootstrapReceipt> PendingBootstrapLaunchReceipt;
	/** Exclusive Core capability owned by this topology adapter for one world. */
	FSeinMatchBootstrapAuthorityHandle MatchBootstrapAuthority;
	FTSTicker::FDelegateHandle BootstrapCoordinatorTimeoutHandle;
	double BootstrapCoordinatorDeadlineSeconds = 0.0;
	FString BootstrapSessionFailureReason;
	bool bBootstrapFailureReported = false;
	bool bBootstrapLaunchBarrierActive = false;
	bool bLocalBootstrapIngressClosed = false;

	/** Host-side reports accepted by the pre-start config barrier. Values are
	 *  retained so a host setting changed after an early report cannot start
	 *  against a stale acceptance. New relay occupants must report afresh. */
	TMap<FSeinNetworkParticipantID, int32> AcceptedConfigFingerprints;

	/** StartLockstepSession is a request: parity RPCs may still be in flight.
	 *  The last accepted report retries and consumes this latch. */
	bool bServerStartRequested = false;

	/** Topology-neutral, immutable per-author turn ledger and assembly gate. */
	FSeinTurnAggregator TurnAggregator;

	/** Local-client cache: this client's slot (replicated via relay). Zero
	 *  until OnRep_AssignedPlayerID fires. */
	FSeinPlayerID LocalPlayerID;
	FSeinNetworkParticipantID LocalParticipantID;
	bool bLocalParticipantSimulates = false;

	/** Session-wide seed. On the server: generated once in EnsureSessionSeed
	 *  on first PostLogin. On clients: latched from the relay's replicated
	 *  property. */
	int64 SessionSeed = 0;

	/** Prepared before travel; survives source-world cleanup until destination re-arm. */
	bool bDestinationStartPending = false;
	ESeinMatchTravelIntent PendingTravelIntent = ESeinMatchTravelIntent::NewMatch;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;

	/** Subscription to FWorldDelegates::OnWorldCleanup. Fires when any UWorld
	 *  is being torn down (PIE stop, map travel, seamless travel transition,
	 *  etc). We use it to reset lockstep state so the next world's sim starts
	 *  with a clean turn buffer and counter. The subsystem itself survives
	 *  the world cleanup (GameInstance scope), so this hook stays bound for
	 *  the GI's lifetime — cleared in Deinitialize. */
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle NetworkFailureHandle;

	/** Subscription to USeinWorldSubsystem::OnSimTickCompleted, installed by
	 *  BindLockstepHooksForCurrentWorld and cleared with the turn epoch. */
	FDelegateHandle TickCompletedHandle;
	FDelegateHandle ExecutionTopologyInvalidatedHandle;
	bool bModuleOwnedStateReleased = false;

	/** Tracks WHICH WorldSubsystem the world-scoped handles are bound to.
	 *  Crucial for seamless travel: the GI-scoped NetSubsystem survives the
	 *  world swap, but the OLD WorldSubsystem (whose multicast handles were
	 *  registered against) is destroyed. The handle IDs become stale.
	 *  BindLockstepHooksForCurrentWorld compares it against the current
	 *  WorldSub — if they differ, it forces a rebind to the new WorldSub.
	 *  Without this, post-travel sims tick freely but never get
	 *  OnSimTickCompleted fired → no turn-boundary submissions → lockstep
	 *  gate stalls forever waiting for the next turn. */
	TWeakObjectPtr<USeinWorldSubsystem> CachedWorldSub;

	/** Client-side: assembled turns received from the server, awaiting
	 *  drainage at the matching sim-tick turn boundary. Keyed by turn ID. */
	TMap<int32, TArray<FSeinCommand>> ReceivedTurns;

	/** Client-side input waiting for real turn boundaries. Capped to four
	 *  author-turn shares by count, wire bytes, and decoded allocation. */
	FSeinOutgoingDraftBacklog PendingOutgoingDrafts;

	/** Frozen per-turn batches awaiting successful local handoff to the
	 *  reliable relay RPC. Normally empty immediately after each boundary. */
	TArray<FSeinPendingTurnSubmission> PendingTurnSubmissions;

	/** Highest turn assigned to PendingTurnSubmissions (sent or unsent). */
	int32 LastQueuedTurn = -1;

	/** Client-side: highest TurnId we've already submitted for. Tracked so a
	 *  single-turn worth of OnSimTickCompleted ticks doesn't double-submit
	 *  the same heartbeat. -1 means "never submitted yet". */
	int32 LastSubmittedTurn = -1;

	/** Standalone resume request. First tick-zero launch belongs to the active
	 *  bootstrap authority, never this compatibility entry point. */
	bool bStartSessionRequested = false;

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
	 *  delegate (bound on USeinWorldSubsystem with the lockstep hooks) and
	 *  drained by InjectDroppedSlotHeartbeats when assembling the outgoing
	 *  turn — replaces the empty heartbeat for slots whose AI emitted this
	 *  tick. Without this, AI commands would call EnqueueCommand directly on
	 *  the host's sim and immediately desync from every client.
	 *
	 *  Each slot is capped to four author-turn shares. Cleared per-slot in
	 *  TeardownAIForSlot (reconnect) and across-the-board in Deinitialize. */
	TMap<FSeinPlayerID, FSeinAICommandBacklog> PendingAICommands;

	/** Interceptor body installed through USeinWorldSubsystem's opaque AI route.
	 *  A bound adapter owns the routing decision: true means accepted; false
	 *  means rejected. Only an unbound adapter permits standalone ingress. */
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

	/** Complete dropped/AI slots for a turn. Only the canonical outgoing-turn
	 *  maintenance call may drain pending AI commands; recovery calls for
	 *  already-open turns must inject heartbeat-only submissions. */
	void InjectDroppedSlotHeartbeats(int32 Turn, bool bAllowAICommands);

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


	/** Per-turn, non-authoritative BUFFER INCOMPLETE diagnostics. Erased on
	 *  completion, protocol pruning, and epoch reset so it stays bounded. */
	TMap<int32, FSeinIncompleteTurnDiagnostic> IncompleteTurnDiagnostics;

	// ============== Determinism gossip ==============

	/** Coordinator-side: per-check-turn collected canonical world roots.
	 *  Inner key is the reporting participant, value is its root. When inner.Num() ==
	 *  GetActiveSlotCount() we run comparison + (on mismatch) fan-out.
	 *  Cleared per-turn after comparison. */
	TMap<int32 /*Turn*/, TMap<FSeinNetworkParticipantID, FGuid /*WorldRoot*/>>
		ServerWorldStateRootReports;

	/** Coordinator-side: turns that have already been compared, so a straggler's
	 *  late report doesn't re-trigger the alarm. Pruned periodically. */
	TSet<int32> CompletedWorldStateRootChecks;
	int32 CompletedWorldStateRootRejectionFloor = -1;

	/** Local-side: set when ClientHandleDesyncNotification fires. Stays true
	 *  until manually cleared. Surfaces via IsLocalDesyncDetected() for
	 *  the gate to consult if the project wants halt-on-desync. */
	bool bDesyncDetected = false;

	/** Local-side: highest turn we've already submitted a world-root report
	 *  for, so OnSimTickCompleted's check doesn't double-submit. */
	int32 LastWorldStateRootReportedTurn = -1;

	/** Exact checkpoint values captured locally but not yet handed to the
	 *  reliable relay RPC. */
	TArray<FSeinPendingWorldStateRootReport> PendingWorldStateRootReports;
	int32 LastWorldStateRootQueuedTurn = -1;

	/** Local terminal state. A participant-local capture failure is provisional
	 *  until the coordinator distributes one canonical failure value. */
	FSeinDeterminismSessionFailure DeterminismSessionFailure;
	bool bDeterminismSessionFailureAuthoritative = false;
	TOptional<FSeinDeterminismSessionFailure>
		PendingDeterminismSessionFailureReport;
	TMap<int32, TArray<FSeinDeterminismSessionFailure>>
		PendingAuthenticatedDeterminismSessionFailures;

#if WITH_DEV_AUTOMATION_TESTS
	/** White-box transport seams used only by the disabled non-shipping test
	 *  plugin to force local handoff failures deterministically. */
	TFunction<bool(int32, const TArray<FSeinCommand>&)> TestTurnSubmitOverride;
	TFunction<bool(int32, const FGuid&)> TestWorldStateRootSubmitOverride;
	TFunction<bool(const FSeinDeterminismSessionFailure&)>
		TestDeterminismSessionFailureSubmitOverride;
	TFunction<bool(FGuid&, FString&)> TestWorldStateRootResolverOverride;
	TOptional<bool> TestServerOverride;
	TOptional<bool> TestDedicatedAuthorityOverride;
	TOptional<TArray<FSeinParticipantBinding>> TestParticipantManifestOverride;
	TOptional<FGuid> TestCommandProtocolDigestOverride;
	TOptional<FGuid> TestSimulationContentDigestOverride;
	TOptional<int32> TestCommandProtocolMaxCommandsOverride;
	TOptional<bool> TestNetworkingActiveOverride;
	TOptional<bool> TestDeterminismGossipEnabledOverride;
	TOptional<int32> TestDeterminismCheckIntervalOverride;
	TOptional<int32> TestCurrentTurnOverride;
	TFunction<bool(FGameplayTag, int32, FSeinCommandSchemaDescriptor&)>
		TestFindCommandSchemaOverride;
#endif

	/**
	 * Resolve the canonical 128-bit root without falling back to the legacy
	 * diagnostic ComputeStateHash. This is the single integration point for
	 * the Core full-world-root API.
	 */
	bool ResolveLocalWorldStateRoot(FGuid& OutRoot, FString& OutError) const;

	/** Compute + submit the local sim's world-state root if `Turn` is a check turn
	 *  per `DeterminismCheckIntervalTurns`. Called from OnSimTickCompleted at
	 *  every turn boundary; the cadence check is internal. */
	void MaybeSubmitWorldStateRootCheck(int32 JustFinishedTurn);

	/** Coordinator-only: run the comparison for `Turn` once all peers have
	 *  reported. On match, log Verbose + clear; on mismatch, fan
	 *  Client_NotifyDesync to every relay. */
	void ServerCompareWorldStateRootsForTurn(int32 Turn);

	/** Read-helpers from settings. */
	bool IsDeterminismGossipEnabled() const;
	bool IsConfigParityCheckEnabled() const;
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
	void FinalizeCompletedTurnDiagnostics(
		int32 TurnId, FSeinPlayerID CompletingSubmitter);
	void RecordStragglerIfApplicable(int32 TurnId, FSeinPlayerID LastSubmittingSlot);

public:
	/** Server-only: read-only accessor for diagnostic console commands
	 *  (Sein.Net.LatencyReport). Returns the per-slot straggle count map. */
	const TMap<FSeinPlayerID, int32>& GetStragglerCounts() const { return StragglerCounts; }

	/** Server-only: total completed turns since session start (denominator
	 *  for straggle-rate calculation in the report). */
	int32 GetTurnsCompletedCount() const { return TurnsCompletedCount; }
};
