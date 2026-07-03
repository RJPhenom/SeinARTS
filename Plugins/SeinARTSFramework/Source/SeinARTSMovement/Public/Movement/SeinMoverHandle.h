/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoverHandle.h
 * @brief   The Blueprint-facing view of a movement tick. Custom movement modes read and write
 *          the unit through this handle inside their Tick / hook events.
 *
 *          FSeinMovementContext (the real per-tick data) is a plain C++ struct holding an entity
 *          reference and raw pointers, so it can't be passed to a Blueprint event directly. The
 *          owning USeinMovement instance points this reusable handle at the live context for the
 *          duration of one dispatch and hands it to the BP event. Every node just forwards a
 *          fixed-point read or write to that borrowed context — it adds no math of its own and is
 *          never part of the hashed sim state. The handle is valid only during the dispatch that
 *          set it; a graph must not store it for later.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "GameplayTagContainer.h"  // FGameplayTag (Get Terrain Tag At return)
#include "SeinPathTypes.h"          // FSeinPathSegment (Get Segment)
#include "SeinMoverHandle.generated.h"

struct FSeinMovementContext;
struct FSeinEntity;
class USeinMovement;

// SeinDeterministic (class meta) whitelists every node here for the movement determinism
// validator — they forward only fixed-point reads/writes.
UCLASS(BlueprintType, meta = (DisplayName = "Sein Mover Handle", SeinDeterministic))
class SEINARTSMOVEMENT_API USeinMoverHandle : public UObject
{
	GENERATED_BODY()

public:

	// C++-only wiring (never exposed to BP): the owning USeinMovement repoints the handle at the
	// live context before each dispatch, or binds an entity alone for OnMoveEnd.
	void SetContext(const FSeinMovementContext* InCtx);
	const FSeinMovementContext* GetContext() const { return Ctx; }
	void SetEntityOnly(FSeinEntity* InEntity);

	/** True when this handle is driving a real movement tick.
	 *
	 *  False during On Move End (the unit is bound but there is no live path/velocity context), so
	 *  the velocity, path, and kinematics reads return zero there. Transform reads/writes always work. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Is Valid Mover"))
	bool IsValidMover() const;

	// ---- Transform (the unit's pose this tick) ----

	/** Where the unit is right now. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Location"))
	FFixedVector GetLocation() const;

	/** Move the unit to a new position this tick. Set this from your Tick once you have computed
	 *  where the unit should end up; the render side interpolates to it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Location"))
	void SetLocation(const FFixedVector& NewLocation);

	/** Which way the unit is currently facing. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Rotation"))
	FFixedQuaternion GetRotation() const;

	/** Set the unit's facing this tick. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Rotation"))
	void SetRotation(const FFixedQuaternion& NewRotation);

	// ---- Velocity (carried between ticks and orders) ----

	/** The unit's current world-space velocity. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Velocity"))
	FFixedVector GetVelocity() const;

	/** Set the unit's velocity. It persists on the unit between ticks and across orders, so a
	 *  re-issued move keeps its momentum and a cancelled one coasts to a stop. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Velocity"))
	void SetVelocity(const FFixedVector& NewVelocity);

	/** How fast the unit is going (the length of its velocity). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Speed"))
	FFixedPoint GetSpeed() const;

	// ---- Authored kinematics (the unit's top-line tuning) ----

	/** The unit's authored maximum speed. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Top Speed"))
	FFixedPoint GetTopSpeed() const;

	// Get Acceleration / Get Deceleration were removed 2026-07-02: accel/decel moved off the bare
	// FSeinMovementComponent into each Movement+ mode's per-class UDS. A custom BP mode reads them
	// from its own tuning data (MovementClassData) — they are not top-line component knobs.

	/** How fast the unit can turn (radians per second). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Turn Rate"))
	FFixedPoint GetTurnRate() const;

	// ---- Mode shape (per-class traits this mode reports for the unit) ----

	/** The tightest turn this unit can make, in world units (0 = it can pivot in place).
	 *
	 *  This mode's Get Min Turn Radius for this unit. Use it in a custom Tick to brake before turns
	 *  sharper than the unit can drive, or to shape a smoothed steering line. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Min Turn Radius"))
	FFixedPoint GetMinTurnRadius() const;

	/** How wide the unit is — the clearance its body needs from walls and other units.
	 *
	 *  Resolved from the unit's Extents (falling back to its nav footprint) — the same radius
	 *  collision and pathfinding use. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Footprint Radius"))
	FFixedPoint GetFootprintRadius() const;

	/** How high above the ground this mode flies (0 = ground-bound).
	 *
	 *  This mode's Get Altitude for this unit; the ground snap adds it on top of the terrain height. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Altitude"))
	FFixedPoint GetAltitude() const;

	// ---- This tick's inputs ----

	/** The length of this simulation tick, in seconds. Multiply speeds/rates by it to get this
	 *  tick's movement. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Delta Time"))
	FFixedPoint GetDeltaTime() const;

	/** The terrain speed multiplier under the unit this tick (1 = normal, below 1 = slow ground like
	 *  mud, above 1 = fast ground like roads). Already folded into Effective Top Speed. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Terrain Speed Multiplier"))
	FFixedPoint GetTerrainSpeedMultiplier() const;

	/** How close to the goal counts as arrived. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Acceptance Radius"))
	FFixedPoint GetAcceptanceRadius() const;

	/** The acceptance radius multiplied by itself.
	 *
	 *  The same value as Get Acceptance Radius but pre-squared, so you can compare it against a
	 *  squared distance and skip a square root in hot per-tick checks. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Acceptance Radius Squared"))
	FFixedPoint GetAcceptanceRadiusSquared() const;

	// ---- The path the unit is following ----

	/** How many waypoints the current path has. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Waypoint Count"))
	int32 GetWaypointCount() const;

	/** Which waypoint the unit is currently heading toward (its index in the path). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Current Waypoint Index"))
	int32 GetCurrentWaypointIndex() const;

	/** Change which waypoint the unit is heading toward. The move action sees the change after this
	 *  tick, so use it to skip ahead or hold on a waypoint. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Current Waypoint Index"))
	void SetCurrentWaypointIndex(int32 Index);

	/** The waypoint at a given index, or zero if the index is out of range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Waypoint"))
	FFixedVector GetWaypoint(int32 Index) const;

	/** The waypoint the unit is currently heading toward. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Current Waypoint"))
	FFixedVector GetCurrentWaypoint() const;

	/** The final waypoint — the unit's destination. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Final Waypoint"))
	FFixedVector GetFinalWaypoint() const;

	/** The flat (ignoring height) distance from the unit to its final waypoint. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Distance To Final Waypoint"))
	FFixedPoint GetDistanceToFinalWaypoint() const;

	/** How many typed segments the current path has — the typed stretches between its waypoints.
	 *
	 *  Where the waypoints are the turn points, a segment is the stretch from one waypoint to the
	 *  next, tagged with how to travel it (today always Straight). Read each with Get Segment to drive
	 *  a path by segment type — ease into a curve, follow an arc instead of a straight line. May be
	 *  zero for simple paths that never derived segments; when present it is the waypoint count minus
	 *  one. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Segment Count"))
	int32 GetSegmentCount() const;

	/** The typed segment at a given index — the stretch from waypoint Index to the next, carrying its
	 *  type (Straight today) and its From / To endpoints. Returns an empty Straight segment if the
	 *  index is out of range. Pair it with Get Segment Count to walk the path one segment at a time. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Segment"))
	FSeinPathSegment GetSegment(int32 Index) const;

	// ---- Render cues (sim → render, one-way) ----

	/** Emits a one-off render cue for this unit — a skid, a dust puff, an engine rev — for the render
	 *  layer to react to (VFX / SFX). Safe to call from a custom Tick.
	 *
	 *  You choose what the cue means via Cue Tag (e.g. Sein.Movement.Cue.Skid); the framework predefines
	 *  none. Value is a free payload the render side reads (turn sharpness, slip amount, impact, …). The
	 *  cue fires at the unit's current location. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Emit Movement Cue"))
	void EmitMovementCue(FGameplayTag CueTag, FFixedPoint Value) const;

	/** Stores a custom per-unit value at a slot for the render/anim layer to read — a bank angle, a
	 *  tread-speed delta, anything visual.
	 *
	 *  Render-only output: it drives visuals, not the simulation. Pick a slot index and document what it
	 *  means for your mode (slot 0 = bank, 1 = lean, …); read it back with Get Movement Render Value
	 *  (Sein Movement Library). Slots are zero until written. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (DisplayName = "Set Render Value"))
	void SetRenderValue(int32 Slot, FFixedPoint Value) const;

	// =====================================================================================
	// Steering toolkit. The same deterministic helpers the built-in movement loop uses,
	// pre-wired to this unit's path, navigation, delta time, and tuning — so you can build a
	// full custom Tick without re-deriving the math. Each returns a safe default if unbound.
	// =====================================================================================

	/** The unit's top speed for this tick, after terrain. Use this as your cruise target. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Effective Top Speed"))
	FFixedPoint GetEffectiveTopSpeed() const;

	/** Eases a speed toward a target over this tick, speeding up at Acceleration and slowing at
	 *  Deceleration. Feed it last tick's speed and your desired speed; use the result this tick. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Step Speed Toward"))
	FFixedPoint StepSpeedToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint Acceleration, FFixedPoint Deceleration) const;

	/** The fastest the unit can be going and still brake to a stop in the given distance.
	 *
	 *  Solves the braking-distance equation for the given Deceleration. Clamp your cruise speed to
	 *  this as the unit nears its goal and it will glide to a clean stop instead of overshooting. A
	 *  Deceleration of zero or less means "no braking limit" and returns a very large value — treat
	 *  it as unlimited, not a real target speed. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Arrival Speed Cap"))
	FFixedPoint GetArrivalSpeedCap(FFixedPoint DistanceToStop, FFixedPoint Deceleration) const;

	/** How far ahead to aim, scaled by speed: BaseDistance plus AbsSpeed times TimeHorizon.
	 *
	 *  Faster units look further down the path so they round corners smoothly; slow units stay
	 *  tight. Feed the result into Get Look Ahead Point. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Adaptive Look Ahead Distance"))
	FFixedPoint GetAdaptiveLookAheadDistance(FFixedPoint BaseDistance, FFixedPoint TimeHorizon, FFixedPoint AbsSpeed) const;

	/** A point on the path a set distance ahead of the unit — the target to steer toward.
	 *
	 *  This is the pure-pursuit "carrot": walk the given distance forward along the path and return
	 *  that point. Steer toward it for smooth path following. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Look Ahead Point"))
	FFixedVector GetLookAheadPoint(FFixedPoint LookAhead) const;

	/** Advances past any waypoints the unit has already reached or driven past.
	 *
	 *  Call this near the top of a custom Tick so the unit always aims at a waypoint ahead of it,
	 *  even after overshooting one at speed. CloseRadius is how near counts as reached. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Advance Waypoint"))
	void AdvanceWaypoint(FFixedPoint CloseRadius);

	/** The shortest turn from one angle to another, in radians (between minus pi and pi). Positive
	 *  turns one way, negative the other. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Shortest Angle Delta"))
	FFixedPoint ShortestAngleDelta(FFixedPoint From, FFixedPoint To) const;

	/** Eases an angle toward a target, turning no more than MaxRatePerSec this tick. Use it to
	 *  smooth pitch/roll (or any angle) so it never snaps. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Smooth Angle Toward"))
	FFixedPoint SmoothAngleToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint MaxRatePerSec) const;

	/** Bends a desired direction away from nearby units, returning where to actually head.
	 *
	 *  Applies the local avoidance nudge the system precomputed for this unit this tick. Pass your
	 *  normalized desired direction; head in the direction it returns. Soft steering only — the hard
	 *  no-overlap guarantee is enforced separately. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Apply Avoidance Steer"))
	FFixedVector ApplyAvoidanceSteer(FFixedVector DesiredDir) const;

	/** The avoidance SPEED-YIELD for this unit this tick — a 0..1 multiplier on cruise speed.
	 *
	 *  The local-avoidance layer can ask a unit to give way by SLOWING, not just turning. Multiply
	 *  your cruise speed by this (1 = full speed, no yield). Pair with Apply Avoidance Steer (heading)
	 *  for the full avoidance response in a custom Tick. The shipped boids model always returns 1. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Avoidance Speed Scale"))
	FFixedPoint GetAvoidanceSpeedScale() const;

	/** Stops a move from crossing into walls; returns a safe position.
	 *
	 *  Give it where the unit was and where it wants to go. If the target lands on a blocked cell it
	 *  slides along one axis so the unit skims the wall, or holds the old position if fully boxed in.
	 *  Checks only the static navigation, not other units. Use it as the last step of a custom Tick. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Clamp To Navigation"))
	FFixedVector ClampToNavigation(FFixedVector OldPos, FFixedVector NewPos) const;

	/** Sets the height of a position to sit on the ground (plus this mode's altitude); returns it.
	 *
	 *  Samples the ground under the position and snaps its height there, rate-limited so it ramps
	 *  over a few ticks near walls instead of popping. Run it on the unit's new position each tick. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Apply Ground Snap And Altitude"))
	FFixedVector ApplyGroundSnapAndAltitude(FFixedVector Pos) const;

	/** How steeply the ground tips up or down at a position for a unit facing a given way (radians,
	 *  positive is uphill). Use it to pitch the unit to match terrain. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Compute Slope Pitch"))
	FFixedPoint ComputeSlopePitch(FFixedVector Pos, FFixedPoint Yaw) const;

	/** How steeply the ground tips side to side at a position for a unit facing a given way
	 *  (radians). Use it to roll the unit to match terrain. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Compute Slope Roll"))
	FFixedPoint ComputeSlopeRoll(FFixedVector Pos, FFixedPoint Yaw) const;

	/** Tilts a facing to the ground slope and returns the unit's final rotation — the way the default
	 *  loop does it.
	 *
	 *  Give it the unit's new position and the yaw it should face. It tilts that yaw to the terrain
	 *  slope, eases the tilt over a few ticks so it never snaps, remembers the smoothed tilt between
	 *  ticks, and returns the full rotation to feed into Set Rotation. Use this in a custom Tick
	 *  instead of building a rotation from yaw alone — otherwise the unit pops on slopes and won't
	 *  match the idle/default behaviour. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Apply Slope Tilt"))
	FFixedQuaternion ApplySlopeTilt(FFixedVector Pos, FFixedPoint Yaw) const;

	/** The built-in reverse decision: should the unit back up to reach the goal?
	 *
	 *  This is the same check the Should Reverse hook uses by default — true when the unit can
	 *  reverse and the goal is close and behind it. Call it from a custom Tick if you want the
	 *  default behaviour without overriding the hook. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Default Reverse Decision"))
	bool GetDefaultReverseDecision(FFixedVector FinalGoal) const;

	// ---- Navigation probes ----

	/** Tests a straight line across the static navigation; returns whether it is blocked.
	 *
	 *  Returns true if something blocks the line (Out Hit Point is the first blocked spot), false if
	 *  the path is clear. Use it to check whether the unit can go straight before pathing around. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Nav Raycast"))
	bool NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const;

	/** The ground height at a world position. (Z is this engine's up axis.)
	 *
	 *  Returns false if there is no navigation data there. Walkable Only ignores blocked cells (use
	 *  it for ground units); turn it off to read the top of any cell (use it for flyers). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Sample Ground Height"))
	bool SampleGroundHeight(const FFixedVector& WorldPos, bool bWalkableOnly, FFixedPoint& OutHeight) const;

	/** The terrain type index under a world position (0 = default / no terrain).
	 *
	 *  Drives routing cost, traversal speed, and vision. The terrain SPEED factor under the unit is
	 *  already available as Get Terrain Speed Multiplier — use this when you need the raw type index. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Terrain Type At"))
	int32 GetTerrainTypeAt(const FFixedVector& WorldPos) const;

	/** The terrain tag at a world position — the friendly identifier for the terrain class there (e.g.
	 *  Terrain.Road). The named version of Get Terrain Type At; empty where there is no terrain. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Get Terrain Tag At"))
	FGameplayTag GetTerrainTagAt(const FFixedVector& WorldPos) const;

	/** Whether a unit could stand at a world position right now — walkable and not blocked (static
	 *  navigation plus current dynamic blockers). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit", meta = (DisplayName = "Is Position Clear"))
	bool IsPositionClear(const FFixedVector& WorldPos) const;

	/** Asks the navigation "from where I am now, which way to the goal?" — returns a planar direction.
	 *
	 *  The pull-style nav query a FIELD-FOLLOWER movement mode samples each tick: it returns the unit
	 *  direction to head (zero = stop / arrived / no route), from this unit's current position toward Goal.
	 *  A field-based nav (flow field) answers cheaply from its precomputed field; the shipped grid nav
	 *  answers by routing (so for the grid nav, a normal waypoint-following mode is cheaper than polling
	 *  this). Group Id lets a shared-field nav reuse one field across an ordered group (0 = lone). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Toolkit",
		meta = (DisplayName = "Query Nav Direction", AdvancedDisplay = "GroupId"))
	FFixedVector QueryNavDirection(FFixedVector Goal, int64 GroupId = 0) const;

	// =====================================================================================
	// Debug draw. Editor/development only — these do nothing in a shipping build and never
	// touch simulation state, so they are safe to call from a custom Tick to SEE what your
	// graph computed: the point you steer toward, your turn-radius circle, a steer vector, etc.
	// =====================================================================================

	/** Draws a line between two world points for one frame (development builds only). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Debug", meta = (DisplayName = "Draw Debug Line"))
	void DebugLine(const FFixedVector& Start, const FFixedVector& End, FLinearColor Color, FFixedPoint Thickness) const;

	/** Draws an arrow from Start to End for one frame (development builds only). Good for showing a
	 *  steer or desired-direction vector. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Debug", meta = (DisplayName = "Draw Debug Arrow"))
	void DebugArrow(const FFixedVector& Start, const FFixedVector& End, FLinearColor Color, FFixedPoint Thickness) const;

	/** Draws a sphere at a world point for one frame (development builds only). Good for marking a
	 *  target or the look-ahead "carrot" point. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Debug", meta = (DisplayName = "Draw Debug Sphere"))
	void DebugSphere(const FFixedVector& Center, FFixedPoint Radius, FLinearColor Color) const;

	/** Draws a flat circle on the ground at a world point for one frame (development builds only).
	 *  Good for visualizing a turn-radius circle. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Debug", meta = (DisplayName = "Draw Debug Circle"))
	void DebugCircle(const FFixedVector& Center, FFixedPoint Radius, FLinearColor Color) const;

private:

	USeinMovement* GetOwningMovement() const;

	// Borrowed for one dispatch only; never owned, never hashed. Stored const — the setters reach
	// mutable state through the context's reference/pointer members (the idiom the C++ loop uses).
	const FSeinMovementContext* Ctx = nullptr;

	// The entity bound this dispatch (from the context, or alone in OnMoveEnd) — transform accessors
	// use this so they work in both modes.
	FSeinEntity* EntityPtr = nullptr;
};
