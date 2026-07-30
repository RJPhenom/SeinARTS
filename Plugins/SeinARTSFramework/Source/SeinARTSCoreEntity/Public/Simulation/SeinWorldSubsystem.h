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
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinCanonicalStateValueStore.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandSchemaRegistry.h"
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
class USeinCommandAuthorityPolicy;
class USeinCommandAuthorityView;
class USeinBuiltInCommandHandler;
class USeinARTSCoreSettings;
class USeinReplayReader;
class USeinNetSubsystem;
class FSeinProductionSystem;
struct FSeinActiveEffect;
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
	 * storage. Checkpoints encode it explicitly in canonical records rather
	 * than relying on reflection.
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

/** Broadcast immediately before component storage is wiped and the entity is
 *  released back to the pool. Subscribers can inspect the exact dying handle through
 *  GetDestroyingEntity, GetDestroyingEntityOwner, and
 *  GetDestroyingComponent<T> (the symmetric counterpart to OnEntitySpawned).
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
 * When bound, the interceptor owns the routing decision: return `true` when
 * accepted and `false` when rejected/dropped. The AI controller never bypasses
 * a bound topology adapter by falling back to direct enqueue. Only an unbound
 * interceptor uses the standalone direct-enqueue path.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinAIEmitInterceptor,
	FSeinPlayerID /*OwnedSlot*/, const FSeinCommand& /*Command*/);

/**
 * Topology adapter for locally authored command drafts. When bound, the active
 * transport authenticates its local participant and ignores all draft provenance.
 * The boolean requests the participant's independently granted match-admin
 * capability; it never derives from coordinator/relay ownership.
 */
DECLARE_DELEGATE_TwoParams(FSeinLocalCommandSubmitter,
	const FSeinCommand& /*Draft*/, bool /*bRequestMatchAdministration*/);

/** Resolve the exact next canonical command frame while ordinary sim time is frozen. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FSeinPauseControlFrameResolver,
	FSeinPauseControlFrame& /*OutFrame*/);

/**
 * Report an accepted or protocol-invalid frozen frame to the active topology adapter.
 * A protocol-successful frame carries its exact post-frame canonical world root.
 * Capture refusal leaves the digest invalid without reclassifying the already-consumed
 * frame as a protocol failure; adapters must fail closed on either condition.
 */
DECLARE_DELEGATE_FourParams(FSeinPauseControlAppliedNotifier,
	const FSeinPauseControlCursor& /*Cursor*/,
	bool /*bStillPaused*/,
	const FGuid& /*CanonicalStateDigest*/,
	bool /*bProtocolFailure*/);

/**
 * World-scoped local materialization provider. Framework or a custom gameplay
 * shell binds exactly one implementation; topology adapters call the Core
 * `EnsureMatchBootstrapLocallyReady` facade rather than depending on it.
 * Core opens the transaction and materializes all registered canonical-state
 * recipes first. The provider authors its topology-neutral native state and
 * closes the transaction through SealLocalMatchBootstrap.
 */
DECLARE_DELEGATE_RetVal_FourParams(bool, FSeinMatchBootstrapMaterializer,
	const FSeinMatchSettings& /*Settings*/,
	const FGuid& /*AuthorizationContextDigest*/,
	FSeinMatchBootstrapReceipt& /*OutReceipt*/,
	FString& /*OutError*/);

/** Downstream gameplay-shell hook for the intentionally exposed standalone
 *  Blueprint launch node. The binder retains and presents its private native
 *  bootstrap authority capability. */
DECLARE_DELEGATE_RetVal(bool, FSeinStandaloneBootstrapLauncher);

/**
 * One-shot lifecycle notification for the world-scoped materialization
 * transaction. Success means the exact sealed receipt was authorized; false
 * means bootstrap entered its terminal Failed state.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnSeinMatchBootstrapClosed,
	bool /*bAuthorized*/);

/** A frozen execution contract changed while the world was still live. */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnSeinExecutionTopologyInvalidated,
	const FString& /*Reason*/);

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
	virtual void PreDeinitialize() override;
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

	/**
	 * Claim this world's exclusive native bootstrap control plane. The stable ID
	 * is diagnostic identity, while the concrete UObject owner prevents another
	 * adapter from spoofing that ID. A live same-owner retry reissues the exact
	 * capability; every different-owner claim fails closed.
	 */
	bool ClaimMatchBootstrapAuthority(
		FName StableAuthorityID,
		const UObject* AuthorityOwner,
		FSeinMatchBootstrapAuthorityHandle& OutHandle,
		FString& OutError);

	/**
	 * Seal local materialization. Core computes the initial-state digest itself;
	 * callers may provide only the canonical plan identity. Provider-only under
	 * the same lexical capability established by EnsureMatchBootstrapLocallyReady.
	 */
	bool SealLocalMatchBootstrap(
		const FGuid& PlanDigest,
		FSeinMatchBootstrapReceipt& OutReceipt,
		FString& OutError);

	/** Accept topology authorization only for this world's exact receipt/context. */
	bool AuthorizeMatchBootstrap(
		const FSeinMatchBootstrapAuthorityHandle& Authority,
		const FSeinMatchBootstrapReceipt& Receipt,
		const FGuid& AuthorizationContextDigest,
		FString& OutError);

	/** Terminally fail an unconsumed bootstrap under the exclusive authority. */
	bool FailMatchBootstrap(
		const FSeinMatchBootstrapAuthorityHandle& Authority,
		const FString& Reason,
		FString& OutError);

	/**
	 * Ask the bound gameplay-shell materializer to establish at least local
	 * readiness, or return the already-sealed receipt for an identical request.
	 */
	bool EnsureMatchBootstrapLocallyReady(
		const FSeinMatchBootstrapAuthorityHandle& Authority,
		const FSeinMatchSettings& Settings,
		const FGuid& AuthorizationContextDigest,
		FSeinMatchBootstrapReceipt& OutReceipt,
		FString& OutError);

	/**
	 * Canonical tick-zero receipt hook. Its implementation is intentionally
	 * separate from the legacy diagnostic StateHash and snapshot serializer.
	 */
	bool ComputeCanonicalInitialStateDigest(
		FGuid& OutDigest,
		FString& OutError) const;

	/**
	 * Add receipt-only deterministic evidence to the tick-zero root. The value
	 * is copied and digested immediately; it is not a persistent/queryable state
	 * slot and checkpoints retain only its digest. Use a canonical-state recipe
	 * plus Set/Get State Value for persistent Blueprint state. Applying only.
	 */
	bool RegisterCanonicalBootstrapEvidenceValue(
		FName StableContributorID,
		uint32 SchemaVersion,
		const FInstancedStruct& Value,
		FString& OutError);

	/** Transactionally replace one registered value under normal sim authority. */
	bool SetCanonicalStateValue(
		const FSeinCanonicalStateKey& Key,
		const FInstancedStruct& Value,
		FString& OutError);

	/** Copy one designer state value without exposing mutable storage. */
	bool GetCanonicalStateValue(
		const FSeinCanonicalStateKey& Key,
		FInstancedStruct& OutValue) const;

	/** Frozen native/Blueprint state plus execution-topology contract identity. */
	FGuid GetCanonicalStateContractDigest() const
	{
		return CanonicalStateValues.GetContractDigest();
	}

	/**
	 * Whether this world's frozen native schema contains the exact contributor
	 * key with the required role. Invalid schemas or keys return false.
	 */
	bool HasFrozenCanonicalStateContributor(
		const FSeinCanonicalStateKey& Key,
		ESeinCanonicalStateRole RequiredRole) const;

	/** True once the per-world deterministic system contract is immutable. */
	bool IsExecutionTopologyFrozen() const
	{
		return bExecutionTopologyFrozen;
	}

	/** False after invalid participation or any live post-freeze mutation. */
	bool IsExecutionTopologyValid() const
	{
		return bExecutionTopologyValid;
	}

	/** Canonical execution-contract identity. Invalid until a successful freeze. */
	FGuid GetExecutionTopologyDigest() const
	{
		return ExecutionTopologyDigest;
	}

	const FString& GetExecutionTopologyManifest() const
	{
		return ExecutionTopologyManifest;
	}

	const FString& GetExecutionTopologyFailureReason() const
	{
		return ExecutionTopologyFailureReason;
	}

	/**
	 * Terminally release all module-owned live-world state before a
	 * deterministic implementation DLL unloads.
	 *
	 * The topology failure is broadcast first so coordinators can close the
	 * match. Core then stops scheduling, releases every Core-held UObject root,
	 * reflected payload, and callback, and synchronously destroys raw component
	 * storage/system state while the withdrawing DLL is still callable. The
	 * world cannot restart.
	 *
	 * This makes foreign implementation-module unload safe from Core-held roots;
	 * unloading SeinARTSCoreEntity itself still requires world/GameInstance
	 * teardown because this subsystem's own class and vtable live there.
	 * Idempotent, stable-boundary, and game-thread only.
	 */
	void TerminateAndReleaseForModuleUnload(
		FName OwnerModuleId,
		const FString& Detail);

	/** True once this live world has entered its irreversible unload terminal. */
	bool IsTerminalAfterModuleUnload() const
	{
		return bModuleUnloadStateReleased;
	}

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match|Bootstrap")
	ESeinMatchBootstrapState GetMatchBootstrapState() const
	{
		return MatchBootstrapState;
	}

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match|Bootstrap")
	FSeinMatchBootstrapReceipt GetMatchBootstrapReceipt() const
	{
		return MatchBootstrapReceipt;
	}

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match|Bootstrap")
	FString GetMatchBootstrapFailureReason() const
	{
		return MatchBootstrapFailureReason;
	}

	/** Retained native-only topology/session identity for replay restore checks. */
	FGuid GetMatchBootstrapAuthorizationContextDigest() const
	{
		return MatchBootstrapAuthorizationContextDigest;
	}

	/** Single local materialization provider; cleared during world teardown. */
	FSeinMatchBootstrapMaterializer MatchBootstrapMaterializer;

	/** Single standalone launcher; Core never reconstructs downstream authority. */
	FSeinStandaloneBootstrapLauncher StandaloneBootstrapLauncher;

	/** Native-only, one-shot Authorized/Failed transaction-close signal.
	 *  Listeners are read-only observers and cannot mutate or enqueue work. */
	FOnSeinMatchBootstrapClosed OnMatchBootstrapClosed;

	/** Native adapter seam for a live-world execution-contract failure. */
	FOnSeinExecutionTopologyInvalidated OnExecutionTopologyInvalidated;

	/**
	 * Fail-stop a world after a deterministic extension detects that its
	 * frozen StateContract can no longer describe future execution. This is
	 * the module-facing counterpart to registration/unload invalidation:
	 * before launch it fails bootstrap; after launch it stops the scheduler
	 * and broadcasts OnExecutionTopologyInvalidated.
	 */
	void InvalidateDeterministicExecutionContract(
		const FString& Reason);

	/** Consume an Authorized bootstrap and launch tick zero. Exact exclusive
	 *  authority is required; successful retries by the same holder are safe. */
	bool LaunchAuthorizedMatchBootstrap(
		const FSeinMatchBootstrapAuthorityHandle& Authority,
		FString& OutError);

	/** Native resume entry point for an already-Consumed match. It deliberately
	 *  refuses the first Authorized launch. */
	bool StartSimulation();

	/** Native topology-adapter and teardown entry point. A RemainStopped
	 *  snapshot adoption holds a dormant scheduler reservation for infallible
	 *  later activation; calling StopSimulation again explicitly abandons that
	 *  reservation. */
	void StopSimulation();

	/** Functional all-build gate for direct deterministic-state writes. It
	 *  permits stopped tick-zero Applying materialization, a running fixed-tick
	 *  sim context, or Core's private validated snapshot-restore scope. Native
	 *  only: Blueprint mutation libraries call this before touching raw state. */
	bool RequireStateMutationAuthorization(const TCHAR* Operation) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationRunning() const { return bIsRunning; }

	/** True iff ordinary sim time is frozen. Systems, ticks, votes, latent work,
	 *  and ordinary commands stop; only canonical FrozenPauseControl frames may
	 *  dispatch through the separate pause lane. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationPaused() const { return bSimPaused; }

	/** Sim-pause setter. Soft pause keeps ordinary commands queued for the first
	 *  resumed tick; hard pause rejects them. Frozen control commands use their
	 *  own canonical frame sequence in both modes. */
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

	/** Validate bootstrap match settings through the same frozen command schema
	 *  and built-in semantic contract used by materialization. */
	bool ValidateMatchSettings(
		const FSeinMatchSettings& Settings,
		FGameplayTag& OutRejectionReason) const;

	/**
	 * Native bootstrap-only installation of the immutable match settings.
	 * Legal exactly while the one-shot barrier is Applying and the match is in
	 * Lobby; transitions Lobby → Starting. A new match requires a new world.
	 */
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

	/** Broadcast after each sim tick completes. Used by presentation and
	 *  lifecycle observers; listeners cannot mutate sim state or enqueue work. */
	FOnSimTickCompleted OnSimTickCompleted;

	/** Fired immediately after a new entity is spawned + initialized. Used by
	 *  optional systems (SeinARTSCover) to discover entities with relevant
	 *  components and self-register them in their per-system registry. See
	 *  FOnEntitySpawned docstring for the "no sim mutation" rule. */
	FOnEntitySpawned OnEntitySpawned;

	/** Fired immediately before an entity is released — symmetric counterpart
	 *  to OnEntitySpawned. Exact entity, owner, and component payload data are
	 *  available read-only through the GetDestroying* accessors only during
	 *  this callback. */
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

	/**
	 * Install the active topology adapter's AI route. The delegate remains
	 * opaque after installation: only the world's exact, lexically active AI
	 * tick may execute it. This keeps custom client/server or P2P adapters
	 * pluggable without exposing an out-of-band command-ingress call.
	 */
	void SetAIEmitInterceptor(FSeinAIEmitInterceptor&& Interceptor);
	void ClearAIEmitInterceptor();
	bool HasAIEmitInterceptor() const;

	/** Install the ordinary local-draft route for the active topology adapter.
	 *  Calls still enter through SubmitLocalCommandDraft, which owns lifecycle
	 *  and observer authorization. */
	void SetLocalCommandSubmitter(FSeinLocalCommandSubmitter&& Submitter);
	void ClearLocalCommandSubmitter();
	bool HasLocalCommandSubmitter() const;

	/**
	 * Topology-neutral frozen-time frame seam. Network adapters resolve an exact
	 * canonical frame and receive its deterministic application result. When the
	 * resolver is unbound, standalone drafts use the same frame executor locally.
	 */
	FSeinPauseControlFrameResolver PauseControlFrameResolver;
	FSeinPauseControlAppliedNotifier PauseControlAppliedNotifier;

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
	const FSeinEntity* GetEntity(FSeinEntityHandle Handle) const;

	/**
	 * Explicit mutable entity access for deterministic implementation code.
	 * Returns nullptr from read-only/observer callbacks. This accessor does not
	 * itself grant simulation mutation authority; callers that initiate a write
	 * must still enter through an authorized mutation front door.
	 */
	FSeinEntity* GetEntityMutable(FSeinEntityHandle Handle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool IsEntityAlive(FSeinEntityHandle Handle) const;

	/** Get entity owner. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinPlayerID GetEntityOwner(FSeinEntityHandle Handle) const;

	/** Set entity owner (for capture mechanics). The central mutation gate
	 *  permits an exact bootstrap materializer, this world's deterministic tick,
	 *  or validated restore; presentation and observer callbacks fail closed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner);

	/** Read-only entity-pool view (for direct canonical iteration/query). */
	const FSeinEntityPool& GetEntityPool() const { return EntityPool; }

	/**
	 * Explicit mutable entity-pool access for deterministic systems. Returns
	 * nullptr from read-only/observer callbacks; no fallback pool is exposed.
	 */
	FSeinEntityPool* GetEntityPoolMutable();

	/** Collision broadphase (two-tier static/dynamic bucket grid). Rebuilt each
	 *  tick by `FSeinCollisionBroadphaseSystem` (PreTick); queried by
	 *  `FSeinCollisionResolutionSystem` (PostTick). Collision-only — navigation
	 *  owns its own (A*-grid) structures and does not use this. */
	const FSeinCollisionSpatialHash& GetCollisionSpatialHash() const { return CollisionSpatialHash; }

	/**
	 * Explicit mutable broadphase access for its owning deterministic system.
	 * Returns nullptr from read-only/observer callbacks.
	 */
	FSeinCollisionSpatialHash* GetCollisionSpatialHashMutable();

	/** The active collision resolver. Owns one tick's full collider separation +
	 *  overlap-event emission; the PostTick FSeinCollisionResolutionSystem delegates
	 *  to it. Instantiated from `USeinARTSCoreSettings::CollisionResolverClass` in
	 *  Initialize (defaults to USeinCollisionResolverDefault). Null when that setting is
	 *  None (collision off, WYSIWYG); otherwise valid after Initialize — the delegator
	 *  system null-guards. Mirrors the pluggable Navigation / Fog-of-War seam. */
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
	const T* GetComponent(FSeinEntityHandle Handle) const;

	/**
	 * Explicit mutable component access for deterministic implementation code.
	 * Returns nullptr from read-only/observer callbacks. This accessor does not
	 * itself grant simulation mutation authority.
	 */
	template<typename T>
	T* GetComponentMutable(FSeinEntityHandle Handle);

	template<typename T>
	bool HasComponent(FSeinEntityHandle Handle) const;

	/**
	 * Read a component from the exact tombstone currently being announced by
	 * OnEntityDestroyed. Returns nullptr at every other time and never exposes a
	 * mutable payload. Ordinary GetComponent remains strictly live-entity-only.
	 */
	template<typename T>
	const T* GetDestroyingComponent(FSeinEntityHandle Handle) const;

	/** Read the exact dying entity/owner only during its destroy notification. */
	const FSeinEntity* GetDestroyingEntity(FSeinEntityHandle Handle) const;
	FSeinPlayerID GetDestroyingEntityOwner(FSeinEntityHandle Handle) const;

	/** Get a read-only raw component storage by struct type. */
	const ISeinComponentStorage* GetComponentStorageRaw(UScriptStruct* StructType) const;

	/**
	 * Explicit mutable raw-storage access for deterministic implementation
	 * code. Returns nullptr from read-only/observer callbacks.
	 */
	ISeinComponentStorage* GetComponentStorageMutable(
		UScriptStruct* StructType);

	/** Get or lazily-create the component storage for a struct type. The
	 *  templated `AddComponent<T>` path goes through this; promoted to
	 *  public so non-templated callers (USeinEntityComponent's array-inject
	 *  flow, K2 thunks) can add components keyed on a runtime UScriptStruct*
	 *  without compile-time type info. */
	ISeinComponentStorage* GetOrCreateStorageForType(UScriptStruct* StructType);

	/**
	 * Copy registered component types without exposing mutable storage
	 * pointers. Ordering is unspecified; callers that execute deterministically
	 * must sort by a stable struct identity before iteration.
	 */
	TArray<UScriptStruct*> GetComponentStorageTypes() const;

	/** Number of registered component-storage types. */
	int32 GetComponentStorageCount() const
	{
		return ComponentStorages.Num();
	}

	// ========== Player & Faction ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/** Get player state by ID. Returns null if not found. C++ only. */
	const FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID) const;

	/** Blueprint-friendly version: returns a copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (DisplayName = "Get Player State"))
	bool GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const;

	/** Registered player IDs in canonical ascending order. The returned array is
	 *  a snapshot: players registered by a callback are first visible next pass. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (DisplayName = "Get Registered Player IDs"))
	TArray<FSeinPlayerID> GetRegisteredPlayerIDs() const;

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterFaction(USeinFaction* Faction);

	/**
	 * Load + register every `USeinFaction` listed in
	 * `USeinARTSCoreSettings::RegisteredFactions`. Called by the world-scoped
	 * materializer while bootstrap is Applying so every peer has the same
	 * faction registry before player registration.
	 *
	 * Settings-driven canonical enumeration closes the orphaned
	 * `RegisterFaction` footgun and makes ResourceKit installation identical
	 * across topology adapters.
	 */
	void RegisterFactionsFromSettings();

	/**
	 * Seed the deterministic sim PRNG. Bit-identical input across peers is
	 * a hard requirement for lockstep — every machine MUST call this with
	 * the same value before tick 0 or rolls diverge from the first call.
	 *
	 * The exclusive bootstrap authority installs this once while the world is
	 * Awaiting, immediately before `EnsureMatchBootstrapLocallyReady`. An
	 * identical retry by that authority is a no-op; a foreign capability,
	 * conflicting value, or any post-Begin call is rejected.
	 */
	bool SeedSimRandom(
		const FSeinMatchBootstrapAuthorityHandle& Authority,
		int64 Seed,
		FString& OutError);

	// ============================================================================
	// World snapshot — capture + restore
	// ============================================================================
	//
	// Used by drop-in/drop-out catch-up (snapshot at tick T → catching peer
	// rehydrates), save/load, and the future state-dump-on-desync path.
	// Snapshot is owned by the subsystem. The current raw
	// FObjectAndNameAsStringProxyArchive wrapper is trusted local developer
	// tooling only; production network, campaign, cloud-save, and replay
	// adapters require a bounded authenticated outer envelope.

	/**
	 * Claim the one-shot native capability required to adopt an authoritative
	 * snapshot. The caller must first authenticate and authorize the complete
	 * outer envelope according to its own topology/session policy. A live
	 * same-owner retry reissues the exact capability; a different owner fails
	 * closed. OutHandle must be invalid and is never mutated on failure. The
	 * capability is consumed by the next exact restore attempt. This is
	 * procedural authorization between trusted native modules, not
	 * cryptographic authentication or proof bound to an artifact digest.
	 */
	bool ClaimSnapshotRestoreAuthority(
		FName StableAuthorityID,
		const UObject* AuthorityOwner,
		FSeinSnapshotRestoreAuthorityHandle& OutHandle,
		FString& OutError);

	/** Explicitly abandon an unused exact restore capability. */
	bool ReleaseSnapshotRestoreAuthority(
		FSeinSnapshotRestoreAuthorityHandle&& Authority,
		FString& OutError);

	/** Capture current sim state into the supplied snapshot. A checkpoint is
	 *  emitted only after bootstrap authorization has been consumed; refusal
	 *  clears the output and leaves `SnapshotVersion == 0`. Call only at a
	 *  quiescent fixed-tick boundary: no callback is executing and the deferred
	 *  destroy/effect-apply queues must be empty. Those transient queues are not
	 *  part of this schema. Read-only on sim state; the post-sim callback receives
	 *  only the local camera slot and cannot alter authoritative checkpoint data. */
	void CaptureSnapshot(struct FSeinWorldSnapshot& OutSnapshot);

	/** Validate and restore a consumed-bootstrap checkpoint from an envelope
	 *  already trusted by the exact one-shot native authority. An existing
	 *  match accepts only its exact receipt/context; a fresh target must still
	 *  be a pristine Awaiting world. Restore is rejected during any fixed-tick
	 *  dispatch, including completion callbacks. Fallible assets and storage
	 *  are staged before authoritative replacement, then the sim is restarted.
	 *  Validation, staging, and scheduler-reservation failures leave the old sim
	 *  untouched. After commit, restart is infallible. Local save/load may apply
	 *  captured presentation; multiplayer catch-up must preserve the peer's
	 *  current local state and may remain stopped while its authenticated
	 *  command tail is installed. */
	bool RestoreSnapshot(
		FSeinSnapshotRestoreAuthorityHandle&& Authority,
		const struct FSeinWorldSnapshot& InSnapshot,
		const FSeinSnapshotRestoreOptions& Options);

	/** Fired after Core capture with access only to the non-authoritative local
	 *  camera slot. CoreEntity cannot depend on Framework's camera provider, so
	 *  the Framework presentation subsystem binds at this inversion point. */
	DECLARE_MULTICAST_DELEGATE_OneParam(
		FOnCaptureSnapshot, struct FSeinCameraSnapshotData& /*CameraState*/);
	FOnCaptureSnapshot OnCaptureSnapshotPostSim;

	/** Mirror delegate for restore. Fired only when the caller explicitly
	 *  chooses RestoreCaptured, after sim state is rehydrated and the actor
	 *  bridge is reconciled. RemainStopped restores still expose a coherent
	 *  world, but fixed ticks do not resume until the outer coordinator is
	 *  ready. */
	DECLARE_MULTICAST_DELEGATE_OneParam(
		FOnRestoreSnapshot,
		const struct FSeinCameraSnapshotData& /*CameraState*/);
	FOnRestoreSnapshot OnRestoreSnapshotPostSim;

	/**
	 * Fired after every successful authoritative snapshot replacement, once
	 * Core state is coherent and before actor-bridge reconciliation or resumed
	 * simulation. Extension systems use this read-only seam to rebuild derived,
	 * non-canonical sim indexes from restored entity/component state so bridge
	 * callbacks cannot observe an abandoned index. It fires for both local-state
	 * policies; handlers must not mutate authoritative sim state, depend on
	 * restored actors, or retain a restore capability.
	 */
	DECLARE_MULTICAST_DELEGATE(FOnAuthoritativeStateRestored);
	FOnAuthoritativeStateRestored OnAuthoritativeStateRestored;

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

	/** Reverse lookup used by exact continuation capture; INDEX_NONE if absent. */
	int32 FindAbilityInstanceID(const USeinAbility* Ability) const;

	int64 GetNextAbilityActivationID() const
	{
		return NextAbilityActivationID;
	}

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
	/** Acquire one refcounted tag reference. Returns false without mutation when
	 *  the tag is invalid or its counter is saturated. Callers that later release
	 *  ownership must retain this result rather than assuming the grant landed. */
	bool GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Whether one additional reference can be acquired without saturation. */
	bool CanGrantTag(FSeinEntityHandle Handle, FGameplayTag Tag) const;

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

	/** Outcome of one synchronous effect transaction. */
	enum class EEffectApplyStatus : uint8
	{
		/** Validation rejected before any victim was removed or new effect committed. */
		RejectedNoMutation,
		/** The new effect or stack/refresh update committed. */
		Applied,
		/** Victim callbacks made the replacement invalid after irreversible removal. */
		InvalidatedAfterReplacementRemoval
	};

	struct FEffectApplyResult
	{
		EEffectApplyStatus Status = EEffectApplyStatus::RejectedNoMutation;
		int64 EffectInstanceID = 0;
	};

	/**
	 * Commit an effect synchronously and report whether a clean rejection or an
	 * irreversible replacement invalidation occurred. Native systems use this
	 * when effect application participates in a larger transaction. Call only
	 * from an authorized bootstrap/restore transaction or serial simulation
	 * context; unlike ApplyEffect, this API never defers.
	 */
	FEffectApplyResult ApplyEffectTransactional(
		FSeinEntityHandle Target,
		TSubclassOf<USeinEffect> EffectClass,
		FSeinEntityHandle Source);

	/**
	 * Apply an effect to a target entity. Calls outside an authorized bootstrap,
	 * validated restore, or this world's deterministic sim context are rejected.
	 * If called during a sim tick (from
	 * OnApply/OnTick/OnExpire/OnRemoved), the apply is queued and drained at
	 * the next PreTick. Authorized bootstrap/restore calls commit immediately.
	 *
	 * Scope from the effect CDO determines where the instance lands:
	 *   Instance → target's FSeinActiveEffectsComponent
	 *   Class    → target owner's FSeinPlayerState::ClassEffects
	 *   Player → target owner's FSeinPlayerState::PlayerEffects
	 *
	 * Returns the assigned effect instance ID, or 0 when deferred or when no
	 * effect instance can be assigned. Zero is not a rollback guarantee:
	 * RemoveEffectsWithTag callbacks can commit removals and then invalidate the
	 * replacement target before its new instance exists. Callers that own a
	 * larger transaction use ApplyEffectTransactional rather than inferring
	 * mutation from this compatibility return value. A deferred zero is not an
	 * ID reservation or cancellation token; pending-operation identity is a
	 * separate API/snapshot contract.
	 */
	int64 ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	/** Drain the pending-apply queue. Called at PreTick by FSeinEffectTickSystem. */
	void ProcessPendingEffectApplies();

	/** Remove an effect by its world-global ID, using a live Target to select its
	 *  Instance storage and owning player's Class/Player storages. */
	bool RemoveEffect(FSeinEntityHandle Target, int64 EffectInstanceID, bool bByExpiration);

	/** Remove an effect using only its world-global ID. This is the canonical
	 *  identity-based removal API; the target-anchored overload remains as a
	 *  convenient, narrower lookup when the target is already known. */
	bool RemoveEffectByID(int64 EffectInstanceID, bool bByExpiration);

	/** Remove a Class/Player-scope effect from a known player. Unlike the target-
	 *  anchored overload, this remains valid when the effect's original target
	 *  entity has already been destroyed. C++ only. */
	bool RemovePlayerEffect(FSeinPlayerID PlayerID, int64 EffectInstanceID, bool bByExpiration);

	/** Ability ownership maintenance used by explicit aggregate/force revokes.
	 *  These only edit live ledgers; source-aware ability rows remain the final
	 *  authority when an effect is already detached inside a callback. */
	void PruneEffectAbilityGrantClaim(int64 EffectInstanceID,
		FSeinEntityHandle Recipient, TSubclassOf<USeinAbility> AbilityClass);
	void PruneAllEffectAbilityGrantClaims(
		FSeinEntityHandle Recipient, TSubclassOf<USeinAbility> AbilityClass);

	/** Instance-scope query conveniences for a known target entity. */
	bool HasInstanceEffectWithTag(FSeinEntityHandle Target, FGameplayTag Tag) const;
	int32 GetInstanceEffectStacks(FSeinEntityHandle Target, FGameplayTag Tag) const;

	/** Query one player-owned effect storage. Scope must be Class or Player;
	 *  Instance has no entity target and therefore returns false/zero. */
	bool HasEffectWithTagForPlayer(FSeinPlayerID PlayerID, ESeinModifierScope Scope, FGameplayTag Tag) const;
	int32 GetEffectStacksForPlayer(FSeinPlayerID PlayerID, ESeinModifierScope Scope, FGameplayTag Tag) const;

	/** Legacy-named target convenience: remove matching effects from the target's
	 *  Instance storage and its owner's Class and Player storages. */
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
	 * Repeating the identical controller/player registration is a no-op. Moving
	 * an already registered controller to another player first calls
	 * `OnUnregistered` with its old registration context, then calls
	 * `OnRegistered` after stamping the new context.
	 *
	 * In multiplayer, only the topology authority registers the AI. Its command
	 * interceptor must accept each emission into the lockstep turn stream;
	 * a missing/declined network hook fails closed instead of mutating host-only.
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
	static constexpr int32 MaxPauseControlCommandsPerFrame = 64;

	/** Trusted deterministic follow-up emitted identically by every sim peer.
	 *  This is the public native-system seam: it is simulation-context-only,
	 *  stamps deterministic-system provenance, and validates payer scope. */
	void EnqueueDerivedCommand(const FSeinCommand& Command);

	/**
	 * Submit an unauthenticated local draft through the active topology adapter.
	 * Standalone fallback trusts the draft's player slot; network adapters replace
	 * it from their local participant binding. Match administration remains a
	 * separate requested capability that the adapter must prove. Ordinary drafts
	 * are accepted only after bootstrap has been consumed and the sim is running.
	 */
	void SubmitLocalCommandDraft(
		const FSeinCommand& Draft,
		bool bRequestMatchAdministration = false);

	/** Safe copy lookup against this world's frozen schema snapshot only. */
	bool FindCommandSchema(
		FGameplayTag CommandType,
		int32 SchemaVersion,
		FSeinCommandSchemaDescriptor& OutSchema) const;

	/** Frozen global wire additions bound into this world's protocol digest. */
	TConstArrayView<const UScriptStruct*> GetCommandAdditionalDynamicPayloadStructs() const
	{
		return CommandSchemaSnapshot.GetAdditionalDynamicPayloadStructs();
	}

	TConstArrayView<FName> GetCommandAdditionalWireNames() const
	{
		return CommandSchemaSnapshot.GetAdditionalWireNames();
	}

	/** Structural validation against this world's frozen schema snapshot only. */
	ESeinCommandStructureResult ValidateCommandStructure(
		const FSeinCommand& Command,
		FSeinCommandSchemaDescriptor* OutSchema = nullptr) const;

	/** Exact cursor the next pause-control frame must carry. */
	FSeinPauseControlCursor GetExpectedPauseControlCursor() const;

	/** Active topology-neutral authority policy, or null when startup failed closed. */
	const USeinCommandAuthorityPolicy* GetCommandAuthorityPolicy() const
	{
		return CommandAuthorityPolicy;
	}

	/** Frozen schema + authority-policy identity for join/replay compatibility. */
	FGuid GetCommandProtocolDigest() const { return CommandProtocolDigest; }

	/** Per-author command cap frozen into GetCommandProtocolDigest(). */
	int32 GetCommandProtocolMaxCommandsPerSubmission() const
	{
		return CommandProtocolMaxCommandsPerSubmission;
	}

	/** Canonical digest of the immutable, sorted match-settings snapshot. */
	FGuid GetMatchSettingsDigest() const { return MatchSettingsDigest; }

	/** Frozen project/extension configuration fingerprint for this world. */
	int32 GetConfigFingerprint() const { return ConfigFingerprint; }

	/** Generated Blueprint/native authored-content identity for compatibility. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation|Content")
	FGuid GetSimulationContentDigest() const
	{
		return SimulationContentDigest;
	}

	bool IsSimulationContentReady() const
	{
		return bSimulationContentReady
			&& SimulationContentDigest.IsValid();
	}

	const FString& GetSimulationContentFailureReason() const
	{
		return SimulationContentFailureReason;
	}

	/** Seed most recently used to initialize the world-owned deterministic PRNG. */
	int64 GetSessionSeed() const { return SimSessionSeed; }

	/** Framework-handler entry point. Commands reach this only after common gates. */
	bool ExecuteBuiltInCommand(
		const FSeinCommand& Command,
		FGameplayTag& OutRejectionReason);

	/** Get current tick's pending commands (for networking). */
	const FSeinCommandBuffer& GetPendingCommands() const { return PendingCommands; }

#if WITH_DEV_AUTOMATION_TESTS
	/** White-box replay-lane visibility; absent from shipping builds. */
	int32 GetPendingReplayCommandCountForTests() const
	{
		return PendingReplayCommands.Num();
	}

	bool BeginReplayExclusiveCommandIngressForTests(FString& OutError)
	{
		return BeginReplayExclusiveCommandIngress(OutError);
	}

	void EndReplayExclusiveCommandIngressForTests()
	{
		EndReplayExclusiveCommandIngress();
	}
#endif

	// ========== System Registration ==========

	/**
	 * Register one deterministic system before topology freeze. The descriptor
	 * is captured once; repeating the same pointer is idempotent. Invalid or
	 * duplicate stable IDs poison the pending contract even if the caller
	 * ignores the return value.
	 */
	bool RegisterSystem(ISeinSystem* System, FString* OutError = nullptr);

	/**
	 * Remove one system. A real post-freeze removal invalidates and stops a live
	 * simulation, except during the world's explicit teardown window.
	 */
	bool UnregisterSystem(ISeinSystem* System, FString* OutError = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
	/** Exact current dispatch-array identity; absent from shipping builds. */
	TArray<FString> GetRegisteredSystemOrderForTests() const
	{
		TArray<FString> Order;
		Order.Reserve(Systems.Num());
		for (const auto& Registered : Systems)
		{
			Order.Add(Registered.CanonicalStableID);
		}
		return Order;
	}
#endif

	// ========== Visual Events ==========

	/** Enqueue a visual event (sim -> render, one-way). */
	void EnqueueVisualEvent(const FSeinVisualEvent& Event);

	/** Flush all visual events (called by render layer). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Visual")
	TArray<FSeinVisualEvent> FlushVisualEvents();

	/** Non-draining peek: true if any visual events are queued. Lets the render bridge skip its
	 *  per-frame tick entirely when there is nothing to dispatch. */
	bool HasPendingVisualEvents() const { return VisualEventQueue.Num() > 0; }

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

	/**
	 * Compute the exact 128-bit deterministic state root at a stable world
	 * boundary. The root covers Core authoritative/continuation state plus every
	 * frozen native and Blueprint canonical-state contributor. It fails closed
	 * when capture is re-entrant, state is mid-transaction, or an active
	 * continuation has no exact checkpoint contract.
	 *
	 * The output root is changed only on success. This is intentionally callable
	 * from Blueprint for diagnostics and scripted PIE verification; transports
	 * consume the same C++ seam. A coherent RemainStopped adoption with its
	 * dormant scheduler reservation may compute the pre-activation root; an
	 * ordinary stopped or explicitly abandoned world may not.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Debug",
		meta = (DisplayName = "Compute Canonical State Root"))
	bool ComputeCanonicalStateRoot(
		FGuid& OutRoot,
		FString& OutError) const;

	/** Legacy in-process transactional fingerprint only. This intentionally
	 *  cannot fail closed and is neither complete nor cross-process canonical.
	 *  Do not use for peer compatibility, checkpoints, replay validation, or
	 *  fresh-process determinism evidence. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Debug",
		meta = (DisplayName = "Compute Legacy Local State Fingerprint",
			DeprecatedFunction,
			DeprecationMessage =
				"Use Compute Canonical State Root for determinism evidence."))
	int32 ComputeStateHash() const;

private:
	// Grants the disabled test module narrow white-box access to seed otherwise
	// unreachable saturation/corruption boundaries. No test code ships here.
	friend struct FSeinWorldSubsystemTestAccess;
	friend struct FSeinCommandIngressTestAccess;
	friend struct FSeinSnapshotPoolTestAccess;
	friend struct FSeinMatchBootstrapPoolTestAccess;
	friend class USeinReplayReader;
	friend class USeinAIController;
	friend class USeinAbility;
	friend class FSeinCanonicalStateRegistry;
	friend class FSeinLatentActionCodecRegistry;
	friend class FSeinPoolObjectCodecRegistry;

	template<typename T>
	T* GetDeferredTeardownComponent(FSeinEntityHandle Handle);
	bool RequireMutableStateAccess(const TCHAR* Operation) const;
	bool ExitContainerInternal(FSeinEntityHandle Entity,
		FFixedVector ExitLocation, bool bAllowDeferredTeardownContainer);
	friend class USeinNetSubsystem;

	/** Sole execution seam for USeinAIController::EmitCommand. */
	bool RouteAICommandFromController(
		USeinAIController* Controller,
		const FSeinCommand& Command);

	/** Execute the begin-time native contributor snapshot exactly once, retain
	 *  only canonical payload digests, and release module-owned callbacks. */
	bool FreezeMatchBootstrapNativeContributions(FString& OutError);
	bool IsExactMatchBootstrapAuthority(
		const FSeinMatchBootstrapAuthorityHandle& Authority) const;
	bool IsExactSnapshotRestoreAuthority(
		const FSeinSnapshotRestoreAuthorityHandle& Authority) const;
	void ClearSnapshotRestoreAuthority();
	void FailMatchBootstrapInternal(const FString& Reason);
	bool ReserveSimulationScheduler(FString& OutError);
	void ReleaseSimulationScheduler();
	bool StartSimulationInternal(FString& OutError);
	bool TryAllocateAbilityActivationID(int64& OutID);

	/** Transport-only ingress. Caller-authored provenance and derived funding
	 *  are discarded before the authenticated principal is buffered. Keeping
	 *  this friend-scoped prevents ordinary extensions from asserting identity. */
	void EnqueueAuthenticatedCommand(
		const FSeinCommand& Command,
		FSeinPlayerID AuthenticatedPlayer,
		ESeinCommandIssuerKind AuthenticatedIssuerKind);

	/** Replay-only canonical ingress. External principals only; friend-scoped. */
	void EnqueueCommand(const FSeinCommand& Command);
	bool BeginReplayExclusiveCommandIngress(FString& OutError);
	void EndReplayExclusiveCommandIngress();

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

	// Player states. Reflected so Blueprint effect/ability classes nested in
	// active effect ledgers remain reachable through GC.
	UPROPERTY(Transient)
	TMap<FSeinPlayerID, FSeinPlayerState> PlayerStates;

	// Factions
	UPROPERTY(Transient)
	TMap<FSeinFactionID, TObjectPtr<USeinFaction>> Factions;

	struct FRegisteredSystem
	{
		ISeinSystem* System = nullptr;
		FSeinSystemDescriptor Descriptor;
		FString CanonicalStableID;
	};

	struct FExecutionTopologyCandidate
	{
		TArray<FRegisteredSystem> Systems;
		FString Manifest;
		FGuid Digest;

		bool IsValid() const
		{
			return !Systems.IsEmpty()
				&& !Manifest.IsEmpty()
				&& Digest.IsValid();
		}

		bool IsEquivalentTo(
			const FExecutionTopologyCandidate& Other) const
		{
			if (Manifest != Other.Manifest
				|| Digest != Other.Digest
				|| Systems.Num() != Other.Systems.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Systems.Num(); ++Index)
			{
				if (Systems[Index].System != Other.Systems[Index].System)
				{
					return false;
				}
			}
			return true;
		}
	};

	// Registered systems become immutable and canonically ordered before tick zero.
	TArray<FRegisteredSystem> Systems;
	TArray<ISeinSystem*> BuiltInSystems; // Owned by this subsystem, deleted on deinit
	FString ExecutionTopologyManifest;
	FString ExecutionTopologyFailureReason;
	FGuid ExecutionTopologyDigest;
	bool bExecutionTopologyFrozen = false;
	bool bExecutionTopologyValid = true;
	bool bExecutionTopologyTeardown = false;
	bool bModuleUnloadStateReleased = false;

	// Command buffer
	UPROPERTY(Transient)
	FSeinCommandBuffer PendingCommands;
	/** Replay-owned commands waiting for their exact upcoming tick. Kept
	 *  separate so an explicit playback abort can retract a primed future turn
	 *  without discarding deterministic-system commands already pending. */
	UPROPERTY(Transient)
	FSeinCommandBuffer PendingReplayCommands;
	UPROPERTY(Transient)
	TArray<FSeinCommand> PendingStandalonePauseControlCommands;
	/** Replay-only turn-boundary callback. Kept separate from the public
	 *  read-only completion notification because it primes canonical ingress. */
	FOnSimTickCompleted ReplayCommandBoundaryNotifier;

	/** Installed topology routes. Binding is public for pluggable adapters;
	 *  invocation remains private so every command crosses Core's gates. */
	FSeinAIEmitInterceptor AIEmitInterceptor;
	FSeinLocalCommandSubmitter LocalCommandSubmitter;

	/** Selected stateless policy CDO. Topology authentication occurs before this seam. */
	UPROPERTY(Transient)
	TObjectPtr<USeinCommandAuthorityPolicy> CommandAuthorityPolicy;

	/** Narrow read-only capability supplied to Blueprint/native policies. */
	UPROPERTY(Transient)
	TObjectPtr<USeinCommandAuthorityView> CommandAuthorityView;

	/** World-owned claims for configured Blueprint/native extension handlers. */
	TArray<FSeinCommandSchemaRegistrationHandle> ConfiguredCommandSchemaHandles;
	FSeinCommandSchemaSnapshot CommandSchemaSnapshot;
	FGuid CommandProtocolDigest;
	int32 CommandProtocolMaxCommandsPerSubmission = 0;
	FGuid MatchSettingsDigest;
	int32 ConfigFingerprint = 0;
	UPROPERTY(Transient)
	TObjectPtr<USeinSimulationContentManifest>
		SimulationContentManifestAsset;
	FSeinSimulationContentManifestProfile SimulationContentProfile;
	FGuid SimulationContentDigest;
	FString SimulationContentFailureReason;
	bool bSimulationContentReady = false;
	bool bCommandProtocolReady = false;
	bool bReplayOwnsExternalCommandIngress = false;
	int32 CommandCohesionOrderSequence = 0;

	// Visual event queue
	FSeinVisualEventQueue VisualEventQueue;

	// Deferred destruction list
	TArray<FSeinEntityHandle> PendingDestroy;
	/**
	 * Exact tombstone whose PostTick teardown is currently executing. Ordinary
	 * entity/component lookup stays live-only; this narrow read window preserves
	 * the documented pre-wipe destroy-notification and internal cleanup contract.
	 */
	FSeinEntityHandle DeferredTeardownHandle;

	// Entity → Blueprint class map (for actor bridge spawning)
	UPROPERTY(Transient)
	TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>> EntityActorClassMap;

	// Transient call-stack transaction metadata. Each real ownership change
	// increments its handle revision so an outer transfer can detect recursive
	// B→C→B supersession and avoid replaying B twice. Not gameplay state.
	TMap<FSeinEntityHandle, uint64> OwnerTransitionRevisions;
	/** Synchronous ownership transactions currently on the call stack. Unlike
	 *  the persistent ABA revisions above, nonzero means state is intermediate:
	 *  old grants may be detached while new-owner grants are not yet replayed. */
	int32 OwnerTransitionDepth = 0;

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

	// World-global, monotonically increasing effect identity. Zero is invalid;
	// IDs are never reused within this simulation timeline.
	int64 NextEffectInstanceID = 1;

	// AI controller registry (DESIGN §16). Ticked in CommandProcessing phase.
	UPROPERTY()
	TArray<TObjectPtr<USeinAIController>> AIControllers;
	/** Call-stack-only capability identifying the exact controller whose Tick
	 *  callback may emit. Never retained across callbacks or frames. */
	USeinAIController* ActiveAICommandEmitter = nullptr;

	// ============================================================================
	// Ability + Resolver pools storage (Phase 4 architecture)
	// ============================================================================
	// Pool slots own the UObject lifetime via UPROPERTY. Indices are stable IDs
	// component data holds. Free-list-recycled to keep the index space compact.

	UPROPERTY()
	TArray<TObjectPtr<USeinAbility>> AbilityPool;
	TArray<int32> AbilityPoolFreeList;
	int64 NextAbilityActivationID = 1;

	UPROPERTY()
	TArray<TObjectPtr<USeinCommandBrokerResolver>> CommandBrokerResolverPool;
	TArray<int32> CommandBrokerResolverPoolFreeList;

	// Tick the registered AI controllers. Called from TickSystems at
	// CommandProcessing phase, right before ProcessCommands.
	void TickAIControllers(FFixedPoint DeltaTime);

	// Compatibility projection used by the deferred/public int64 path.
	int64 ApplyEffectInternal(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	// Central teardown path shared by explicit, tag, expiry, and source-death
	// removal. Removes before dispatching callbacks so synchronous re-entry is safe.
	bool RemoveEffectFromStorage(TArray<FSeinActiveEffect>& Storage, int64 EffectInstanceID,
		FSeinPlayerID PlayerForTags, bool bByExpiration);

	struct FEffectLocator
	{
		ESeinModifierScope Scope = ESeinModifierScope::Instance;
		FSeinEntityHandle InstanceTarget;
		FSeinPlayerID PlayerID;
		int64 EffectInstanceID = 0;
	};

	// Callback-capable hot paths carry a stable storage locator and re-resolve
	// only that target/player array. Global scanning remains for rare public
	// identity operations and explicit ownership repair.
	FSeinActiveEffect* FindActiveEffectByID(int64 EffectInstanceID);
	FSeinActiveEffect* ResolveEffect(const FEffectLocator& Locator);
	bool IsEffectGrantRecipientEligible(const FEffectLocator& Locator,
		FSeinEntityHandle Recipient);
	bool GrantAbilityTrackedByEffect(const FEffectLocator& Locator, FSeinEntityHandle Recipient,
		TSubclassOf<USeinAbility> AbilityClass);

	// Simulation state
	ESeinMatchBootstrapState MatchBootstrapState =
		ESeinMatchBootstrapState::Awaiting;
	FSeinMatchBootstrapReceipt MatchBootstrapReceipt;
	FGuid MatchBootstrapAuthorizationContextDigest;
	FString MatchBootstrapFailureReason;
	FName MatchBootstrapAuthorityID;
	FGuid MatchBootstrapAuthorityToken;
	TWeakObjectPtr<const UObject> MatchBootstrapAuthorityOwner;
	/** Process-local, one-shot envelope-adoption authority. Not canonical state. */
	FName SnapshotRestoreAuthorityID;
	FGuid SnapshotRestoreAuthorityToken;
	TWeakObjectPtr<const UObject> SnapshotRestoreAuthorityOwner;
	/** Lexical provider capability opened only by an authority-gated Ensure call. */
	bool bMatchBootstrapMaterializerInvocationActive = false;
	TArray<FSeinCanonicalInitialStateNativeContribution>
		MatchBootstrapNativeContributors;
	TArray<FSeinCanonicalInitialStateValueContribution>
		MatchBootstrapValueContributions;
	/** Frozen native schemas plus Core-owned Blueprint value slots. */
	FSeinCanonicalStateSchemaSnapshot NativeCanonicalStateSchema;
	FSeinLatentActionCodecManifest LatentActionCodecManifest;
	FSeinPoolObjectCodecManifest PoolObjectCodecManifest;
	FSeinCanonicalStateValueStore CanonicalStateValues;
	/**
	 * Exact provider-bound world identities adopted into the match
	 * StateContract. Core recaptures these before each fixed tick so no
	 * command, built-in system, or custom early-priority system can consume a
	 * drifted static environment.
	 */
	TArray<FString> FrozenCanonicalStateWorldBindingFrames;
	bool bMatchBootstrapClosedBroadcast = false;
	/** Private capability set only after RestoreSnapshot has fully validated a
	 *  checkpoint. It never spans scheduler start or external delegates. */
	bool bSnapshotRestoreMutationAuthorized = false;
	/** Blocks capture/restore re-entry while a snapshot capture owns its output
	 *  and extension callback window. */
	bool bSnapshotCaptureInProgress = false;
	/** Blocks lifecycle re-entry while snapshot restore owns the scheduler and
	 *  authoritative state transition. Callback-capable cancellation and
	 *  post-restore hooks execute while this guard is set. */
	bool bSnapshotRestoreInProgress = false;
	/** Suppresses deterministic mutation authority while explicitly read-only
	 *  observer and host-AI callbacks execute, including during Applying. */
	bool bReadOnlyCallbackInProgress = false;
	/** Read-only observer callbacks may neither mutate state nor enqueue work.
	 *  Host AI uses only the read-only guard so its command-only seam remains. */
	bool bObserverCallbackInProgress = false;
	/** Narrows GetDestroyingComponent to the synchronous destroy notification. */
	bool bDestroyNotificationInProgress = false;
	/** True for the full fixed-tick callback, including completion delegates
	 *  that run after the lexical sim scope closes. Snapshot boundaries may not
	 *  be entered while this guard is set. */
	bool bSimulationTickDispatchInProgress = false;
	bool bIsRunning = false;
	bool bSimulationSchedulerReserved = false;
	bool bSimPaused = false;     // DESIGN §17 — scenario-driven pause gate
	bool bSimPausedHard = false; // DESIGN §18 — true = reject sim-mutating commands while paused
	int64 PauseEpoch = 0;
	int32 PauseFrozenTick = INDEX_NONE;
	int64 LastAppliedPauseControlSequence = -1;
	bool bDispatchingPauseControlFrame = false;
	bool bPauseControlDispatchProtocolViolation = false;
	int32 ActivePauseControlCommandIndex = INDEX_NONE;
	int32 ActivePauseControlCommandCount = 0;
	int32 CurrentTick = 0;
	int64 SimSessionSeed = 0;
	bool bSimSessionSeedInstalled = false;
	FTSTicker::FDelegateHandle TickerHandle;

	// Match state (DESIGN §18). State machine drives pre-match countdown,
	// end-match cleanup, spectator + pause filters in ProcessCommands. Reflected
	// so extension FInstancedStruct types remain reachable through GC.
	ESeinMatchState MatchState = ESeinMatchState::Lobby;
	UPROPERTY(Transient)
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
	enum class ECommandHandleResult : uint8
	{
		Unhandled,
		Handled
	};

	bool TickSimulation(float DeltaTime);
	bool ValidateFrozenConfigFingerprint();
	bool ValidateFrozenCanonicalStateWorldBindings();
	void TickSystems(FFixedPoint DeltaTime);
	void ProcessCommands();
	void PumpPauseControlFrame();
	bool ResolvePauseControlFrame(FSeinPauseControlFrame& OutFrame);
	bool PreflightPauseControlFrame(
		const FSeinPauseControlFrame& Frame,
		TArray<FSeinCommandSchemaDescriptor>& OutSchemas) const;
	void DispatchValidatedCommand(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor& Schema);
	bool InitializeCommandProtocol();
	void ShutdownCommandProtocol();
	bool InitializeSimulationContent(
		const USeinARTSCoreSettings* Settings);
	void ShutdownSimulationContent();
	bool IsCurrentWorldCoveredBySimulationContent(
		FString& OutError) const;
	/**
	 * Open the sole tick-zero transaction. This is deliberately private: Core
	 * invokes it only inside the authority-gated Ensure facade, before calling
	 * the pluggable materializer.
	 */
	bool BeginMatchBootstrap(
		const FGuid& ContractDigest,
		const FGuid& AuthorizationContextDigest,
		FString& OutError);
	/**
	 * Build the complete passive state contract from locally registered
	 * native/Blueprint recipes. Checkpoint data never supplies this schema.
	 */
	bool BuildLocallyDeclaredCanonicalState(
		const FSeinMatchSettings& MatchSettings,
		bool bMaterializeInitialValues,
		const FString& TopologyManifest,
		FSeinCanonicalStateValueStore& OutStore,
		TArray<FString>& OutWorldBindingFrames,
		FString& OutError);
	bool TryBuildExecutionTopologyCandidate(
		FExecutionTopologyCandidate& OutCandidate,
		FString& OutError) const;
	void AdoptExecutionTopologyCandidate(
		FExecutionTopologyCandidate&& Candidate);
	bool FreezeExecutionTopology(FString& OutError);
	void RecordExecutionTopologyFailure(const FString& Reason);
	void InvalidateFrozenExecutionTopology(const FString& Reason);
	void ReleaseAllModuleOwnedState();
	/** Direct materialization is legal only inside Applying; later gameplay
	 *  mutation must be executing in the deterministic sim context. */
	bool IsStateMutationAuthorized() const;
	bool IsCommandContextAllowed(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor& Schema,
		FGameplayTag& OutRejectionReason) const;
	static FGameplayTag StructureResultToRejectionTag(ESeinCommandStructureResult Result);
	bool ValidateBuiltInCommandSemantics(
		const FSeinCommand& Command,
		FGameplayTag& OutRejectionReason) const;
	void RejectCommand(const FSeinCommand& Command, FGameplayTag Reason = FGameplayTag());
	ECommandHandleResult TryHandleMatchFlowOrVoteCommand(const FSeinCommand& Command);
	ECommandHandleResult TryHandlePingCommand(const FSeinCommand& Command);
	ECommandHandleResult TryHandleBrokerOrderCommand(const FSeinCommand& Command, int32& CohesionOrderSeq);
	ECommandHandleResult TryHandleActivateAbilityCommand(const FSeinCommand& Command);
	ECommandHandleResult TryHandleCancelAbilityCommand(const FSeinCommand& Command);
	ECommandHandleResult TryHandleCancelProductionCommand(const FSeinCommand& Command);
	void ProcessDeferredDestroys();

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
	if (!RequireStateMutationAuthorization(TEXT("AddComponent")))
	{
		return;
	}
	if (!EntityPool.IsValid(Handle))
	{
		return;
	}
	ISeinComponentStorage* Storage = GetOrCreateStorageForType(T::StaticStruct());
	if (Storage)
	{
		Storage->AddComponent(Handle, &Component);
	}
}

template<typename T>
void USeinWorldSubsystem::RemoveComponent(FSeinEntityHandle Handle)
{
	if (!RequireStateMutationAuthorization(TEXT("RemoveComponent")))
	{
		return;
	}
	if (!EntityPool.IsValid(Handle))
	{
		return;
	}
	if (ISeinComponentStorage* Storage =
		GetComponentStorageMutable(T::StaticStruct()))
	{
		Storage->RemoveComponent(Handle);
	}
}

template<typename T>
T* USeinWorldSubsystem::GetComponentMutable(FSeinEntityHandle Handle)
{
	if (!RequireMutableStateAccess(TEXT("GetComponentMutable")))
	{
		return nullptr;
	}
	if (!EntityPool.IsValid(Handle)) return nullptr;
	ISeinComponentStorage* Storage =
		GetComponentStorageMutable(T::StaticStruct());
	return Storage ? static_cast<T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
const T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle) const
{
	if (!EntityPool.IsValid(Handle)) return nullptr;
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<const T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
bool USeinWorldSubsystem::HasComponent(FSeinEntityHandle Handle) const
{
	if (!EntityPool.IsValid(Handle)) return false;
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage && Storage->HasComponent(Handle);
}

template<typename T>
const T* USeinWorldSubsystem::GetDestroyingComponent(
	FSeinEntityHandle Handle) const
{
	if (!bDestroyNotificationInProgress
		|| Handle != DeferredTeardownHandle
		|| !EntityPool.IsDeferredDestroyTombstone(Handle))
	{
		return nullptr;
	}
	const ISeinComponentStorage* Storage =
		GetComponentStorageRaw(T::StaticStruct());
	return Storage
		? static_cast<const T*>(Storage->GetComponentRaw(Handle))
		: nullptr;
}

template<typename T>
T* USeinWorldSubsystem::GetDeferredTeardownComponent(
	FSeinEntityHandle Handle)
{
	if (Handle != DeferredTeardownHandle
		|| !EntityPool.IsDeferredDestroyTombstone(Handle))
	{
		return nullptr;
	}
	ISeinComponentStorage* Storage =
		GetComponentStorageMutable(T::StaticStruct());
	return Storage
		? static_cast<T*>(Storage->GetComponentRaw(Handle))
		: nullptr;
}
