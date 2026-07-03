/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWedgeFormation.h
 * @brief   Wedge / arrowhead as HOLLOW nested chevrons — a tip at the front with two arms fanning
 *          back-left / back-right. Deeper chevrons emerge automatically from footprint packing when
 *          the selection won't fit the drawn front (short drag -> more layers; a plain click is one
 *          tight chevron), the apexes stepping back so the chevrons nest, instead of one ballooning
 *          V. Footprint-spaced along the arms. Drag-oriented (tip forward over the guide
 *          line, body behind); a plain click faces the move target.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinWedgeFormation.generated.h"

/**
 * Arranges the selected units into a wedge, or arrowhead: a single unit at the tip out front with
 * two arms fanning back to the left and right. On a click-drag the tip sits forward over the guide
 * line and the body trails behind it; a plain click points the wedge at the move target.
 *
 * This is a WEDGE / chevron formation shape, built as HOLLOW nested chevrons rather than one solid
 * filled triangle. The tip anchors the front and units are footprint-spaced outward along each arm.
 * Each arm's angle off the wedge's back axis is set by Half Angle Degrees (10-80 degrees; 45 gives a
 * right-angle chevron, smaller is a sharper/narrower wedge, larger is flatter/wider). When the
 * selection has more units than the drawn front can hold, deeper chevrons emerge automatically from
 * footprint packing (a short drag yields more layers; a plain click is one tight chevron), with each
 * layer's apex stepping back so the chevrons nest cleanly instead of one arm ballooning outward.
 * Inter Unit Spacing (in UE world units / centimeters, default 0 = footprints touch for the densest
 * non-overlapping packing) adds extra gap both along the arms and between nested layers. All spacing
 * uses each unit's real footprint so nobody overlaps.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Wedge Formation"))
class SEINARTSCOREENTITY_API USeinWedgeFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** EXTRA gap added to the footprint DIAMETER when spacing units along the chevron arms (UE world
	 *  units, cm). 0 (the default) = footprints touch — the densest non-overlapping spacing; raise to
	 *  open the arms up. Also added to the perpendicular gap between nested chevron layers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::Zero;

	/** Half-angle of each arm from the wedge's back axis, in degrees. 45 = a right-angle chevron;
	 *  smaller = a sharper/narrower wedge, larger = a flatter/wider one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Half Angle Degrees", ClampMin = "10.0", ClampMax = "80.0", UIMin = "20.0", UIMax = "70.0"))
	FFixedPoint HalfAngleDegrees = FFixedPoint::FromInt(45);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
