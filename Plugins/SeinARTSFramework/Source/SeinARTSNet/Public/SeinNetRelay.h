/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetRelay.h
 * @brief   Per-PlayerController relay actor carrying the lockstep wire.
 *
 * One relay is server-spawned for every connected APlayerController and
 * owned by it (so the client legitimately owns the actor it's RPC'ing
 * through). The actor exposes two RPCs:
 *
 *   Server_SubmitCommands    — client -> server. The owning client packs its
 *                              locally captured FSeinCommands for an upcoming
 *                              turn; server forwards to USeinNetSubsystem for
 *                              per-turn aggregation.
 *
 *   Client_ReceiveTurn       — server -> owning client. Once the host has
 *                              gathered every player's submissions for a
 *                              turn, it iterates every relay and unicasts
 *                              the assembled turn back. Reaches every
 *                              connected player (host iterates all relays;
 *                              the host's own relay reaches the host process
 *                              by RPC-loopback).
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
#include "SeinNetRelay.generated.h"

class USeinNetSubsystem;
class USeinLobbySubsystem;

/**
 * Per-slot state-hash entry exchanged for determinism gossip (Phase 4 desync
 * detection). Server collects one of these per peer per check turn; on
 * mismatch the assembled list is fanned back to every peer so the on-screen
 * red alarm can show *who* diverged, not just that something diverged.
 */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinSlotHashEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinPlayerID Slot;

	/** Sim state hash for the slot at the check turn. int32 (not uint32)
	 *  because UHT rejects uint32 on BlueprintType structs; the bit pattern
	 *  is what we compare anyway, signed/unsigned reinterpret is identical. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	int32 Hash = 0;

	FSeinSlotHashEntry() = default;
	FSeinSlotHashEntry(FSeinPlayerID InSlot, int32 InHash) : Slot(InSlot), Hash(InHash) {}
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
 * gossip (each client reports its per-turn state hash; on divergence the server fans the full
 * per-slot hash list back so every machine can show which slot desynced).
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

	/** Session-wide deterministic seed. Same value on every relay (one per PC,
	 *  same value across all). Lockstep-critical: every client's PRNG MUST
	 *  initialize from this exact value before tick 0, or rolls diverge from
	 *  the first frame. */
	UPROPERTY(ReplicatedUsing = OnRep_SessionSeed, BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 SessionSeed = 0;

	/** Client -> server. Owning client packs its commands for `TurnId`; server
	 *  forwards to USeinNetSubsystem for aggregation. Reliable: lockstep cannot
	 *  tolerate dropped commands. The server treats the source relay's
	 *  `AssignedPlayerID` as the authoritative sender (the FSeinCommand's
	 *  PlayerID field is rewritten on the server before fan-out). */
	UFUNCTION(Server, Reliable)
	void Server_SubmitCommands(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Server -> owning client. Delivered after the host has assembled every
	 *  player's commands for `TurnId`. Client hands the unified turn to its
	 *  USeinWorldSubsystem (Phase 2 sim integration). */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Server -> owning client. Lockstep session-start trigger (Phase 2b).
	 *  Server fires this on every connected relay simultaneously once the
	 *  match is ready (e.g. via Sein.Net.StartMatch). Each client starts
	 *  its local sim at tick 0 with the gate already engaged, so no client
	 *  ticks ungated and produces dispatched turns the others can't match. */
	UFUNCTION(Client, Reliable)
	void Client_StartSession();

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

	/** Client -> server. Determinism gossip (Phase 4). Owning client reports
	 *  its computed state hash for the given simulation turn. Server collects
	 *  one report per peer per check turn (cadence = settings'
	 *  DeterminismCheckIntervalTurns). When all reports for a turn are in,
	 *  server compares — agreement is silent (Verbose log); divergence fires
	 *  Client_NotifyDesync on every peer so the red on-screen alarm shows
	 *  identical info on all machines. Cheap (4 bytes per peer per N turns),
	 *  off in shipping unless designer explicitly enables. */
	UFUNCTION(Server, Reliable)
	void Server_ReportStateHash(int32 Turn, int32 Hash);

	/** Client -> server. Every joining client reports its sim-settings
	 *  fingerprint; the host's Check Settings Parity On Join setting decides
	 *  whether to enforce it. This prevents a client's local opt-out from
	 *  bypassing the host's pre-start barrier. int32 carries the CRC bit
	 *  pattern, matching Server_ReportStateHash's UHT-safe representation. */
	UFUNCTION(Server, Reliable)
	void Server_ReportConfigFingerprint(int32 Fingerprint);

	/** Server -> owning client. Determinism gossip alarm. Fired by the server
	 *  when peer state hashes diverge for `Turn`. Each client logs at Error
	 *  level + shows a persistent red on-screen debug message listing which
	 *  slot's hash diverges from the rest. Includes EVERY peer's hash so each
	 *  player can see the full picture, not just "you desynced". */
	UFUNCTION(Client, Reliable)
	void Client_NotifyDesync(int32 Turn, const TArray<FSeinSlotHashEntry>& PeerHashes);

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Resolve the owning game-instance subsystem on whichever side this body
	 *  is executing. nullptr only if the GI was torn down ahead of the actor. */
	USeinNetSubsystem* GetNetSubsystem() const;
};
