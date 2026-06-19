/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGridFormation.h
 * @brief   Grid formation — a uniform square-ish grid centered on the anchor,
 *          rotated to face the move direction, each slot nav-projected.
 *
 *          What the removed `bFormationSpreadEnabled = true` used to produce; now
 *          an opt-in formation a project selects on the broker resolver. The
 *          anti-cross slot re-match still runs afterward in the resolver.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinGridFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Grid Formation"))
class SEINARTSCOREENTITY_API USeinGridFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** World-space spacing between units in the grid. Scale in UE world units (cm).
	 *  150 ≈ one infantryman's personal-space radius. (Moved here from the default
	 *  broker resolver's InterUnitSpacing.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
