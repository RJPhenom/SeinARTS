/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinColumnFormation.h
 * @brief   Single-file column — members line up one-behind-another (1 wide, N deep)
 *          along the formation's depth axis, trailing behind the lead. The marching-
 *          column counterpart to USeinLineFormation's single rank. Drag-aware: a
 *          right-click-drag faces forward over the guide line with the file trailing
 *          behind it (centered on the line midpoint); a plain click faces the move
 *          target and centers on the anchor.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinColumnFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Column Formation"))
class SEINARTSCOREENTITY_API USeinColumnFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	/** World-space spacing between consecutive units in the file (UE world units, cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
