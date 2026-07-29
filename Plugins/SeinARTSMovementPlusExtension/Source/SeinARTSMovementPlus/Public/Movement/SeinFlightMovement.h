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

	// All Flight tuning lives in FSeinFlyingMovementData (the MovementClassData sub-data) — no
	// class-level tuning UPROPERTYs, matching the Wheeled/Tracked exemplar. Normalized 2026-07-02.

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

	/** Braking rate for the impl-agnostic idle coast + arrival-imminent estimate — reads
	 *  Deceleration out of the unwrapped FSeinFlyingMovementData sub-data. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const override;

protected:

	/** Per-instance current bank angle (radians, +/- MaxSteerAngle). Reset on
	 *  OnMoveBegin — bank settles to neutral at the start of each new order.
	 *  Reflected so canonical movement snapshots restore mid-order steering. */
	UPROPERTY()
	FFixedPoint CurrentSteer = FFixedPoint::Zero;
};
