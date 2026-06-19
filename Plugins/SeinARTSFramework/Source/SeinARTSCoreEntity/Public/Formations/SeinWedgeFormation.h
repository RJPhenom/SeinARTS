/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWedgeFormation.h
 * @brief   Wedge / arrowhead — a lead member at the tip with two arms fanning back-
 *          left and back-right behind it (V shape, point forward). Drag-aware: the
 *          tip sits on the guide-line midpoint facing forward over it, arms trailing
 *          behind; a plain click faces the move target. Arms spread at ~45° per side.
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
	/** World-space step between wedge ranks, in depth and to the side (UE world units, cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
