/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.h
 * @brief   World subsystem managing the deterministic simulation.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/EntityID.h"
#include "Types/FixedPoint.h"
#include "Types/Random.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/ComponentStorage.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Input/SeinCommand.h"
#include "Events/SeinVisualEvent.h"
#include "Components/SeinContainmentTypes.h"
#include "Components/SeinExtentsComponent.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinVoteState.h"
#include "SeinWorldSubsystem.generated.h"

class ASeinActor;
class USeinFaction;
class USeinAbility;
class USeinCommandBrokerResolver;
class USeinCollisionResolver;
class USeinAIController;
class USeinEffect;
class USeinLatentActionManager;
struct FSeinModifier;

/**
 * Scratch record for an effect apply queued during a tick hook. Drained at the
 * next PreTick via `ProcessPendingEffectApplies` — prevents infinite cascades
 * from `OnApply` / `OnTick` hooks per DESIGN §8 Q9c.
 */
struct FSeinPendingEffectApply
{
	FSeinEntityHandle Target;
	TSubclassOf<USeinEffect> EffectClass;
	FSeinEntityHandle Source;
};

/**
 * Per-entity tag state. Replaces the deleted `FSeinTagData` sim-component
 * struct — tags are now centralized in `USeinWorldSubsystem::EntityTagStates`
 * (a `TMap<FSeinEntityHandle, FSeinEntityTagState>`) rather than sitting in
 * generic component storage. Justification: tags are universal to every
 * entity AND queried constantly (HasTag is on the critical path of ability
 * activation, AI target acquisition, broker dispatch, etc.). A dedicated
 * map keyed by entity handle is faster than a generic component lookup AND
 * removes the "designer might forget to add a TagsComponent" footgun —
 * every entity automatically has a tag state slot at spawn.
 *
 * Authoring surface: `USeinEntityComponent::BaseTags` UPROPERTY on the
 * actor's entity bridge. Spawn flow seeds the entity's BaseTags from that
 * field; runtime mutation goes through `GrantTag`/`UngrantTag`/`AddBaseTag`/
 * `RemoveBaseTag`/`ReplaceBaseTags` on the subsystem.
 *
 * Refcount-based presence tracking: a tag is "present" (appears in
 * `CombinedTags`) iff its refcount is positive. Multiple sources (BaseTags,
 * abilities, effects, components) can grant the same tag independently;
 * the tag only disappears when the last source releases it.
 *
 * Plain C++ struct (no USTRUCT/UPROPERTY) — this isn't sim-component
 * storage, it's subsystem state that's reconstructed from commands on each
 * client, so no reflection-based serialization is required.
 */
struct SEINARTSCOREENTITY_API FSeinEntityTagState
{
	/** Designer-authored tags seeded into refcounts at spawn from
	 *  `USeinEntityComponent::BaseTags`. Runtime-mutable via the subsystem's
	 *  AddBaseTag/RemoveBaseTag/ReplaceBaseTags methods. */
	FGameplayTagContainer BaseTags;

	/** Live refcount per tag. A tag is present (in CombinedTags) iff its
	 *  refcount is > 0. Keys with refcount 0 are not retained. */
	TMap<FGameplayTag, int32> TagRefCounts;

	/** Cache of "present" tags for O(1) HasTag queries. Maintained
	 *  incrementally by GrantTagInternal/UngrantTagInternal; rebuilt from
	 *  TagRefCounts by RebuildCombinedTags if BaseTags is mutated out-of-band. */
	FGameplayTagContainer CombinedTags;

	bool HasTag(const FGameplayTag& Tag) const { return CombinedTags.HasTag(Tag); }
	bool HasAnyTag(const FGameplayTagContainer& Tags) const { return CombinedTags.HasAny(Tags); }
	bool HasAllTags(const FGameplayTagContainer& Tags) const { return CombinedTags.HasAll(Tags); }

	void RebuildCombinedTags();

	/** Increment a tag's refcount. Returns true on the 0→1 edge so the caller
	 *  can add the entity to the global EntityTagIndex bucket. */
	bool GrantTagInternal(const FGameplayTag& Tag);

	/** Decrement a tag's refcount. Returns true on the 1→0 edge so the caller
	 *  can remove the entity from the global EntityTagIndex bucket. No-op
	 *  (returns false) on tags that were never granted or have refcount 0. */
	bool UngrantTagInternal(const FGameplayTag& Tag);
};

/** Broadcast after each sim tick completes (for actor bridge, replay, etc.). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimTickCompleted, int32 /*Tick*/);

/** Broadcast just before commands are processed each tick (for debug logging). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCommandsProcessing, int32 /*Tick*/, const TArray<FSeinCommand>& /*Commands*/);

/** Broadcast immediately after a new entity finishes spawning (handle valid,
 *  components injected, BaseTags seeded, EntitySpawned visual event enqueued).
 *  Subscribers can read the entity's component storage to decide on per-system
 *  registration (e.g. SeinARTSCover's USeinCoverSubsystem registers any entity
 *  with FSeinCoverComponent storage as a cover provider).
 *
 *  Listeners must NOT mutate sim state from this delegate — it fires during
 *  the spawn pipeline. Treat it as a notification, not a hook for sim writes. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntitySpawned, FSeinEntityHandle /*Handle*/);

/** Broadcast immediately before an entity is released back to the pool — after
 *  the EntityDestroyed visual event is enqueued but before component storage
 *  is wiped. Subscribers can read the entity's component storage to decide on
 *  per-system unregistration (the symmetric counterpart to OnEntitySpawned).
 *
 *  Listeners must NOT mutate sim state. Read-only notification. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityDestroyed, FSeinEntityHandle /*Handle*/);

/** One resolved ability slot in a broker dispatch — the tag the resolver picked
 *  plus a debug-friendly display name pulled from `USeinAbility::AbilityName`.
 *  Used by the OnBrokerOrderDispatched delegate to feed the debug command-log
 *  overlay. NOT a sim primitive — purely a debug-side payload, the name field
 *  is FString (not FText) because it crosses a delegate boundary and only ever
 *  feeds a debug HUD. */
struct SEINARTSCOREENTITY_API FSeinBrokerResolvedAbility
{
	FGameplayTag Tag;
	FString DisplayName;  // USeinAbility::AbilityName.ToString() at dispatch time
};

/** Broadcast each time a CommandBroker dispatches an order (immediate or queue-popped),
 *  carrying the deduped unique abilities the resolver picked. Used by the debug
 *  command-log overlay to enrich BrokerOrder entries post-resolution. NOT a sim
 *  primitive — listeners must not mutate sim state. Fires once per dispatch from
 *  `SeinCommandBrokerDispatch::DispatchFrontOrder`, after the resolver returns. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnBrokerOrderDispatched,
	int32 /*Tick*/,
	FSeinPlayerID /*PlayerID*/,
	const TArray<FSeinBrokerResolvedAbility>& /*UniqueResolved*/);

/**
 * Delegate sim uses to ask "is this target nav-reachable for a Move-like ability?"
 * Registered by USeinNavigationSubsystem (SeinARTSNavigation module) at OnWorldBeginPlay
 * so SeinARTSCoreEntity code can consult it without a circular dependency.
 *
 *   From:      entity's current sim position (fixed-point)
 *   To:        target sim position the command asked to activate against
 *   AgentTags: owning entity's tag container (for navlink eligibility filtering)
 *
 * Return true = target is reachable. False = pre-reject with PathUnreachable.
 * Sim skips the gate if no resolver is registered (tests, nav-less games).
 *
 * Parameters are FFixedVector — not FVector — so the query stays bit-identical
 * across clients. Sim callers already carry FFixedVector, and the nav impl's
 * pathability check is fixed-point throughout.
 */
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FSeinPathableTargetResolver,
	const FFixedVector& /*FromWorld*/, const FFixedVector& /*ToWorld*/, const FGameplayTagContainer& /*AgentTags*/);

/**
 * Delegate sim uses to ask "is this target visible for the owner's VisionGroup?"
 * Registered by USeinFogOfWarSubsystem (SeinARTSFogOfWar) at OnWorldBeginPlay so
 * SeinARTSCoreEntity code can consult it without a circular dependency.
 * Returns true = target is visible. False = reject with NoLineOfSight.
 *
 * Target position is FFixedVector (not FVector) to avoid a lossy float round-
 * trip on the determinism-critical query path — sim callers already carry
 * FFixedVector, and the fog impl's cell lookup is fixed-point throughout.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinLineOfSightResolver,
	FSeinPlayerID /*ObserverPlayer*/, const FFixedVector& /*TargetWorld*/);

/**
 * Delegate sim uses to ask "are these footprint cells free for placement?"
 * Registered by USeinNavigationSubsystem (SeinARTSNavigation) at OnWorldBeginPlay
 * so SeinARTSCoreEntity code can consult it without a circular dependency.
 *
 *   CenterWorld:   anchor world position the building footprint will occupy
 *   YawDegrees:    rotation around vertical axis (degrees), applied to the
 *                  shape's local axes — drives Box footprint orientation;
 *                  Capsule shape ignores yaw (rotationally symmetric in XY)
 *   Shape:         the FSeinExtentsShape that will occupy the cells (typically
 *                  pulled from the building Blueprint CDO's USeinExtentsComponent)
 *   AgentLayerMask: nav layer mask used for blocking checks — passes the
 *                  player's "what blocks me" bits so e.g. an amphibious
 *                  building can place over water
 *
 * Return true = all cells covered by Shape at (Center, Yaw) are walkable for
 * the layer mask. False = pre-reject with FootprintBlocked.
 *
 * Sim skips the gate if no resolver is registered (tests, nav-less games);
 * abilities with bRequiresFreeFootprint=true effectively become no-op gates
 * in that case rather than always-rejecting.
 */
DECLARE_DELEGATE_RetVal_FourParams(bool, FSeinFootprintPlacementResolver,
	const FFixedVector& /*CenterWorld*/, const FFixedPoint& /*YawDegrees*/,
	const FSeinExtentsShape& /*Shape*/, uint8 /*AgentLayerMask*/);

/**
 * Delegate sim uses for single-point passability queries — "would a unit be
 * able to occupy this world position?" Registered by USeinNavigationSubsystem
 * (SeinARTSNavigation) at OnWorldBeginPlay so SeinARTSCoreEntity systems
 * (penetration resolution, etc.) can validate proposed positions without a
 * circular dependency.
 *
 * Distinct from FSeinPathableTargetResolver (reachability between two points)
 * and FSeinFootprintPlacementResolver (multi-cell shape placement). This is
 * the per-cell version: cheapest possible check, used by hot-path systems
 * that need to gate proposed positions at scale.
 *
 * Returns true if the cell is walkable. Sim defaults to "permit" if no
 * resolver is registered (tests, nav-less games).
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FSeinPassableResolver,
	const FFixedVector& /*WorldPos*/);

/**
 * Delegate sim uses to snap a world position to the nearest passable cell.
 * Registered by USeinNavigationSubsystem (SeinARTSNavigation) at OnWorldBeginPlay.
 *
 *   InWorld:     position to snap (typically a formation slot the broker
 *                computed in 2D, ignorant of elevation / blocked terrain)
 *   OutPassable: nearest walkable position (output). Unchanged if the input
 *                was already passable (resolver is a no-op in that case).
 *
 * Returns true if a passable cell was found within the nav's scan radius;
 * false if every nearby cell is blocked. Sim defaults to "permit + identity"
 * (`OutPassable = InWorld`, returns true) if no resolver is registered.
 *
 * Used by formation resolvers to ensure slot positions land on walkable
 * terrain — without this, a slot grid spreading off a raised platform
 * places members on impassable cells and they pathfind to weird edges.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinNavProjectResolver,
	const FFixedVector& /*InWorld*/, FFixedVector& /*OutPassable*/);

/**
 * Delegate sim uses to snap a world position to the nearest passable cell that is ALSO free of a set
 * of footprints — the occupancy-aware sibling of FSeinNavProjectResolver. Registered by
 * USeinNavigationSubsystem at OnWorldBeginPlay.
 *
 *   InWorld:      position to snap (a formation slot that overflowed the play area)
 *   SelfRadius:   the snapping slot's footprint radius
 *   AvoidCentres: world positions already claimed by peer slots (index-aligned with AvoidRadii)
 *   AvoidRadii:   peer footprint radii; a candidate cell must clear `SelfRadius + AvoidRadii[j]` of each
 *   OutProjected: nearest walkable + unoccupied position (output)
 *
 * Returns true if a cell was found (falls back to occupancy-blind nearest-walkable rather than fail).
 * Sim defaults to "permit + identity" (`OutProjected = InWorld`, returns true) if no resolver is
 * registered. Used by USeinFormation::ProjectPositionsToNavigable to pack off-nav slots onto the
 * inside edge of the play area without piling them onto each other.
 */
DECLARE_DELEGATE_RetVal_FiveParams(bool, FSeinNavProjectFreeResolver,
	const FFixedVector& /*InWorld*/, FFixedPoint /*SelfRadius*/,
	const TArray<FFixedVector>& /*AvoidCentres*/, const TArray<FFixedPoint>& /*AvoidRadii*/,
	FFixedVector& /*OutProjected*/);

/**
 * Delegate the sim uses to ask whether a world position is an AUTHORITATIVE
 * destination — one that OVERRULES the coarse nav bake (a cover slot). Registered
 * by USeinCoverSubsystem (SeinARTSCover) at world begin-play; unbound when the
 * cover extension is absent (→ no authoritative destinations, default behavior).
 *
 * When true, the path/movement layer delivers the unit to the EXACT position even
 * if its cell is bake-blocked: a cover slot is a valid standing spot, and a
 * blocked ("red") cell under it is a low-resolution false-negative, not a reason
 * to relocate the destination (root CLAUDE.md invariant #6 — the destination is an
 * INPUT, not an opinion nav may move).
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FSeinAuthoritativeDestinationResolver,
	const FFixedVector& /*WorldPos*/);

/**
 * Delegate the destination-preview subsystem uses to fetch an optional per-cell
 * QUALITY tag for each previewed formation position (the preview actor maps tags →
 * decal tints). Bound by an extension — e.g. USeinCoverSubsystem (SeinARTSCover)
 * returns cover quality per cell, FoW-observer-gated. Unbound → no tags (neutral
 * preview). Returns an array parallel to the input positions (or empty). Render-
 * side only (preview), so it is NOT determinism-bound.
 */
DECLARE_DELEGATE_RetVal_OneParam(TArray<FGameplayTag>, FSeinPreviewQualityProvider,
	const TArray<FFixedVector>& /*Positions*/);

/**
 * Delegate sim uses to query ground height at a world position. Registered by
 * USeinNavigationSubsystem (SeinARTSNavigation) at OnWorldBeginPlay.
 *
 * Returns true if a valid height sample exists; false if the position is
 * outside the nav bounds or on an impassable cell (walkable-only gate ON).
 * Sim defaults to "no data" if no resolver is registered — callers must
 * handle the false return.
 *
 * Used by the penetration resolution system's step-height gate to prevent
 * pushes that would teleport entities to a different elevation (wall-top
 * cells that are passable but vertically inaccessible from the side).
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinHeightResolver,
	const FFixedVector& /*WorldPos*/, FFixedPoint& /*OutZ*/);

/**
 * Delegates sim uses to (un)register entities in the spatial tile grid
 * (DESIGN §13 broadphase). Bound by USeinNavigationSubsystem at
 * OnWorldBeginPlay. Invoked by containment (DESIGN §14) when visibility-
 * mode transitions take an entity off the grid (`Hidden`) or put it back on.
 * Unbound delegates are no-ops — tests and nav-less games skip.
 */
DECLARE_DELEGATE_OneParam(FSeinSpatialGridRegister,   FSeinEntityHandle /*Entity*/);
DECLARE_DELEGATE_OneParam(FSeinSpatialGridUnregister, FSeinEntityHandle /*Entity*/);

/**
 * Lockstep gate (Phase 2b). Bound by USeinNetSubsystem when the local slot
 * is assigned. Sim consults at every turn boundary before advancing into the
 * first tick of a new turn — if the network turn for that boundary hasn't
 * been received yet, the sim stalls (the wall-clock accumulator is held;
 * frame retries next pump).
 *
 *   `Turn`     The sim turn we're about to enter (= NextTick / TicksPerTurn).
 *   Returns    true  → assembled turn is in NetSubsystem.ReceivedTurns,
 *                      sim may proceed (Notifier follows to drain it).
 *              false → no data yet, sim stalls.
 *
 * Unbound resolver = no gating (Standalone, Phase 0/2a behavior).
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FSeinTurnReadyResolver, int32 /*Turn*/);

/**
 * Lockstep drain (Phase 2b). Paired with FSeinTurnReadyResolver: once the
 * resolver greenlights a turn, the sim invokes the notifier so NetSubsystem
 * can drain the assembled turn's commands into PendingCommands. Fires once
 * per turn boundary, just before the first tick of the new turn executes.
 *
 * Unbound notifier = nothing to drain (Standalone, single-player).
 */
DECLARE_DELEGATE_OneParam(FSeinTurnConsumeNotifier, int32 /*Turn*/);

/**
 * Lockstep-routing interceptor for `USeinAIController::EmitCommand`.
 * Bound by `USeinNetSubsystem` on the server when networking is active so
 * AI-emitted commands cross the lockstep wire (every peer applies them in
 * the same per-turn order) instead of bypassing the network and only
 * landing in the host's local sim — which would desync immediately.
 *
 * Return `true` to indicate the interceptor handled the command (it's been
 * routed onto the lockstep wire); the AI controller's fallback path skips
 * its own direct enqueue. Return `false` (or leave unbound) to fall through
 * to the legacy direct-enqueue behavior, which is correct for Standalone
 * (no network, no other peers to sync with) and for projects that disable
 * networking in plugin settings.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinAIEmitInterceptor,
	FSeinPlayerID /*OwnedSlot*/, const FSeinCommand& /*Command*/);

/**
 * World subsystem that owns and ticks the deterministic simulation.
 * Manages entity pool, component storage, phase-based tick loop,
 * player states, command processing, and visual event dispatch.
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * GC reference walker. Component payloads live as raw bytes inside
	 * FSeinGenericComponentStorage, so the collector cannot see TObjectPtr /
	 * reflected UObject refs nested in those structs by default. Without this,
	 * ability instances (FSeinAbilityComponent), broker resolvers (FSeinCommandBrokerData),
	 * and any other designer-authored component holding a UObject ref get
	 * garbage-collected mid-play, leaving dangling pointers that crash on tick.
	 */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// ========== Simulation Control ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StopSimulation();

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationRunning() const { return bIsRunning; }

	/** True iff the sim is paused (DESIGN §17). Paused sim halts the tick
	 *  system loop; commands continue to accumulate in the pending buffer
	 *  (Tactical mode — §18 adds a Hard mode that rejects during pause).
	 *  Visual events still flush so UI can respond to scenario-driven pauses. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationPaused() const { return bSimPaused; }

	/** Sim-pause setter. Default behavior queues commands during pause and
	 *  drains them on resume (campaign/single-player friendly). Set
	 *  `bRejectCommandsWhilePaused` to true for competitive vote-pause UX
	 *  where commands during pause are rejected with `Command.Reject.SimPaused`.
	 *  Pass-by-call so designers can pick per-pause without configuration. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void SetSimPaused(bool bPaused, bool bRejectCommandsWhilePaused = false);

	// ========== Match Flow (DESIGN §18) ==========

	/** Current match-state-machine state. `Lobby` on Initialize, transitions
	 *  via `StartMatch` / `EndMatch` / pause / etc. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match")
	ESeinMatchState GetMatchState() const { return MatchState; }

	/** Snapshotted match settings (immutable after StartMatch). Reads return
	 *  the captured value so mid-match mutation is impossible by construction. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match")
	const FSeinMatchSettings& GetMatchSettings() const { return CurrentMatchSettings; }

	/**
	 * Start a match with the given settings. Snapshots settings into the
	 * subsystem, transitions state Lobby → Starting, kicks off the
	 * PreMatchCountdown. Calling from any state other than Lobby / Ended
	 * logs a warning + no-ops.
	 *
	 * On countdown completion the subsystem auto-transitions to Playing and
	 * fires `MatchStarted`. Scenarios / UI subscribe to the visual event
	 * stream for those transitions.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match")
	void StartMatch(const FSeinMatchSettings& Settings);

	/** End the match with a declared winner + reason tag. Transitions
	 *  Playing / Paused → Ending → Ended. Scenarios call this when their
	 *  game-specific victory condition fires (DESIGN §18). `Reason` lives
	 *  under designer-authored `MyGame.Victory.*` by convention. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match")
	void EndMatch(FSeinPlayerID Winner, FGameplayTag Reason);

	// ========== Voting (DESIGN §18) ==========

	/** Open a new vote. No-op (+ warning) if a vote with that tag is already
	 *  active. `ExpiresInTicks <= 0` means no expiration (designer-resolved). */
	void StartVote(FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator);

	/** Register a vote. Re-casts overwrite the prior value. */
	void CastVote(FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue);

	/** Status lookup for UI / scenario gating. */
	ESeinVoteStatus GetVoteStatus(FGameplayTag VoteType) const;

	/** Snapshot of all active votes (for UI listing). */
	TArray<FSeinVoteState> GetActiveVotes() const;

	// ========== CommandBroker helpers (DESIGN §5 — public entry points) ==========

	/** Find or build a broker for a member set + first order. Evicts members
	 *  from any prior broker (one-broker-per-member invariant). Returns the
	 *  broker's entity handle, invalid if post-filter member list is empty.
	 *  Public so framework systems (production rally auto-move, scenario
	 *  orchestration) can dispatch internal broker orders without routing
	 *  through the command buffer. */
	FSeinEntityHandle CreateBrokerForMembers(
		const TArray<FSeinEntityHandle>& FilteredMembers,
		FSeinPlayerID OwnerPlayerID,
		const struct FSeinBrokerQueuedOrder& FirstOrder);

	/** If every member already shares a single broker, return it; else invalid.
	 *  Used to append shift-queued orders to the existing group's broker. */
	FSeinEntityHandle FindSharedBroker(const TArray<FSeinEntityHandle>& Members) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	int32 GetCurrentTick() const { return CurrentTick; }

	/** Fixed delta between sim ticks (seconds). Derived from SimulationTickRate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	float GetFixedDeltaTimeSeconds() const { return FixedDeltaTimeSeconds; }

	/** Interpolation alpha between sim ticks (0-1) for smooth rendering. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	float GetInterpolationAlpha() const;

	// ========== Sim Tick Delegate ==========

	/** Broadcast after each sim tick completes. Used by the actor bridge to sync transforms. */
	FOnSimTickCompleted OnSimTickCompleted;

	/** Fired immediately after a new entity is spawned + initialized. Used by
	 *  optional systems (SeinARTSCover) to discover entities with relevant
	 *  components and self-register them in their per-system registry. See
	 *  FOnEntitySpawned docstring for the "no sim mutation" rule. */
	FOnEntitySpawned OnEntitySpawned;

	/** Fired immediately before an entity is released — symmetric counterpart
	 *  to OnEntitySpawned. Component storage is still readable at this point. */
	FOnEntityDestroyed OnEntityDestroyed;

	/** Broadcast just before ProcessCommands runs, with the pending command buffer. For debug tooling. */
	FOnCommandsProcessing OnCommandsProcessing;

	/** Broadcast each time a broker dispatches an order. For the debug command-log overlay's
	 *  resolved-ability annotation. Listeners are read-only — never mutate sim state from here. */
	FOnBrokerOrderDispatched OnBrokerOrderDispatched;

	/** Cross-module resolver for USeinAbility::bRequiresPathableTarget. Registered
	 *  by USeinNavigationSubsystem at OnWorldBeginPlay. */
	FSeinPathableTargetResolver PathableTargetResolver;

	/** Cross-module resolver for USeinAbility::bRequiresLineOfSight. Registered
	 *  by USeinFogOfWarSubsystem at OnWorldBeginPlay. If unbound, LOS checks permit. */
	FSeinLineOfSightResolver LineOfSightResolver;

	/** Cross-module resolver for USeinAbility::bRequiresFreeFootprint. Registered
	 *  by USeinNavigationSubsystem at OnWorldBeginPlay. If unbound, footprint
	 *  checks permit (tests + nav-less games). */
	FSeinFootprintPlacementResolver FootprintPlacementResolver;

	/** Cross-module resolver for per-cell passability — used by penetration
	 *  resolution to gate symmetric pushes against nav blockers. Registered
	 *  by USeinNavigationSubsystem at OnWorldBeginPlay. If unbound, passability
	 *  checks permit (penetration push runs unconstrained — same as before
	 *  this resolver was introduced). */
	FSeinPassableResolver PassableResolver;

	/** Like PassableResolver, but ALSO rejects cells under runtime dynamic blockers
	 *  (bBlocksNav stamps), not just the static bake. Bound by USeinNavigationSubsystem
	 *  for a default ground agent (nav layer 0x01). Cover-slot selection uses this so
	 *  units are never dispatched onto a cell a dynamically-blocking wall occupies.
	 *  If unbound, defaults to permit (tests / nav-less games). */
	FSeinPassableResolver DynamicPassableResolver;

	/** Cross-module resolver for "snap to nearest passable cell" — used by
	 *  formation resolvers to ensure slot positions land on walkable
	 *  terrain. Registered by USeinNavigationSubsystem at OnWorldBeginPlay.
	 *  If unbound, projection is a no-op (slot stays where it was). */
	FSeinNavProjectResolver NavProjectResolver;

	/** Cross-module resolver for "snap to nearest passable cell that is also FREE of these footprints" —
	 *  the occupancy-aware sibling of NavProjectResolver. Registered by USeinNavigationSubsystem at
	 *  OnWorldBeginPlay. If unbound, projection is a no-op (slot stays where it was). Used by
	 *  USeinFormation::ProjectPositionsToNavigable to clamp off-nav formation slots onto free play-area
	 *  space without piling. */
	FSeinNavProjectFreeResolver NavProjectFreeResolver;

	/** Cross-module resolver: "is this world position an AUTHORITATIVE destination
	 *  (a cover slot) that overrules the coarse nav bake?" Bound by
	 *  USeinCoverSubsystem. Unbound → no authoritative destinations (default: nav
	 *  decides reachability; partial paths stop at the nearest reachable cell). */
	FSeinAuthoritativeDestinationResolver AuthoritativeDestinationResolver;

	/** Cross-module hook: per-cell QUALITY tags for the destination preview (the
	 *  preview actor tints decals by tag). Bound by USeinCoverSubsystem (cover
	 *  quality, FoW-gated). Unbound → neutral preview. Render-side; not sim state. */
	FSeinPreviewQualityProvider PreviewQualityProvider;

	/** Cross-module ground-height resolver — used by penetration resolution
	 *  to gate pushes against step-height violations (wall-top cells
	 *  reachable via walkable-only passability but vertically inaccessible
	 *  from the side). Registered by USeinNavigationSubsystem at
	 *  OnWorldBeginPlay. If unbound, step-height checks are skipped. */
	FSeinHeightResolver HeightResolver;

	/** Cross-module spatial-grid register/unregister callbacks (§13 + §14).
	 *  USeinNavigationSubsystem binds these; containment calls them on
	 *  enter/exit transitions for Hidden-visibility containers. Unbound = no-op. */
	FSeinSpatialGridRegister   SpatialGridRegisterCallback;
	FSeinSpatialGridUnregister SpatialGridUnregisterCallback;

	/** Lockstep gate (Phase 2b). USeinNetSubsystem binds these once the local
	 *  slot is assigned. Sim's TickSimulation consults the resolver at every
	 *  turn boundary; on green, fires the notifier to drain that turn's
	 *  assembled commands into PendingCommands. Unbound = no gating
	 *  (Standalone or networking disabled). */
	FSeinTurnReadyResolver     TurnReadyResolver;
	FSeinTurnConsumeNotifier   TurnConsumeNotifier;

	/** Lockstep-routing interceptor for `USeinAIController::EmitCommand`.
	 *  Bound by `USeinNetSubsystem` on the server when networking is active.
	 *  Routes the command onto the per-turn wire so every peer sees it in
	 *  lockstep order — without this, AI-emitted commands would only run on
	 *  the host's sim and immediately desync from clients on the next state
	 *  hash check. Unbound = direct local enqueue (correct for Standalone). */
	FSeinAIEmitInterceptor     AIEmitInterceptor;

	// ========== Entity Management ==========

	/**
	 * Spawn a new entity from a Blueprint class.
	 * Walks the Blueprint CDO's entity bridge (USeinEntityComponent) ComponentData
	 * array and copies each FInstancedStruct payload into deterministic storage,
	 * then initializes abilities and seeds tags (identity/cost come from the
	 * injected FSeinIdentityComponent / FSeinProducibleComponent payloads).
	 * @param ActorClass - Blueprint class (must be ASeinActor or subclass)
	 * @param SpawnTransform - Initial transform in simulation space
	 * @param OwnerPlayerID - Owning player
	 * @return Handle to the spawned entity
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	FSeinEntityHandle SpawnEntity(TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/**
	 * Spawn a sim entity for an already-existing (level-placed) ASeinActor.
	 * Walks the LIVE actor's entity-bridge ComponentData (USeinEntityComponent,
	 * not the CDO) so per-instance edits in the level are captured into sim
	 * component storage.
	 * Uses the actor's world transform as the sim transform.
	 *
	 * Skips the EntitySpawned visual event — the actor is already in the
	 * world, so we don't want the bridge spawning a duplicate. Caller is
	 * responsible for `RegisterActor + InitializeWithEntity` to link the
	 * existing actor to the new entity.
	 *
	 * Used by USeinActorBridgeSubsystem::OnWorldBeginPlay to auto-register
	 * placed actors. Returns invalid handle if PlacedActor is null.
	 */
	FSeinEntityHandle SpawnEntityFromPlacedActor(ASeinActor* PlacedActor, FSeinPlayerID OwnerPlayerID);

	/**
	 * Spawn an abstract sim entity (no BP class, no render actor). Used for
	 * command brokers, scenario owners, squad containers — anything that needs
	 * pooled handle + component storage but zero render presence. The caller is
	 * responsible for adding whatever sim components it needs via `AddComponent`.
	 * The actor bridge skips handles without a `EntityActorClassMap` entry.
	 */
	FSeinEntityHandle SpawnAbstractEntity(const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/**
	 * Queue entity for deferred destruction (processed in PostTick).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void DestroyEntity(FSeinEntityHandle Handle);

	/** Get entity data by handle. Returns nullptr if handle is stale. */
	FSeinEntity* GetEntity(FSeinEntityHandle Handle);
	const FSeinEntity* GetEntity(FSeinEntityHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool IsEntityAlive(FSeinEntityHandle Handle) const;

	/** Get entity owner. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinPlayerID GetEntityOwner(FSeinEntityHandle Handle) const;

	/** Set entity owner (for capture mechanics). Mutates sim state, so call it
	 *  only from within the sim tick — e.g. a passive ability/effect on a
	 *  capture point. Enforced via SEIN_CHECK_SIM in non-shipping builds. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner);

	/** Get the entity pool (for direct iteration). */
	FSeinEntityPool& GetEntityPool() { return EntityPool; }
	const FSeinEntityPool& GetEntityPool() const { return EntityPool; }

	/** Collision broadphase (two-tier static/dynamic bucket grid). Rebuilt each
	 *  tick by `FSeinCollisionBroadphaseSystem` (PreTick); queried by
	 *  `FSeinCollisionResolutionSystem` (PostTick). Collision-only — navigation
	 *  owns its own (A*-grid) structures and does not use this. */
	FSeinCollisionSpatialHash& GetCollisionSpatialHash() { return CollisionSpatialHash; }
	const FSeinCollisionSpatialHash& GetCollisionSpatialHash() const { return CollisionSpatialHash; }

	/** The active collision resolver. Owns one tick's full collider separation +
	 *  overlap-event emission; the PostTick FSeinCollisionResolutionSystem delegates
	 *  to it. Instantiated from `USeinARTSCoreSettings::CollisionResolverClass` in
	 *  Initialize (defaults to USeinCollisionResolverDefault). Never null after
	 *  Initialize. Mirrors the pluggable Navigation / Fog-of-War seam. */
	USeinCollisionResolver* GetCollisionResolver() const { return CollisionResolver; }

	/** Get the Blueprint actor class stored for an entity (for actor bridge spawning). */
	TSubclassOf<ASeinActor> GetEntityActorClass(FSeinEntityHandle Handle) const;

	/** Get mutable player state by ID. Returns null if not found. C++ only. */
	FSeinPlayerState* GetPlayerStateMutable(FSeinPlayerID PlayerID);

	// ========== Component Management (slot-indexed) ==========
	//
	// Component types are resolved at spawn time by walking the Blueprint CDO's
	// entity bridge (USeinEntityComponent) ComponentData; each FInstancedStruct
	// payload is injected into a reflection-backed FSeinGenericComponentStorage
	// keyed by UScriptStruct. Templated accessors are thin typed wrappers over
	// the raw-bytes path.

	template<typename T>
	void AddComponent(FSeinEntityHandle Handle, const T& Component);

	template<typename T>
	void RemoveComponent(FSeinEntityHandle Handle);

	template<typename T>
	T* GetComponent(FSeinEntityHandle Handle);

	template<typename T>
	const T* GetComponent(FSeinEntityHandle Handle) const;

	template<typename T>
	bool HasComponent(FSeinEntityHandle Handle) const;

	/** Get raw component storage by struct type. */
	ISeinComponentStorage* GetComponentStorageRaw(UScriptStruct* StructType);
	const ISeinComponentStorage* GetComponentStorageRaw(UScriptStruct* StructType) const;

	/** Get or lazily-create the component storage for a struct type. The
	 *  templated `AddComponent<T>` path goes through this; promoted to
	 *  public so non-templated callers (USeinEntityComponent's array-inject
	 *  flow, K2 thunks) can add components keyed on a runtime UScriptStruct*
	 *  without compile-time type info. */
	ISeinComponentStorage* GetOrCreateStorageForType(UScriptStruct* StructType);

	/** Read-only view over every registered storage (UScriptStruct* → storage). Used by
	 *  USeinComponentBPFL for "list every component on entity X" iteration. */
	const TMap<UScriptStruct*, ISeinComponentStorage*>& GetAllComponentStorages() const { return ComponentStorages; }

	// ========== Player & Faction ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/** Get player state by ID. Returns null if not found. C++ only. */
	FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID);
	const FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID) const;

	/** Blueprint-friendly version: returns a copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (DisplayName = "Get Player State"))
	bool GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const;

	/** Iterate every registered player state (mutable). Used by the effect tick
	 *  system to walk per-player effect lists. C++ only. */
	template<typename Func>
	void ForEachPlayerStateMutable(Func&& Callback)
	{
		for (auto& Pair : PlayerStates)
		{
			Callback(Pair.Key, Pair.Value);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterFaction(USeinFaction* Faction);

	/**
	 * Load + register every `USeinFaction` listed in
	 * `USeinARTSCoreSettings::RegisteredFactions`. Called from BOTH server-side
	 * GameMode and client-side MatchBootstrap at world init so each peer's
	 * `Factions` map ends up with bit-identical contents (same set, same
	 * iteration order driven by the settings array).
	 *
	 * Phase 3d: closes the orphaned-`RegisterFaction` footgun. Before this, no
	 * one called RegisterFaction → `Factions` map empty → `RegisterPlayer`'s
	 * ResourceKit lookup silently failed → `StartingResources` GameMode CDO
	 * filled the gap. Benign today; the moment a designer relied on the
	 * faction-driven kit path, server would populate via game-side code while
	 * clients had nothing — silent desync. Settings-driven enumeration makes
	 * the registration deterministic-by-construction.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterFactionsFromSettings();

	/**
	 * Seed the deterministic sim PRNG. Bit-identical input across peers is
	 * a hard requirement for lockstep — every machine MUST call this with
	 * the same value before tick 0 or rolls diverge from the first call.
	 *
	 * Today: not yet wired into the live-session start path
	 * (`USeinNetSubsystem::StartLockstepSession` should call this with
	 *  `SessionSeed`). Replay reader uses it to recreate the original PRNG
	 *  state from the .seinreplay header.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Replay")
	void SeedSimRandom(int64 Seed);

	// ============================================================================
	// World snapshot — capture + restore (Phase 4 architecture)
	// ============================================================================
	//
	// Used by drop-in/drop-out catch-up (snapshot at tick T → catching peer
	// rehydrates), save/load, and the future state-dump-on-desync path.
	// Snapshot is owned by the subsystem; the file/wire serializer wraps it
	// in `FObjectAndNameAsStringProxyArchive` (replay writer pattern).

	/** Capture current sim state into the supplied snapshot. Read-only on
	 *  sim state, but fires `OnCaptureSnapshotPostSim` (non-const broadcast)
	 *  so upstream modules can stamp their own fields — hence the method
	 *  is non-const. */
	void CaptureSnapshot(struct FSeinWorldSnapshot& OutSnapshot);

	/** Restore sim state from the snapshot. Wipes current entity pool /
	 *  component storages / pools / player states first. After restore,
	 *  the sim is paused — caller resumes via `StartSimulation` / sets the
	 *  match state to Playing. Returns false on snapshot version mismatch
	 *  or other unrecoverable validation failure. */
	bool RestoreSnapshot(const struct FSeinWorldSnapshot& InSnapshot);

	/** Fired during CaptureSnapshot AFTER all sim-side state is written, so
	 *  upstream modules (SeinARTSFramework: camera, UI scroll position;
	 *  designer-extended modules: project-specific local UI state) can stamp
	 *  their own fields on the snapshot. Module dependency note: SeinARTSCoreEntity
	 *  can't directly include SeinARTSFramework headers (would close a cycle),
	 *  so this delegate is the inversion point — Framework binds, CoreEntity
	 *  fires. The snapshot struct's `bHasLocalCameraState` + camera fields are
	 *  the well-known slot SeinARTSFramework writes into. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCaptureSnapshot, struct FSeinWorldSnapshot& /*Snapshot*/);
	FOnCaptureSnapshot OnCaptureSnapshotPostSim;

	/** Mirror delegate for restore. Fired AFTER sim state is rehydrated and
	 *  the actor bridge is reconciled, so Framework's camera-restore path
	 *  hits a fully-live world. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRestoreSnapshot, const struct FSeinWorldSnapshot& /*Snapshot*/);
	FOnRestoreSnapshot OnRestoreSnapshotPostSim;

	// ============================================================================
	// Ability + Resolver pools (Phase 4 architecture cleanup)
	// ============================================================================
	//
	// Component data structs reference ability / broker-resolver UObject instances
	// via stable int32 IDs (indices into these pools), not direct TObjectPtr refs.
	// This makes:
	//   - State hashes deterministic across processes (int32 IDs vs pointer values)
	//   - World snapshots portable (IDs survive disk/wire round-trips)
	//   - Components-are-pure-data invariant satisfied (no live UObject refs)
	//
	// Pools own the UObject lifetime via UPROPERTY (rooted on the subsystem).
	// Free-list-recycled IDs keep the index space compact across spawn/despawn.
	// IDs are deterministic by construction: the sim processes commands in a
	// fixed order on every peer → ability creation order matches → pool slot
	// allocation matches.

	/** Register a freshly-NewObject'd ability with the pool. Returns the
	 *  stable int32 ID component data should hold. INDEX_NONE on null input. */
	int32 RegisterAbilityInstance(USeinAbility* Ability);

	/** Release an ability slot back to the free list. Sets pool[ID] to null
	 *  and adds the slot to the free list for reuse. Idempotent on invalid IDs. */
	void UnregisterAbilityInstance(int32 AbilityID);

	/** Pool lookup. Returns null on INDEX_NONE / out-of-range / unregistered. */
	USeinAbility* GetAbilityInstance(int32 AbilityID) const;

	/** Same shape for command-broker resolvers. Each broker spawns one. */
	int32 RegisterCommandBrokerResolver(USeinCommandBrokerResolver* Resolver);
	void UnregisterCommandBrokerResolver(int32 ResolverID);
	USeinCommandBrokerResolver* GetCommandBrokerResolver(int32 ResolverID) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player")
	int32 GetPlayerCount() const { return PlayerStates.Num(); }

	// ========== Tags (refcounted, auto-indexed) ==========
	//
	// All tag mutations route through the subsystem so the global
	// EntityTagIndex stays in sync with per-entity refcounts. Per-entity tag
	// state lives in `EntityTagStates` (a TMap keyed by handle) — there is no
	// FSeinTagData sim component anymore. Designer authoring surface is
	// `USeinEntityComponent::BaseTags`.

	/** True iff the entity currently has the given tag (refcount > 0). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags")
	bool HasTag(FSeinEntityHandle Handle, FGameplayTag Tag) const;

	/** True iff the entity has any of the given tags (refcount > 0 for any). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags")
	bool HasAnyTag(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const;

	/** True iff the entity has every one of the given tags (refcount > 0 for all). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags")
	bool HasAllTags(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const;

	/** Return the entity's CombinedTags (the refcount > 0 projection). Returns
	 *  a const ref to the empty container if the entity has no tag state. */
	const FGameplayTagContainer& GetEntityTags(FSeinEntityHandle Handle) const;

	/** Return the entity's BaseTags (designer-authored set). Returns a const
	 *  ref to the empty container if the entity has no tag state. */
	const FGameplayTagContainer& GetEntityBaseTags(FSeinEntityHandle Handle) const;

	/** Grant a tag (refcount++). Adds to EntityTagIndex on the 0→1 edge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Ungrant a tag (refcount--). Removes from EntityTagIndex on the 1→0 edge.
	 *  Safe to call on tags that were never granted (no-op). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void UngrantTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Add a tag to BaseTags and grant. Returns true if BaseTags changed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	bool AddBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Remove a tag from BaseTags and ungrant. Returns true if BaseTags changed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	bool RemoveBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Replace the entity's BaseTags. Diffs vs the current set: ungrants removed
	 *  tags, grants new ones, leaves unchanged tags alone. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void ReplaceBaseTags(FSeinEntityHandle Handle, const FGameplayTagContainer& NewBaseTags);

	/** Returns a copy of the entity handles currently carrying the tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags")
	TArray<FSeinEntityHandle> GetEntitiesWithTag(FGameplayTag Tag) const;

	/** Pointer to the underlying index bucket (C++ only; nullptr if tag is absent). */
	const TArray<FSeinEntityHandle>* FindEntitiesWithTag(FGameplayTag Tag) const;

	/** Whole-index access (C++ only, for iteration and debugging). */
	const TMap<FGameplayTag, TArray<FSeinEntityHandle>>& GetEntityTagIndex() const { return EntityTagIndex; }

	// ========== Named Entity Registry ==========

	/** Register an entity under a named alias. Overwrites any existing mapping. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void RegisterNamedEntity(FName Name, FSeinEntityHandle Handle);

	/** Look up an entity by its registered name. Returns an invalid handle if unregistered. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinEntityHandle LookupNamedEntity(FName Name) const;

	/** Remove a named alias. No-op if the name was never registered. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void UnregisterNamedEntity(FName Name);

	// ========== Attribute Resolution ==========

	/**
	 * Resolve an entity attribute with all active Instance + Class-scope
	 * modifiers applied. Instance modifiers come from the entity's
	 * `FSeinActiveEffectsComponent`; Class-scope modifiers come from the owner's
	 * `FSeinPlayerState::ClassEffects` (filtered by `TargetClassTag`).
	 * Tech-granted modifiers flow through the same effect pipeline since
	 * Session 2.4 unified tech with effects (DESIGN §10).
	 */
	FFixedPoint ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName);

	/**
	 * Resolve a player-state attribute with all active Player-scope modifiers
	 * applied. Targets fields on `FSeinPlayerState` or designer-authored sub-structs.
	 */
	FFixedPoint ResolvePlayerAttribute(FSeinPlayerID PlayerID, UScriptStruct* StructType, FName FieldName) const;

	// ========== Effects (DESIGN §8) ==========

	/**
	 * Apply an effect to a target entity. If called during a sim tick (from
	 * OnApply/OnTick/OnExpire/OnRemoved), the apply is queued and drained at
	 * the next PreTick. If called outside a sim tick (render-layer authored
	 * ability, test harness), the apply runs immediately.
	 *
	 * Scope from the effect CDO determines where the instance lands:
	 *   Instance → target's FSeinActiveEffectsComponent
	 *   Class    → target owner's FSeinPlayerState::ClassEffects
	 *   Player → target owner's FSeinPlayerState::PlayerEffects
	 *
	 * Returns the assigned effect instance ID (0 if the apply failed or was
	 * deferred — deferred applies don't return a usable ID to the caller).
	 */
	uint32 ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	/** Drain the pending-apply queue. Called at PreTick by FSeinEffectTickSystem. */
	void ProcessPendingEffectApplies();

	/** Remove an Instance-scope effect by instance ID. Returns true if removed. */
	bool RemoveInstanceEffect(FSeinEntityHandle Target, uint32 EffectInstanceID, bool bByExpiration);

	/** Remove all Instance-scope effects matching a tag. */
	void RemoveInstanceEffectsWithTag(FSeinEntityHandle Target, FGameplayTag Tag);

	/** Strip every active effect whose `Source == DeadHandle` and whose CDO declares
	 *  `bRemoveOnSourceDeath = true`. Walks all three scope storages (Instance on
	 *  every entity, Class/Player on every player state). Called by
	 *  ProcessDeferredDestroys before the pool releases the handle. */
	void RemoveEffectsFromDeadSource(FSeinEntityHandle DeadHandle);

	// ========== Relationships (DESIGN §14) ==========

	/**
	 * Move `Entity` into `Container` as a plain occupant.
	 *
	 * Validates: both handles alive; entity has `FSeinContainmentMemberData`;
	 * container has `FSeinContainmentData`; entity not already contained;
	 * accepted-query match; capacity + Size fits. On success: updates
	 * occupant list, bumps `CurrentLoad`, assigns visual slot if tracking
	 * enabled, unregisters from spatial grid if `Visibility == Hidden`,
	 * emits `EntityEnteredContainer` event.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool EnterContainer(FSeinEntityHandle Entity, FSeinEntityHandle Container);

	/**
	 * Remove `Entity` from its current container. `ExitLocation` zero means
	 * the container's transform + `FSeinTransportSpec::DeployOffset` (if any);
	 * a non-zero vector uses that as the exit world position directly.
	 * Writes the entity's new transform, clears container back-ref, decrements
	 * load, re-registers in spatial grid, emits `EntityExitedContainer`.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool ExitContainer(FSeinEntityHandle Entity, FFixedVector ExitLocation = FFixedVector());

	/**
	 * Attach `Entity` to `Container`'s named `SlotTag` slot. Container must
	 * carry both `FSeinContainmentData` and `FSeinAttachmentSpec`; the slot
	 * must exist in the spec's `Slots` array; the slot must be unfilled; and
	 * the slot's own `AcceptedEntityQuery` must match the entity's tags.
	 * Succeeds after a normal containment enter has been performed (attach
	 * implies containment — the entity occupies both slots + occupant list).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool AttachToSlot(FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag);

	/**
	 * Detach `Entity` from its attachment slot (and exit the container). This
	 * is a combined attachment-detach + container-exit; designers who want to
	 * detach without exiting containment handle that via their own BP logic.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool DetachFromSlot(FSeinEntityHandle Entity);

	/**
	 * Propagate container death per DESIGN §14 rules. Called by
	 * `ProcessDeferredDestroys` when a dying entity has `FSeinContainmentData`.
	 *   bEjectOnContainerDeath=true: eject each occupant at container's last
	 *     transform; apply `OnEjectEffect` to each if set; occupants survive.
	 *   bEjectOnContainerDeath=false: apply `OnContainerDeathEffect` to each;
	 *     enqueue each occupant for destroy (recursive — their containments
	 *     propagate too on the next sweep of ProcessDeferredDestroys).
	 */
	void PropagateContainerDeath(FSeinEntityHandle DyingContainer);

	// Introspection — C++ read-only helpers; BPFL wraps.
	FSeinEntityHandle GetImmediateContainer(FSeinEntityHandle Entity) const;
	FSeinEntityHandle GetRootContainer(FSeinEntityHandle Entity) const;
	bool IsContained(FSeinEntityHandle Entity) const;
	TArray<FSeinEntityHandle> GetAllNestedOccupants(FSeinEntityHandle Container) const;
	FSeinContainmentTree BuildContainmentTree(FSeinEntityHandle Container) const;

	// ========== Player Tags (refcounted, DESIGN §10) ==========

	/** Grant a tag to a player (refcount++). Adds to `FSeinPlayerState::PlayerTags`
	 *  on the 0→1 edge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void GrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag);

	/** Ungrant a tag from a player (refcount--). Removes from `PlayerTags` on the
	 *  1→0 edge. Safe to call on tags the player never received (no-op). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void UngrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag);

	// ========== AI (DESIGN §16) ==========

	/**
	 * Register an AI controller on this world's host. Stamps `OwnedPlayerID` +
	 * `WorldSubsystem`, calls `OnRegistered`, and adds to the tick list. Ticks
	 * fire during CommandProcessing phase so AI-emitted commands process
	 * same-tick.
	 *
	 * In multiplayer, only the designated host should call this — other
	 * clients receive the AI's emitted commands via the lockstep buffer.
	 * V1 does not enforce host-only; designer-side gating. Host migration /
	 * dropped-player takeover plumbing lands with §18 match flow.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI")
	void RegisterAIController(USeinAIController* Controller, FSeinPlayerID OwnedPlayer);

	/** Unregister an AI controller (calls `OnUnregistered`). Safe to call on
	 *  a null / already-unregistered controller. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI")
	void UnregisterAIController(USeinAIController* Controller);

	/** Get the registered AI controller driving `PlayerID`, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|AI")
	USeinAIController* GetAIControllerForPlayer(FSeinPlayerID PlayerID) const;

	/** Read-only view over every registered AI controller. */
	const TArray<TObjectPtr<USeinAIController>>& GetAIControllers() const { return AIControllers; }

	// ========== Command System ==========

	/** Enqueue a command for processing next tick. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command")
	void EnqueueCommand(const FSeinCommand& Command);

	/** Get current tick's pending commands (for networking). */
	const FSeinCommandBuffer& GetPendingCommands() const { return PendingCommands; }

	// ========== System Registration ==========

	/** Register a simulation system to be ticked each frame. */
	void RegisterSystem(ISeinSystem* System);

	/** Unregister a system. */
	void UnregisterSystem(ISeinSystem* System);

	// ========== Visual Events ==========

	/** Enqueue a visual event (sim -> render, one-way). */
	void EnqueueVisualEvent(const FSeinVisualEvent& Event);

	/** Flush all visual events (called by render layer). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Visual")
	TArray<FSeinVisualEvent> FlushVisualEvents();

	// ========== Latent Actions ==========

	UPROPERTY()
	TObjectPtr<USeinLatentActionManager> LatentActionManager;

	// ========== Sim PRNG ==========
	//
	// Framework-owned deterministic RNG used by `USeinRandomBPFL` and any other
	// sim-side roll that must be replay-identical. Designers who want their own
	// PRNG stream (e.g., per-weapon) make an FFixedRandom of their own; this
	// one is shared-framework scratch.

	FFixedRandom SimRandom;

	// ========== State Hashing ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Debug")
	int32 ComputeStateHash() const;

private:
	// Entity pool (replaces TMap<FSeinID, FSeinEntity>)
	FSeinEntityPool EntityPool;

	// Collision broadphase — pure C++, rebuilt each tick by
	// FSeinCollisionBroadphaseSystem. Lives next to the entity pool because its
	// lifetime is identical and the collision systems need a stable, world-scoped
	// query surface. Initialized in Initialize(). Collision-only (not navigation).
	FSeinCollisionSpatialHash CollisionSpatialHash;

	// Active collision resolver — owns the per-tick separation + overlap-event
	// logic the PostTick FSeinCollisionResolutionSystem delegates to. Instantiated
	// in Initialize() from USeinARTSCoreSettings::CollisionResolverClass (falls back
	// to USeinCollisionResolverDefault on empty/abstract/stale). UPROPERTY so the
	// resolver is GC-rooted by the subsystem. Pluggable seam — mirrors NavigationClass.
	UPROPERTY(Transient)
	TObjectPtr<USeinCollisionResolver> CollisionResolver;

	// Component storage registry (slot-indexed, keyed by UScriptStruct*)
	TMap<UScriptStruct*, ISeinComponentStorage*> ComponentStorages;

	// Player states
	TMap<FSeinPlayerID, FSeinPlayerState> PlayerStates;

	// Factions
	TMap<FSeinFactionID, TObjectPtr<USeinFaction>> Factions;

	// Registered systems sorted by phase then priority
	TArray<ISeinSystem*> Systems;
	TArray<ISeinSystem*> BuiltInSystems; // Owned by this subsystem, deleted on deinit
	bool bSystemsSorted = false;

	// Command buffer
	FSeinCommandBuffer PendingCommands;

	// Visual event queue
	FSeinVisualEventQueue VisualEventQueue;

	// Deferred destruction list
	TArray<FSeinEntityHandle> PendingDestroy;

	// Entity → Blueprint class map (for actor bridge spawning)
	TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>> EntityActorClassMap;

	// Per-entity tag state. Replaces the old FSeinTagData sim-component
	// storage — see FSeinEntityTagState doc. Seeded at spawn from the
	// entity bridge's BaseTags + any explicit additions; mutated at runtime
	// via the public GrantTag/UngrantTag/AddBaseTag/RemoveBaseTag methods.
	// Destroy paths clear the entity's entry along with the EntityTagIndex
	// buckets.
	TMap<FSeinEntityHandle, FSeinEntityTagState> EntityTagStates;

	// Global tag → entity index. Maintained by Grant/UngrantTag on 0↔1 refcount
	// transitions. Destroy paths call UnindexEntityTags to clear a handle's
	// buckets before its tag state is freed.
	TMap<FGameplayTag, TArray<FSeinEntityHandle>> EntityTagIndex;

	// Named entity registry (designer-addressable aliases).
	TMap<FName, FSeinEntityHandle> NamedEntityRegistry;

	// Pending-apply queue for effects applied during tick hooks. Drained at PreTick.
	TArray<FSeinPendingEffectApply> PendingEffectApplies;

	// AI controller registry (DESIGN §16). Ticked in CommandProcessing phase.
	UPROPERTY()
	TArray<TObjectPtr<USeinAIController>> AIControllers;

	// ============================================================================
	// Ability + Resolver pools storage (Phase 4 architecture)
	// ============================================================================
	// Pool slots own the UObject lifetime via UPROPERTY. Indices are stable IDs
	// component data holds. Free-list-recycled to keep the index space compact.

	UPROPERTY()
	TArray<TObjectPtr<USeinAbility>> AbilityPool;
	TArray<int32> AbilityPoolFreeList;

	UPROPERTY()
	TArray<TObjectPtr<USeinCommandBrokerResolver>> CommandBrokerResolverPool;
	TArray<int32> CommandBrokerResolverPoolFreeList;

	// Tick the registered AI controllers. Called from TickSystems at
	// CommandProcessing phase, right before ProcessCommands.
	void TickAIControllers(FFixedPoint DeltaTime);

	// Commit an apply synchronously (used by ApplyEffect when not in a tick, and
	// by ProcessPendingEffectApplies when draining). Returns instance ID or 0.
	uint32 ApplyEffectInternal(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	// Simulation state
	bool bIsRunning = false;
	bool bSimPaused = false;     // DESIGN §17 — scenario-driven pause gate
	bool bSimPausedHard = false; // DESIGN §18 — true = reject sim-mutating commands while paused
	int32 CurrentTick = 0;
	FTSTicker::FDelegateHandle TickerHandle;

	// Match state (DESIGN §18). State machine drives pre-match countdown, end-match
	// cleanup, spectator + pause filters in ProcessCommands.
	ESeinMatchState MatchState = ESeinMatchState::Lobby;
	FSeinMatchSettings CurrentMatchSettings;
	int32 StartingStateDeadlineTick = 0;   // tick at which Starting → Playing
	int32 MatchStartTick = 0;              // tick at which Playing was entered
	// Advance match state each tick (poll Starting countdown → Playing transition).
	void TickMatchState();

	// Votes (DESIGN §18). Keyed by VoteType. Drained on resolution.
	UPROPERTY()
	TMap<FGameplayTag, FSeinVoteState> ActiveVotes;
	// Tick votes — resolves expired ones at the head of each sim tick.
	void TickVotes();
	// Evaluate a single vote's pass/fail condition and resolve if satisfied.
	// Returns true if the vote was resolved (removed).
	bool EvaluateAndResolveVote(FGameplayTag VoteType);

	// Wall-clock scheduling (NOT sim state — do not "fix" these to FFixedPoint).
	// TimeAccumulator is the render-frame wall-clock budget waiting to be drained
	// into sim ticks; FixedDeltaTimeSeconds is the wall-clock cadence of one tick.
	// The delta actually fed into the sim each tick is a deterministic FFixedPoint
	// derived from SimulationTickRate (see TickSimulation). Clients may drift
	// apart on wall clock between ticks but remain bit-identical at any given
	// tick N, which is what determinism/lockstep requires.
	float TimeAccumulator = 0.0f;
	float FixedDeltaTimeSeconds = 1.0f / 30.0f;

	// Persistence tracking for the "Simulation falling behind" log: most
	// clamps are transient single-frame hitches and don't warrant a Warning.
	// Only escalate to Warning when clamping has been CONTINUOUS for ≥1s.
	double ClampWindowStartTime = 0.0;
	double LastClampTime = 0.0;
	double LastClampWarnTime = 0.0;
	bool bClampWarningEmitted = false;

	// Tick pipeline
	bool TickSimulation(float DeltaTime);
	void TickSystems(FFixedPoint DeltaTime);
	void ProcessCommands();
	void ProcessDeferredDestroys();
	void SortSystemsIfNeeded();

	// Ability initialization for spawned entities
	void InitializeEntityAbilities(FSeinEntityHandle Handle);

	// Replay active player-scope effects' GrantedAbilities for a freshly-
	// spawned entity. Runs AFTER tag seeding (relies on entity tag state
	// to test AbilityTargetClassTag matches). See implementation comment
	// for the timing rationale.
	void ReplayEffectAbilityGrants(FSeinEntityHandle Handle);

	// Seed an entity's tag refcounts + EntityTagIndex from its BaseTags (at spawn).
	void SeedEntityTagsFromBase(FSeinEntityHandle Handle);

	// Strip a handle from every EntityTagIndex bucket it appears in (at destroy).
	void UnindexEntityTags(FSeinEntityHandle Handle);

	// Drop any named-registry entries that reference the given handle (at destroy).
	void UnregisterHandleFromNames(FSeinEntityHandle Handle);
};

// ==================== Template Implementations ====================

template<typename T>
void USeinWorldSubsystem::AddComponent(FSeinEntityHandle Handle, const T& Component)
{
	ISeinComponentStorage* Storage = GetOrCreateStorageForType(T::StaticStruct());
	if (Storage)
	{
		Storage->AddComponent(Handle, &Component);
	}
}

template<typename T>
void USeinWorldSubsystem::RemoveComponent(FSeinEntityHandle Handle)
{
	if (ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct()))
	{
		Storage->RemoveComponent(Handle);
	}
}

template<typename T>
T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle)
{
	ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
const T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<const T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
bool USeinWorldSubsystem::HasComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage && Storage->HasComponent(Handle);
}
