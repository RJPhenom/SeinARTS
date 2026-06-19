/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBrokerTypes.h
 * @brief   Shared types for the CommandBroker primitive (DESIGN §5):
 *          - FSeinBrokerQueuedOrder: one order sitting in a broker's queue
 *          - FSeinBrokerOrderInput : resolver input packet
 *          - FSeinBrokerMemberDispatch / FSeinBrokerDispatchPlan : resolver output
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Abilities/SeinTargeterTypes.h"
#include "SeinBrokerTypes.generated.h"

/**
 * One queued order on a broker. Shift-chained dispatches append to
 * FSeinCommandBrokerData::OrderQueue; each is consumed in FIFO order as the
 * previous completes.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerQueuedOrder
{
	GENERATED_BODY()

	/** Raw click context — RightClick + Target.Ground/Friendly/Enemy/Neutral plus
	 *  any designer-added target tags. Resolved sim-side by the broker's resolver
	 *  per-member (DESIGN §5). Replaces the pre-resolved AbilityTag field: the
	 *  player controller emits one pre-resolution command, the broker interprets. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTagContainer Context;

	/** Optional target entity (attack target, repair target, etc.). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	/** Target world location (move destination, attack point, rally, …). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	/** Optional second endpoint for drag orders (formation line end). Legacy single
	 *  endpoint — superseded by GuidePoints; retained until the gesture migration. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector FormationEnd;

	/** Ordered guide geometry for this order — the gesture's path. Empty/1 point =
	 *  simple click; 2 = a line; N = a path. Consumed by the formation. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FFixedVector> GuidePoints;

	/** Gesture-nominated formation identity. Invalid → the resolver's default
	 *  formation (DefaultFormationClass). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag FormationTag;

	/** Subset of the broker's members this order dispatches against. Empty = all
	 *  members. Used by (a) player shift-click on a subset of a shared broker —
	 *  the new order is appended to the broker's queue but targets only the
	 *  selected subset, and (b) framework-injected prefixes (e.g. AutoMoveThen's
	 *  Move prefix for a single out-of-range member). Non-target members stay
	 *  idle for this order; the broker's completion predicate waits on the
	 *  effective subset only. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> TargetMembers;

	/** True if this order was injected internally by framework machinery
	 *  (e.g., AutoMoveThen prefix). Designer dispatches leave this false. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	bool bIsInternalPrefix = false;

	/** Captured targeter points when this order originated from the targeter
	 *  subsystem (action-slot trigger flow). Empty for right-click smart commands.
	 *  Per-member ability activation forwards these into USeinAbility::TargeterPoints
	 *  so OnActivate can read either TargetLocation (single-point convenience) or
	 *  the full array (multi-target abilities). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinTargeterPoint> TargeterPoints;

	/** Predetermined ability tag when this order originated from the targeter
	 *  (action-slot trigger flow). The player picked the ability before targeting,
	 *  so the broker resolver should NOT run per-member context resolution against
	 *  this order — it dispatches the predetermined ability via the ability's
	 *  DispatchMode policy (All/Single/ByTag). Invalid for right-click smart
	 *  commands (those use Context-driven resolution via
	 *  FSeinAbilityComponent::DefaultCommands). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag PredeterminedAbilityTag;

	// ─── Per-order execution state (Option C parallelism) ───
	//
	// Previously bIsExecuting + LastDispatchTick lived as single fields on
	// FSeinCommandBrokerData — only one order could be in flight per broker.
	// Per-order state lets non-overlapping subset-targeted orders run
	// concurrently: when 5 squad members each chain an AutoMoveThen-prefixed
	// Build via SeinIssueBrokerOrderFromEntity, the 5 resulting subset orders
	// don't share members → they dispatch in parallel. Player-issued
	// full-broker orders (which DO share members) still serialize naturally
	// because the broker tick's dispatch loop gates on a LockedMembers set
	// computed from every executing order.

	/** True from dispatch tick until completion. Per-order so concurrent
	 *  orders can each track their own state without a shared mutex on the
	 *  broker. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bIsExecuting = false;

	/** Sim tick at which this order was dispatched. The completion check
	 *  gates on `CurrentTick > LastDispatchTick` to avoid popping an order
	 *  on the same tick it dispatched — per-member ActivateAbility commands
	 *  the dispatch enqueued don't process until the next CommandProcessing
	 *  phase, so members' ActiveAbilityID is still INDEX_NONE same-tick and
	 *  a naive "all idle = done" check would falsely fire. INDEX_NONE before
	 *  first dispatch. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	int32 LastDispatchTick = INDEX_NONE;
};

/**
 * Input handed to USeinCommandBrokerResolver::ResolveDispatch. Mirrors the
 * live queued-order shape plus the broker's owning player ID and current anchor.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerOrderInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTagContainer Context;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector FormationEnd;

	/** Ordered guide geometry (mirrors FSeinBrokerQueuedOrder::GuidePoints) — the
	 *  gesture's path, nav-projected. Empty/1 = click; 2 = line; N = path. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FFixedVector> GuidePoints;

	/** Gesture-nominated formation identity (mirrors the queued order). Invalid →
	 *  the resolver's default formation. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag FormationTag;

	/** The effective member set this order dispatches against — TargetMembers
	 *  from the queued order if non-empty, else the broker's full Members list.
	 *  Resolvers should iterate EffectiveMembers (not the broker's Members)
	 *  when building their dispatch plan. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> EffectiveMembers;

	/** Targeter-captured points (mirrors FSeinBrokerQueuedOrder::TargeterPoints).
	 *  Resolvers can inspect these to make per-member dispatch decisions for
	 *  multi-target abilities — e.g., distribute 3 grenade points across 3
	 *  squad members rather than dispatching all 3 to the leader. Default
	 *  resolver ignores it (passes points to one member); custom resolvers
	 *  can implement more elaborate distribution. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinTargeterPoint> TargeterPoints;

	/** Predetermined ability tag (mirrors FSeinBrokerQueuedOrder::PredeterminedAbilityTag).
	 *  When valid, default resolver dispatches this ability to the first capable
	 *  member instead of running per-member context resolution. Invalid = use
	 *  per-member ResolveMemberAbility / DefaultCommands path. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag PredeterminedAbilityTag;
};

/**
 * Typed payload for FSeinCommand when CommandType == Command.Type.BrokerOrder.
 * Carries the raw click context (smart-resolved sim-side by the broker resolver)
 * and formation endpoint for drag orders. Lives in FSeinCommand::Payload —
 * keeps the base command struct lean while allowing broker-order-specific
 * fields to evolve without touching unrelated command types.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerOrderPayload
{
	GENERATED_BODY()

	/** Click context tag container — RightClick + Target.Ground / Friendly /
	 *  Enemy / Neutral plus any designer-added target tags. Resolver interprets
	 *  per-member to pick which ability to activate. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FGameplayTagContainer CommandContext;

	/** Drag-order formation endpoint in world space. Zero = not a drag order.
	 *  Legacy single endpoint — superseded by GuidePoints. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FFixedVector FormationEnd;

	/** Ordered guide geometry — the gesture's path (empty/1 = click, 2 = line, N =
	 *  path). ProcessCommands nav-projects each point into the queued order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	TArray<FFixedVector> GuidePoints;

	/** Gesture-nominated formation identity (invalid = resolver default). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FGameplayTag FormationTag;

	/** Targeter-captured points when this command originated from the targeter
	 *  subsystem (player triggered an ability via action slot, then placed targets).
	 *  Empty for right-click smart commands. ProcessCommands forwards these into
	 *  the resulting FSeinBrokerQueuedOrder::TargeterPoints. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	TArray<FSeinTargeterPoint> TargeterPoints;

	/** Predetermined ability tag when this command originated from the targeter
	 *  (player picked the ability before placing targets). When valid, broker
	 *  resolver dispatches this ability instead of running context resolution.
	 *  Invalid for right-click smart commands. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FGameplayTag PredeterminedAbilityTag;
};

/**
 * One (member, ability, target) tuple produced by the resolver. The broker
 * system issues the per-member ActivateAbility internally on tick.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerMemberDispatch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle Member;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	/** Targeter points to forward into the member's ability instance. Default
	 *  resolver passes the order's full TargeterPoints array to the chosen
	 *  member; custom resolvers can slice (e.g. one point per member for
	 *  distributed multi-target). Empty for right-click-originated dispatches. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinTargeterPoint> TargeterPoints;
};

/**
 * Full resolver output for one order. The broker system walks
 * MemberDispatches on the dispatch tick and fires an internal ActivateAbility
 * against each member's FSeinAbilityComponent.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerDispatchPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinBrokerMemberDispatch> MemberDispatches;
};

/**
 * Full formation layout returned by `USeinCommandBrokerResolver::ResolveFormationLayout`.
 * Used by:
 *   - the dispatch path (`ResolveDispatch` consumes Positions for per-member
 *     target locations and Facing to write back to broker data)
 *   - the preview path (`USeinWorldSubsystem::ComputeFormationPreview` reads
 *     Positions to render destination decals under the cursor for hover preview;
 *     Facing is exposed for previews that want to render facing arrows).
 *
 * Same data, two consumers — keeps preview and commit in lockstep with no
 * "actually move" bool flag on a single function.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFormationLayout
{
	GENERATED_BODY()

	/** Per-member world positions, index-aligned with the input Members array. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker|Formation")
	TArray<FFixedVector> Positions;

	/** Formation's facing at the anchor — the direction the front rank faces.
	 *  Always rotated to point from the centroid toward the move target (even a
	 *  straight 180° reverse): the formation pivots to face where it's going. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker|Formation")
	FFixedQuaternion Facing;
};

/**
 * The resolved target of a movement/formation order — the input a `USeinFormation`
 * consumes to lay out members. Bundles the gesture-produced GUIDE geometry (the
 * serialized part, carried on the order) with the formation's current centroid /
 * facing (filled by the resolver at solve time, NOT serialized).
 *
 * The guide is an ordered point list — the universal payload across order shapes:
 *   - empty or 1 point  → a simple click (point order)
 *   - 2 points          → a line (drag start → end)
 *   - N points          → a path / spline (drawn order)
 * A formation consumes only the parts it needs: a Blob ignores the guide, a Line
 * uses the endpoints, a path-march walks every point.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinOrderTarget
{
	GENERATED_BODY()

	/** Primary destination — the nav-projected click/anchor point. For a simple
	 *  click this is the whole order; for a guide it is the guide's representative
	 *  point (GuidePoints[0] by convention). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedVector Anchor;

	/** Ordered guide geometry (the drag path). Empty/single = simple click; two =
	 *  line; many = path. The serialized, gesture-produced part of the order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	TArray<FFixedVector> GuidePoints;

	/** Optional entity target (attack / repair). Formations usually ignore it (the
	 *  member's ability handles get-in-range); carried for completeness. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FSeinEntityHandle TargetEntity;

	/** Gesture-nominated formation identity. Invalid → the resolver's default
	 *  formation. The resolver maps this tag to a USeinFormation class. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FGameplayTag FormationTag;

	/** Formation's CURRENT centroid (source). Filled by the resolver at solve time
	 *  from broker data — NOT serialized on the order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedVector CurrentCentroid;

	/** Formation's CURRENT facing (source). Filled by the resolver at solve time —
	 *  NOT serialized on the order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedQuaternion CurrentFacing;
};
