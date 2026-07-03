/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquareFormation.h
 * @brief   Hollow square — members spaced evenly around a square OUTLINE about the anchor
 *          (a hollow rank-and-file square). A drag sets the outer half-side; squares fill outside-in and
 *          inner squares form automatically from footprint packing (square count automatic, not a
 *          preset), centre left empty. Footprint-spaced; a single member stands at the center. Drag-
 *          aware facing orients the square's sides.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinSquareFormation.generated.h"

/**
 * Arranges the selected units in a hollow square: bodies line the edges of a square outline
 * around the target point while the middle stays empty, like a massed-infantry square. A
 * click-drag sets how big the square is; a lone unit just stands in the center.
 *
 * Members are placed evenly along a square OUTLINE about the anchor, not filled solid. A drag
 * sets the outer half-side (the distance from centre to a side, in UE world units, cm); rings
 * fill outside-in and additional concentric squares form automatically from footprint packing
 * (the number of nested squares is derived from how many members fit, not a preset), leaving the
 * centre open. Spacing is footprint-aware so bodies never overlap, and drag-aware facing orients
 * which way the square's sides point. Defaults FacingMode to RadialOutward so members face away
 * from the centre.
 */
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
