/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinFlyingMovementData.h
 * @brief:   Per-class movement data for `USeinFlightMovement`. Surfaces in
 *           the entity bridge via `FSeinMovementComponent::MovementClassData`
 *           when the designer picks USeinFlightMovement as the movement
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
#include "SeinFlyingMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinFlyingMovementData : public FSeinComponent
{
	GENERATED_BODY()

	/** Cruise altitude (world units) above the ground-snapped Z. Flight
	 *  movement subclasses lerp toward this value so flying units climb to
	 *  altitude after take-off instead of popping to it. Persists across
	 *  move orders — a flying unit retains altitude between commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Altitude = FFixedPoint::Zero;
};

FORCEINLINE uint32 GetTypeHash(const FSeinFlyingMovementData& C)
{
	return GetTypeHash(C.Altitude);
}
