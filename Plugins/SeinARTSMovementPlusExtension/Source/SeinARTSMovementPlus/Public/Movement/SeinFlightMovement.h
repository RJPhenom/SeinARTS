/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFlightMovement.h
 * @brief   Fixed-wing aerial movement — flying wheeled semantics. Planes,
 *          jets, anything that can't stop in mid-air.
 *
 *          - Always moving — minimum speed enforced via `MinSpeedRatio`
 *            (e.g., 0.6 x MoveSpeed). Planes physically can't stall.
 *          - Bicycle-like turning (turn radius coupled to speed).
 *          - Bypasses A* — flies straight-line through static obstacles.
 *          - Z = SurfaceZ + Altitude — auto-clears whatever's in the cell.
 *          - Altitude lerps toward CruiseAltitude.
 *          - On arrival: speed stays at MinSpeed; the unit drifts past
 *            the destination. Higher-level idle behavior is the AI
 *            controller's job (out of scope).
 *          - No kinematic arrival brake (plane keeps flying).
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "SeinFlightMovement.generated.h"

struct FSeinMovementComponent;

UCLASS(meta = (DisplayName = "Flight (Fixed-Wing)"))
class SEINARTSMOVEMENTPLUS_API USeinFlightMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	USeinFlightMovement();

	/** Preferred altitude offset above the cell surface (world units).
	 *  Fixed-wing cruise is typically much higher than helicopter — fighters
	 *  ~600, bombers ~800. Effective altitude is `max(CruiseAltitude,
	 *  AltitudeClearanceThreshold)`. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.0"))
	FFixedPoint CruiseAltitude;

	/** Minimum altitude offset above the cell surface (world units).
	 *  Floor for safety; the unit will not descend below this even if
	 *  `CruiseAltitude` is set lower. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeClearanceThreshold;

	/** Vertical climb / descent rate (world units per second). Smoothly
	 *  closes the gap between current Altitude and target. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeChangeRate;

	/** Distance between front and rear axles (world units) — analogous to
	 *  wheeled vehicles. Smaller = tighter turn radius for a given speed.
	 *  For aircraft, this loosely models "turning circle." */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "1.0"))
	FFixedPoint Wheelbase;

	/** Maximum bank angle in radians (+/-). Larger = tighter possible turns. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.0"))
	FFixedPoint MaxSteerAngle;

	/** How fast the steer angle interpolates toward its desired value
	 *  (1/seconds). Higher = snappier banking; lower = gradual. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.1"))
	FFixedPoint SteerResponse;

	/** Look-ahead distance along the (straight-line) path for the steering
	 *  carrot. Fighter aircraft track at 400-800 typically. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance;

	/** Minimum forward speed as a fraction of MoveSpeed (0..1). The plane
	 *  will not decelerate below `MoveSpeed * MinSpeedRatio` even on
	 *  arrival — fixed-wing aircraft can't stall in mid-air. Default 0.6. */
	UPROPERTY(EditAnywhere, Category = "Flight", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	FFixedPoint MinSpeedRatio;

	virtual bool BypassPathfinding() const override { return true; }
	virtual bool QueryReferenceZ(class USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;

	/** Per-class sub-data this movement consumes (FSeinFlyingMovementData) —
	 *  holds the persistent current Altitude that lerps toward CruiseAltitude
	 *  across move orders. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Altitude hook for ApplyGroundSnapAndAltitude. Reads
	 *  `FSeinFlyingMovementData::Altitude` (the lerped runtime altitude
	 *  written by Tick) out of the polymorphic sub-data. */
	virtual FFixedPoint GetAltitude(const FSeinMovementComponent* MovementData) const override;

protected:

	/** Per-instance current bank angle (radians, +/- MaxSteerAngle). Reset on
	 *  OnMoveBegin — bank settles to neutral at the start of each new order. */
	FFixedPoint CurrentSteer = FFixedPoint::Zero;
};
