/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareDefaultBrokerResolver.h
 * @brief   Default broker resolver with automatic cover-snap.
 *
 *          Overrides `PostProcessPositions` to substitute cover-slot positions
 *          for members carrying the `SeinARTS.Cover.UsesCover` tag, when those
 *          slots are within the configured search radius of the move target.
 *          Members without the tag (vehicles, aircraft, etc.) and members
 *          that didn't find a free slot keep their default formation positions.
 *
 *          Designer enables cover-snap by pointing
 *          `USeinARTSCoreSettings::DefaultBrokerResolverClass` at this class.
 *          Without the cover module loaded, this class doesn't exist —
 *          existing settings fall back to the framework default and behavior
 *          is unchanged.
 */

#pragma once

#include "CoreMinimal.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Types/FixedPoint.h"
#include "SeinCoverAwareDefaultBrokerResolver.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Cover-Aware Default Broker Resolver"))
class SEINARTSCOVER_API USeinCoverAwareDefaultBrokerResolver : public USeinDefaultCommandBrokerResolver
{
	GENERATED_BODY()

public:
	// Tuning lives in Project Settings → SeinARTS Plugin → Cover:
	//   * Cover Snap Radius — distance gate around the move target.
	// Cursor-side preference is deterministic policy, not a second radius. The
	// shared planner maximizes coverage, then minimizes wrong-side use and total
	// squared distance. Reading the shared snap radius from Cover settings
	// keeps the default and optional Squad bridge resolvers aligned.
	// Designers wanting per-resolver overrides can subclass and override
	// PostProcessPositions to read different values.

	virtual void PostProcessPositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation) override;
};
