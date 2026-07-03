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

/**
 * Arranges the selected units in a ring (or nested rings) around the order point, leaving the
 * centre empty. Good for a defensive "hold this point" perimeter; a single unit just stands at
 * the middle. On a click-drag the drag length sets the ring's outer radius; a plain click packs
 * the units as tightly as possible.
 *
 * Uses concentric-ring packing with chord-based angular spacing. Each unit's footprint radius
 * (plus half of the optional Inter Unit Spacing margin, in UE world units / cm) is turned into an
 * angular width on its ring via 2*asin(radius / ringRadius); a ring is full when those widths sum
 * to 2*pi, so neighbours touch by true centre-to-centre chord distance and never overlap. Rings
 * fill OUTSIDE-IN — the outer perimeter packs tight and full first, then each inner ring steps in
 * by one full unit-diameter (so adjacent rings clear), and only the innermost ring is ever partial
 * (its leftover slack is spread evenly around the circle). The ring COUNT is emergent from how many
 * units the footprints require, not a preset. A drag is honoured fully (it may leave one sparse
 * ring if the units can't fill the drawn circle) but is never shrunk below the most-compact nested
 * packing; a plain click always uses that most-compact packing. All trig is deterministic
 * fixed-point. Facing defaults to RadialOutward (units face away from the centre); on a drag the
 * facing is cosmetic because the ring is symmetric.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Ring Formation"))
class SEINARTSCOREENTITY_API USeinRingFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** Defaults FacingMode to RadialOutward — members face away from the ring centre. */
	USeinRingFormation();

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
