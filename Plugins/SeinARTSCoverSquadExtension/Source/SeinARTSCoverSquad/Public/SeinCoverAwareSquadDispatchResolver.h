/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareSquadDispatchResolver.h
 * @brief   Squad dispatch resolver with automatic cover-snap.
 *
 *          Lives in the optional Cover + Squad bridge plugin. It provides the
 *          same cover-snap behavior as USeinCoverAwareDefaultBrokerResolver
 *          but inherits from USeinSquadDispatchResolver so squads with
 *          authored slot offsets get cover-snap on top of their per-slot
 *          formation positions + the backward-walk slot mirror. Designer
 *          enables per-squad by pointing
 *          `FSeinSquadComponent::DispatchResolverClass` at this class.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinSquadDispatchResolver.h"
#include "Types/FixedPoint.h"
#include "SeinCoverAwareSquadDispatchResolver.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Cover-Aware Squad Dispatch Resolver"))
class SEINARTSCOVERSQUAD_API USeinCoverAwareSquadDispatchResolver : public USeinSquadDispatchResolver
{
	GENERATED_BODY()

public:
	// Tuning lives in Project Settings → SeinARTS Plugin → Cover:
	//   * Cover Snap Radius — distance gate around the move target.
	// Cursor-side preference is a deterministic preferred-pass plus wrong-side
	// capacity fallback. See SeinCoverAwareDefaultBrokerResolver.h for the full
	// rationale on sharing the radius instead of duplicating BP CDO tuning.

	virtual void PostProcessPositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation) override;
};
