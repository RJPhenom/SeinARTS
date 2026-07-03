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

	/** Acceleration rate (world units per second²) — current speed ramps UP toward the target
	 *  (feeds StepSpeedToward). Moved off the bare FSeinMovementComponent 2026-07-02. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Acceleration = FFixedPoint::FromInt(750);

	/** Deceleration rate (world units per second²) — current speed ramps DOWN, and the kinematic
	 *  arrival-brake rate into the final waypoint. Typically >= Acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Deceleration = FFixedPoint::FromInt(750);

	/** Preferred altitude offset above the cell surface (world units). Effective altitude is
	 *  max(CruiseAltitude, AltitudeClearanceThreshold). Choppers ~150, gunships ~200. (Moved off the
	 *  class into this UDS 2026-07-02 so all Hover tuning is authored in one place.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint CruiseAltitude = FFixedPoint::FromInt(200);

	/** Hard minimum altitude offset above the cell surface (world units) — the unit won't descend
	 *  below this even if CruiseAltitude is set lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeClearanceThreshold = FFixedPoint::FromInt(100);

	/** Vertical climb/descent rate (world units per second) — smoothly closes the gap between the
	 *  current altitude and the target altitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeChangeRate = FFixedPoint::FromInt(200);

	/** Look-ahead distance along the (straight-line) path for the steering carrot. Short = nimble;
	 *  long = ponderous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance = FFixedPoint::FromInt(200);

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
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	H = HashCombine(H, GetTypeHash(C.CruiseAltitude));
	H = HashCombine(H, GetTypeHash(C.AltitudeClearanceThreshold));
	H = HashCombine(H, GetTypeHash(C.AltitudeChangeRate));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.Altitude));
	return H;
}
