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

	/** Preferred altitude offset above the cell surface (world units). Fighters ~600, bombers ~800.
	 *  Effective altitude is max(CruiseAltitude, AltitudeClearanceThreshold). (Moved off the class into
	 *  this UDS 2026-07-02 so all Flight tuning is authored in one place.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint CruiseAltitude = FFixedPoint::FromInt(600);

	/** Minimum altitude offset above the cell surface (world units) — the unit won't descend below
	 *  this even if CruiseAltitude is set lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeClearanceThreshold = FFixedPoint::FromInt(200);

	/** Vertical climb/descent rate (world units per second) — smoothly closes the gap between the
	 *  current altitude and the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeChangeRate = FFixedPoint::FromInt(100);

	/** Distance between front/rear axles (world units) — loosely models the aircraft's turning circle;
	 *  smaller = tighter turn radius for a given speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "1.0"))
	FFixedPoint Wheelbase = FFixedPoint::FromInt(300);

	/** Maximum bank angle in radians (±). Larger = tighter possible turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint MaxSteerAngle = FFixedPoint::Pi / FFixedPoint::FromInt(3);

	/** How fast the bank angle interpolates toward its desired value (1/seconds). Higher = snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.1"))
	FFixedPoint SteerResponse = FFixedPoint::FromInt(2);

	/** Look-ahead distance along the (straight-line) path for the steering carrot. Fighters ~400-800. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance = FFixedPoint::FromInt(500);

	/** Minimum forward speed as a fraction of TopSpeed (0..1) — a plane won't decelerate below
	 *  TopSpeed × this even on arrival (fixed-wing can't stall). Default 0.6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
	FFixedPoint MinSpeedRatio = FFixedPoint::FromInt(6) / FFixedPoint::FromInt(10);

	/** CURRENT runtime flight altitude (world units) above the ground-snapped Z —
	 *  the value the Flight Tick drives toward CruiseAltitude after take-off so
	 *  flying units climb instead of popping to it (editable as the initial value).
	 *  Persists across move orders — a flying unit retains altitude between commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Altitude = FFixedPoint::Zero;
};

FORCEINLINE uint32 GetTypeHash(const FSeinFlyingMovementData& C)
{
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	H = HashCombine(H, GetTypeHash(C.CruiseAltitude));
	H = HashCombine(H, GetTypeHash(C.AltitudeClearanceThreshold));
	H = HashCombine(H, GetTypeHash(C.AltitudeChangeRate));
	H = HashCombine(H, GetTypeHash(C.Wheelbase));
	H = HashCombine(H, GetTypeHash(C.MaxSteerAngle));
	H = HashCombine(H, GetTypeHash(C.SteerResponse));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.MinSpeedRatio));
	H = HashCombine(H, GetTypeHash(C.Altitude));
	return H;
}
