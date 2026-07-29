/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.h
 * @brief   Wheeled-vehicle movement -- bicycle-kinematics controller with
 *          Reeds-Shepp-style start-maneuver planning.
 *
 *          Bicycle model: angular velocity w = v/L * tan(d), where
 *          L = Wheelbase, d = steer angle.
 *
 *          Two cooperating halves:
 *          - PLANNER (`PlanPath` override + SeinWheeledManeuver toolkit):
 *            post-processes the coarse nav polyline into typed Arc / Straight
 *            segments (per-segment bReverse for cusps) whenever the chassis is
 *            badly misaligned with its route — U-turn arcs sized to the space
 *            (full-speed radius preferred), straight reverse for close
 *            behind-goals, multi-point turns against walls, and reverse-out of
 *            too-tight corridors. Master switch:
 *            `FSeinWheeledMovementData::bManeuverPlanning`.
 *          - DRIVER (`Tick` override, Tier-2): follows typed maneuver segments
 *            with a geometric segment cursor (curvature feed-forward steer on
 *            arcs with `v <= TurnRate * R`, reverse pure-pursuit on reverse
 *            legs, brake-to-zero cusp gates, anticipatory braking into the
 *            next segment), then hands the all-forward tail back to the
 *            classic speed-adaptive pure-pursuit carrot. Includes a
 *            displacement-based stuck detector with probe-gated reverse-nudge
 *            recovery.
 *
 *          Tuning lives entirely on `FSeinWheeledMovementData` — the per-class
 *          sub-data slot on `FSeinMovementComponent::MovementClassData`. This
 *          class holds no editable authoring properties; its per-instance
 *          runtime state (steer, drive latch, segment cursor, stuck
 *          accumulators) is reflected for exact canonical snapshot restore.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinWheeledVehicleMovement.generated.h"

struct FSeinMovementComponent;
struct FSeinWheeledMovementData;

UCLASS(meta = (DisplayName = "Wheeled Vehicle"))
class SEINARTSMOVEMENTPLUS_API USeinWheeledVehicleMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	USeinWheeledVehicleMovement();

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;

	/** Reset the driver/planner instance state when an order ends. Load-bearing
	 *  for the engage-hysteresis read in PlanPath: a NEW order's initial plan
	 *  runs BEFORE OnMoveBegin's reset, so without this a stale in-maneuver
	 *  flag from the previous order would leak into the fresh plan. */
	virtual void OnMoveEnd(FSeinEntity& Entity) override;

	/** Maneuver-planning path resolve: run the base coarse plan (budgeted A*,
	 *  BP-overridable), then — when `bManeuverPlanning` is on and the chassis
	 *  is badly misaligned with the route — reshape the HEAD of the path into
	 *  typed Arc/Straight segments via the planner-handle emit API. The
	 *  destination is never relocated (root invariant #6) and `Throttled` /
	 *  `NotFound` pass through untouched. */
	virtual ESeinPathResult PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const override;

	/** Roll-through arrival: keep the (already kinematically-braked) residual
	 *  velocity instead of hard-zeroing, so the idle coast-down finishes the
	 *  stop through the same decel ramp — a vehicle eases to rest instead of
	 *  snapping. */
	virtual FSeinMotion ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover) override;

	/** Bicycle minimum turn radius — `Wheelbase / tan(MaxSteerAngle)`. Consumed
	 *  by this mode's own maneuver planner (arc radii are never emitted below
	 *  it; the driver's arc speed law `v <= TurnRate * R` covers the yaw-rate
	 *  clamp side). Returns 0 when MaxSteerAngle is degenerate (avoids
	 *  div-by-zero). Reads kinematic values from the unwrapped
	 *  FSeinWheeledMovementData sub-data. */
	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementComponent* MovementData) const override;

	/** Per-class sub-data this movement consumes — the picker on
	 *  `FSeinMovementComponent::MovementClassData` resolves to this struct
	 *  when USeinWheeledVehicleMovement is selected. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Braking rate for the impl-agnostic idle coast + arrival-imminent estimate — reads
	 *  Deceleration out of the unwrapped FSeinWheeledMovementData sub-data. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const override;

protected:

	/** Per-instance current steer angle (radians, +/- MaxSteerAngle). Smoothed
	 *  toward desired across ticks at the data struct's SteerResponse. Reset
	 *  per move action in OnMoveBegin and at each cusp flip. */
	UPROPERTY()
	FFixedPoint CurrentSteer = FFixedPoint::Zero;

	/** LEGACY auto-reverse latch — used only when `bManeuverPlanning` is OFF
	 *  (the maneuver planner expresses reverse as typed segments instead).
	 *  Latched at OnMoveBegin for close behind-chassis goals; desired yaw
	 *  flips to "back faces goal," steer inverts, target speed goes negative. */
	UPROPERTY()
	bool bIsReversing = false;

	// ------------------------------------------------------------------
	// Segment-driver state (typed maneuver paths). Reflected into the canonical
	// movement provider so a mid-order restore resumes from the exact cursor,
	// latch, and recovery state. Reset in OnMoveBegin and whenever the path
	// identity changes.
	// ------------------------------------------------------------------

	/** Index of the typed segment currently being driven (< TailStartSeg while
	 *  in the maneuver head). Advanced geometrically (end-plane crossover),
	 *  never derived from the flattened waypoint backbone (which has no
	 *  waypoint↔segment mapping). */
	UPROPERTY()
	int32 SegCursor = 0;

	/** First segment index of the all-forward-straight TAIL. Cursor >= this
	 *  (or 0 when the path has no maneuver head) = classic carrot pursuit. */
	UPROPERTY()
	int32 TailStartSeg = 0;

	/** Current drive direction latch: true = backing. Flips only at a cusp
	 *  once |speed| has braked under the cusp epsilon; the wheels PRE-STEER
	 *  toward the next leg's lock during the brake-out and the flip keeps
	 *  that angle. */
	UPROPERTY()
	bool bDriveReverseLatch = false;

	/** Path identity stamp — detects path swaps (initial plan + every repath)
	 *  so the segment driver re-derives its cursor/latch for the new geometry.
	 *  Byte-exact fixed-point compares; derived purely from hashed path state. */
	UPROPERTY()
	int32 CachedPathWaypointNum = -1;
	UPROPERTY()
	int32 CachedPathSegmentNum = -1;
	UPROPERTY()
	FFixedVector CachedPathFirstWp = FFixedVector::ZeroVector;
	UPROPERTY()
	FFixedVector CachedPathLastWp = FFixedVector::ZeroVector;
	/** TotalCost + the first segment's endpoint join the stamp so two plans
	 *  with identical counts and endpoints but different middle geometry
	 *  (possible on a from-rest replan after a world change) still register
	 *  as a swap. */
	UPROPERTY()
	FFixedPoint CachedPathTotalCost = FFixedPoint::Zero;
	UPROPERTY()
	FFixedVector CachedPathFirstSegTo = FFixedVector::ZeroVector;

	// ------------------------------------------------------------------
	// Stuck / orbit protection (vehicles are outside the move action's
	// hold-escape ladder by the commanded-velocity exemption — this is the
	// mode's own recovery).
	// ------------------------------------------------------------------

	/** Entity position at the previous tick's ENTRY — cross-tick displacement
	 *  includes the PostTick collision resolver's pushes, so entity/crowd pins
	 *  register as stuck, not only nav-floor wall pins. */
	UPROPERTY()
	FFixedVector LastEntryPos = FFixedVector::ZeroVector;
	UPROPERTY()
	bool bLastEntryPosValid = false;

	/** Seconds of sustained commanded-but-not-moving (entry-to-entry
	 *  displacement vs the commanded step). Triggers recovery at the stuck
	 *  threshold. */
	UPROPERTY()
	FFixedPoint StuckTime = FFixedPoint::Zero;

	/** Seconds a maneuver leg has been held near zero speed (avoidance yield
	 *  against traffic) — abandons the maneuver head into carrot pursuit at
	 *  the abandon threshold, whose steer-bend routes around traffic. */
	UPROPERTY()
	FFixedPoint ManeuverStallTime = FFixedPoint::Zero;

	/** Seconds of recovery drive remaining (probe-gated straight nudge). */
	UPROPERTY()
	FFixedPoint RecoveryTime = FFixedPoint::Zero;

	/** Recovery drive direction: -1 reverse nudge, +1 forward nudge, 0 idle. */
	UPROPERTY()
	int32 RecoveryDir = 0;

	/** |yaw| swept since the last waypoint/segment progress — the orbit
	 *  backstop. Exceeding ~2.5*pi without progress triggers recovery. */
	UPROPERTY()
	FFixedPoint YawAccumSinceProgress = FFixedPoint::Zero;
	UPROPERTY()
	int32 LastProgressWaypointIndex = -1;

	/** Reset every per-order / per-plan driver latch. */
	void ResetDriverState();

	/** Re-derive the segment-driver cache when the path identity changed
	 *  (initial plan or repath). Returns true if the cache was rebuilt. */
	bool RefreshPathCache(const struct FSeinPath& Path, FFixedPoint CurrentSpeed, FFixedPoint CuspFlipSpeed);
};
