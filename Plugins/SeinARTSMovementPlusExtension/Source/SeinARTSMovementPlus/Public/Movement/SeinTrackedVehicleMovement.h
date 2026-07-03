/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.h
 * @brief   Tracked-vehicle movement — Arc/Pivot mode split by speed.
 *
 *          Two modes, one transition speed (`PivotSpeed` from
 *          FSeinTrackedMovementData):
 *
 *            ARC MODE  (AbsSpeed > PivotSpeed):
 *              The chassis behaves like a wheeled vehicle. Full throttle,
 *              yaw rotates toward the steering target at `TurnRate × Dt`,
 *              and an optional sharp-turn brake (`SharpTurnBrakeAngle` /
 *              `SharpTurnBrakeFactor`) softens throttle for hard turns at
 *              high speed. Open-terrain U-turns arc through without
 *              stopping — matches the wheeled vehicle feel.
 *
 *            PIVOT MODE (AbsSpeed ≤ PivotSpeed):
 *              The chassis can pivot in place. If misaligned with the
 *              steering target (`dot < PivotAlignDot`), throttle = 0 and
 *              the chassis rotates at TurnRate without translating — the
 *              tracked-exclusive "spin to face." Once aligned, throttle
 *              goes to 1 and the chassis accelerates forward.
 *
 *          Transition: speed-based, single threshold. Tight terrain
 *          naturally drops speed via per-segment arrival caps and
 *          short-segment path geometry, sliding the chassis into pivot
 *          mode for the final approach.
 *
 *          Path-following uses the same `USeinMovement::ResolveLookAheadPoint`
 *          as wheeled. Arrival uses kinematic braking. Reverse latch
 *          mirrors wheeled — `bIsReversing` is set at OnMoveBegin from
 *          `ShouldAutoReverse` and flips the steering target.
 *
 *          Top-line tuning (TopSpeed / TurnRate / Accel / Decel / reverse)
 *          lives on FSeinMovementComponent. Tracked-specific tuning
 *          (PivotSpeed, PivotAlignDot, SharpTurnBrake*, LookAhead*,
 *          MinTurnRadius) lives on FSeinTrackedMovementData, accessed via
 *          `FSeinMovementComponent::MovementClassData`. This class has no
 *          per-instance UPROPERTYs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "SeinTrackedVehicleMovement.generated.h"

struct FSeinMovementComponent;

UCLASS(meta = (DisplayName = "Tracked Vehicle"))
class SEINARTSMOVEMENTPLUS_API USeinTrackedVehicleMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;

	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementComponent* MovementData) const override;

	/** Per-class sub-data this movement consumes — the picker on
	 *  `FSeinMovementComponent::MovementClassData` swaps to this struct when
	 *  USeinTrackedVehicleMovement is selected. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Braking rate for the impl-agnostic idle coast + arrival-imminent estimate — reads
	 *  Deceleration out of the unwrapped FSeinTrackedMovementData sub-data. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const override;

protected:

	/** Latched-at-OnMoveBegin reverse decision. Tracked needs no
	 *  steering-inversion (yaw control is direct, no bicycle); reversing
	 *  flips the steering target so the BACK faces the goal, and the
	 *  Arc/Pivot mode split still drives the throttle decision off
	 *  `forward · effective_target_dir`. */
	bool bIsReversing = false;
};
