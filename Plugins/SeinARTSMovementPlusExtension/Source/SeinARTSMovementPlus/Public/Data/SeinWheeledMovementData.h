/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinWheeledMovementData.h
 * @brief:   Per-class movement data for `USeinWheeledVehicleMovement`.
 *           Surfaces in the entity bridge via
 *           `FSeinMovementComponent::MovementClassData` when the designer
 *           picks USeinWheeledVehicleMovement as the movement class.
 *
 *           Single source of truth for wheeled tuning. The wheeled
 *           controller class (USeinWheeledVehicleMovement) holds NO
 *           UPROPERTYs of its own — every tunable lives here. Per the
 *           "no shared source of truth" rule, fields are NOT shared with
 *           other movement-class data structs even when they have the
 *           same name (e.g. tracked has its own SharpTurnBrakeAngle).
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
#include "SeinWheeledMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinWheeledMovementData : public FSeinComponent
{
	GENERATED_BODY()

	// ---------------------------------------------------------------------
	// Bicycle kinematics
	// ---------------------------------------------------------------------

	/** Distance between front and rear axles (world units). Bicycle-kinematic
	 *  primitive: smaller values give tighter MinTurnRadius for a given
	 *  MaxSteerAngle. Jeeps ~150-220, armored cars ~300-400, half-tracks
	 *  ~300-450, modern light tanks ~400-500. Default 350 = armored-car
	 *  class. Combined with MaxSteerAngle=30° gives MinTR ≈ 606 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Kinematics",
		meta = (ClampMin = "1.0"))
	FFixedPoint Wheelbase = FFixedPoint::FromInt(350);

	/** Maximum steer angle in radians (±). Real-world wheeled vehicles cap at
	 *  30-40° (= π/6 to ~0.7 rad). Values above π/4 (45°) produce kart-like
	 *  turning that won't read as vehicles at speed. Default π/6 = 30°. With
	 *  Wheelbase=350 yields MinTR ≈ 606 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Kinematics",
		meta = (ClampMin = "0.0"))
	FFixedPoint MaxSteerAngle = FFixedPoint::Pi / FFixedPoint::FromInt(6);

	/** Steer-angle slew rate (1/s) — how quickly the chassis's CurrentSteer
	 *  interpolates toward DesiredSteer. Higher = snappier wheel turn-in,
	 *  lower = gradual. Default 3.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Kinematics",
		meta = (ClampMin = "0.1"))
	FFixedPoint SteerResponse = FFixedPoint::FromInt(3);

	// ---------------------------------------------------------------------
	// Look-ahead / carrot
	// ---------------------------------------------------------------------

	/** Floor look-ahead distance (world units) for the steering carrot.
	 *  Applies even at zero speed so a stationary chassis still has a
	 *  steering target. Effective per-tick lookahead is
	 *  `LookAheadDistance + |Speed| × LookAheadTimeHorizon`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Carrot",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance = FFixedPoint::FromInt(300);

	/** Speed-adaptive look-ahead horizon (seconds). When > 0, look-ahead
	 *  extends as `LookAheadDistance + |Speed| × TimeHorizon` — faster
	 *  vehicles see further ahead.
	 *
	 *  Default 0 (disabled). Rationale: a wheeled vehicle climbing a slope
	 *  slows to grade-limited speed, which drops the velocity contribution
	 *  to ~0 and shrinks look-ahead back to the LookAheadDistance floor —
	 *  visible as carrot oscillation between adjacent waypoints on slopes.
	 *  Holding the carrot at a constant distance regardless of speed
	 *  avoids that failure mode at the cost of slightly looser arcs at
	 *  high speed (which the sharp-turn brake already softens). Set > 0
	 *  if you have flat open-ground vehicles where high-speed corner
	 *  anticipation matters more than slope robustness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Carrot",
		meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadTimeHorizon = FFixedPoint::Zero;

	// ---------------------------------------------------------------------
	// Speed scaling / arrival
	// ---------------------------------------------------------------------

	/** Optional minimum-slowdown distance — a LINEAR speed cap inside this
	 *  range, applied only if stricter than the physics-based kinematic
	 *  arrival brake. Set 0 for pure physics-based braking (recommended). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0"))
	FFixedPoint ArrivalSlowdownDistance = FFixedPoint::Zero;

	/** Min throttle multiplier at full steer (1.0 = no slow-down on sharp
	 *  turns; 0.5 = halve speed at MaxSteerAngle). Quadratic falloff by
	 *  |steer|/MaxSteer. Acts on the SMOOTHED CurrentSteer (lags behind
	 *  commanded turn by ~1/SteerResponse seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint TurnSpeedFloor = FFixedPoint::Half;

	/** Sharp-turn brake threshold (radians). When the carrot is asking for a
	 *  yaw change larger than this, throttle scales down by
	 *  `SharpTurnBrakeStrength`. Acts on the COMMANDED turn (raw yaw error)
	 *  immediately, while `TurnSpeedFloor` acts on the smoothed steer angle
	 *  which lags by ~`SteerResponse` time constant — so this brake engages
	 *  at the moment of sharp turn input, before the steer settles.
	 *
	 *  Default π/3 (60°). Lower for earlier braking on milder turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0"))
	FFixedPoint SharpTurnBrakeAngle = FFixedPoint::Pi / FFixedPoint::FromInt(3);

	/** Strength of the sharp-turn brake. Scales linearly with both yaw-error
	 *  magnitude (above `SharpTurnBrakeAngle`) AND current speed / TopSpeed
	 *  — slow vehicles aren't penalized (bicycle kinematics already pivot
	 *  slowly at low speed anyway).
	 *
	 *  ThrottleScale = 1.0 − SharpTurnBrakeStrength × AngleT × SpeedT
	 *
	 *  0.0 = no brake (disabled — chassis arcs at full throttle).
	 *  0.5 = throttle = 0.5 at full sharp turn at TopSpeed (default).
	 *  1.0 = throttle = 0   at full sharp turn at TopSpeed (full stop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint SharpTurnBrakeStrength = FFixedPoint::Half;
};

FORCEINLINE uint32 GetTypeHash(const FSeinWheeledMovementData& C)
{
	uint32 H = GetTypeHash(C.Wheelbase);
	H = HashCombine(H, GetTypeHash(C.MaxSteerAngle));
	H = HashCombine(H, GetTypeHash(C.SteerResponse));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.LookAheadTimeHorizon));
	H = HashCombine(H, GetTypeHash(C.ArrivalSlowdownDistance));
	H = HashCombine(H, GetTypeHash(C.TurnSpeedFloor));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeAngle));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeStrength));
	return H;
}
