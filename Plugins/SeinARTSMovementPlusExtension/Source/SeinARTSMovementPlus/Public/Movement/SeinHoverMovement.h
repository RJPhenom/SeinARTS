/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinHoverMovement.h
 * @brief   Hovering aerial movement — flying tank semantics. Helicopters,
 *          gunships, dropships.
 *
 *          Behaves like USeinTrackedVehicleMovement but airborne:
 *          - Can stop in place (hover at zero horizontal speed).
 *          - Pivots to face target on the spot, then drives forward.
 *          - Full TurnRate at any speed (no bicycle coupling).
 *          - Bypasses A* — flies straight-line through static obstacles.
 *          - Z = SurfaceZ + Altitude — automatically clears anything in
 *            the cell because SurfaceZ stores the top of whatever's there
 *            (building roof, wall top, mountain peak).
 *          - Altitude lerps toward MoveData.Altitude target.
 *          - Uses Accel/Decel for smooth speed ramping.
 *          - Uses KinematicArrivalSpeedCap for braking.
 *          - Uses ResolveNavCollision (respects ground nav for XY).
 *          - Uses ApplyGroundSnapAndAltitude (with altitude offset).
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "SeinHoverMovement.generated.h"

struct FSeinMovementComponent;

UCLASS(meta = (DisplayName = "Hover (Helicopter)"))
class SEINARTSMOVEMENTPLUS_API USeinHoverMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	USeinHoverMovement();

	/** Preferred altitude offset above the cell surface (world units).
	 *  Helicopter cruise heights are typically 100-300; gunships ~200,
	 *  transport choppers ~150. Effective altitude is `max(CruiseAltitude,
	 *  AltitudeClearanceThreshold)` — the floor protects against
	 *  designer-mistuned cruise dropping the unit too close to obstacles.
	 *
	 *  NOTE: legacy class-level UPROPERTY, kept here for now. The current
	 *  altitude lerp state (which persists across orders) lives on
	 *  `FSeinHoverMovementData::Altitude` in the polymorphic per-class
	 *  sub-data; this knob is its target. */
	UPROPERTY(EditAnywhere, Category = "Hover", meta = (ClampMin = "0.0"))
	FFixedPoint CruiseAltitude;

	/** Hard minimum altitude offset above the cell surface (world units).
	 *  Effective altitude floor — the unit will not descend below this even
	 *  if `CruiseAltitude` is set lower. Default 100. */
	UPROPERTY(EditAnywhere, Category = "Hover", meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeClearanceThreshold;

	/** Vertical climb / descent rate (world units per second). Smoothly
	 *  closes the gap between current Altitude and target altitude. */
	UPROPERTY(EditAnywhere, Category = "Hover", meta = (ClampMin = "0.0"))
	FFixedPoint AltitudeChangeRate;

	/** Look-ahead distance along the (straight-line) path for the steering
	 *  carrot. Short = nimble; long = ponderous. */
	UPROPERTY(EditAnywhere, Category = "Hover", meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance;

	virtual bool BypassPathfinding() const override { return true; }
	virtual bool QueryReferenceZ(class USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;

	/** Per-class sub-data this movement consumes (FSeinHoverMovementData) —
	 *  holds the persistent current Altitude that lerps toward this class's
	 *  CruiseAltitude across move orders. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Altitude hook for ApplyGroundSnapAndAltitude. Reads
	 *  `FSeinHoverMovementData::Altitude` (the lerped runtime altitude
	 *  written by Tick) out of the polymorphic sub-data. */
	virtual FFixedPoint GetAltitude(const FSeinMovementComponent* MovementData) const override;
};
