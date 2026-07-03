/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinTrackedMovementData.h
 * @brief:   Per-class movement data for `USeinTrackedVehicleMovement`.
 *           Surfaces in the entity bridge via
 *           `FSeinMovementComponent::MovementClassData` when the designer
 *           picks USeinTrackedVehicleMovement as the movement class.
 *
 *           Marked with `SeinSubData` so it appears in the polymorphic
 *           sub-data picker on FSeinMovementComponent::MovementClassData
 *           but is filtered out of the entity bridge's top-level
 *           ComponentData picker.
 *
 *           Design summary: the tracked controller is a two-mode state
 *           machine split by speed.
 *             - ARC MODE (AbsSpeed > PivotSpeed): drives like a wheeled
 *               vehicle — full throttle, yaw rotates at TurnRate, optional
 *               sharp-turn brake softens hard turns at high speed.
 *             - PIVOT MODE (AbsSpeed ≤ PivotSpeed): tracked-exclusive
 *               behavior — when misaligned (dot < PivotAlignDot), throttle
 *               is zero and the chassis rotates in place at TurnRate; once
 *               aligned, throttle goes to 1 and the chassis accelerates.
 *
 *           The split is purely speed-based. High-speed U-turns in open
 *           ground arc through the turn (no stop-and-pivot); low-speed
 *           sharp turns near walls pivot in place. Tight terrain naturally
 *           drops speed (arrival cap / per-segment geometry) which slides
 *           the chassis into pivot mode without a separate "terrain
 *           tightness" check.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "SeinTrackedMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinTrackedMovementData : public FSeinComponent
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

	/** Speed (cm/s) separating ARC mode (above) from PIVOT mode (at or below).
	 *  Above PivotSpeed the chassis behaves like a wheeled vehicle — drives
	 *  at full speed, yaw rotates at TurnRate, no stop-and-pivot. At or below
	 *  PivotSpeed the chassis can pivot in place when misaligned. Tune higher
	 *  if you want tanks to always brake-and-pivot at sharp turns; tune lower
	 *  for more aggressive "tank does high-speed donuts" feel.
	 *
	 *  Default 50 cm/s — roughly the speed at which a real tank would commit
	 *  to a pivot rather than carve an arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint PivotSpeed = FFixedPoint::FromInt(50);

	/** Alignment dot (forward · toTarget) below which a PIVOT-mode chassis
	 *  refuses to drive forward — sits stationary and rotates at TurnRate
	 *  until aligned. Above this dot, the chassis accelerates forward from
	 *  the pivot.
	 *
	 *  Has no effect in ARC mode (high speed) — that mode always drives at
	 *  full throttle subject to SharpTurnBrake. PivotAlignDot only gates
	 *  the "stand and turn" vs "drive while turning" decision at low speed.
	 *
	 *  Default 0.5 ≈ 60° aim error — the chassis pivots until it's roughly
	 *  facing forward, then drives. Lower → starts driving earlier (less
	 *  pivot, more arc-from-rest); higher → demands tighter alignment
	 *  before driving (more committed pivots). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	FFixedPoint PivotAlignDot = FFixedPoint::Half;

	/** Sharp-turn brake threshold (radians) for ARC mode. When the chassis
	 *  is in arc mode AND the commanded yaw delta exceeds this angle,
	 *  throttle scales down by `SharpTurnBrakeStrength` — mimics wheeled's
	 *  behavior for high-speed corners. Has no effect in pivot mode.
	 *
	 *  Default π/3 (60°). Lower for earlier braking on milder turns. Set to
	 *  π (180°), or `SharpTurnBrakeStrength = 0`, to disable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint SharpTurnBrakeAngle = FFixedPoint::Pi / FFixedPoint::FromInt(3);

	/** Strength of the arc-mode sharp-turn brake. Scales linearly with both
	 *  yaw-error magnitude (above `SharpTurnBrakeAngle`) AND current speed /
	 *  TopSpeed — slow arcs aren't penalized (pivot mode handles those).
	 *
	 *  ThrottleScale = 1.0 − SharpTurnBrakeStrength × AngleT × SpeedT
	 *
	 *  0.0 = no brake (disabled — chassis arcs at full throttle).
	 *  0.5 = throttle = 0.5 at full sharp turn at TopSpeed (default).
	 *  1.0 = throttle = 0   at full sharp turn at TopSpeed (full stop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint SharpTurnBrakeStrength = FFixedPoint::Half;

	/** Floor look-ahead distance (world units) for the steering carrot.
	 *  Applies even at zero speed so a stationary chassis still has a
	 *  steering target. Effective per-tick lookahead is
	 *  `LookAheadDistance + |Speed| × LookAheadTimeHorizon`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance = FFixedPoint::FromInt(200);

	/** Speed-adaptive look-ahead horizon (seconds). When > 0, look-ahead
	 *  extends as `LookAheadDistance + |Speed| × TimeHorizon` — faster
	 *  vehicles see further ahead.
	 *
	 *  Default 0 (disabled) — same rationale as wheeled. Speed-adaptive
	 *  lookahead shrinks to the floor at low speed (e.g., when climbing
	 *  a slope) which causes carrot oscillation between adjacent
	 *  waypoints. Constant look-ahead avoids that failure mode. Set > 0
	 *  if you have open-ground tanks where high-speed corner anticipation
	 *  matters more than slope robustness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadTimeHorizon = FFixedPoint::Zero;

	/** Optional authored minimum turn radius for tracked vehicles (world
	 *  units). Tracked can pivot in place — 0 = "always pivot at sharp
	 *  corners." Setting non-zero biases nav-layer corner rounding toward
	 *  arcs of this radius for a more deliberate "rolling tank" feel.
	 *  Consumed by `GetMinTurnRadius` which the path planner reads when
	 *  building cell paths. Default 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint MinTurnRadius = FFixedPoint::Zero;
};

FORCEINLINE uint32 GetTypeHash(const FSeinTrackedMovementData& C)
{
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	H = HashCombine(H, GetTypeHash(C.PivotSpeed));
	H = HashCombine(H, GetTypeHash(C.PivotAlignDot));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeAngle));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeStrength));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.LookAheadTimeHorizon));
	H = HashCombine(H, GetTypeHash(C.MinTurnRadius));
	return H;
}
