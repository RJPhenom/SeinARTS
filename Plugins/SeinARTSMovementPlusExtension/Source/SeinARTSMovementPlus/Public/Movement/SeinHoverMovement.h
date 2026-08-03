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

	// All Hover tuning lives in FSeinHoverMovementData (the MovementClassData sub-data) — no
	// class-level tuning UPROPERTYs, matching the Wheeled/Tracked exemplar. Normalized 2026-07-02.

	virtual bool BypassPathfinding() const override { return true; }
	virtual bool QueryReferenceZ(class USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;
	virtual bool SupportsExactIdleMutationTracking() const override
	{
		return GetClass() == StaticClass();
	}

	/** Per-class sub-data this movement consumes (FSeinHoverMovementData) —
	 *  holds the persistent current Altitude that lerps toward this class's
	 *  CruiseAltitude across move orders. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Altitude hook for ApplyGroundSnapAndAltitude. Reads
	 *  `FSeinHoverMovementData::Altitude` (the lerped runtime altitude
	 *  written by Tick) out of the polymorphic sub-data. */
	virtual FFixedPoint GetAltitude(const FSeinMovementComponent* MovementData) const override;

	/** Braking rate for the impl-agnostic idle coast + arrival-imminent estimate — reads
	 *  Deceleration out of the unwrapped FSeinHoverMovementData sub-data. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const override;
};
