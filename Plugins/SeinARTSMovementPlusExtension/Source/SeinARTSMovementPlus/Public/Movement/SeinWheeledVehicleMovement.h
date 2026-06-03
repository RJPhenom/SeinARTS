/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.h
 * @brief   Wheeled-vehicle movement -- bicycle-kinematics pure-pursuit
 *          controller.
 *
 *          Bicycle model: angular velocity w = v/L * tan(d), where
 *          L = Wheelbase, d = steer angle.
 *
 *          Pure-pursuit: compute desired steer angle from a speed-adaptive
 *          look-ahead carrot point on the path polyline. Smooth steer
 *          interpolation at SteerResponse rate. Throttle reduction at
 *          sharp turns via TurnSpeedFloor (smoothed steer) and
 *          SharpTurnBrake* (commanded turn). Kinematic arrival braking
 *          + optional linear slowdown floor.
 *
 *          Auto-reverse latch for close behind-unit goals at OnMoveBegin.
 *          Footprint-aware nav collision (inherited from USeinMovement).
 *
 *          Tuning lives entirely on `FSeinWheeledMovementData` — the
 *          per-class sub-data slot on `FSeinMovementComponent::MovementClassData`.
 *          This class holds NO authoring UPROPERTYs of its own; only
 *          per-instance runtime state (CurrentSteer, bIsReversing).
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "SeinWheeledVehicleMovement.generated.h"

struct FSeinMovementComponent;

UCLASS(meta = (DisplayName = "Wheeled Vehicle"))
class SEINARTSMOVEMENTPLUS_API USeinWheeledVehicleMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	USeinWheeledVehicleMovement();

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;

	/** Bicycle minimum turn radius — `Wheelbase / tan(MaxSteerAngle)`.
	 *  Consumed by the nav layer for corner rounding. Returns 0 when
	 *  MaxSteerAngle is degenerate (avoids div-by-zero). Reads kinematic
	 *  values from the unwrapped FSeinWheeledMovementData sub-data. */
	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementComponent* MovementData) const override;

	/** Per-class sub-data this movement consumes — the picker on
	 *  `FSeinMovementComponent::MovementClassData` resolves to this struct
	 *  when USeinWheeledVehicleMovement is selected. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

protected:

	/** Per-instance current steer angle (radians, +/- MaxSteerAngle). Smoothed
	 *  toward desired across ticks at the data struct's SteerResponse. Reset
	 *  per move action in OnMoveBegin. */
	FFixedPoint CurrentSteer = FFixedPoint::Zero;

	/** Latched-at-OnMoveBegin auto-reverse decision. When true, the vehicle
	 *  drives backward to reach a destination that was behind it at move-
	 *  start -- desired yaw flips to "back faces goal," steer command inverts
	 *  for bicycle reverse-kinematics, target speed becomes negative. */
	bool bIsReversing = false;
};
