/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDefaultCommandBrokerResolver.h
 * @brief   Default resolver — capability-map-filtered dispatch with
 *          anchor-aligned uniform-spacing positions (DESIGN §5).
 *
 *          Per-member dispatch rule:
 *            1. If the member can service the order's ContextTag, dispatch
 *               that ability against the order target.
 *            2. Otherwise, fall back to SeinARTS.Ability.Move toward the
 *               formation position for that member (non-combatants tagging
 *               along with an attack order, etc.).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "SeinDefaultCommandBrokerResolver.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Default Command Broker Resolver"))
class SEINARTSCOREENTITY_API USeinDefaultCommandBrokerResolver : public USeinCommandBrokerResolver
{
	GENERATED_BODY()

public:
	/** World-space spacing between units in the uniform grid formation. Scale in
	 *  UE world units (cm). 150 ≈ one infantryman's personal-space radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation")
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	/** Formation-level slot RE-MATCH on the LATERAL (left/right) axis. OPT-OUT, default true.
	 *  When a non-squad selection moves, members are re-matched to the grid slots by left/right rank
	 *  so a rotating formation doesn't make everyone cross to their old index slot. Clear to fall back
	 *  to raw index order. (Squads carry their OWN per-squad opt-IN flags on FSeinSquadComponent.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Reassign Slots Lateral"))
	bool bReassignSlotsLateral = true;

	/** Formation-level slot RE-MATCH on the DEPTH (front/back) axis. OPT-OUT, default true.
	 *  With Lateral (the default), the grid gets a full 2-D nearest-slot assignment so a deep block
	 *  converges without crossing front-to-back as well as left-to-right. Clear to drop back to a 1-D
	 *  (lateral-only) match, or clear both for raw index order. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Reassign Slots Depth"))
	bool bReassignSlotsDepth = true;

	/** Ability tag dispatched for members whose own DefaultCommands table doesn't
	 *  resolve the click context (ResolveMemberAbility returned invalid). Targets
	 *  the formation slot, not the order target — "tag along with the group" for
	 *  non-combatants on an attack order, unmapped click types, etc. Set to an
	 *  invalid tag to disable the tag-along behavior entirely (those members
	 *  stay idle for the order). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Dispatch",
		meta = (Categories = "SeinARTS.Ability"))
	FGameplayTag TagAlongAbility;

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;

	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing) override;

	virtual FSeinFormationLayout ResolveFormationLayout_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation,
		bool bReassignLateral,
		bool bReassignDepth) override;

	/**
	 * Compute the formation's anchor-facing for a move/attack order.
	 *
	 * Facing ALWAYS rotates the formation so its forward axis points from
	 * `CurrentCentroid` → `TargetLocation` — every move, including a straight 180°
	 * reverse; the formation pivots to face where it's going. (Move to where the
	 * formation already stands → keeps `CurrentFacing` rather than degenerating
	 * to identity.)
	 *
	 * Pure compute — no world state read or written. Static so preview consumers
	 * (no resolver instance) can call it directly without instantiating.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Compute Formation Facing"))
	static FFixedQuaternion ComputeFormationFacing(
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation);

protected:
	/** Re-match members to formation slots so a rotating/translating formation doesn't make units
	 *  cross paths. Per-axis, driven by the two designer flags:
	 *    - bLateral only → 1-D rank match on the formation RIGHT axis (preserve left/right order;
	 *                      front/back untouched — the wedge/arrow behavior).
	 *    - bDepth only   → 1-D rank match on the formation FORWARD axis (preserve front/back order).
	 *    - both          → 2-D nearest-slot assignment (greedy min-distance in centroid-aligned local
	 *                      space; non-crossing for deep grids — the line/block behavior).
	 *    - neither       → no-op (keep ResolvePositions' index order / authored slots as-is).
	 *  Reads each member's CURRENT position and permutes `Positions` in place so `Positions[i]` becomes
	 *  member i's slot — path-independent, so consecutive moves don't oscillate.
	 *
	 *  DETERMINISTIC: every sort carries a handle/slot-index tie-break (TOTAL order → bit-identical
	 *  across clients). A magnitude-only sort is unstable on ties → order-dependent → lockstep desync.
	 *  The 2-D path aligns the member/slot clouds by their own centroids before squaring distances, so
	 *  the bulk move translation cancels (a unit half a map from its slot can't overflow 32.32). */
	static void ReassignSlots(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& Positions,
		FFixedQuaternion FormationFacing,
		bool bLateral,
		bool bDepth);
};
