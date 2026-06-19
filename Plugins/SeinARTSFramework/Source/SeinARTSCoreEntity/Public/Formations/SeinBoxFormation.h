/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBoxFormation.h
 * @brief   Total-War-style rectangular block formation. The order guide's two
 *          endpoints define the FRONT rank's width (= the drag length); members
 *          fill ranks behind the front (toward the group's current centroid) to
 *          fit N. The front faces perpendicular to the line, away from the centroid.
 *
 *          This is the default right-click-drag formation (nominated by
 *          SeinARTS.Formation.Box). Degrades gracefully: a short drag / few units →
 *          a column or a single sparse rank spanning the drag. Distinct from
 *          USeinLineFormation, which is a true single rank (no depth).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinBoxFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Box Formation"))
class SEINARTSCOREENTITY_API USeinBoxFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** World-space spacing between files (across the front) and ranks (in depth).
	 *  Scale in UE world units (cm). Front-rank column count = how many fit across
	 *  the drag width at this spacing (capped at N). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
