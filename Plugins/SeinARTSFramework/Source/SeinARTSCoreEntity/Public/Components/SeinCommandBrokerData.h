/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerData.h
 * @brief   Sim-component carrying CommandBroker state (DESIGN §5). Lives on
 *          the abstract broker entity; holds member list, resolver reference,
 *          centroid/anchor, cached capability map, and the order queue.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Components/SeinComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinCommandBrokerData.generated.h"

class USeinCommandBrokerResolver;

/** Capability-map value wrapper — `TMap<FGameplayTag, TArray<FSeinEntityHandle>>`
 *  isn't allowed as a raw UPROPERTY map. Wrapping the array in a USTRUCT
 *  sidesteps UHT's "nested containers" restriction the same way
 *  `FSeinCellTagOverflowEntry` does in the nav module. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerCapabilityBucket
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> Members;
};

/**
 * Sim-component for command broker entities (DESIGN §5).
 *
 * A broker wraps a set of entities (all owned by the same FSeinPlayerID) as a
 * single dispatch target. Member dispatch is delegated to a designer-pluggable
 * `USeinCommandBrokerResolver`, which consumes the capability map + order
 * context and returns per-member (ability, target) tuples the system issues
 * internally on tick.
 *
 * Not authored on a Blueprint — created on demand by
 * `USeinWorldSubsystem::ProcessCommands` when a `BrokerOrder` command arrives.
 * Culled by `FSeinCommandBrokerSystem` when the member list and order queue
 * both empty.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinCommandBrokerData : public FSeinComponent
{
	GENERATED_BODY()

	/** Current member list. All members must share the broker's owning player.
	 *  `FSeinCommandBrokerSystem` strips dead handles each tick. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> Members;

	/** Pool ID of this broker's resolver instance. Index into
	 *  `USeinWorldSubsystem::CommandBrokerResolverPool`. INDEX_NONE before
	 *  the broker is fully initialized. Populated by SpawnBroker from
	 *  `USeinARTSCoreSettings::DefaultBrokerResolverClass` (or the C++
	 *  default `USeinDefaultCommandBrokerResolver` if unset).
	 *
	 *  Phase 4 architecture: stored as int32 ID (not TObjectPtr) so the
	 *  state hash is portable across processes + world snapshots round-trip
	 *  cleanly. Walk sites resolve via `World.GetCommandBrokerResolver(ID)`. */
	UPROPERTY()
	int32 ResolverID = INDEX_NONE;

	/** Dynamic centroid of the live member set (updated each tick). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedVector Centroid;

	/** "Where the formation is trying to stand" — stamped when an order
	 *  dispatches. Used by tight-formation resolvers + UI banners. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedVector Anchor;

	/** Facing associated with Anchor. Zero-rotation = unset. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedQuaternion AnchorFacing;

	/** Cached "which members can service each ability tag." Rebuilt from
	 *  members' FSeinAbilityComponent when `bCapabilityMapDirty` is true. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TMap<FGameplayTag, FSeinBrokerCapabilityBucket> CapabilityMap;

	/** Set on member add/remove. Cleared after the next `RebuildCapabilityMap`. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bCapabilityMapDirty = true;

	/** Click context of the most-recently-dispatched order. Container, not a
	 *  single tag. With per-order parallelism (each order tracks its own
	 *  bIsExecuting), this is purely informational — it reflects the latest
	 *  dispatch for UI / diagnostics but doesn't gate dispatch eligibility.
	 *  Empty when no order has dispatched since the broker was created. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FGameplayTagContainer CurrentOrderContext;

	/** FIFO queue of orders. Under per-order parallelism, any non-executing
	 *  entry whose effective members aren't locked by an executing entry is
	 *  eligible to dispatch — the broker tick walks the queue in order each
	 *  pass, dispatching all eligible orders concurrently. Player shift-
	 *  chained full-broker orders serialize naturally because they share
	 *  every member; subset-targeted orders (AutoMoveThen pairs, per-member
	 *  IssueBrokerOrderFromEntity submissions) run in parallel when their
	 *  member sets are disjoint. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FSeinBrokerQueuedOrder> OrderQueue;

	/** When true (default), the broker system culls this broker entity once
	 *  Members.Num() == 0 AND OrderQueue.Num() == 0. (Queue-empty implies
	 *  nothing executing under the per-order model.) The ephemeral player-
	 *  selection brokers spawned by ProcessCommands all use the default.
	 *  Persistent broker carriers (squads — DESIGN §sub-broker) flip this
	 *  to false on spawn so the broker survives empty member lists and is
	 *  destroyed by its OWNING SYSTEM (e.g. FSeinSquadSystem culls the
	 *  squad entity, which incidentally takes the broker with it). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bSelfCullOnEmpty = true;

	/** Lateral extent of the broker's formation, in world units. Maintained by
	 *  whatever system owns this broker (e.g. a squad system computes this from
	 *  its slot offsets). Zero = point-sized (no lateral offset in multi-broker
	 *  layouts). Read by ProcessCommands to compute per-broker lateral anchors
	 *  when multiple persistent brokers are selected simultaneously. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedPoint FormationWidth = FFixedPoint::Zero;

	/** Bounding-circle radius of the broker's formation (world units): the distance from the broker's
	 *  placement origin out to the farthest member EDGE, i.e. inclusive of every member's footprint.
	 *  Maintained by the owning system (the squad system computes it from slot offsets + member
	 *  footprints, like FormationWidth). Read by USeinFormation::GetFootprintRadius so a parent
	 *  formation can place the whole broker as ONE footprint-sized element. Zero = not maintained
	 *  (falls back to the broker actor's own extents). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedPoint FormationRadius = FFixedPoint::Zero;

	/** Where this formation's spots are on the ground - the slot positions of the last
	 *  ground move this broker dispatched.
	 *
	 *  The FORMATION owns its slots; which member stands in which slot is re-decided at
	 *  use (a scattered line re-forms with members in whatever arrangement crosses least,
	 *  via Reassign Slots) - so this is a plain slot list, deliberately NOT a member-to-slot
	 *  map. Returned by the resolver in its dispatch plan and committed atomically by the
	 *  broker system at every ground-move dispatch with the FINAL delivered goals (after
	 *  nav projection and cover snap). Entity-targeted orders (attack, repair) leave it
	 *  untouched - slots do not apply to a moving target. Empty until the first ground
	 *  order. Consumers: formation re-form / re-seek and per-slot settle policies. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FFixedVector> SettledSlotPositions;

	/** Which way each of this formation's spots faces, index-aligned with Settled Slot
	 *  Positions.
	 *
	 *  From the formation's facing mode (uniform, radial in/out) computed on the final
	 *  positions; paths that carry no per-slot facing (pre-placed parent-formation slots)
	 *  fill with the broker's Anchor Facing. Same lifetime and ownership rules as Settled
	 *  Slot Positions. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FFixedQuaternion> SettledSlotFacings;

	/** Reserved exact destinations retained after successful movement arrival. An
	 *  entry remains authoritative until that member starts another move, leaves
	 *  the broker, or dies. Provider identity remains provenance; provider motion
	 *  or destruction does not relocate the settled point. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FSeinFrozenDestination> SettledDestinationArtifact;

	/** The earliest sim tick this formation may next consider an idle re-seek.
	 *
	 *  Advanced by the broker tick on every re-seek scan (the re-check cadence) and pushed
	 *  further out after an episode ends (the quiet period), so re-seek neither scans every
	 *  tick nor machine-guns follow-up orders onto crowded ground. Runtime state; unused
	 *  while the Idle Re-Seek setting is off. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	int32 NextReseekAllowedTick = 0;

	/** Whether each settled slot belongs to a SPECIFIC member (slot i = Members i), or the
	 *  slots are free for any member to fill.
	 *
	 *  Set by whichever resolver captured the layout: the default (loose-formation) resolver
	 *  leaves this false — a re-form re-matches members to slots so the return crosses as
	 *  little as possible. The squad resolver sets it true — squads have AUTHORED slot roles
	 *  (pinned by default), so a re-form sends each member back to ITS OWN slot instead of
	 *  re-shuffling the roster. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bSettledSlotsMemberAligned = false;

	/** OBSTACLE-SIDE avoidance opinion: how OTHER units treat this broker's members. When true, a
	 *  transiting unit routes around this formation's whole extent (Centroid + FormationRadius) as
	 *  ONE cohesive body instead of steering through the gaps between its members (which makes the
	 *  transiting unit chase the moving gap and orbit). Set each tick by the owning system — the
	 *  squad system copies it from FSeinSquadComponent::bAvoidAsBlob; left false for loose /
	 *  ephemeral brokers (they carry no maintained FormationRadius, so the kernel treats them
	 *  per-member regardless). Default false. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bAvoidAsCohesiveBody = false;

	/** COHESION-SIDE opinion (mirrors bAvoidAsCohesiveBody's stamp pattern): whether this broker's
	 *  squad participates in OUTER cohesion — pacing the OTHER squads of the same multi-squad order
	 *  (keyed on CohesionGroupId) so the whole ordered body stays together in transit. Set each tick
	 *  by the owning squad system from the project-wide USeinARTSSquadSettings::bPaceSquadsTogether;
	 *  left false for loose / ephemeral brokers and when the setting is off (=> inner-only cohesion,
	 *  bit-exact). Read by the avoidance kernel via the broker handle — the framework kernel never
	 *  sees the squad extension. Default false. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bPaceSquadsTogether = false;

	/** The sim tick this formation's current re-seek episode began, or 0 when no episode
	 *  is active.
	 *
	 *  An episode starts when displaced members are first noticed (with the ground clear
	 *  of traffic) and ends when nobody is displaced and no re-form orders remain in
	 *  flight. Each member's staggered release delay is measured from this anchor, so
	 *  soldiers peel back toward their slots at individually jittered moments instead of
	 *  all at once. Runtime state; unused while the Idle Re-Seek setting is off. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	int32 ReseekEpisodeStartTick = 0;

	// Per-order execution state (`bIsExecuting` + `LastDispatchTick`) was
	// promoted onto FSeinBrokerQueuedOrder so non-overlapping subset-targeted
	// orders can dispatch concurrently. The broker tick computes a
	// LockedMembers set from every executing order and dispatches each
	// non-executing order whose Effective members are all unlocked.
};

FORCEINLINE uint32 GetTypeHash(const FSeinCommandBrokerData& Data)
{
	uint32 Hash = GetTypeHash(Data.Members.Num());
	Hash = HashCombine(Hash, GetTypeHash(Data.OrderQueue.Num()));
	for (const FSeinBrokerQueuedOrder& Order : Data.OrderQueue)
	{
		Hash = HashCombine(Hash, GetTypeHash(Order.DerivedResourcePayer));
	}
	Hash = HashCombine(Hash, GetTypeHash(Data.Centroid));
	Hash = HashCombine(Hash, GetTypeHash(Data.FormationWidth));
	Hash = HashCombine(Hash, GetTypeHash(Data.FormationRadius));
	Hash = HashCombine(Hash, GetTypeHash(Data.NextReseekAllowedTick));
	Hash = HashCombine(Hash, GetTypeHash(Data.ReseekEpisodeStartTick));
	Hash = HashCombine(Hash, GetTypeHash(Data.bSettledSlotsMemberAligned));
	Hash = HashCombine(Hash, GetTypeHash(Data.bAvoidAsCohesiveBody));
	Hash = HashCombine(Hash, GetTypeHash(Data.bPaceSquadsTogether));
	Hash = HashCombine(Hash, GetTypeHash(Data.SettledSlotPositions.Num()));
	for (const FFixedVector& Slot : Data.SettledSlotPositions)
	{
		Hash = HashCombine(Hash, GetTypeHash(Slot));
	}
	for (const FFixedQuaternion& Facing : Data.SettledSlotFacings)
	{
		Hash = HashCombine(Hash, GetTypeHash(Facing));
	}
	return Hash;
}
