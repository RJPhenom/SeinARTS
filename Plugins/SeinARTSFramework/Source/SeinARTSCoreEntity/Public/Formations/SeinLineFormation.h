/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLineFormation.h
 * @brief   Line formation — members spread evenly along the order's guide line
 *          (Total War / Rise of Nations style), facing perpendicular to it.
 *
 *          The reference formation that consumes an order's GuidePoints: a
 *          right-click-drag (start→end) lays the members out along that line.
 *          With < 2 guide points (a plain click) it degrades to a blob at the
 *          anchor. Bound to SeinARTS.Formation.Line in the default resolver's
 *          FormationsByTag, which the default order gesture nominates on a drag.
 */

#pragma once

#include "CoreMinimal.h"
#include "Formations/SeinFormation.h"
#include "SeinLineFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Line Formation"))
class SEINARTSCOREENTITY_API USeinLineFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** Facing rule. True (default): members face perpendicular to the line, on the
	 *  side away from where the group came from (battle-line feel). False: members
	 *  face the move direction (current centroid → line midpoint). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Face Perpendicular"))
	bool bFacePerpendicular = true;

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
