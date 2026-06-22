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
