/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.h
 * @brief   Tracked-vehicle movement — Arc/Pivot mode split by speed, with
 *          maneuver planning where planning beats pivoting.
 *
 *          Two modes, one transition speed (`PivotSpeed` from
 *          FSeinTrackedMovementData):
 *
 *            ARC MODE  (AbsSpeed > PivotSpeed):
 *              The chassis behaves like a wheeled vehicle. Full throttle,
 *              yaw rotates toward the steering target at `TurnRate × Dt`,
 *              and an optional sharp-turn brake (`SharpTurnBrakeAngle` /
 *              `SharpTurnBrakeStrength`) softens throttle for hard turns at
 *              high speed.
 *
 *            PIVOT MODE (AbsSpeed ≤ PivotSpeed):
 *              The chassis can pivot in place. If misaligned with the
 *              steering target (`dot < PivotAlignDot`), throttle = 0 and
 *              the chassis rotates at TurnRate without translating — the
 *              tracked-exclusive "spin to face." Once aligned, throttle
 *              goes to 1 and the chassis accelerates forward.
 *
 *          MANEUVER PLANNING (`PlanPath` override, gated by the sub-data's
 *          `bManeuverPlanning`): a neutral-steer tank needs far fewer planned
 *          words than a wheeled chassis — pivoting covers most misalignment
 *          for free — so the planner engages only where planning beats
 *          pivoting:
 *            - STRAIGHT REVERSE for close behind-goals (segment-native,
 *              re-decided on every repath — replaces the old whole-order
 *              auto-reverse latch).
 *            - MOMENTUM U-TURN ARC when ordered to turn around while already
 *              AT SPEED (forward, above max(2×PivotSpeed, TopSpeed/4)):
 *              sweeps a probed arc of radius `speed / TurnRate` (clamped to
 *              [100, 10000]) instead of braking to a pivot — the "rolling
 *              tank" turnaround. The driver holds arc speed at 7/8 of
 *              TurnRate·R so the yaw clamp keeps cross-track correction
 *              authority.
 *            - When `MinTurnRadius` is authored non-zero (a chassis declared
 *              non-pivoting), the FULL wheeled-style word ladder runs instead
 *              (U-turn / 3-point / reverse-out via the shared maneuver
 *              toolkit).
 *          Typed maneuver paths are driven by a geometric segment cursor
 *          (arc tangents + per-segment reverse + cusp gates); the tail and
 *          bManeuverPlanning-off units run the classic arc/pivot controller.
 *          Cusps cost tracked almost nothing: the stopped chassis simply
 *          pivots to the next leg's direction under the existing mode split.
 *
 *          Top-line tuning (TopSpeed / TurnRate / Accel / Decel / reverse)
 *          lives on FSeinMovementPayload. Tracked-specific tuning lives on
 *          FSeinTrackedMovementData, accessed via
 *          `FSeinMovementPayload::MovementClassData`. This class holds only
 *          per-instance runtime state (drive latch, segment cursor, stuck
 *          accumulators). Those non-editable fields are reflected so the
 *          framework's canonical movement provider can restore them exactly.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinTrackedVehicleMovement.generated.h"

struct FSeinMovementPayload;

UCLASS(meta = (DisplayName = "Tracked Vehicle"))
class SEINARTSMOVEMENTPLUS_API USeinTrackedVehicleMovement : public USeinMovement
{
	GENERATED_BODY()

public:

	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;
	virtual void UpdateSettledRenderState(
		const FSeinSettledMovementRenderContext& Context,
		const FSeinMovementPayload& MovementData,
		FSeinMovementRenderStateWriter& Writer) const override;
	virtual bool SupportsExactIdleMutationTracking() const override
	{
		return GetClass() == StaticClass();
	}

	/** Reset the driver/planner instance state when an order ends — the NEXT
	 *  order's initial plan runs BEFORE OnMoveBegin's reset, so stale
	 *  in-maneuver/recovery state must not leak into it. */
	virtual void OnMoveEnd(FSeinEntity& Entity) override;

	/** Maneuver-planning path resolve — see the class docstring for the
	 *  tracked word set. The destination is never relocated and Throttled /
	 *  NotFound pass through untouched. */
	virtual ESeinPathResult PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const override;

	/** Roll-through arrival: keep the (already kinematically-braked) residual
	 *  velocity instead of hard-zeroing; the idle coast-down finishes the
	 *  stop through GetDeceleration. */
	virtual FSeinMotion ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover) override;

	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementPayload* MovementData) const override;

	/** Per-class sub-data this movement consumes — the picker on
	 *  `FSeinMovementPayload::MovementClassData` swaps to this struct when
	 *  USeinTrackedVehicleMovement is selected. */
	virtual UScriptStruct* GetMovementDataStruct() const override;

	/** Braking rate for the impl-agnostic idle coast + arrival-imminent estimate — reads
	 *  Deceleration out of the unwrapped FSeinTrackedMovementData sub-data. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementPayload* MovementData) const override;

protected:

	/** LEGACY auto-reverse latch — used only when `bManeuverPlanning` is OFF
	 *  (the planner expresses reverse as typed segments instead). Tracked
	 *  needs no steering inversion (yaw control is direct); reversing flips
	 *  the steering target so the BACK faces the goal. */
	UPROPERTY()
	bool bIsReversing = false;

	// ------------------------------------------------------------------
	// Segment-driver state (typed maneuver paths) — the tracked port of the
	// wheeled driver's cursor/latch/recovery block (unify into a shared
	// Movement+ vehicle-driver helper once the wheeled PIE pass lands; kept
	// duplicated for now so this fork touches no wheeled files). All
	// derivable from hashed sim state + deterministic history; reset in
	// OnMoveBegin/OnMoveEnd and on path identity change.
	// ------------------------------------------------------------------

	/** Index of the typed segment currently being driven. Advanced
	 *  geometrically, never derived from the flattened waypoint backbone. */
	UPROPERTY()
	int32 SegCursor = 0;

	/** First segment index of the all-forward-straight TAIL. */
	UPROPERTY()
	int32 TailStartSeg = 0;

	/** Current drive direction latch: true = backing. Flips at a cusp once
	 *  |speed| has braked under the cusp epsilon; the stopped chassis then
	 *  pivots to the flipped leg's heading under the normal mode split. */
	UPROPERTY()
	bool bDriveReverseLatch = false;

	/** Path identity stamp (byte-exact) — includes TotalCost + the first
	 *  segment endpoint so same-shape plans with different middle geometry
	 *  still register as swaps. */
	UPROPERTY()
	int32 CachedPathWaypointNum = -1;
	UPROPERTY()
	int32 CachedPathSegmentNum = -1;
	UPROPERTY()
	FFixedVector CachedPathFirstWp = FFixedVector::ZeroVector;
	UPROPERTY()
	FFixedVector CachedPathLastWp = FFixedVector::ZeroVector;
	UPROPERTY()
	FFixedPoint CachedPathTotalCost = FFixedPoint::Zero;
	UPROPERTY()
	FFixedVector CachedPathFirstSegTo = FFixedVector::ZeroVector;

	/** Current hull yaw rate (radians/sec, signed) — the rotational-inertia
	 *  state behind the optional TurnAcceleration slew. Persists across
	 *  repaths (momentum), resets per order. Unused (stays untouched) while
	 *  TurnAcceleration is 0. */
	UPROPERTY()
	FFixedPoint CurrentYawRate = FFixedPoint::Zero;

	/** One tick of yaw with optional rotational inertia: returns the applied
	 *  yaw step toward closing `YawDelta`, clamped to TurnRate, with the rate
	 *  slewed through TurnAcceleration when it is authored (> 0) — and the
	 *  instant-rate legacy step (bit-exact) when it is 0. Overshoot-clamped:
	 *  never turns past the demand within a tick. Call with YawDelta = 0 on
	 *  no-steering ticks (cusp brake, recovery) so the rate decays instead of
	 *  going stale. */
	FFixedPoint StepYawWithInertia(FFixedPoint YawDelta, FFixedPoint TurnRate,
		FFixedPoint TurnAccel, FFixedPoint Dt);

	// Stuck / orbit protection (gated on bManeuverPlanning so OFF stays a
	// faithful legacy A/B control; suppressed near the goal).
	UPROPERTY()
	FFixedVector LastEntryPos = FFixedVector::ZeroVector;
	UPROPERTY()
	bool bLastEntryPosValid = false;
	UPROPERTY()
	FFixedPoint StuckTime = FFixedPoint::Zero;
	UPROPERTY()
	FFixedPoint ManeuverStallTime = FFixedPoint::Zero;
	UPROPERTY()
	FFixedPoint RecoveryTime = FFixedPoint::Zero;
	UPROPERTY()
	int32 RecoveryDir = 0;
	UPROPERTY()
	FFixedPoint YawAccumSinceProgress = FFixedPoint::Zero;
	UPROPERTY()
	int32 LastProgressWaypointIndex = -1;

	/** Reset every per-order / per-plan driver latch. */
	void ResetDriverState();

	/** Re-derive the segment-driver cache when the path identity changed. */
	bool RefreshPathCache(const struct FSeinPath& Path, FFixedPoint CurrentSpeed, FFixedPoint CuspFlipSpeed);
};
