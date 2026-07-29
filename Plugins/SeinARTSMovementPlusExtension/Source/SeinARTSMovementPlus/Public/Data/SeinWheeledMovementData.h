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
 *           controller class (USeinWheeledVehicleMovement) holds no editable
 *           tuning properties — every tunable lives here; its private
 *           reflected fields are canonical runtime state. Per the
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
	// Speed ramp (moved off the bare FSeinMovementComponent 2026-07-02)
	// ---------------------------------------------------------------------

	/** Acceleration rate (world units per second²) — how quickly current speed ramps UP toward the
	 *  target (feeds StepSpeedToward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0"))
	FFixedPoint Acceleration = FFixedPoint::FromInt(750);

	/** Deceleration rate (world units per second²) — how quickly current speed ramps DOWN, and the
	 *  kinematic arrival-brake rate into the final waypoint. Typically >= Acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Speed",
		meta = (ClampMin = "0.0"))
	FFixedPoint Deceleration = FFixedPoint::FromInt(750);

	// ---------------------------------------------------------------------
	// Bicycle kinematics
	//
	// COUPLING WARNING — the unit-level `FSeinMovementComponent::TurnRate`
	// is a THIRD turning governor alongside the two fields below, and its
	// base default (5 rad/s) is sized for pivot-capable units, not vehicles:
	//   - Minimum turn radius   R_min    = Wheelbase / tan(MaxSteerAngle)
	//   - Cruise (planned) arc  R_cruise = max(R_min, TopSpeed / TurnRate)
	//   - Arc speed law (drive) v        <= TurnRate * R
	// At TurnRate = 5 with typical TopSpeeds, R_cruise collapses to R_min —
	// every planned U-turn is a minimum-radius arc and the arc speed law
	// never brakes, so the "full-speed wide swoop in open ground vs braked
	// tight arc in confined ground" differentiation NEVER materializes.
	// Author vehicle TurnRate ~= TopSpeed / desired-cruise-arc-radius
	// (e.g. TopSpeed 500, wanted swoop radius ~500 -> TurnRate ~1.0).
	// TurnRate also hard-clamps the bicycle yaw rate in the driver, so
	// setting it very low makes even min-radius arcs crawl.
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
	// Low-speed turn assist (the "helping hand")
	// ---------------------------------------------------------------------

	/** The low-speed turn-assist "helping hand": how fast (radians/sec) a near-stationary wheeled
	 *  vehicle may rotate toward its goal, ABOVE what the honest bicycle model allows.
	 *  A real car can't turn while stopped — this relaxes that so a stopped/slow chassis
	 *  can pivot toward its destination, giving tight u-turns from rest, snappy
	 *  responsiveness, and an escape hatch when boxed in. Fades to zero by Turn Assist
	 *  Fade Speed, so at cruising speed the turn is physically honest. Looks slightly
	 *  unphysical if you stare at a stopped vehicle pivoting, but reads fine in motion —
	 *  that is the trade. Set 0 for a strict bicycle model. Default π (~a 1-second u-turn
	 *  from rest); the designer dials it for feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|LowSpeed",
		meta = (ClampMin = "0.0"))
	FFixedPoint LowSpeedTurnRate = FFixedPoint::Pi;

	/** Speed (world units/sec) at which the helping-hand turn assist has fully faded to
	 *  zero — above this the chassis turns by the honest bicycle model only. The assist
	 *  is full at rest and ramps down linearly to here, so it shapes the from-rest
	 *  reorient without touching cruising turns. Larger = the assist helps through more
	 *  of a turn (tighter, less honest); smaller = assist only at a crawl. Default 300.
	 *  Ignored when Low Speed Turn Rate is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|LowSpeed",
		meta = (ClampMin = "0.0"))
	FFixedPoint TurnAssistFadeSpeed = FFixedPoint::FromInt(300);

	// ---------------------------------------------------------------------
	// Maneuver planning (Reeds-Shepp-style start maneuvers)
	// ---------------------------------------------------------------------

	/** Plans an explicit start maneuver (U-turn arc, straight reverse, multi-point turn, or
	 *  reverse-out of a corridor) whenever the chassis is badly misaligned with its route, and
	 *  drives it as typed arc/reverse path segments with planned speeds — full-speed wide arcs in
	 *  open ground, braked tight arcs and cusped turns against walls. Turn OFF to fall back to the
	 *  plain pursuit steering (the pre-maneuver behavior) for comparison or for a deliberately
	 *  simpler unit. Default on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver")
	bool bManeuverPlanning = true;

	/** Lets this wheeled vehicle drive maneuver legs in reverse (multi-point turns, backing out of
	 *  corridors, short reverse parking). Defaults ON for wheeled vehicles — this is the mode's own
	 *  gate and is OR-combined with the unit-level Can Reverse flag (which defaults off), so wheeled
	 *  units reverse out of the box; untick BOTH to forbid reverse. Reverse speed still comes from
	 *  the unit's Reverse Top Speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver")
	bool bCanReverse = true;

	/** How strongly a forward-only maneuver is preferred over one that needs reversing. A cusped
	 *  plan (3-point turn, reverse-out) wins only when the forward route is more than this factor
	 *  longer. 1.0 = pick purely by length; higher = stronger forward preference. Default 1.35. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver",
		meta = (ClampMin = "1.0"))
	FFixedPoint ForwardPathBias = FFixedPoint::FromInt(135) / FFixedPoint::FromInt(100);

	/** Farthest the planner will drive in reverse along its own route to find room to turn around
	 *  (the corridor-escape maneuver). Beyond this it gives up on reversing out and falls back to
	 *  pivot-assisted pursuit. Default 1200 (12 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver",
		meta = (ClampMin = "0.0"))
	FFixedPoint ReversePlanMaxDistance = FFixedPoint::FromInt(1200);

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
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	H = HashCombine(H, GetTypeHash(C.Wheelbase));
	H = HashCombine(H, GetTypeHash(C.MaxSteerAngle));
	H = HashCombine(H, GetTypeHash(C.SteerResponse));
	H = HashCombine(H, GetTypeHash(C.LowSpeedTurnRate));
	H = HashCombine(H, GetTypeHash(C.TurnAssistFadeSpeed));
	H = HashCombine(H, GetTypeHash(C.bManeuverPlanning));
	H = HashCombine(H, GetTypeHash(C.bCanReverse));
	H = HashCombine(H, GetTypeHash(C.ForwardPathBias));
	H = HashCombine(H, GetTypeHash(C.ReversePlanMaxDistance));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.LookAheadTimeHorizon));
	H = HashCombine(H, GetTypeHash(C.ArrivalSlowdownDistance));
	H = HashCombine(H, GetTypeHash(C.TurnSpeedFloor));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeAngle));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeStrength));
	return H;
}
