/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerResolver.h
 * @brief   Abstract Blueprintable UObject that decides, for a broker's member
 *          set + order context, which members run which abilities against
 *          which targets (DESIGN §5).
 *
 *          Designers subclass this for custom formation/dispatch behavior.
 *          Framework ships USeinDefaultCommandBrokerResolver (C++) as the
 *          default; projects override via `DefaultBrokerResolverClass` on
 *          `USeinARTSCoreSettings`.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinCommandBrokerResolver.generated.h"

class USeinWorldSubsystem;

/**
 * Decides, when an order lands on a broker (a squad or a multi-unit selection), which members run
 * which abilities against which targets and where each member stands. This is the abstract base you
 * subclass for custom formation and dispatch behavior; the framework ships the concrete default that
 * projects override via Default Broker Resolver Class in the SeinARTS Core settings.
 *
 * A broker fans one player order out to many members. Resolve Dispatch is the entry point: given the
 * broker handle and the order input, it walks the order's effective member set (a subset-aware slice
 * that honors the queued order's targeted-members field, not the broker's full roster) and returns a
 * dispatch plan of per-member (ability, target, position) tuples the broker system issues internally;
 * members left out of the plan are silently skipped. Resolve Member Ability is the per-member "which
 * ability does this member want for this click context" hook (default: consult the member's own
 * command table, highest-priority match, then fallback tag; return an invalid tag to skip the member).
 *
 * Formation layout is a pure, side-effect-free solver so the destination preview can render the exact
 * positions the player will get on commit. Resolve Formation Layout rotates the group's facing so its
 * forward axis points from the current centroid toward the target, lays members out on a symmetric
 * grid, then re-matches members to grid slots to stop paths crossing: the lateral / depth reassign
 * flags pick left-right rank matching, front-back rank matching, full 2-D nearest-slot matching, or
 * raw index order. Resolve Positions is the lower-level per-member placement (default returns the
 * anchor for every member; subclasses add ranks, class clusters, wedges, authored slots), and
 * Post Process Positions is a generic no-op extension pass to nudge those positions afterward (the
 * cover extension uses it to snap cover-using members onto nearby cover slots) — it must stay
 * deterministic (fixed-point only, no float or RNG) since it runs in sim command processing.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup = (SeinARTS),
	meta = (DisplayName = "Command Broker Resolver"))
class SEINARTSCOREENTITY_API USeinCommandBrokerResolver : public UObject
{
	GENERATED_BODY()

public:
	/** Explicit write barrier for a custom stateful resolver. Shipped resolver
	 *  hooks are pure by contract; a Blueprint that deliberately stores
	 *  future-affecting state must call this after the write. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Broker|State",
		meta = (DisplayName = "Mark Deterministic State Dirty"))
	void MarkDeterministicStateDirty(USeinWorldSubsystem* World);

	/**
	 * Per-member tag resolution hook — "which ability does THIS member want to
	 * run for THIS click context?" The default implementation delegates to the
	 * member's own `FSeinAbilityPayload::ResolveCommandContext` (walks the unit's
	 * DefaultCommands table, picks highest-priority match, falls back to
	 * FallbackAbilityTag). Override this when you want faction / state /
	 * relationship-based overrides without reimplementing the full dispatch
	 * plan — e.g. "workers prefer Repair over Move when right-clicking a
	 * damaged ally."
	 *
	 * Return an invalid tag to tell the broker this member cannot service
	 * this context — the member will be silently skipped for this order.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Member Ability"))
	FGameplayTag ResolveMemberAbility(
		USeinWorldSubsystem* World,
		FSeinEntityHandle Member,
		const FGameplayTagContainer& Context);
	virtual FGameplayTag ResolveMemberAbility_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle Member,
		const FGameplayTagContainer& Context);

	/**
	 * Resolve a broker order into per-member dispatches.
	 *
	 * Implementations iterate `Order.EffectiveMembers` (NOT the broker's full
	 * Members list — EffectiveMembers is subset-aware, honoring the queued
	 * order's TargetMembers field). For each effective member, call
	 * `ResolveMemberAbility` to pick the ability, decide target + position,
	 * and emit a dispatch tuple. Members that don't appear in the returned
	 * plan are silently skipped for this order. Resolver implementations must not
	 * mutate broker state. Return optional facing/settled-slot changes through the
	 * plan; the broker system validates and commits them transactionally.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Dispatch"))
	FSeinBrokerDispatchPlan ResolveDispatch(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order);
	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order);

	/**
	 * Compute desired world positions for the given members around an anchor.
	 * Returned array is index-aligned with `Members`. Default implementation
	 * returns `Anchor` for every member (no formation); subclasses override
	 * for tight-rank / class-cluster / wedge layouts.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Positions"))
	TArray<FFixedVector> ResolvePositions(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing);
	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing);

	/**
	 * Pure formation solver — given a centroid + current facing + target,
	 * compute (a) the formation's facing at the target and (b) per-member
	 * world positions around that target.
	 *
	 * Same logic the dispatch path uses internally to build `MemberDispatch`
	 * target locations, exposed as a standalone entry point so the destination
	 * preview decals (cover module's hover preview) can render the EXACT
	 * positions the player will get on right-click — no parallel "what would
	 * happen if I clicked here" implementation that drifts from dispatch.
	 *
	 * No side effects: implementations MUST NOT mutate broker data, member
	 * data, or any sim state. Pure compute. The dispatch path writes results
	 * back to broker data itself after calling.
	 *
	 * Default resolver: facing rotates so forward axis points centroid → target;
	 * positions come from the symmetric grid layout, then members are re-matched to
	 * slots per the two re-assign flags. Squad resolver: same, plus authored slot
	 * offsets via its ResolvePositions override.
	 *
	 * `bReassignLateral` / `bReassignDepth` select the anti-cross slot re-match (see
	 * `ReassignSlots`): lateral = left/right rank, depth = front/back rank, both =
	 * 2-D nearest-slot, neither = raw index order. The caller passes the formation-
	 * level opt-OUT flags (non-squad selections) or the squad's per-squad opt-IN
	 * flags (FSeinSquadPayload).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Formation Layout"))
	FSeinFormationLayout ResolveFormationLayout(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target,
		bool bReassignLateral,
		bool bReassignDepth);
	virtual FSeinFormationLayout ResolveFormationLayout_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target,
		bool bReassignLateral,
		bool bReassignDepth);

	/**
	 * Optional post-process pass on the per-member positions computed by
	 * `ResolvePositions`. Called inside `ResolveFormationLayout` after the
	 * default geometry is laid out. Default impl is a no-op — subclasses
	 * override to apply system-specific position adjustments without having
	 * to reimplement the whole layout pipeline.
	 *
	 * Designed as a generic extension point — not coupled to any one system.
	 * Current consumer: the cover module's cover-aware resolver subclasses
	 * use it to snap eligible squad members (those carrying the
	 * `SeinARTS.Cover.UsesCover` tag) onto nearby cover slots when moving.
	 * Future consumers could include: terrain-cost reposition, formation-
	 * morale clumping, group-cohesion enforcement.
	 *
	 * `InOutPositions` is index-aligned with `Members` and starts populated
	 * with whatever `ResolvePositions` returned. Subclasses can mutate any
	 * entries in place. Adding/removing entries is forbidden — the index
	 * alignment with Members is part of the contract.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Post Process Positions"))
	void PostProcessPositions(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation);
	/** Default: no-op. Runs in sim command-processing — overrides (C++ or
	 *  Blueprint) MUST be deterministic (fixed-point only; no float / RNG). */
	virtual void PostProcessPositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation) {}

	/**
	 * Shared helper for ability-level dispatch policy. Filters a candidate
	 * member set down to the actual dispatcher set per the ability's
	 * `DispatchMode` / `DispatchSelector` / `DispatchPreferredTag` /
	 * `DispatchFallback` fields.
	 *
	 * Both `USeinDefaultCommandBrokerResolver` and `USeinSquadDispatchResolver`
	 * call this from their predetermined-ability dispatch paths so the
	 * behavior is uniform across squad brokers and selection brokers — the
	 * design intent is that an ability authored once carries its dispatch
	 * shape into any broker context.
	 *
	 * Inputs:
	 *   - `Ability`: the ability instance whose policy we're applying. Read
	 *     via the broker's capability map (or freshly looked up). Null →
	 *     defaults to `All` (matches "no entry" semantics).
	 *   - `BrokerHandle`: the broker carrier entity. Used to look up
	 *     `FSeinSquadPayload` for squad-specific Leader semantics; non-squad
	 *     brokers degrade gracefully (Leader → first candidate in order).
	 *   - `Candidates`: the entities that hold an instance of the ability,
	 *     pre-intersected with the order's `EffectiveMembers` by the caller.
	 *     For squad brokers the squad-self handle may be present too.
	 *   - `World`: subsystem reference for entity-tag lookups (ByTag selector).
	 *
	 * Returns the dispatcher set. Empty = ability unavailable this frame.
	 */
	static TArray<FSeinEntityHandle> ApplyAbilityDispatchPolicy(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const class USeinAbility* Ability,
		const TArray<FSeinEntityHandle>& Candidates);
};
