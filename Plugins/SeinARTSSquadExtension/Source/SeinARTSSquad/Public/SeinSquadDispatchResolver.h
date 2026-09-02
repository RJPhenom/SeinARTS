/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadDispatchResolver.h
 * @brief   Specialized broker resolver for the squad-as-sub-broker pattern.
 *          Reads per-slot OffsetTransforms from FSeinSquadPayload for formation
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
 *     matches leader-performs semantics — the squad leader performs the ability.
 *     Right-click smart-command
 *     orders fall through to the default per-member context-resolution path.
 *     Squad smart-orders also DROP the gesture guide/formation tag so squads stay
 *     slot-driven (ignore drag-formations).
 *
 *   - selects USeinSlotFormation as its DefaultFormationClass (in the constructor),
 *     so members lay out at their squad's authored per-slot OffsetTransforms through
 *     the formation pipeline (this replaced a ResolvePositions override). Unresolved
 *     members / unauthored squads fall back to a blob at the anchor.
 *
 * Designers can subclass further or replace entirely via
 * `FSeinSquadPayload::DispatchResolverClass`.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Squad Dispatch Resolver"))
class SEINARTSSQUAD_API USeinSquadDispatchResolver : public USeinDefaultCommandBrokerResolver
{
	GENERATED_BODY()

public:
	/** NOTE: the per-squad slot RE-MATCH toggles (Reassign Slots Lateral / Depth)
	 *  live on FSeinSquadPayload, not on the resolver — they're per-squad
	 *  behavioral features. ResolveDispatch reads them from the squad's component
	 *  data and passes them into the (inherited) ResolveFormationLayout. */

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;

	/** Default the formation to USeinSlotFormation so members lay out at their
	 *  squad's authored per-slot offsets (this replaced the old ResolvePositions
	 *  override). Squads ignore the gesture formation tag (the ResolveDispatch
	 *  guard drops it), so this default always wins — squads stay slot-driven. */
	USeinSquadDispatchResolver();
};
