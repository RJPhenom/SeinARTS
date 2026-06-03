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
		bool bInvertWhenBackward) override;

	/**
	 * Compute the formation's anchor-facing for a move/attack order.
	 *
	 * Facing ALWAYS rotates the formation so its forward axis points from
	 * `CurrentCentroid` → `TargetLocation` — every move, including a straight 180°
	 * reverse; the formation pivots to face where it's going. (Move to where the
	 * formation already stands → keeps `CurrentFacing` rather than degenerating
	 * to identity.)
	 *
	 * Anti-cross flag (`bInvertWhenBackward = true` AND move dot current forward
	 * < 0): sets `bAntiCrossReorder = true`. This does NOT touch facing — it tells
	 * the squad resolver to re-match members to slots by current left/right rank
	 * so a reverse move doesn't make members cross paths. Default resolver's
	 * symmetric grid ignores it.
	 *
	 * Pure compute — no world state read or written. Static so preview consumers
	 * (no resolver instance) can call it directly without instantiating.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Compute Formation Facing"))
	static FSeinFormationFacing ComputeFormationFacing(
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation,
		bool bInvertWhenBackward);
};
