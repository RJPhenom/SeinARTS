/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoverHandle.h
 * @brief   Blueprint-facing view of a movement tick's context.
 *
 *          `FSeinMovementContext` is a plain C++ struct (it holds an `FSeinEntity&`
 *          plus raw pointers), so it can never be a BlueprintNativeEvent parameter.
 *          This UObject is the bridge: the owning `USeinMovement` instance points a
 *          single reusable handle at the live context for the duration of one
 *          dispatch and hands it to the BP-overridable tick / hooks. All accessors
 *          read or write through that borrowed context.
 *
 *          DETERMINISM: the handle only forwards fixed-point reads/writes — it adds
 *          no math of its own and is never hashed (transient scratch). The wrapped
 *          context pointer is valid ONLY during the dispatch that set it; a BP graph
 *          must not stash the handle across ticks.
 *
 *          V1 surface = transform / velocity / authored kinematics / path / per-tick
 *          inputs. Nav queries, the steering toolkit, and the typed tuning accessor
 *          are added by later phases (see Movement_Mode_Authoring_Plan.md §6 D/B).
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinMoverHandle.generated.h"

struct FSeinMovementContext;
struct FSeinEntity;
class USeinMovement;

// `SeinDeterministic` (class meta) whitelists every accessor/toolkit node here for the
// movement determinism validator — they forward only fixed-point reads/writes.
UCLASS(BlueprintType, meta = (DisplayName = "Sein Mover Handle", SeinDeterministic))
class SEINARTSMOVEMENT_API USeinMoverHandle : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// C++-only wiring. The owning USeinMovement instance repoints the handle
	// at the live context before each BP dispatch. NOT BP-exposed — the
	// context pointer never escapes to Blueprint.
	// ----------------------------------------------------------------------
	void SetContext(const FSeinMovementContext* InCtx);
	const FSeinMovementContext* GetContext() const { return Ctx; }

	/** Bind in entity-only mode (no live movement context) — used by OnMoveEnd.
	 *  Transform reads/writes work; context reads (velocity/path/kinematics) return
	 *  defaults and IsValidMover() is false. */
	void SetEntityOnly(FSeinEntity* InEntity);

	/** True when the handle wraps a live context with a movement component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Is Valid Mover"))
	bool IsValidMover() const;

	// ---- Transform (read/write the entity pose this tick) ----

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Location"))
	FFixedVector GetLocation() const;

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Location"))
	void SetLocation(const FFixedVector& NewLocation);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Rotation"))
	FFixedQuaternion GetRotation() const;

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Rotation"))
	void SetRotation(const FFixedQuaternion& NewRotation);

	// ---- Velocity (persistent runtime state on the movement component) ----

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Velocity"))
	FFixedVector GetVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Velocity"))
	void SetVelocity(const FFixedVector& NewVelocity);

	/** Scalar magnitude of the current velocity. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Speed"))
	FFixedPoint GetSpeed() const;

	// ---- Authored kinematics (read-only; top-line FSeinMovementComponent fields) ----

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Top Speed"))
	FFixedPoint GetTopSpeed() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Acceleration"))
	FFixedPoint GetAcceleration() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Deceleration"))
	FFixedPoint GetDeceleration() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Turn Rate"))
	FFixedPoint GetTurnRate() const;

	// ---- Per-tick inputs ----

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Delta Time"))
	FFixedPoint GetDeltaTime() const;

	/** Terrain speed multiplier under the unit this tick (1 = normal, <1 mud, >1 road). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Terrain Speed Multiplier"))
	FFixedPoint GetTerrainSpeedMultiplier() const;

	/** Squared acceptance radius — the unit counts as arrived inside this of the final waypoint. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Acceptance Radius Sq"))
	FFixedPoint GetAcceptanceRadiusSq() const;

	// ---- Path / waypoints ----

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Waypoint Count"))
	int32 GetWaypointCount() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Current Waypoint Index"))
	int32 GetCurrentWaypointIndex() const;

	/** Set the active waypoint index. Writes the context's index by reference, so
	 *  the move action sees the advance after the dispatch returns. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Current Waypoint Index"))
	void SetCurrentWaypointIndex(int32 Index);

	/** Waypoint at `Index`, or zero if out of range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Waypoint"))
	FFixedVector GetWaypoint(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Current Waypoint"))
	FFixedVector GetCurrentWaypoint() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Final Waypoint"))
	FFixedVector GetFinalWaypoint() const;

	/** Planar (XY) distance from the unit's current location to the final waypoint. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Distance To Final"))
	FFixedPoint GetDistanceToFinal() const;

	// =====================================================================================
	// Steering toolkit (Tier 2 "power route"). Thin BP wrappers over USeinMovement's
	// deterministic steering helpers, pre-bound to this dispatch's context (path / nav /
	// delta / movement data). Use these to author a full Tick without re-deriving the math.
	// All fixed-point; each degrades to a safe default when the handle is unbound.
	// =====================================================================================

	/** Authored TopSpeed scaled by this tick's terrain multiplier — the cruise reference. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Effective Top Speed"))
	FFixedPoint GetEffectiveTopSpeed() const;

	/** Smooth-step a scalar speed toward Target over this tick (accel when growing, decel when shrinking). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Step Speed Toward"))
	FFixedPoint StepSpeedToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint Acceleration, FFixedPoint Deceleration) const;

	/** Fastest speed from which the unit can still brake to rest within DistanceToStop, given Decel. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Kinematic Arrival Speed Cap"))
	FFixedPoint KinematicArrivalSpeedCap(FFixedPoint DistanceToStop, FFixedPoint Deceleration) const;

	/** Speed-adaptive look-ahead distance: BaseDistance + AbsSpeed * TimeHorizon. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Compute Adaptive Look Ahead"))
	FFixedPoint ComputeAdaptiveLookAhead(FFixedPoint BaseDistance, FFixedPoint TimeHorizon, FFixedPoint AbsSpeed) const;

	/** The pure-pursuit carrot: the point LookAhead world-units along the path ahead of the unit. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Resolve Look Ahead Point"))
	FFixedVector ResolveLookAheadPoint(FFixedPoint LookAhead) const;

	/** Advance the current waypoint index past any waypoint the unit has crossed or reached. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Advance Waypoint"))
	void AdvanceWaypoint(FFixedPoint CloseRadius);

	/** Shortest signed angular delta From -> To, wrapped to [-pi, pi]. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Shortest Angle Delta"))
	FFixedPoint ShortestAngleDelta(FFixedPoint From, FFixedPoint To) const;

	/** Rate-limited move of Current toward Target, capped at MaxRatePerSec * dt this tick. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Smooth Angle Toward"))
	FFixedPoint SmoothAngleToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint MaxRatePerSec) const;

	/** Bend a normalized desired direction by this unit's precomputed local-avoidance steer. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Apply Avoidance Steer"))
	FFixedVector ApplyAvoidanceSteer(FFixedVector DesiredDir) const;

	/** Hard nav-collision floor: if NewPos lands on a blocked cell, axis-slide along walls or hold. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Resolve Nav Collision"))
	FFixedVector ResolveNavCollision(FFixedVector OldPos, FFixedVector NewPos) const;

	/** Snap Pos.Z to the ground plus this mode's altitude (rate-limited). Returns the adjusted Pos. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Apply Ground Snap And Altitude"))
	FFixedVector ApplyGroundSnapAndAltitude(FFixedVector Pos) const;

	/** Terrain slope pitch (radians, +uphill) at Pos facing Yaw. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Compute Slope Pitch"))
	FFixedPoint ComputeSlopePitch(FFixedVector Pos, FFixedPoint Yaw) const;

	/** Terrain slope roll (radians) at Pos facing Yaw. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Compute Slope Roll"))
	FFixedPoint ComputeSlopeRoll(FFixedVector Pos, FFixedPoint Yaw) const;

	/** Should the unit reverse to reach FinalGoal? Honors bCanReverse + the reverse thresholds. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Should Auto Reverse"))
	bool ShouldAutoReverse(FFixedVector FinalGoal) const;

private:

	/** The USeinMovement instance that owns (and repoints) this handle — its Outer. */
	USeinMovement* GetOwningMovement() const;

	/** Borrowed, valid only for the duration of one dispatch. Never owned, never hashed.
	 *  Stored const — the entity/component mutations the setters perform reach mutable
	 *  state through the context's reference/pointer members (same idiom the C++ loop
	 *  uses to mutate through its `const FSeinMovementContext&`). */
	const FSeinMovementContext* Ctx = nullptr;

	/** Entity bound this dispatch — from Ctx->Entity in full mode, or directly in
	 *  entity-only mode (OnMoveEnd). Transform accessors use this so they work in both. */
	FSeinEntity* EntityPtr = nullptr;
};
