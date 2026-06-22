/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRingFormation.h
 * @brief   Ring — members spaced evenly around a circle about the anchor (defensive
 *          ring / "hold this point"). A drag sets the outer radius; rings fill outside-in and
 *          inner rings form automatically from footprint packing (ring count automatic, not a
 *          preset), centre left empty. Footprint-spaced; a single member stands at the center.
 *          Drag-aware facing (cosmetic for a symmetric ring).
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
	/** EXTRA gap added to the footprint DIAMETER when spacing neighbours (UE world units, cm). 0 (the
	 *  default) = footprints touch — the densest non-overlapping spacing; raise to open the ring up.
	 *  Also added to the radial gap between concentric layers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::Zero;

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
