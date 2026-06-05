/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovement.h
 * @brief   Abstract base for per-unit movement modes.
 *
 *          USeinMoveToAction resolves a path via USeinNavigation, then
 *          delegates per-tick advancement to a subclass of this. Designers
 *          pick the movement per unit via FSeinMovementComponent::MovementClass;
 *          null defaults to USeinBasicMovement.
 *
 *          Shipped subclasses:
 *          - USeinBasicMovement            — direct seek + arrive (default)
 *          - USeinInfantryMovement         — basic + face-velocity + smoothed kinematics
 *          - USeinWheeledVehicleMovement   — turn-while-moving, no in-place rotation
 *          - USeinTrackedVehicleMovement   — can rotate in place, slow accel
 *
 *          Movement instances are owned by the action (one per active move).
 *          Long-lived state (current waypoint index) lives on the action and
 *          is passed in by reference; transient per-instance state (facing
 *          interpolation, accel ramp) can live on the movement itself.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinPathTypes.h"
#include "SeinMovement.generated.h"

class USeinNavigation;
class USeinNavigationSubsystem;
class USeinWorldSubsystem;
class UScriptStruct;
struct FSeinEntity;
struct FSeinMovementComponent;
struct FSeinNavigationComponent;

/**
 * Per-tick context bundle handed to a movement. One struct so we can add new
 * services (formation, threat field) without further signature churn —
 * movement subclasses just read what they care about and ignore the rest.
 * All references are valid for the duration of a single Tick call.
 *
 * Post-decomposition: carries pointers to both `FSeinMovementComponent` (the
 * movement-class authoring + runtime state) AND `FSeinNavigationComponent` (the
 * pathfinding / nav-layer / repath authoring). MovementData is null only for
 * entities with no move component — most subclasses should bail early if
 * MovementData is null; NavData null is more permissive (some subclasses with
 * BypassPathfinding don't need it).
 */
struct FSeinMovementContext
{
	FSeinEntity& Entity;
	FSeinMovementComponent* MovementData;    // movement authoring + runtime state
	const FSeinNavigationComponent* NavData; // pathfinding / nav-layer / repath
	const FSeinPath& Path;
	int32& CurrentWaypointIndex;
	FFixedPoint AcceptanceRadiusSq;
	FFixedPoint DeltaTime;
	USeinNavigation* Nav;
	USeinWorldSubsystem* World;     // gives access to spatial hash + components
	FSeinEntityHandle SelfHandle;   // self-exclusion in spatial queries

	/** True when the move's final destination (Path's last waypoint) is an
	 *  AUTHORITATIVE position — a cover slot that overrules the coarse nav bake.
	 *  The mover may step onto / occupy it even if its cell is bake-blocked.
	 *  Set by USeinMoveToAction from the AuthoritativeDestinationResolver. */
	bool bAuthoritativeDestination = false;
};

/**
 * Per-call context for USeinMovement::PlanPath. Bundled so subclasses can
 * compose movement-specific planning pipelines (e.g. cell A* → kinematic-curve
 * fit → arc-tagged segments for wheeled) without further signature churn.
 *
 * Lifecycle differs from FSeinMovementContext: this is built once per path
 * resolution (initial + each repath), not once per sim tick. All references
 * are valid for the duration of the PlanPath call.
 */
struct FSeinPlanPathContext
{
	const FSeinEntity& Entity;
	const FSeinMovementComponent* MovementData;
	const FSeinNavigationComponent* NavData;
	FFixedVector Destination;
	USeinNavigation* Nav;
	USeinNavigationSubsystem* NavSub;
	FSeinEntityHandle SelfHandle;
	/** World subsystem — needed by `PlanPath` to resolve the entity's
	 *  collision footprint cascade (Extents → NavComp). Without this,
	 *  the path planner could only see `NavData->FallbackFootprintRadius`,
	 *  which is the FALLBACK in the cascade — leading to a body-size
	 *  mismatch between path planning (uses NavComp) and runtime
	 *  collision (uses Extents). Vehicles got stuck on corners
	 *  because of this exact mismatch. */
	USeinWorldSubsystem* World;
};

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Sein Movement"))
class SEINARTSMOVEMENT_API USeinMovement : public UObject
{
	GENERATED_BODY()

public:

	/** Called once when a MoveTo action resolves a path (before the first
	 *  Tick). Default: no-op. Override to initialize subclass state.
	 *  Receives the same context shape as Tick — convenient for one-shot
	 *  decisions (e.g. auto-reverse latch) that depend on neighbor queries
	 *  or world services. MoveData is non-const so subclasses can read
	 *  carry-over runtime state (e.g. Velocity) — preserve it instead
	 *  of zeroing if you want momentum to flow across order changes. */
	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) {}

	/** Called each sim tick while the move is active. Mutates Entity.Transform
	 *  and writes runtime state back through MoveData (e.g. Velocity).
	 *  Advances CurrentWaypointIndex as waypoints are consumed.
	 *  @return true when the entity has reached the final waypoint. */
	virtual bool Tick(const FSeinMovementContext& Ctx)
		PURE_VIRTUAL(USeinMovement::Tick, return true;);

	/** Called when the action ends (completed/cancelled/failed). Default:
	 *  no-op. Override to clean up subclass transient state. */
	virtual void OnMoveEnd(FSeinEntity& Entity) {}

	/** True if this movement subclass does NOT need a `Nav->FindPath` call —
	 *  it consumes a straight-line `[Start, End]` polyline directly. Flying
	 *  movements override to true: they fly over static obstacles (using
	 *  the no-gate variant of `GetCellHeightAt` for Z, which auto-clears
	 *  anything in the cell) and don't benefit from A*. Ground movements
	 *  stay false. The action reads this on first tick to decide whether
	 *  to call PlanPath at all on repath ticks (flying paths don't drift in
	 *  any meaningful sense — see the action's repath gate). The default
	 *  `PlanPath` impl also reads it to decide between synthesize-straight-
	 *  line and Nav->RequestPath. */
	virtual bool BypassPathfinding() const { return false; }

	/** Returns the per-class sub-data UScriptStruct this movement consumes
	 *  out of `FSeinMovementComponent::MovementClassData`. Used by the custom
	 *  details panel to auto-swap the MovementClassData field's struct type
	 *  when designers change the MovementClass selection, AND by runtime
	 *  helpers that need to unwrap the per-class authoring (Altitude
	 *  resolution, per-class kinematic params, etc.).
	 *
	 *  Default returns nullptr — the subclass has no per-class authoring
	 *  (e.g., bare USeinBasicMovement). Subclasses with sub-data override
	 *  to return e.g. `FSeinWheeledMovementData::StaticStruct()`. */
	virtual UScriptStruct* GetMovementDataStruct() const { return nullptr; }

	/** Cruise / hover altitude the unit wants to maintain above ground.
	 *  Default 0 (ground-bound). Subclasses with their own altitude field
	 *  (hover / flight) override to fetch from their MovementClassData
	 *  sub-struct. */
	virtual FFixedPoint GetAltitude(const FSeinMovementComponent* MovementData) const { return FFixedPoint::Zero; }

	/** One-time immediate ground + slope snap for a spawned/placed entity that has
	 *  not yet moved. Snaps the entity's Z to the nav reference height (+ this
	 *  movement's GetAltitude) and sets pitch/roll to match the terrain slope under
	 *  the entity's current facing — the same result the movement Tick produces, but
	 *  instant (no smoothing). Writes Entity.Transform (Z + rotation) and
	 *  MovementData.SmoothedPitch/Roll. Safe no-op when Nav is null or can't sample
	 *  (out of bounds / no bake). Const and routes through the virtual
	 *  QueryReferenceZ / GetAltitude, so it can be called on a class CDO
	 *  (FSeinInitialSnapSystem does exactly that). */
	void SnapToGroundImmediate(FSeinEntity& Entity, FSeinMovementComponent& MovementData, USeinNavigation* Nav) const;

	/** Plan a path from the entity's current position to `Ctx.Destination`.
	 *  Called once at move start, and again on each repath (Interval or
	 *  OffPathOnly drift).
	 *
	 *  Default implementation:
	 *    - If `BypassPathfinding()` is true, synthesizes a straight-line
	 *      two-waypoint path `[currentPos, destination]`. Used by flying
	 *      movements that don't path through static obstacles.
	 *    - Otherwise, builds an `FSeinPathRequest` populated with kinematic
	 *      hints from `MovementData` + `NavData` (footprint radius, velocity-
	 *      scaled turn radius, wall padding, start heading) and routes it
	 *      through `Ctx.NavSub->RequestPath` — the budgeted call that
	 *      returns `Throttled` when the per-tick path-request budget is spent.
	 *
	 *  Subclasses override to compose movement-specific planning pipelines.
	 *  Wheeled, for example, can call the nav's cell A* to get a coarse
	 *  polyline, then post-process through a kinematic curve fitter that
	 *  emits arc + straight + reverse segments suited to its turn dynamics
	 *  (CoH-style armored car driving). Helicopters / jets compose their
	 *  own 3D planners.
	 *
	 *  Return values:
	 *    Found        — OutPath valid (check bIsPartial for partial/best-effort)
	 *    NotFound     — no path exists; caller should fail the order
	 *    Throttled    — per-tick budget spent; caller should retry next tick
	 *    NoNavigation — nav subsystem unavailable; caller should fail
	 *
	 *  Const because PlanPath should never mutate the movement instance's
	 *  state — that's what OnMoveBegin / Tick are for. */
	virtual ESeinPathResult PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const;

	/** Maximum speed at which the unit can still brake to zero EXACTLY at
	 *  `DistToFinal` ahead, given `Deceleration`. Solves v² = 2·a·d for v.
	 *  Returns a very large value when Deceleration <= 0 (no kinematic cap
	 *  desired; falls back to MoveSpeed). Public because the move-to action
	 *  uses it to derive `bArrivalImminent` (unit is in the brake zone iff
	 *  this cap < cruise MoveSpeed); also used by the vehicle movement
	 *  subclasses for their internal arrival ramps. */
	static FFixedPoint KinematicArrivalSpeedCap(
		FFixedPoint DistToFinal, FFixedPoint Deceleration);

	/** Minimum turn radius (world units) the unit's steering can physically
	 *  execute. Drives two downstream features:
	 *    1. Path corner rounding (nav layer) — corners get arcs of radius
	 *       `min(half-incoming-segment, half-outgoing-segment, MinTurnRadius)`
	 *       so vehicles trace smooth curves they can actually drive.
	 *    2. Curvature-aware throttle preview — vehicles slow when the path
	 *       ahead curves tighter than their min radius.
	 *
	 *  Default 0 — no rounding constraint. Infantry, BasicMovement, and
	 *  tracked vehicles (which can pivot in place) leave it at 0. Wheeled
	 *  overrides via the bicycle kinematic identity `Wheelbase / tan(MaxSteerAngle)`,
	 *  reading both values from `MovementData::MovementClassData` unwrapped as
	 *  `FSeinWheeledMovementData`. Tracked may also opt into a preferred radius
	 *  via its own per-class data.
	 *
	 *  Takes `MovementData` so subclass overrides can read their per-class
	 *  sub-data (Wheelbase / MaxSteerAngle / preferred-radius) directly. */
	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementComponent* /*MovementData*/) const { return FFixedPoint::Zero; }

#if UE_ENABLE_DEBUG_DRAWING
	/** Always-on steering-vector viz for a single entity. Draws:
	 *    - Yellow horizontal footprint ring (radius `FootprintRadius`) at the
	 *      entity's XY plane + a small ZLift so it sits above terrain.
	 *    - Cyan arrow along `Velocity` (world units / sec) — origin offset
	 *      along the velocity direction by `FootprintRadius` so it isn't
	 *      occluded by the chassis mesh; length scaled by `VelocityScale`
	 *      (seconds-of-projected-travel) for readable magnitude at typical
	 *      sim speeds.
	 *
	 *  Decoupled from the per-Tick carrot viz in subclass Tick blocks —
	 *  that one fires only for entities inside an active move action, this
	 *  one fires for every entity with a movement component (idle units
	 *  included). Called from the module's per-world ticker; gating on
	 *  the SeinSteering show flag and the camera-cull / budget cap happens
	 *  at the call site. No-op when FootprintRadius <= 0 (intangible units
	 *  opt out of viz by design).
	 *
	 *  Draws the footprint ring + two WORLD-SPACE arrows straight from the entity (NOT rotated
	 *  by the chassis transform — both inputs are already world vectors): ORANGE = the VELOCITY
	 *  vector (entity → velocity, at true magnitude); RED = the local-avoidance steer expressed
	 *  as the sideways velocity it adds (AvoidanceSteer × speed), comparable to the velocity
	 *  arrow. Both skip when ~zero. Callers pass zero while a unit has no active move order, so a
	 *  unit at rest shows the ring only (its stored Velocity / AvoidanceSteer may be stale).
	 *
	 *  Pure draw — no sim mutation, safe to call off the sim tick. */
	static void DrawSteeringDebugViz(
		UWorld* World,
		const FFixedVector& EntityPos,
		float FootprintRadius,
		const FFixedVector& Velocity,
		const FFixedVector& AvoidanceSteer = FFixedVector::ZeroVector);
#endif // UE_ENABLE_DEBUG_DRAWING

	/** Populate the footprint cache used by `ResolveNavCollision`. Called by
	 *  `USeinMoveToAction` once at first-tick setup, before `OnMoveBegin`.
	 *
	 *  Cascade for the effective collision radius:
	 *    Tier 1: `FSeinExtentsComponent` on the entity if present. For Capsule
	 *      shapes uses `Radius`; for Box uses `max(HalfExtentX, HalfExtentY)`
	 *      as a conservative bounding circle. Compound entities take the
	 *      max bounding radius across all shapes.
	 *    Tier 2: `NavData.FallbackFootprintRadius` (used only when no Extents).
	 *    Tier 3: 0 — point-only, no ring samples.
	 *
	 *  Precomputes 8 ring sample offsets at the resolved radius. Per-tick
	 *  collision checks then sample 1 center + 8 ring points = 9 IsPassable
	 *  calls per step, ~450ns total. Entity-stable for the duration of the
	 *  move action; never re-derived per tick. */
	void CacheFootprintFromContext(const FSeinMovementContext& Ctx);

	/** Resolve the effective collision radius for an entity via the shared
	 *  cascade: Extents.Shapes (max bounding radius across all shapes) →
	 *  NavData->FallbackFootprintRadius → 0. Used by BOTH collision (footprint-
	 *  sample cache built by `CacheFootprintFromContext`) AND path planning
	 *  (path request clearance built by `PlanPath`) so they agree on body
	 *  size — without this shared resolution, A* could plan for one radius
	 *  while the runtime body had a different one, producing the classic
	 *  "planned path that the body physically can't follow" stuckness.
	 *
	 *  Box shape conversion: takes `max(HalfExtentX, HalfExtentY)` as a
	 *  conservative bounding-circle radius. This is correct for radially-
	 *  symmetric movement (infantry, hover) but conservative for elongated
	 *  units (long tanks): the planner refuses corridors narrower than the
	 *  bounding circle even when the body could fit if perfectly oriented.
	 *
	 *  // TODO(PlannerAStar): Orientation-aware pathfinding can fit a long
	 *  // tank through a corridor narrower than its bounding circle by
	 *  // tracking facing per A* node (per-orientation state). That belongs
	 *  // in the turn-planning nav variant (SeinNavigationPlannerAStar)
	 *  // alongside Reeds-Shepp curve fitting, NOT here. The base AStar
	 *  // uses the conservative bounding circle and that's the correct
	 *  // trade-off for generic / infantry-centric games. */
	static FFixedPoint ResolveCollisionRadius(
		USeinWorldSubsystem* World,
		FSeinEntityHandle SelfHandle,
		const FSeinNavigationComponent* NavData);

protected:

	// ----------------------------------------------------------------------
	// Footprint cache for ResolveNavCollision. Populated once per move
	// action by `CacheFootprintFromContext` (called from MoveToAction's
	// first-tick setup, before OnMoveBegin). Entity-stable for the duration
	// of the move — no per-tick component lookups.
	// ----------------------------------------------------------------------

	/** Effective collision radius (world units) from the
	 *  Extents → FallbackFootprintRadius → 0 cascade. 0 = point-only check. */
	FFixedPoint CachedCollisionRadius = FFixedPoint::Zero;

	/** Number of ring samples in CachedFootprintSamples. 0 when
	 *  CachedCollisionRadius == 0 (point-only). Otherwise 8 (45° spacing). */
	int32 CachedNumFootprintSamples = 0;

	/** Local-space XY offsets for footprint sampling. Computed from
	 *  CachedCollisionRadius once per move action; passed to IsPassable
	 *  at each ResolveNavCollision call. Fixed-size to avoid per-tick
	 *  allocations. */
	FFixedVector CachedFootprintSamples[8];

	/** Maximum traversable height difference between adjacent positions.
	 *  Prevents units from stepping up vertical walls whose top cells are
	 *  passable (connected to ground elsewhere). Default 75 world units —
	 *  allows slopes up to ~78° at typical per-tick step distances while
	 *  rejecting multi-meter wall step-ups. Set 0 to disable. */
	FFixedPoint CachedMaxStepHeight = FFixedPoint::FromInt(75);

	/** Shortest signed angular delta from `From` to `To`, wrapped to [-π, π]. */
	static FFixedPoint ShortestAngleDelta(FFixedPoint From, FFixedPoint To);

	/** Clamp `Val` to [Min, Max]. */
	static FFixedPoint ClampFP(FFixedPoint Val, FFixedPoint Min, FFixedPoint Max);

	/** Rate-limited move of `Current` toward `Target` — change per call is
	 *  capped at `MaxChangePerSec * DeltaTime`. Used by ground movements to
	 *  smooth pitch/roll updates so a brief spike in the slope sampler
	 *  (e.g. crossing a wall edge for a few ticks) doesn't produce a visible
	 *  "pop" in the unit's orientation. Combined with the per-call cap in
	 *  `ComputeSlopePitch` / `ComputeSlopeRoll`, this filters both the
	 *  *magnitude* (sustained extreme tilts) and the *rate* (abrupt changes)
	 *  of orientation updates. No angle-wrap handling — pitch/roll stay in
	 *  [-π/2, +π/2] for upright units, never wrap. */
	static FFixedPoint SmoothAngleToward(
		FFixedPoint Current,
		FFixedPoint Target,
		FFixedPoint MaxChangePerSec,
		FFixedPoint DeltaTime);

	/** Build a yaw-only rotation quaternion (upright units, flat ground). */
	static FFixedQuaternion YawOnly(FFixedPoint YawRadians);

	/** Build a yaw + pitch + roll rotation quaternion. Follows the
	 *  MakeFromEulers (X=Roll, Y=Pitch, Z=Yaw) convention. Pitch
	 *  positive = nose up (climbing), Roll positive = right side down
	 *  (UE left-handed). Used by ground movements to tilt the entity to
	 *  match terrain slope in both the forward and lateral axes. */
	static FFixedQuaternion YawPitchRoll(FFixedPoint YawRadians, FFixedPoint PitchRadians, FFixedPoint RollRadians);

	/** Compute slope pitch at `Pos` by sampling terrain height at two
	 *  points along the facing direction (Yaw). Returns the pitch angle
	 *  in radians (positive = uphill). Returns zero when nav is null or
	 *  either height sample fails. Non-static because it routes through
	 *  the virtual QueryReferenceZ for walkable-only gating. */
	FFixedPoint ComputeSlopePitch(
		const FFixedVector& Pos,
		FFixedPoint Yaw,
		USeinNavigation* Nav) const;

	/** Compute slope roll at `Pos` by sampling terrain height at two
	 *  points perpendicular to the facing direction (Yaw). Returns the
	 *  roll angle in radians, sign-matched to MakeFromEulers' convention
	 *  (positive Roll = right side down). When the right-side terrain is
	 *  higher, returns a negative angle so the entity banks right-side-
	 *  up to match the slope. Returns zero when nav is null or either
	 *  sample fails. */
	FFixedPoint ComputeSlopeRoll(
		const FFixedVector& Pos,
		FFixedPoint Yaw,
		USeinNavigation* Nav) const;

	/** Yaw in radians extracted from the forward vector of a rotation. Matches
	 *  atan2(forward.Y, forward.X) so it round-trips with YawOnly. */
	static FFixedPoint YawFromRotation(const FFixedQuaternion& Rotation);

	/** Walk forward along the path polyline starting at AgentPos and return
	 *  the point reached after consuming `LookAhead` world-units of travel.
	 *  The first segment is (AgentPos → Path.Waypoints[CurrentWaypointIndex])
	 *  so the carrot is always strictly ahead of the unit; subsequent
	 *  segments connect successive waypoints. If `LookAhead` exceeds the
	 *  remaining path length, returns the final waypoint (clamped). Returns
	 *  AgentPos when the path is empty or the index is past the end.
	 *  Z is interpolated linearly along the active segment by XY fraction so
	 *  the carrot tracks the path's elevation profile continuously.
	 *
	 *  Before walking, the polyline is THINNED: waypoints that are close to
	 *  their predecessor (<2m) AND roughly collinear (<30° direction change)
	 *  with the next segment are dropped. These are smoother-emitted
	 *  intermediates (LoS failed on an off-path wall-edge cell), not real
	 *  corners, and including them in the carrot walk causes the chassis to
	 *  steer toward each in turn (visible as zigzag near the agent in tight
	 *  terrain). Thinning is per-call and never touches Path.Waypoints —
	 *  drift / repath logic still sees all original waypoints.
	 *
	 *  `MaxCornerAngleRadians` — DEPRECATED. Previously controlled a cos(angle)
	 *  weighting on segment budget consumption to soften corner anticipation;
	 *  removed because it interacted badly with off-path drift and never
	 *  cleanly solved the wheeled corner-cutting problem. Parameter retained
	 *  for ABI compatibility but ignored. Use the new cluster-skip thinning
	 *  for cluster handling; for sharp-corner anticipation, lean on lower
	 *  `LookAheadDistance` instead. */
	static FFixedVector ResolveLookAheadPoint(
		const FFixedVector& AgentPos,
		const FSeinPath& Path,
		int32 CurrentWaypointIndex,
		FFixedPoint LookAhead,
		FFixedPoint MaxCornerAngleRadians = FFixedPoint::Zero);

	/** Cross-over waypoint advance — advances `CurrentWaypointIndex` past
	 *  any waypoint the agent has CROSSED along the segment toward the
	 *  next waypoint, not just any waypoint within `CloseRadius`.
	 *
	 *  Why: distance-based advance alone (the legacy "if (dist <
	 *  CloseRadius) ++Idx" loop) misses crossover when a vehicle
	 *  overshoots a waypoint at speed — the waypoint ends up BEHIND the
	 *  agent but outside the close-radius, so the index never advances.
	 *  `ResolveLookAheadPoint` then walks BACKWARD along the (agent →
	 *  behind-waypoint) segment, producing a carrot behind the chassis.
	 *  Pure pursuit happily aims at the backward carrot and the vehicle
	 *  spins off-path. The dot-product crossover test catches this
	 *  regardless of overshoot magnitude.
	 *
	 *  Algorithm: while CurrentWaypointIndex < N - 1, test
	 *    1. Crossover: dot(AgentPos - W[i], W[i+1] - W[i]) > 0 → past W[i].
	 *    2. Distance fallback: |AgentPos - W[i]| < CloseRadius → close enough.
	 *  Either advances; otherwise stops.
	 *
	 *  Last-waypoint case: arrival logic owns it; this helper never
	 *  advances PAST the final waypoint. CloseRadius typically
	 *  `max(2 × OneStep, 50cm)` — sized to bridge the per-tick step
	 *  while still being meaningful at low speed. */
	static void AdvanceWaypointAlongPath(
		int32& CurrentWaypointIndex,
		const FSeinPath& Path,
		const FFixedVector& AgentPos,
		FFixedPoint CloseRadius);


	/** Speed-adaptive look-ahead distance:
	 *      Effective = max(0, BaseDistance + AbsSpeed × TimeHorizon)
	 *  Slow units get tight steering (BaseDistance dominates); fast units
	 *  look further ahead (TimeHorizon × Speed dominates), so the carrot
	 *  crosses corners before the chassis does — produces visibly smoother
	 *  arcs at speed without sacrificing precision at low speed.
	 *
	 *  TimeHorizon is in seconds (canonical: 0.4–0.8s). AbsSpeed is the
	 *  magnitude of the unit's current scalar speed (sign discarded — used
	 *  by both forward and reverse symmetrically). */
	static FFixedPoint ComputeAdaptiveLookAhead(
		FFixedPoint BaseDistance,
		FFixedPoint TimeHorizon,
		FFixedPoint AbsSpeed);

	/** Smooth-step a scalar speed toward `Target`. Picks `Accel` rate when the
	 *  speed magnitude is growing (or sign-flipping through zero), `Decel`
	 *  when shrinking — matches the asymmetric throttle/brake feel of
	 *  vehicles where braking is harder than accel. Returns the new speed
	 *  after one tick of `Dt`. Pure scalar — sign carries reverse direction. */
	static FFixedPoint StepSpeedToward(
		FFixedPoint Current, FFixedPoint Target,
		FFixedPoint Accel, FFixedPoint Decel, FFixedPoint Dt);

	/** Hard nav-collision resolve for a translation step. Wall avoidance is a
	 *  steering FORCE — it can be overcome by path-attraction + momentum, so
	 *  units can clip through static blocked cells. This is the floor: if
	 *  `NewPos` lands on an impassable cell, try axis-only slides
	 *  (X-only, then Y-only) so the unit skims grid-aligned walls instead of
	 *  stopping dead; if both axes are blocked, hold at `OldPos`.
	 *
	 *  Footprint-aware: checks not just the center cell of the candidate
	 *  position but also `CachedNumFootprintSamples` ring points around it
	 *  at `CachedCollisionRadius`. Any ring sample on a blocked cell rejects
	 *  the candidate. When `CachedCollisionRadius == 0` (no Extents component
	 *  and no `NavData.FallbackFootprintRadius`), the ring is empty and behavior
	 *  collapses to legacy point-only check.
	 *
	 *  Static-only by design — `Nav->IsPassable` reads the bake, not the
	 *  dynamic blocker overlay (that lives on a per-FindPath path); blocking
	 *  vehicles against each other is penetration resolution's job. So this
	 *  is "don't walk through walls", not "don't walk through tanks." */
	FFixedVector ResolveNavCollision(
		const FFixedVector& OldPos,
		const FFixedVector& NewPos,
		USeinNavigation* Nav,
		const FFixedVector* AuthoritativeDest = nullptr) const;

	/** Footprint-aware passability check at a candidate position. True iff
	 *  the candidate's center AND every cached ring sample land on passable
	 *  cells (per `CachedCollisionRadius` / `CachedFootprintSamples`).
	 *  Subclasses use this for direction-clear probes — e.g., the unstick
	 *  state machine probes the reverse direction before committing to
	 *  Reversing, so a vehicle in a corner doesn't reverse INTO another
	 *  wall. When `CachedNumFootprintSamples == 0` (no Extents + no
	 *  FallbackFootprintRadius), degrades to legacy point-only check. */
	bool IsFootprintPassable(const FFixedVector& Pos, USeinNavigation* Nav) const;

	/** Bend a NORMALIZED desired direction by this unit's precomputed local-
	 *  avoidance steer (`FSeinMovementComponent::AvoidanceSteer`, written one-sided
	 *  at PreTick by `FSeinAvoidanceSystem`). This is the single integration point
	 *  every movement mode calls, so the general avoidance system applies across
	 *  all of them. Soft layer — the hard penetration floor remains the no-overlap
	 *  guarantee.
	 *
	 *  PURE READ: never query the spatial hash or read neighbour state here.
	 *  Movement runs through the insertion-ordered latent-action manager, so a
	 *  neighbour read at this point would be order-dependent → desync. Bit-exact
	 *  no-op when the steer is ~zero (returns the input direction UNCHANGED), so an
	 *  opted-out unit moves identically to a world with no avoidance. Input is
	 *  assumed unit-length; the steer is sized in that same unit space. */
	FFixedVector ApplyAvoidanceSteer(const FSeinMovementContext& Ctx, const FFixedVector& DesiredDir) const;

	// CacheFootprintFromContext is declared in the public section below —
	// called by USeinMoveToAction's first-tick setup.

	/** Resolve `NewPos.Z` against the nav's reference Z sample plus the
	 *  entity's altitude (queried via the virtual `GetAltitude`). The
	 *  reference sample is sourced via `QueryReferenceZ` — overrideable
	 *  per-subclass. Ground movements (default) call `Nav->GetCellHeightAt(...)`
	 *  with the walkable-only gate ON, so the unit holds previous Z when
	 *  crossing wall slivers; flying movements override to call with the
	 *  gate OFF (top-of-surface for any cell), so a flyer rises over
	 *  buildings automatically.
	 *
	 *  When the reference query returns false (outside bounds, blocked-cell
	 *  refusal for ground), Z is left untouched — the movement subclass owns
	 *  whatever Z value it wrote pre-call.
	 *
	 *  DeltaTime: used to rate-limit the per-tick Z change to
	 *  `TopSpeed × DeltaTime`. Without this cap, the bilinear ground sampler's
	 *  SafeHeight clamp produces abrupt cell-boundary Z transitions near walls
	 *  (primary-cell flips can move Z by 30-70cm in one tick), reading as a
	 *  hard pop independent of orientation smoothing. Natural slopes ≤45°
	 *  pass through fully — their per-tick Z change is ≤ the horizontal step. */
	void ApplyGroundSnapAndAltitude(
		FFixedVector& NewPos,
		const FSeinMovementComponent* MovementData,
		USeinNavigation* Nav,
		FFixedPoint DeltaTime) const;

	/** Source the reference Z sample for `ApplyGroundSnapAndAltitude` at
	 *  `WorldPos`. Default: `Nav->GetCellHeightAt(WorldPos, OutZ, true)` —
	 *  walkable-only gate ON. Flying subclasses override to pass
	 *  `bWalkableOnly=false` so they fly over blocked cells rather than
	 *  holding stale Z. Returns false when the nav has no usable sample. */
	virtual bool QueryReferenceZ(USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const;

	/** Graceful-stop trigger — returns true when the unit has effectively
	 *  overshot the destination and can't physically circle back tightly
	 *  enough to land inside AcceptanceRadius. Three gates that must ALL
	 *  hold:
	 *      1. Within `VicinityRadiusSq` of `FinalWp` (close to goal)
	 *      2. |speed| <= `MaxSpeedForOvershoot` (winding down)
	 *      3. forward · toFinal < 0 (heading away from goal)
	 *  The vicinity + speed gates together prevent false fires during
	 *  legitimate U-turn maneuvers, where the unit is still far from the
	 *  goal AND at high speed during the arc. */
	static bool IsOvershootArrival(
		const FFixedVector& AgentPos,
		const FFixedVector& FinalWp,
		const FFixedQuaternion& Rotation,
		FFixedPoint CurrentSpeed,
		FFixedPoint VicinityRadiusSq,
		FFixedPoint MaxSpeedForOvershoot);

	/** Auto-reverse decision — returns true if the unit should drive in
	 *  reverse to reach `FinalGoal` instead of forward. Triggers when ALL of:
	 *      1. `MovementData.bCanReverse` is true
	 *      2. distance(AgentPos, FinalGoal) <= MovementData.ReverseEngageDistanceThreshold
	 *      3. forward · normalize(toGoal) <= MovementData.ReverseEngageDotThreshold
	 *  Far-away rear targets fail the distance gate and U-turn forward.
	 *  Caller decides when to query — typically once at OnMoveBegin and
	 *  again on each repath; oscillation hysteresis is not built into this
	 *  helper, so per-tick polling without state needs care. */
	static bool ShouldAutoReverse(
		const FFixedVector& AgentPos,
		const FFixedQuaternion& Rotation,
		const FFixedVector& FinalGoal,
		const FSeinMovementComponent& MovementData);

};
