/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinHoverMovementData.h
 * @brief:   Per-class movement data for `USeinHoverMovement`. Surfaces in
 *           the entity bridge via `FSeinMovementComponent::MovementClassData`
 *           when the designer picks USeinHoverMovement as the movement
 *           class.
 *
 *           Marked with `SeinSubData` so it appears in the polymorphic
 *           sub-data picker on FSeinMovementComponent::MovementClassData
 *           but is filtered out of the entity bridge's top-level
 *           ComponentData picker.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "SeinHoverMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinHoverMovementData : public FSeinComponent
{
	GENERATED_BODY()

	/** Hover altitude (world units) above the ground-snapped Z. Hover
	 *  movement subclasses lerp toward this value over time so units don't
	 *  pop to altitude. Persists across move orders — a hovering unit
	 *  retains altitude between commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Altitude = FFixedPoint::Zero;
};

FORCEINLINE uint32 GetTypeHash(const FSeinHoverMovementData& C)
{
	return GetTypeHash(C.Altitude);
}
