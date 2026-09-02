/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinTrackedMovementData.h
 * @brief:   Per-class movement data for `USeinTrackedVehicleMovement`.
 *           Surfaces in the entity bridge via
 *           `FSeinMovementPayload::MovementClassData` when the designer
 *           picks USeinTrackedVehicleMovement as the movement class.
 *
 *           Marked with `SeinSubData` so it appears in the polymorphic
 *           sub-data picker on FSeinMovementPayload::MovementClassData
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
#include "Components/SeinPayload.h"
#include "Types/FixedPoint.h"
#include "SeinTrackedMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinTrackedMovementData : public FSeinPayload
{
	GENERATED_BODY()

	/** Acceleration rate (world units per second²) — current speed ramps UP toward the target
	 *  (feeds StepSpeedToward). Moved off the bare FSeinMovementPayload 2026-07-02. */
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
	 *  to a pivot rather than carve an arc. Maneuver-plan cusps treat the
	 *  pivot band as at least the driver's cusp-flip speed (30) regardless of
	 *  a lower authored value, so a flipped reverse leg always pivots to its
	 *  heading before driving. */
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

	/** Yaw-rate slew (radians per second²) — how quickly the hull's turn RATE
	 *  ramps toward the demanded rate, the tracked analog of a heavy hull's
	 *  rotational inertia (a 25-ton chassis doesn't snap from straight to
	 *  full rotation in one tick). Applies to arc turns, pivots, and cusp
	 *  settling alike; the rate also decays through this on the way OUT of a
	 *  turn, so turn exits ease instead of stopping dead.
	 *
	 *  0 (default) = OFF — yaw rate changes instantly, the pre-inertia
	 *  behavior, bit-exact. Recommended when authored: ~3-5 × TurnRate (the
	 *  hull reaches full rotation rate in ~0.2-0.3 s — subtle weight without
	 *  sluggishness); lower values read heavier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint TurnAcceleration = FFixedPoint::Zero;

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
	 *  corners." Setting non-zero declares "this chassis does NOT neutral-
	 *  turn": the maneuver planner then treats it like a wheeled vehicle and
	 *  plans the FULL word ladder (U-turn arcs, 3-point turns, reverse-out)
	 *  at this radius instead of relying on pivots. KNOWN EXCEPTION: the idle
	 *  settle-facing turn still rotates a parked chassis in place (a base
	 *  behavior shared by all ground modes — disable globally via Settle To
	 *  Formation Facing, or accept the cosmetic pivot). Default 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint MinTurnRadius = FFixedPoint::Zero;

	// ---------------------------------------------------------------------
	// Maneuver planning
	//
	// COUPLING NOTE — the unit-level `FSeinMovementPayload::TurnRate` is
	// the tracked turn ENGINE (pivot rate AND arc yaw rate), and it also
	// sizes the momentum U-turn arc: an at-speed turnaround sweeps radius
	// R = speed / TurnRate, clamped to [100, 10000] world units, and the
	// word only engages above max(2 × PivotSpeed, TopSpeed / 4) while
	// driving FORWARD (the driver then holds arc speed at 7/8 of
	// TurnRate·R for correction margin). At the base default TurnRate = 5
	// the radius sits on the 100-unit floor (a near-pivot); author
	// TurnRate ~= TopSpeed / desired-sweep-radius (~1.0 rad/s for a
	// visible rolling U-turn at cruise) if you want tanks to CARVE their
	// at-speed turnarounds.
	// ---------------------------------------------------------------------

	/** Plans explicit start maneuvers as typed path segments: a straight
	 *  reverse for close behind-goals, a momentum-preserving U-turn arc when
	 *  ordered to turn around while already at speed, and — when Min Turn
	 *  Radius is authored non-zero — the full wheeled-style maneuver ladder
	 *  (arcs, 3-point turns, reverse-out). Turn OFF for the plain arc/pivot
	 *  controller (the pre-maneuver behavior) for comparison. Default on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver")
	bool bManeuverPlanning = true;

	/** Lets this tracked vehicle drive maneuver legs in reverse (backing to a
	 *  close behind-goal, reverse legs of an authored-radius 3-point turn).
	 *  Defaults ON for vehicles — this is the mode's own gate and is
	 *  OR-combined with the unit-level Can Reverse flag (default off), so
	 *  tracked units reverse out of the box; untick BOTH to forbid reverse.
	 *  Reverse speed still comes from the unit's Reverse Top Speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver")
	bool bCanReverse = true;

	/** How strongly a forward maneuver is preferred over one that reverses.
	 *  A reversing plan wins only when the forward route is more than this
	 *  factor longer. 1.0 = pick purely by length. Default 1.35. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver",
		meta = (ClampMin = "1.0"))
	FFixedPoint ForwardPathBias = FFixedPoint::FromInt(135) / FFixedPoint::FromInt(100);

	/** Farthest the planner will drive in reverse along its own route to find
	 *  room to turn around — used only by the authored-radius (non-pivoting)
	 *  ladder; a neutral-steer tank never needs it. Default 1200 (12 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Maneuver",
		meta = (ClampMin = "0.0"))
	FFixedPoint ReversePlanMaxDistance = FFixedPoint::FromInt(1200);
};

FORCEINLINE uint32 GetTypeHash(const FSeinTrackedMovementData& C)
{
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	H = HashCombine(H, GetTypeHash(C.PivotSpeed));
	H = HashCombine(H, GetTypeHash(C.PivotAlignDot));
	H = HashCombine(H, GetTypeHash(C.TurnAcceleration));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeAngle));
	H = HashCombine(H, GetTypeHash(C.SharpTurnBrakeStrength));
	H = HashCombine(H, GetTypeHash(C.LookAheadDistance));
	H = HashCombine(H, GetTypeHash(C.LookAheadTimeHorizon));
	H = HashCombine(H, GetTypeHash(C.MinTurnRadius));
	H = HashCombine(H, GetTypeHash(C.bManeuverPlanning));
	H = HashCombine(H, GetTypeHash(C.bCanReverse));
	H = HashCombine(H, GetTypeHash(C.ForwardPathBias));
	H = HashCombine(H, GetTypeHash(C.ReversePlanMaxDistance));
	return H;
}
