/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadDispatchResolver.h
 * @brief   Specialized broker resolver for the squad-as-sub-broker pattern.
 *          Reads per-slot OffsetTransforms from FSeinSquadComponent for formation
 *          positions and dispatches predetermined-ability orders to the
 *          leader first (fallback first capable).
 */

#pragma once

#include "CoreMinimal.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "SeinSquadDispatchResolver.generated.h"

/**
 * Squad-aware broker resolver. Subclass of the default resolver — overrides:
 *
 *   - `ResolveDispatch` for predetermined-ability orders so the leader dispatches
 *     the squad-level ability instead of "first capable in member order." This
 *     matches CoH "the squad leader throws the smoke." Right-click smart-command
 *     orders fall through to the default per-member context-resolution path.
 *
 *   - `ResolvePositions` to read each member's slot offset off the squad's
 *     FSeinSquadComponent and use it (rotated by anchor facing) as the formation
 *     position. Members without a resolvable slot fall back to the default
 *     resolver's grid layout (defensive — shouldn't happen for properly-wired
 *     squads).
 *
 * Designers can subclass further or replace entirely via
 * `FSeinSquadComponent::DispatchResolverClass`.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Squad Dispatch Resolver"))
class SEINARTSSQUAD_API USeinSquadDispatchResolver : public USeinDefaultCommandBrokerResolver
{
	GENERATED_BODY()

public:
	/** NOTE: the "invert slot order when moving backward" toggle moved to
	 *  FSeinSquadComponent::bInvertSlotOrderWhenMovingBackward — it's a
	 *  per-squad behavioral feature, not a resolver tunable. The resolver
	 *  reads that flag from the squad's component data at dispatch time.
	 *  Designers configure it on the Squad Component in the BP editor. */

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;

	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing) override;

	/** Overrides the default impl to apply the squad's authored slot-mirror
	 *  for backward-walk: when bInvertWhenBackward is set AND the move heading
	 *  is roughly opposite the squad's current facing, the formation keeps its
	 *  current facing (handled by ComputeFormationFacing) AND the per-member
	 *  positions are mirrored across the anchor's forward axis so the squad's
	 *  authored "front row" ends up on the leading edge of the destination.
	 *  Without the mirror the kept-facing path would walk the squad's BACK row
	 *  to the leading edge — visible spaghetti. */
	virtual FSeinFormationLayout ResolveFormationLayout_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation,
		bool bInvertWhenBackward) override;
};
