/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRingFormation.h
 * @brief   Ring — members spaced evenly around a circle about the anchor (defensive
 *          ring / "hold this point"). Radius scales with N so neighbours sit about one
 *          spacing apart, clamped to a minimum. Drag-aware facing (cosmetic for a
 *          symmetric ring); a single member just stands at the center.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinRingFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Ring Formation"))
class SEINARTSCOREENTITY_API USeinRingFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** Target arc spacing between neighbours around the ring (UE world units, cm). The
	 *  radius is derived so the circumference fits N units at this spacing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
