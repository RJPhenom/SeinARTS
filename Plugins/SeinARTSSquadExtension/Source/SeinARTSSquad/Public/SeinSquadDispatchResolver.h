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
	/** NOTE: the per-squad slot RE-MATCH toggles (Reassign Slots Lateral / Depth)
	 *  live on FSeinSquadComponent, not on the resolver — they're per-squad
	 *  behavioral features. ResolveDispatch reads them from the squad's component
	 *  data and passes them into the (inherited) ResolveFormationLayout. */

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;

	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing) override;
};
