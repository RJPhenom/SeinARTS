/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledManeuver.h
 * @brief   Plan-time maneuver toolkit for the wheeled vehicle mode — a curated
 *          Reeds-Shepp-style word set (U-turn arc / straight reverse / 3-point
 *          turn / reverse-out-then-forward) solved CLOSED-FORM in fixed point.
 *
 *          Private to SeinARTSMovementPlus on purpose: this is the Wheeled
 *          mode's planning policy, not a shared seam — the shared seam is the
 *          base `USeinMovement::PlanPath` + typed `FSeinPathSegment` contract
 *          these functions produce legs for. Pure functions over value inputs
 *          (no UObject state), so each candidate is independently testable and
 *          trivially deterministic.
 *
 *          NOT a runtime Reeds-Shepp family solver: every candidate is one
 *          closed-form tangent/sweep solve plus a bounded number of footprint
 *          probes, evaluated only inside PlanPath (initial plan + repaths),
 *          never per tick. All tolerances/spacings are compile-time constants
 *          (same rule as GArcFlattenChordError — a per-run tunable here would
 *          be a silent desync).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

class USeinNavigation;

namespace SeinWheeledManeuver
{

/** One drivable leg of a start maneuver. Arcs follow the FSeinPathSegment Arc
 *  convention: `Sweep` is SIGNED radians, positive = counter-clockwise;
 *  `bReverse` marks the leg driven backward (a cusp sits wherever two adjacent
 *  legs disagree on it). */
struct FLeg
{
	bool bArc = false;
	bool bReverse = false;
	FFixedVector From;
	FFixedVector To;
	FFixedVector Center;                       // arc only
	FFixedPoint Radius = FFixedPoint::Zero;    // arc only
	FFixedPoint Sweep = FFixedPoint::Zero;     // arc only, signed (positive = CCW)
	FFixedPoint Length = FFixedPoint::Zero;    // planar drive length (arc = R·|Sweep|)
};

/** A chosen start maneuver: the legs to prepend, and where they rejoin the
 *  coarse polyline. `JoinWaypointIndex` = the polyline index the last leg ends
 *  AT (the tail continues from there); -1 = the legs themselves reach the
 *  final waypoint (no tail — e.g. the straight-reverse word). */
struct FPlan
{
	TArray<FLeg, TInlineAllocator<12>> Legs;
	int32 JoinWaypointIndex = -1;
	FFixedPoint HeadCost = FFixedPoint::Zero;  // reverse-weighted drive length of the legs
};

/** Value inputs for one plan solve — everything read out of hashed sim state /
 *  authored data by the caller (PlanPath), so the functions below touch no
 *  UObject state beyond the nav probe interface. */
struct FInputs
{
	FFixedVector Pos;                           // entity position (XY used)
	FFixedPoint Yaw = FFixedPoint::Zero;        // entity yaw, radians
	FFixedPoint MinTurnRadius = FFixedPoint::Zero;    // bicycle R_min, must be > 0
	FFixedPoint CruiseTurnRadius = FFixedPoint::Zero; // max(R_min, TopSpeed / TurnRate)
	FFixedPoint FootprintRadius = FFixedPoint::Zero;  // shared collision-cascade radius
	uint8 NavLayerMask = 0x01;
	FFixedPoint ReverseSpeedPenalty = FFixedPoint::One; // TopSpeed / effective reverse speed, >= 1
	FFixedPoint ForwardPathBias = FFixedPoint::One;     // >= 1; forward-only wins unless this much longer
	FFixedPoint ReverseEngageDistance = FFixedPoint::Zero;
	FFixedPoint ReverseEngageDot = FFixedPoint::Zero;
	FFixedPoint ReversePlanMaxDistance = FFixedPoint::Zero;
	bool bCanReverse = false;
	bool bCurrentlyReversing = false;           // from hashed Velocity·Forward — replan continuity
	/** Heading-error engage threshold (radians). The caller passes a LOWER
	 *  threshold while a maneuver is already being driven (hysteresis), so an
	 *  interval replan mid-maneuver keeps producing the continuation instead
	 *  of truncating the plan the moment the error dips under the cold
	 *  threshold. <= 0 = use the default cold threshold. */
	FFixedPoint EngageAngle = FFixedPoint::Zero;
	USeinNavigation* Nav = nullptr;
};

// ---------------------------------------------------------------------------
// Angle helpers (radians). Local because USeinMovement's equivalents are
// protected statics not visible to a namespace.
// ---------------------------------------------------------------------------

/** Wrap to [-pi, pi]. */
FFixedPoint WrapSigned(FFixedPoint A);

/** Wrap to [0, 2pi). */
FFixedPoint WrapPositive(FFixedPoint A);

// ---------------------------------------------------------------------------
// Probes (plan-time; static bake + dynamic blockers via IsWorldPositionClear)
// ---------------------------------------------------------------------------

/** Footprint-clear test at one position: center + 4 ring samples at
 *  `FootprintRadius` (coarser than the runtime 8-ring — a plan candidate only
 *  needs go/no-go, the runtime floor still owns exactness). True when Nav is
 *  null (no data = no basis to refuse; the runtime floor still guards). */
bool ProbeClearAt(const FInputs& In, const FFixedVector& Pos);

/** Sample a straight From→To at a fixed spacing; false on the first blocked
 *  footprint sample. */
bool ProbeStraightClear(const FInputs& In, const FFixedVector& From, const FFixedVector& To);

/** Sample an arc (Center/Radius, from angle `StartAngle`, signed `Sweep`) at a
 *  fixed spacing; false on the first blocked footprint sample. */
bool ProbeArcClear(const FInputs& In, const FFixedVector& Center, FFixedPoint Radius,
	FFixedPoint StartAngle, FFixedPoint Sweep);

/** Largest clear sweep (radians, unsigned) achievable along the arc before a
 *  probe fails, scanned in fixed angular steps up to `MaxSweep`. */
FFixedPoint ProbeArcMaxSweep(const FInputs& In, const FFixedVector& Center, FFixedPoint Radius,
	FFixedPoint StartAngle, FFixedPoint SweepSign, FFixedPoint MaxSweep);

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

/** Tangent-arc solve: from pose (Pos, Yaw) turning on side `TurnSign`
 *  (+1 = left/CCW) at `Radius`, find the arc that departs tangent toward
 *  `Target`. Fails when Target is inside the turn circle (+ margin) — the
 *  classic orbit configuration the caller must resolve with a cusp word.
 *  Outputs the circle center, the tangent departure point, and the SIGNED
 *  sweep from the current pose to departure. */
bool SolveTangentArc(const FFixedVector& Pos, FFixedPoint Yaw, FFixedPoint TurnSign,
	FFixedPoint Radius, const FFixedVector& Target,
	FFixedVector& OutCenter, FFixedVector& OutDepart, FFixedPoint& OutSweep);

/** Planar length of the polyline from `FromIndex` to the end. */
FFixedPoint PolylineLengthFrom(const TArray<FFixedVector>& Waypoints, int32 FromIndex);

// ---------------------------------------------------------------------------
// The planner
// ---------------------------------------------------------------------------

/** Choose a start maneuver for a coarse nav polyline, or return false to drive
 *  it unmodified (aligned enough / nothing feasible / reverse not allowed
 *  where required). See the candidate ladder in
 *  Docs/Engineering/WheeledVehicleMovement.md. Deterministic: fixed candidate
 *  order, fixed probe spacings, all-fixed-point math. */
bool PlanStartManeuver(const FInputs& In, const TArray<FFixedVector>& Waypoints, FPlan& Out);

} // namespace SeinWheeledManeuver
