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
	// Cover's shared deterministic planner maximizes coverage, then minimizes
	// wrong-side use and total squared distance. See the default cover-aware
	// resolver for the rationale on sharing one radius setting.

	virtual void PostProcessPositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation) override;
};
