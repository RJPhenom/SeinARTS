/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBoxFormation.h
 * @brief   Rank-and-file rectangular block formation. The order guide's two
 *          endpoints define the FRONT rank's width (= the drag length); members
 *          fill ranks behind the front (toward the group's current centroid) to
 *          fit N. The front faces perpendicular to the line, away from the centroid.
 *
 *          This is the default right-click-drag formation (nominated by
 *          SeinARTS.Formation.Box). Degrades gracefully: a short drag / few units →
 *          a single sparse rank spanning the drag.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinBoxFormation.generated.h"

/**
 * Arranges a selection into a rank-and-file rectangular block when you order them to move. On a
 * click-drag the line you draw becomes the block's FRONT edge, and the units pack into ranks behind
 * it; on a plain click you get a square-ish block on the cursor. The biggest units sit front-and-
 * centre with smaller ones filling the flanks and rear ranks.
 *
 * This is a BOX formation shape: a footprint-aware rank block. It uses the same tight cell-grid
 * packer as the Grid Formation, but instead of aiming for a square aspect it sets the front-rank
 * WIDTH from the drag length, so a wide drag makes a wide, shallow block and a short drag makes a
 * narrow, deep one. On a drag, the drawn line is the front edge: its length sets the front width and
 * its perpendicular (fixed handedness) sets the facing, and the body packs BEHIND the line, centred
 * on the line's midpoint so the front rank spans the drawn width. On a plain click with no drag, the
 * front width falls back to square-ish and the block centres on the cursor, facing the move direction
 * (identical to the Grid Formation). Inter Unit Spacing (in UE world units / centimeters) is the
 * MINIMUM cell size, so a designer can open the ranks up wider than the tight footprint packing but
 * never tighter. Every slot is projected onto walkable navigation so nobody is placed inside a wall or
 * off the grid. This is the default right-click-drag formation (nominated by SeinARTS.Formation.Box),
 * and it degrades gracefully: a short drag or a handful of units collapses to a single sparse front
 * rank spanning the drag.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Box Formation"))
class SEINARTSCOREENTITY_API USeinBoxFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	virtual bool IsStatelessExecutionAdmitted(FString& OutError) const override
	{
		return AdmitStatelessNativeAnchor(StaticClass(), OutError);
	}

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
