/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetRelay.h
 * @brief   Per-PlayerController relay actor carrying the lockstep wire.
 *
 * One relay is server-spawned for every connected APlayerController and
 * owned by it (so the client legitimately owns the actor it's RPC'ing
 * through). Its RPCs carry authenticated command submissions, canonical turn
 * fan-out, compatibility reports, and the prepare/receipt/authorize bootstrap
 * handshake. Participant identity always comes from relay ownership rather
 * than a caller-provided identity field.
 *
 * No multicast — fan-out is done by iterating per-PC relays. Same total
 * bandwidth as a true Multicast_BroadcastTurn, but no Always-Relevant
 * singleton needed and ownership stays clean (each client owns only its own
 * relay).
 *
 * The RPC bodies are not stubs: a client submission routes into
 * USeinNetSubsystem::ServerHandleSubmission (per-turn buffering + completeness
 * gate), and the assembled turn fans back to each relay into
 * USeinNetSubsystem::ClientHandleTurn (buffered for the sim's turn-boundary
 * drain).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h" // ESeinSlotState
#include "GameplayTagContainer.h"
#include "SeinNetProtocolTypes.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinNetRelay.generated.h"

class USeinNetSubsystem;
class USeinLobbySubsystem;

/**
 * Per-participant canonical world-state root exchanged for determinism gossip.
 * The coordinator collects one of these per peer per check turn; on
 * mismatch the assembled list is fanned back to every peer so the on-screen
 * red alarm can show *who* diverged, not just that something diverged.
 */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinParticipantWorldRootEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID ParticipantID;

	/** Canonical BLAKE3-128 world-state root at the exact check turn. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid WorldRoot;

	FSeinParticipantWorldRootEntry() = default;
	FSeinParticipantWorldRootEntry(
		FSeinNetworkParticipantID InParticipantID,
		FGuid InWorldRoot)
		: ParticipantID(InParticipantID), WorldRoot(InWorldRoot) {}
};

/**
 * Carries the lockstep networking wire between one player and the host. Every connected player
 * gets their own relay, and it is server-spawned automatically — designers do not place or pick it.
 *
 * The server spawns one relay per APlayerController and hands ownership to that controller, so a
 * client legitimately owns the actor whose RPCs it drives (spoofing-resistant: a client owns only
 * its own relay). The relay is the RPC endpoint for the whole lockstep session: clients submit
 * their turn commands up to the server (Server_SubmitCommands), the server unicasts each assembled
 * turn back down (Client_ReceiveTurn), and it also carries session start, the pre-match lobby verbs
 * (slot claim / ready / team / slot state / map select / kick / leave), and the Phase 4 determinism
 * gossip (each simulation peer reports its per-turn canonical world root; on divergence the
 * coordinator fans the full participant-root list back so every machine can show who desynced).
 *
 * Fan-out is done by the host iterating every per-controller relay rather than by a Multicast — the
 * bandwidth is the same as a true broadcast, but no Always-Relevant singleton is needed and each
 * client owns only its own relay. Command RPCs are Reliable because lockstep cannot tolerate a
 * dropped or reordered turn. The server treats the sending relay's AssignedPlayerID as the
 * authoritative sender and rewrites the command's PlayerID before fan-out.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Net Relay"))
class SEINARTSNET_API ASeinNetRelay : public AActor
{
	GENERATED_BODY()

public:
	ASeinNetRelay();

	/** Sim-side player slot (1..N) the server has assigned to this relay's
	 *  owner. Stamped server-side at spawn and replicated to the owning client.
	 *  Phase 1 — sequential by PostLogin order; Phase 2 lobby flow will let
	 *  designers pick / shuffle slots before StartMatch. */
	UPROPERTY(ReplicatedUsing = OnRep_AssignedPlayerID, BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinPlayerID AssignedPlayerID;

	/** Stable process identity, intentionally distinct from gameplay slots. */
	UPROPERTY(ReplicatedUsing = OnRep_ProtocolAssignment, BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID AssignedParticipantID;

	/** Exact match/epoch/term/membership namespace for every lockstep RPC. */
	UPROPERTY(ReplicatedUsing = OnRep_ProtocolAssignment, BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinProtocolContext ProtocolContext;

	/** Session-wide deterministic seed. Same value on every relay (one per PC,
	 *  same value across all). Lockstep-critical: every client's PRNG MUST
	 *  initialize from this exact value before tick 0, or rolls diverge from
	 *  the first frame. */
	UPROPERTY(ReplicatedUsing = OnRep_SessionSeed, BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 SessionSeed = 0;

	/** Client -> server. Owning client packs its commands for `TurnId` into a
	 *  bounded opaque batch; server resolves only frozen schema identities and
	 *  forwards to USeinNetSubsystem for aggregation. Reliable: lockstep cannot
	 *  tolerate dropped commands. The server treats the source relay's
	 *  `AssignedPlayerID` as the authoritative sender (the FSeinCommand's
	 *  PlayerID field is rewritten on the server before fan-out). */
	UFUNCTION(Server, Reliable)
	void Server_SubmitCommands(
		const FSeinProtocolContext& Context,
		int32 TurnId,
		const FSeinOpaqueCommandBatch& OpaqueDrafts);

	/** Server -> owning client. Delivered as the same bounded opaque command
	 *  format after the host has assembled every
	 *  player's commands for `TurnId`. Client hands the unified turn to its
	 *  USeinWorldSubsystem (Phase 2 sim integration). */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveTurn(
		const FSeinProtocolContext& Context,
		int32 TurnId,
		const FSeinOpaqueCommandBatch& OpaqueCommands);

	/** Coordinator -> simulating peer. Materialize this exact prepared context
	 *  and report the sealed tick-zero receipt. This is intentionally separate
	 *  from Client_PrepareMatchBootstrap so pre-travel preparation never seals
	 *  the source world. */
	UFUNCTION(Client, Reliable)
	void Client_RequestMatchBootstrapReceipt(const FSeinProtocolContext& Context);

	/** Simulating peer -> coordinator. Source identity comes from relay ownership;
	 *  the payload cannot claim a participant. */
	UFUNCTION(Server, Reliable)
	void Server_ReportMatchBootstrapReceipt(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Simulating peer -> coordinator. Local materialization failed closed. */
	UFUNCTION(Server, Reliable)
	void Server_ReportMatchBootstrapFailure(const FSeinProtocolContext& Context);

	/** Coordinator -> simulating peer. Unanimous authorization for the peer's
	 *  exact locally sealed receipt and protocol context. */
	UFUNCTION(Client, Reliable)
	void Client_AuthorizeMatchBootstrap(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Simulating peer -> coordinator. Core accepted the exact authorization. */
	UFUNCTION(Server, Reliable)
	void Server_ReportMatchBootstrapAuthorizedReady(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Coordinator -> simulating peer. Launch the already-authorized receipt. */
	UFUNCTION(Client, Reliable)
	void Client_LaunchMatchBootstrap(
		const FSeinProtocolContext& Context,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Coordinator -> peer. The frozen bootstrap attempt failed terminally. */
	UFUNCTION(Client, Reliable)
	void Client_FailMatchBootstrap(
		const FSeinProtocolContext& Context,
		const FString& Reason);

	/** Server -> owning client. Atomic pre-travel match bootstrap. The owning
	 *  GameInstance stores this before destination world initialization so
	 *  client pre-registration and tick zero use the authority's exact settings. */
	UFUNCTION(Client, Reliable)
	void Client_PrepareMatchBootstrap(
		FSeinPlayerID Slot,
		FSeinNetworkParticipantID ParticipantID,
		const FSeinProtocolContext& Context,
		int64 Seed,
		bool bSimulates,
		bool bAllowCurrentWorldActivation,
		const FSeinMatchSettings& MatchSettings);

	/** Coordinator cancelled a prepared travel before destination activation. */
	UFUNCTION(Client, Reliable)
	void Client_CancelPreparedMatchTravel(const FSeinProtocolContext& Context);

	/** Client -> server. Lobby slot claim (Phase 3b). Owning client requests
	 *  that the server place them in `SlotIndex` with `Faction`. Server-side
	 *  USeinLobbySubsystem validates + commits + replicates the updated lobby
	 *  state to all peers via the ASeinLobbyState actor's RepNotify. The
	 *  command's authoritative sender is the owning PC; spoofing-resistant
	 *  by virtue of relay ownership (clients only own their own relay). */
	UFUNCTION(Server, Reliable)
	void Server_RequestSlotClaim(int32 SlotIndex, FSeinFactionID Faction);

	/** Client -> server. Owning PC toggles their slot's bReady flag. */
	UFUNCTION(Server, Reliable)
	void Server_RequestSetReady(bool bReady);

	/** Client -> server. Host-only: change a slot's TeamID. */
	UFUNCTION(Server, Reliable)
	void Server_RequestSetTeam(int32 SlotIndex, uint8 TeamID);

	/** Client -> server. Host-only: change a slot's state (Open/AI/Closed —
	 *  Human transitions go through Server_RequestSlotClaim). AIProfile only
	 *  honored when NewState == AI. */
	UFUNCTION(Server, Reliable)
	void Server_RequestSetSlotState(int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile);

	/** Client -> server. Host-only: change the lobby's selected gameplay
	 *  map (must be one of `PluginSettings::AvailableMaps`). Server resizes
	 *  the lobby's slot count to match the entry's SlotCount; rejects shrink
	 *  if any claimed slot would be lost. */
	UFUNCTION(Server, Reliable)
	void Server_RequestSelectMap(const TSoftObjectPtr<UWorld>& Map);

	/** Client -> server. Host-only: kick whoever occupies `SlotIndex` and
	 *  reset the slot to Open. Drops Human-claimed PCs via
	 *  `ClientReturnToMainMenuWithTextReason`; AI/Closed slots just open.
	 *  Cannot kick the host's own slot (would tear down the listen server). */
	UFUNCTION(Server, Reliable)
	void Server_RequestKickPlayer(int32 SlotIndex);

	/** Client -> server. Voluntary leave signal. Sent by the BPFL Leave verb
	 *  BEFORE disconnecting so the server can distinguish "intentional quit"
	 *  from "lost connection." Server fully releases the slot (skips the
	 *  reconnect-grace window that `OnLogout` would otherwise apply). The
	 *  client should follow this RPC immediately with `disconnect`. */
	UFUNCTION(Server, Reliable)
	void Server_RequestLeave();

	/** Peer -> coordinator. Owning peer reports its canonical world-state root
	 *  for the given simulation turn. The coordinator collects
	 *  one report per peer per check turn (cadence = settings'
	 *  DeterminismCheckIntervalTurns). When all reports for a turn are in,
	 *  it compares — agreement is silent (Verbose log); divergence fires
	 *  Client_NotifyDesync on every peer so the red on-screen alarm shows
	 *  identical info on all machines. Cheap (16 bytes per peer per N turns),
	 *  off in shipping unless designer explicitly enables. */
	UFUNCTION(Server, Reliable)
	void Server_ReportWorldStateRoot(
		const FSeinProtocolContext& Context,
		int32 Turn,
		FGuid WorldRoot);

	/** Reporter -> coordinator. Reports a terminal determinism-session failure.
	 *  The payload's participant identity is untrusted and is overwritten from
	 *  this relay's authenticated coordinator-side binding. */
	UFUNCTION(Server, Reliable)
	void Server_ReportDeterminismSessionFailure(
		const FSeinProtocolContext& Context,
		const FSeinDeterminismSessionFailure& Failure);

	/** Client -> server. Every joining client independently reports command,
	 *  match-settings, generated simulation-content, and project-config
	 *  compatibility identities. All digest parity is mandatory; config
	 *  enforcement is host policy. */
	UFUNCTION(Server, Reliable)
	void Server_ReportConfigFingerprint(
		const FSeinProtocolContext& Context,
		int32 Fingerprint,
		FGuid CommandProtocolDigest,
		FGuid MatchSettingsDigest,
		FGuid SimulationContentDigest);

	/** Coordinator -> owning peer. Determinism gossip alarm. Fired when peer
	 *  world-state roots diverge for `Turn`. Each client logs at Error
	 *  level + shows a persistent red on-screen debug message listing which
	 *  participant's root diverges from the rest. Includes every peer's root so each
	 *  player can see the full picture, not just "you desynced". */
	UFUNCTION(Client, Reliable)
	void Client_NotifyDesync(
		const FSeinProtocolContext& Context,
		int32 Turn,
		const TArray<FSeinParticipantWorldRootEntry>& PeerRoots);

	/** Coordinator -> owning peer. Canonical root health can no longer be
	 *  proven, so this lockstep epoch is terminal rather than fail-open. */
	UFUNCTION(Client, Reliable)
	void Client_NotifyDeterminismSessionFailure(
		const FSeinProtocolContext& Context,
		const FSeinDeterminismSessionFailure& Failure);

	/** Server -> owning client. Lobby kick notification. The kicked player's
	 *  process disconnects from the listen server and travels to the project's
	 *  configured `USeinARTSCoreSettings::MainMenuMap`. Mirrors the same
	 *  disconnect+travel logic as `SeinRequestLeaveLobby`, so kicked clients
	 *  end up at the menu (not orphaned in the gameplay map's default world).
	 *  `Reason` is logged for debugging; future UI could surface it as a
	 *  toast on the menu. */
	UFUNCTION(Client, Reliable)
	void Client_NotifyKicked(const FString& Reason);

	UFUNCTION()
	void OnRep_AssignedPlayerID();

	/** AssignedPlayerID and SessionSeed are separate replicated properties;
	 *  either RepNotify may run first. Re-notify the subsystem when the seed
	 *  arrives so a deferred start cannot remain blocked forever. */
	UFUNCTION()
	void OnRep_SessionSeed();

	UFUNCTION()
	void OnRep_ProtocolAssignment();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Resolve the owning game-instance subsystem on whichever side this body
	 *  is executing. nullptr only if the GI was torn down ahead of the actor. */
	USeinNetSubsystem* GetNetSubsystem() const;
};
