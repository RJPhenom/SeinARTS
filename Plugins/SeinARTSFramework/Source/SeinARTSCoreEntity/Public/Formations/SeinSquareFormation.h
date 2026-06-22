/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquareFormation.h
 * @brief   Hollow square — members spaced evenly around a square OUTLINE about the anchor
 *          (Total-War "square"). A drag sets the outer half-side; squares fill outside-in and
 *          inner squares form automatically from footprint packing (square count automatic, not a
 *          preset), centre left empty. Footprint-spaced; a single member stands at the center. Drag-
 *          aware facing orients the square's sides.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinSquareFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Square Formation"))
class SEINARTSCOREENTITY_API USeinSquareFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** Defaults FacingMode to RadialOutward — members face away from the square centre. */
	USeinSquareFormation();

	/** EXTRA gap added to the footprint DIAMETER when spacing neighbours (UE world units, cm). 0 (the
	 *  default) = footprints touch — the densest non-overlapping spacing; raise to open the square up.
	 *  Also added to the gap between concentric layers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::Zero;

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
