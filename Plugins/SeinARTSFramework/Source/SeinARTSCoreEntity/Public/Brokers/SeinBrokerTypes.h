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
#include "Core/SeinPlayerID.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Abilities/SeinTargeterTypes.h"
#include "SeinBrokerTypes.generated.h"

class USeinFormation;

namespace SeinBrokerOrderProtocol
{
	constexpr int32 SchemaVersion = 4;
	constexpr int32 MaxMembers = 4096;
	constexpr int32 MaxGuidePoints = 4096;
	constexpr int32 MaxTargeterPoints = 256;
	constexpr int32 MaxDestinationArtifactEntries = 4096;
	constexpr int32 MaxRecipientPlanEntries = 4096;
	constexpr int32 MaxQueuedOrdersPerBroker = 8192;
	constexpr int32 MaxPayloadBytes = 384 * 1024;
	constexpr int32 MaxAggregateContainerEntries =
		MaxGuidePoints + MaxTargeterPoints
		+ MaxDestinationArtifactEntries + MaxRecipientPlanEntries;
}

/**
 * One member's exact destination captured from the displayed selection plan.
 * WorldPosition is a value, never a live follow binding. SourceEntity and
 * SourceIndex are provenance only: provider movement or destruction after
 * capture does not relocate or invalidate the destination.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFrozenDestination
{
	GENERATED_BODY()

	/** Unit that owns this destination. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	FSeinEntityHandle Member;

	/** Exact world-space point shown by preview and consumed by dispatch. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	FFixedVector WorldPosition;

	/** Standing footprint used by preview and, when reserved, contention tests. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	FFixedPoint FootprintRadius = FFixedPoint::Zero;

	/** True for provider-backed exact points that reserve their world footprint
	 *  while queued/executing and, after successful arrival, while the member
	 *  remains settled there. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	bool bReserveFootprint = false;

	/** Optional source provider at capture time. Provenance, not a live binding. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	FSeinEntityHandle SourceEntity;

	/** Optional stable item index inside SourceEntity at capture time. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Destination")
	int32 SourceIndex = INDEX_NONE;
};

/**
 * Canonical boundary for one original BrokerOrder recipient. The matching
 * members occupy the next MemberCount entries in DestinationArtifact. This
 * preserves broker boundaries across network input delay without storing a
 * live provider or membership binding.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerRecipientPlanSegment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FSeinEntityHandle Recipient;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	int32 MemberCount = 0;
};

/**
 * One queued order on a broker. Queue order is preserved for orders whose
 * effective member sets overlap; disjoint subset orders may run concurrently.
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

	/** Funding principal preserved only for an internal ability follow-up. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FSeinPlayerID DerivedResourcePayer;

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

	/** A2 unified-formation pre-placement. When non-empty, the broker dispatches each listed member
	 *  straight to its paired position instead of solving its own formation — these are the loose
	 *  subset of a UNIFIED parent formation already solved over the whole selection (squads + loose)
	 *  in ProcessCommands, so a mixed selection forms ONE shape. Parallel arrays keyed by handle
	 *  (PreplacedMembers[i] → PreplacedPositions[i]); empty = solve a formation normally. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> PreplacedMembers;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FFixedVector> PreplacedPositions;

	/** Exact admitted preview artifact. When non-empty this is authoritative over
	 *  the legacy Preplaced* arrays and survives queueing/snapshot continuation.
	 *  Reserved entries protect their frozen world footprint; source provenance
	 *  never turns them into moving targets. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinFrozenDestination> DestinationArtifact;

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

	/** A2 pre-placed goals (mirrors FSeinBrokerQueuedOrder): PreplacedMembers[i] → PreplacedPositions[i].
	 *  When non-empty, the default resolver dispatches each member to its pre-placed goal instead of
	 *  solving a formation (the unified parent formation already placed it). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> PreplacedMembers;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FFixedVector> PreplacedPositions;

	/** Exact admitted destinations (mirrors the queued order). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinFrozenDestination> DestinationArtifact;
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

	/** Original recipient boundaries for an exact DestinationArtifact. Empty is
	 *  valid only when DestinationArtifact is also empty. At deterministic
	 *  admission, each segment must still contain the same surviving members. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	TArray<FSeinBrokerRecipientPlanSegment> RecipientPlan;

	/** Optional exact selection plan captured from the visible preview. The sim
	 *  validates complete member coverage and either admits the whole artifact or
	 *  rejects it; it never silently recomputes a preview-changing fallback. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	TArray<FSeinFrozenDestination> DestinationArtifact;
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
 * Full side-effect-free resolver output for one order. The broker system
 * validates the complete plan against unchanged live state, then atomically
 * commits its optional layout output and execution state before enqueueing the
 * validated per-member ability commands.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerDispatchPlan
{
	GENERATED_BODY()

	/** Unique live dispatchers from the effective member set. The broker carrier
	 *  itself is the sole exception when it actually owns AbilityTag. Invalid,
	 *  duplicate, foreign, or schema-oversized output rejects the entire plan. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinBrokerMemberDispatch> MemberDispatches;

	/** Apply AnchorFacing from this plan when it commits. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout")
	bool bApplyAnchorFacing = false;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout",
		meta = (EditCondition = "bApplyAnchorFacing"))
	FFixedQuaternion AnchorFacing = FFixedQuaternion::Identity;

	/** Replace the broker's settled-slot state when this plan commits. Positions
	 *  may not outnumber broker members; facings must be index-aligned. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout")
	bool bApplySettledSlots = false;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout",
		meta = (EditCondition = "bApplySettledSlots"))
	TArray<FFixedVector> SettledSlotPositions;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout",
		meta = (EditCondition = "bApplySettledSlots"))
	TArray<FFixedQuaternion> SettledSlotFacings;

	/** True means slot i belongs to broker member i and therefore requires one
	 *  settled slot per broker member. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Layout",
		meta = (EditCondition = "bApplySettledSlots"))
	bool bSettledSlotsMemberAligned = false;
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
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Formation")
	TArray<FFixedVector> Positions;

	/** Per-member footprint radius (world units), index-aligned with Positions —
	 *  the radius the formation spaced each slot by. Emitted so the destination
	 *  preview can size each dot to the unit's footprint (preview === commit). May
	 *  be empty when a formation doesn't size by footprint; consumers then fall back
	 *  to a uniform dot size. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Formation")
	TArray<FFixedPoint> Radii;

	/** Formation's facing at the anchor — the direction the front rank faces.
	 *  Always rotated to point from the centroid toward the move target (even a
	 *  straight 180° reverse): the formation pivots to face where it's going. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Formation")
	FFixedQuaternion Facing;

	/** Per-member facing, index-aligned with Positions — position-DEPENDENT (a ring faces each member
	 *  radially out, etc.). Filled by the resolver after layout from the formation's FacingMode. Empty →
	 *  consumers fall back to the single `Facing`. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker|Formation")
	TArray<FFixedQuaternion> Facings;
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

	/** Optional explicit formation CLASS override. When set, the resolver uses THIS formation directly
	 *  (bypassing FormationTag / FormationsByTag) — e.g. a squad lays its members out with its authored
	 *  FSeinSquadComponent::FormationClass. Empty → resolve via FormationTag as usual. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	TSoftClassPtr<USeinFormation> FormationClass;

	/** Formation's CURRENT centroid (source). Filled by the resolver at solve time
	 *  from broker data — NOT serialized on the order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedVector CurrentCentroid;

	/** Formation's CURRENT facing (source). Filled by the resolver at solve time —
	 *  NOT serialized on the order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedQuaternion CurrentFacing;
};
