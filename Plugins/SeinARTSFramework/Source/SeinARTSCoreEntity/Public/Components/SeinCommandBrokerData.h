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
	Hash = HashCombine(Hash, GetTypeHash(Data.Centroid));
	Hash = HashCombine(Hash, GetTypeHash(Data.FormationWidth));
	Hash = HashCombine(Hash, GetTypeHash(Data.FormationRadius));
	return Hash;
}
