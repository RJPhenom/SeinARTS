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
 *          Shipped by this module:
 *          - USeinBasicMovement      — raw seek + arrive (the null/invalid fallback)
 *          - USeinBasicUnitMovement  — RTS default: seek+arrive + kinematic arrival
 *                                      ramp + face-velocity turning
 *          The concrete modes (Infantry / Wheeled / Tracked / Hover / Flight) live
 *          in the SeinARTSMovementPlus extension and resolve through the soft
 *          FSeinMovementComponent::MovementClass path — the framework has no
 *          compile-time dependency on them.
 *
 *          Movement instances are PERSISTENT PER UNIT (CP2.1, Decisions D-R2):
 *          owned by USeinMovementSubsystem's registry, created lazily, and
 *          surviving across orders — actions BORROW the instance for the
 *          order's duration (OnMoveBegin is the per-order reset point).
 *          Per-order state (current waypoint index) lives on the action and is
 *          passed in by reference; per-instance kinematic state (steer, ramps)
 *          persists across orders by construction. The always-on driver
 *          (FSeinMovementDriverSystem) calls TickIdle between orders, so no
 *          unit is ever tick-orphaned.
 */

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "UObject/Object.h"
#include "Core/SeinEntityHandle.h"
#include "Navigation/SeinNavAgentProfile.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinPathTypes.h"
#include "SeinMovement.generated.h"

class USeinNavigation;
class USeinNavigationSubsystem;
class USeinWorldSubsystem;
class UScriptStruct;
class USeinMoverHandle;
class USeinPlannerHandle;
struct FSeinMovementCanonicalStateProvider;
struct FSeinEntity;
struct FSeinMovementComponent;
struct FSeinNavigationComponent;
struct FSeinExtentsComponent;

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

	/** Terrain SPEED multiplier at the unit's position this tick (1 = normal; <1 mud,
	 *  >1 road). Sampled once per tick by USeinMoveToAction from the unit's terrain type
	 *  (Nav->GetTerrainTypeAt → USeinARTSCoreSettings::GetTerrainSpeedMultiplier) and
	 *  applied via USeinMovement::EffectiveTopSpeed so every mode scales uniformly.
	 *  Independent of nav routing cost. Default 1 (no terrain effect / no data). */
	FFixedPoint TerrainSpeedMultiplier = FFixedPoint::One;
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

/**
 * The per-tick OUTPUT of a movement mode's steering POLICY (USeinMovement::ComputeMotion). The
 * mode answers only "where do I want to go and face this tick?"; the base Tick harness applies the
 * shared mechanism (translate, nav-collision floor, ground snap, TurnRate-clamped turn, slope tilt,
 * velocity persistence). One struct so the policy contract can grow additively without churn.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Sein Motion"))
struct FSeinMotion
{
	GENERATED_BODY()

	/** Intended planar (XY) world velocity this tick — direction × speed. The harness moves the unit
	 *  by Velocity·dt (clamped so it can't overshoot the current waypoint), applies the hard
	 *  nav-collision floor + ground snap, and persists the unit's Velocity from the ACTUAL
	 *  post-collision delta. Zero = hold position (still rotates if bUpdateFacing). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector Velocity = FFixedVector::ZeroVector;

	/** Target facing yaw (radians) to turn toward this tick, clamped by the harness to TurnRate·dt
	 *  (then slope-tilted for ground modes). Applied only when bUpdateFacing is true. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint TargetYaw = FFixedPoint::Zero;

	/** When false the unit holds its current facing (no rotation this tick) — e.g. the strafe-free
	 *  Basic mode. When true the harness turns current yaw toward TargetYaw at TurnRate. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bUpdateFacing = false;
};

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Sein Movement"))
class SEINARTSMOVEMENT_API USeinMovement : public UObject
{
	GENERATED_BODY()

	// The BP-facing handle wraps this instance's per-tick context and re-exposes the
	// protected steering toolkit as Tier-2 power-route nodes (bound to the context), so
	// the toolkit stays out of the public C++ API. See USeinMoverHandle.
	friend class USeinMoverHandle;
	// Hydrates per-unit tuning onto a freshly-created instance (so shape virtuals read correct
	// values at plan-time / idle, not just after OnMoveBegin) — calls protected HydrateTuningFromData.
	friend class USeinMovementSubsystem;
	// Canonical restore stages under the final subsystem outer, hydrates the
	// candidate component baseline, then applies exact reflected state.
	friend struct FSeinMovementCanonicalStateProvider;

public:

	/** Called once when a MoveTo action resolves a path (before the first
	 *  Tick). Default: no-op. Override to initialize subclass state.
	 *  Receives the same context shape as Tick — convenient for one-shot
	 *  decisions (e.g. auto-reverse latch) that depend on neighbor queries
	 *  or world services. MoveData is non-const so subclasses can read
	 *  carry-over runtime state (e.g. Velocity) — preserve it instead
	 *  of zeroing if you want momentum to flow across order changes.
	 *
	 *  SEALED dispatcher → BP_OnMoveBegin. C++ modes overriding OnMoveBegin(Ctx)
	 *  directly (Movement+ vehicles) bypass the BP hook, exactly as before. */
	virtual void OnMoveBegin(const FSeinMovementContext& Ctx);

	/** Runs once when the unit starts a new move order, before the first Tick. Use it to reset
	 *  anything that should start fresh each order.
	 *
	 *  Optional; the default does nothing. A common use is setting up per-order state your Tick or
	 *  Compute Steer reads. You can read the unit through the handle here. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "On Move Begin"))
	void BP_OnMoveBegin(USeinMoverHandle* Mover);
	virtual void BP_OnMoveBegin_Implementation(USeinMoverHandle* Mover) {}

	/** Called each sim tick while the move is active. The base implementation is the shared MECHANISM
	 *  HARNESS: it advances CurrentWaypointIndex, tests arrival (acceptance ring OR the
	 *  IsOvershootArrival graceful-stop guard that prevents a unit orbiting a slot it can't quite
	 *  hit), calls the mode's ComputeMotion policy for this tick's desired velocity + facing, then
	 *  applies the universal mechanism — translate + hard nav-collision floor + ground snap +
	 *  TurnRate-clamped turn + slope tilt + velocity persistence.
	 *  @return true when the entity has reached the final waypoint.
	 *
	 *  A mode customizes FEEL by overriding ComputeMotion (Tier 1 — Basic / BasicUnit / Infantry, and
	 *  BP-authored modes). A mode that needs full control of the tick (the Movement+ vehicles)
	 *  overrides Tick(Ctx) directly and bypasses the harness + ComputeMotion entirely. */
	virtual bool Tick(const FSeinMovementContext& Ctx);

	/** The single steering-POLICY hook: return this unit's desired motion for the current tick — the
	 *  intended planar velocity, and the yaw to turn toward (or bUpdateFacing = false to hold facing).
	 *  The base Tick harness owns everything else (waypoint advance, arrival, the nav-collision floor,
	 *  ground snap, the TurnRate-clamped turn, slope tilt, velocity persistence), so a mode answers
	 *  only "where do I want to go and face this tick?".
	 *
	 *  The default is the ultra-basic ground policy: head to the current waypoint at terrain-scaled
	 *  top speed (bent by local avoidance, scaled by the avoidance speed-yield) and face the direction
	 *  of travel. Override for a custom feel; override the whole Tick only when you must drive the unit
	 *  yourself with the Mover Handle toolkit. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Compute Motion"))
	FSeinMotion ComputeMotion(USeinMoverHandle* Mover);
	virtual FSeinMotion ComputeMotion_Implementation(USeinMoverHandle* Mover);

	/** Decides what state the unit is left in at the moment its move completes. Default: a hard
	 *  stop - zero velocity, facing untouched.
	 *
	 *  The arrival-POLICY hook, one tick only: return residual velocity to roll or loiter through
	 *  the arrival instead of snapping to rest, and set Update Facing for one final TurnRate-clamped
	 *  facing step. Together with the other two hooks it covers the whole arrival without any mode
	 *  copying harness internals: shape the APPROACH (braking curves) in Compute Motion; author
	 *  post-arrival SETTLING (the settle-facing turn, idle motion) in Tick Idle; decide the arrival
	 *  INSTANT here. The arrival TRIGGER itself (acceptance ring, overshoot guard, the move action's
	 *  crowd-stall failsafe) is harness mechanism and cannot be vetoed - completion always proceeds,
	 *  so no policy can hold an order open. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Compute Arrival Motion"))
	FSeinMotion ComputeArrivalMotion(USeinMoverHandle* Mover);
	virtual FSeinMotion ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover);

	/** MECHANISM: dispatch ComputeArrivalMotion with a bound handle and APPLY the result (the
	 *  velocity write + the optional final facing step). Called by the base Tick harness on
	 *  ring/overshoot arrival AND by USeinMoveToAction's crowd-stall failsafe, so both arrival
	 *  owners apply the same per-class stop semantics. A Tier-2 Tick override should call this at
	 *  its own arrival point for the same reason (instead of hand-writing `Velocity = 0`). */
	void DispatchArrivalMotion(const FSeinMovementContext& Ctx);

	/** Called when the action ends (completed/cancelled/failed). Default:
	 *  no-op. Override to clean up subclass transient state.
	 *
	 *  SEALED dispatcher → BP_OnMoveEnd, binding the handle in entity-only mode
	 *  (no live path/movement context at end). C++ overrides of OnMoveEnd(Entity)
	 *  bypass it. */
	virtual void OnMoveEnd(FSeinEntity& Entity);

	/** Runs when the unit's move order ends, completes, or is cancelled. Use it to clean up.
	 *
	 *  Optional; the default does nothing. Only transform reads are available here, since there is no
	 *  live path or velocity at the end of an order. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "On Move End"))
	void BP_OnMoveEnd(USeinMoverHandle* Mover);
	virtual void BP_OnMoveEnd_Implementation(USeinMoverHandle* Mover) {}

	/** Always-on idle tick — called by FSeinMovementDriverSystem every sim tick
	 *  for entities with NO active move order this tick (CP2.1, Decisions D-R2:
	 *  persistent per-unit movement). Owns the un-ordered half of a unit's life:
	 *    1. First contact: one-time immediate ground + slope snap (subsumed the
	 *       retired FSeinInitialSnapSystem; latched via bInitialGroundSnapDone,
	 *       waits until the nav bake is loaded).
	 *    2. Coast-down: residual Velocity (left set by cancelled/preempted
	 *       orders BY DESIGN) decays to rest through the same decel ramp orders
	 *       use, with the footprint-aware nav floor applied — a unit whose
	 *       order is cancelled mid-stride now visibly coasts to a stop instead
	 *       of freezing.
	 *    3. Settle: per-tick Z/altitude re-snap + smoothed slope pitch/roll at
	 *       the current position, so a unit shoved by collision settles where
	 *       it lands (settle-in-place semantics — no return-to-home). Yaw is NEVER touched
	 *       while idle.
	 *  Ctx.Path is a shared EMPTY path during idle ticks and the waypoint index
	 *  is a dummy — overrides must not read them. PURE SELF-MUTATION: never
	 *  query the spatial hash or read neighbour entities here (pool-order
	 *  iteration would make that order-dependent → desync); precomputed inputs
	 *  only — same contract as ApplyAvoidanceSteer. Overrideable for per-class
	 *  idle behavior; the base impl is correct for ground classes and (via the
	 *  GetAltitude / QueryReferenceZ virtuals) hover + flight classes. */
	virtual void TickIdle(const FSeinMovementContext& Ctx);

	/** Internal performance contract for the always-on movement driver. True
	 *  only for an exact shipped native class whose idle implementation is known
	 *  to mutate entity transform plus the base runtime movement fields tracked
	 *  by the driver's deferred-revision apply. Custom native classes default to
	 *  false and keep conservative dirty tracking; Blueprint classes are always
	 *  conservative. Exact-class checks in shipped overrides prevent an unknown
	 *  subclass from inheriting this promise accidentally. */
	virtual bool SupportsExactIdleMutationTracking() const
	{
		return false;
	}

	/** Runs every frame while the unit stands still (no move order). Use it for idle motion.
	 *
	 *  Optional; the default keeps the unit on the ground, coasts any leftover speed to a stop, and
	 *  settles it on slopes where it stands. Override for custom idle such as a hover bob. There is no
	 *  path while idle, and you must not read other units here. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Tick Idle"))
	void BP_TickIdle(USeinMoverHandle* Mover);
	virtual void BP_TickIdle_Implementation(USeinMoverHandle* Mover);

	/** Whether this mode flies straight over obstacles instead of routing around them.
	 *
	 *  When on, the unit's path is a straight line to the destination (no pathfinding search) and it
	 *  flies over walls and buildings — turn it on for flyers and hovercraft. When off (the default)
	 *  the unit routes around obstacles like a ground unit. This is a per-mode trait, so it is a
	 *  checkbox here rather than a per-unit tuning variable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS", meta = (DisplayName = "Bypass Pathfinding"))
	bool bBypassPathfinding = false;

	/** Whether the unit sticks to walkable ground, or rides the top of whatever is below it.
	 *
	 *  When on (the default) the unit's height is taken only from walkable ground, so it holds its
	 *  height when sliding past a wall instead of climbing it. Turn it off for flyers so they ride
	 *  over buildings and other blocked cells. A per-mode trait, so it is a checkbox. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS", meta = (DisplayName = "Snaps To Ground"))
	bool bSnapsToGround = true;

	/** Whether this mode flies straight over obstacles (returns the Bypass Pathfinding flag). The move
	 *  action and the default planner read it; C++ modes may override for conditional behaviour. */
	virtual bool BypassPathfinding() const { return bBypassPathfinding; }

	/** Returns the per-class sub-data UScriptStruct this movement consumes
	 *  out of `FSeinMovementComponent::MovementClassData`. Used by runtime
	 *  helpers that unwrap the per-class authoring (Altitude resolution,
	 *  per-class kinematic params, etc.).
	 *
	 *  Also drives the editor auto-swap: FSeinInstancedStructDetails reads this
	 *  (via reflection — hence the UFUNCTION) to restrict the MovementClassData
	 *  picker to this struct and auto-fill it when MovementClass changes. Opt in
	 *  on the data field with meta = (SeinDataStructFromClass = "MovementClass").
	 *
	 *  Default returns `TuningStruct` (null unless set) — so a BP-authored mode
	 *  declares its sub-data by pointing TuningStruct at a UDS (the Phase-B export
	 *  stamps it; a designer may also set it by hand). C++ subclasses with sub-data
	 *  override this to return e.g. `FSeinWheeledMovementData::StaticStruct()`. */
	UFUNCTION()
	virtual UScriptStruct* GetMovementDataStruct() const { return TuningStruct; }

	/** The tuning data this mode uses, shown read-only — it is generated for you.
	 *
	 *  When you add tuning variables and click Generate Tuning Data Structure, this points at the
	 *  struct holding them, which then auto-fills each unit's Movement Class Data so values can be set
	 *  per unit. You do not edit this directly; it is derived from your variables. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS", meta = (DisplayName = "Tuning Struct"))
	TObjectPtr<UScriptStruct> TuningStruct;

	/** This mode's Deceleration rate (world units per second²) — how hard it brakes. Base returns 0:
	 *  the ultra-basic Basic/BasicUnit have no ramp (they stop crisply). Modes with a speed ramp
	 *  (Infantry + the Movement+ vehicles) override this to return their per-class UDS Deceleration.
	 *  Read by the impl-agnostic consumers that need a mode's braking rate WITHOUT knowing its UDS
	 *  type: the base idle coast-down (TickIdle) and USeinMoveToAction's arrival-imminent estimate. */
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const { return FFixedPoint::Zero; }

	/** Returns the unit's cruise altitude above ground (reads the Get Altitude hook by default).
	 *  C++ modes may override this directly. */
	virtual FFixedPoint GetAltitude(const FSeinMovementComponent* MovementData) const { return BP_GetAltitude(); }

	/** How high above the ground the unit flies. Return 0 to sit on the ground.
	 *
	 *  Used by hover and flight modes to hold a cruising height; the unit's ground snap adds this on
	 *  top of the terrain height. Read it from one of your tuning variables so it can be set per unit. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Altitude"))
	FFixedPoint BP_GetAltitude() const;
	virtual FFixedPoint BP_GetAltitude_Implementation() const { return FFixedPoint::Zero; }

	/** One-time immediate ground + slope snap for a spawned/placed entity that has
	 *  not yet moved. Snaps the entity's Z to the nav reference height (+ this
	 *  movement's GetAltitude) and sets pitch/roll to match the terrain slope under
	 *  the entity's current facing — the same result the movement Tick produces, but
	 *  instant (no smoothing). Writes Entity.Transform (Z + rotation) and
	 *  MovementData.SmoothedPitch/Roll. Safe no-op when Nav is null or can't sample
	 *  (out of bounds / no bake). Const and routes through the virtual
	 *  QueryReferenceZ / GetAltitude. Const because ground projection must not
	 *  mutate persistent movement-policy state. */
	void SnapToGroundImmediate(FSeinEntity& Entity, FSeinMovementComponent& MovementData, USeinNavigation* Nav) const;

	/** Plan a path from the entity's current position to `Ctx.Destination`.
	 *  Called once at move start, and again on each repath (Interval or
	 *  OffPathOnly drift).
	 *
	 *  Default implementation:
	 *    - If `BypassPathfinding()` is true, synthesizes a straight-line
	 *      two-waypoint path `[currentPos, destination]`. Used by flying
	 *      movements that don't path through static obstacles.
	 *    - Otherwise, builds an `FSeinPathRequest` from `MovementData` + `NavData`
	 *      (footprint radius, wall-padding cells, nav-layer mask) and routes it
	 *      through `Ctx.NavSub->RequestPath` — the budgeted call that returns
	 *      `Throttled` when the per-tick path-request budget is spent.
	 *
	 *  Subclasses override to compose movement-specific planning pipelines.
	 *  Wheeled, for example, can call the nav's cell A* to get a coarse
	 *  polyline, then post-process through a kinematic curve fitter that
	 *  emits arc + straight + reverse segments suited to its turn dynamics
	 *  (high-fidelity armored car driving). Helicopters / jets compose their
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

	/** Builds the route a unit will follow to its destination, and reports the result.
	 *
	 *  Called when an order starts and on each repath. The default flies straight for flyers and runs
	 *  the pathfinder for ground units. Override to build a custom route with the Planner handle: ask
	 *  for a normal path with Request Nav Path and then smooth or curve-fit its waypoints, or build one
	 *  from scratch with Add Waypoint and Finalize Path. Return the planning result. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Plan Path"))
	ESeinPathResult BP_PlanPath(USeinPlannerHandle* Planner) const;
	virtual ESeinPathResult BP_PlanPath_Implementation(USeinPlannerHandle* Planner) const;

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
	 *  execute — 0 means it can pivot in place (no radius constraint). This is a
	 *  per-unit QUERY: it REPORTS the radius; nothing in the base acts on it yet.
	 *
	 *  SANCTIONED base seam (NOT extension-anticipation): the movement base is
	 *  deliberately purpose-built for vehicle movement — PlanPath / PlannerHandle /
	 *  GetMinTurnRadius are the kinematic-planning API that lives here on purpose, the
	 *  same way the opt-in reverse fields do. It is intentionally a no-op consumer in
	 *  the base; the producer (the curve planner) is a Movement+ concern. Do not flag
	 *  this as a "base must not anticipate extensions" violation.
	 *
	 *  Intended consumer: a Movement+ vehicle planner (see PlanPath) would read it
	 *  to round path corners into drivable arcs and to slow before turns tighter
	 *  than the radius. That curve-fitting / curvature-throttle is NOT built today —
	 *  this hook exists so the planner can read the radius when it lands.
	 *
	 *  Default 0. Infantry, BasicMovement, and tracked vehicles (which pivot in
	 *  place) leave it at 0. Wheeled overrides via the bicycle identity
	 *  `Wheelbase / tan(MaxSteerAngle)`, reading both from
	 *  `MovementData::MovementClassData` as `FSeinWheeledMovementData`; Tracked may
	 *  opt into a preferred radius via its own per-class data. Takes `MovementData`
	 *  so overrides can read their per-class sub-data directly. */
	virtual FFixedPoint GetMinTurnRadius(const FSeinMovementComponent* /*MovementData*/) const { return BP_GetMinTurnRadius(); }

	/** The tightest turn the unit can make, in world units. Return 0 for no limit (it can pivot).
	 *
	 *  Reports the unit's turn radius so a vehicle planner can shape its driving (round corners into
	 *  drivable arcs, slow before tight turns). That planner is a Movement+ feature not yet built, so
	 *  today the value is read but nothing acts on it. A wheeled vehicle computes it from its wheelbase
	 *  and steering angle; pivoting units leave it 0. Read it from your tuning variables (per unit). */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Movement", meta = (DisplayName = "Get Min Turn Radius"))
	FFixedPoint BP_GetMinTurnRadius() const;
	virtual FFixedPoint BP_GetMinTurnRadius_Implementation() const { return FFixedPoint::Zero; }

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
	 *  by the chassis transform — both inputs are already world vectors): the VELOCITY vector
	 *  (entity → velocity, at true magnitude) and RED = the local-avoidance steer expressed
	 *  as the sideways velocity it adds (AvoidanceSteer × speed), comparable to the velocity
	 *  arrow. Both skip when ~zero. The velocity arrow is TINTED by SpeedScale — orange at 1
	 *  (neutral), toward red below 1 (avoidance braking / cohesion hold-back), toward green
	 *  above 1 (cohesion catch-up boost) — so the speed-yield channel is visible per unit.
	 *  Callers pass zero vectors / One while a unit has no active move order, so a unit at
	 *  rest shows the ring only (its stored Velocity / AvoidanceOutput may be stale).
	 *
	 *  Pure draw — no sim mutation, safe to call off the sim tick. */
	static void DrawSteeringDebugViz(
		UWorld* World,
		const FFixedVector& EntityPos,
		float FootprintRadius,
		const FFixedVector& Velocity,
		const FFixedVector& AvoidanceSteer = FFixedVector::ZeroVector,
		const FFixedPoint& SpeedScale = FFixedPoint::One);
#endif // UE_ENABLE_DEBUG_DRAWING

	/** Populate the footprint cache used by `ResolveNavCollision`. Called by
	 *  `USeinMoveToAction` once at first-tick setup, before `OnMoveBegin`.
	 *
	 *  Cascade for the effective collision radius:
	 *    Tier 1: `FSeinExtentsComponent` on the entity if present. For Capsule
	 *      shapes uses `Radius`; for Box uses the diagonal
	 *      `sqrt(HalfExtentX² + HalfExtentY²)` (smallest enclosing circle).
	 *      Compound entities take the max bounding radius across all shapes.
	 *    Tier 2: `NavData.FallbackFootprintRadius` (used only when no Extents).
	 *    Tier 3: 0 — point-only, no ring samples.
	 *
	 *  Precomputes 8 ring sample offsets at the resolved radius. Per-tick
	 *  collision checks then sample 1 center + 8 ring points = 9 IsWorldPositionClear
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
	 *  Box shape conversion: takes the diagonal `sqrt(HalfExtentX² + HalfExtentY²)`
	 *  as the smallest enclosing bounding-circle radius. This is correct for radially-
	 *  symmetric movement (infantry, hover) but conservative for elongated
	 *  units (long tanks): the planner refuses corridors narrower than the
	 *  bounding circle even when the body could fit if perfectly oriented.
	 *
	 *  This conservative bounding circle is the right trade-off for generic /
	 *  infantry-centric unit sets. Two SEPARATE pieces address different
	 *  long-vehicle concerns in different layers, so don't conflate them:
	 *    - Drivable start maneuvers with arcs + reversing are shipped by the
	 *      Movement+ wheeled/tracked PlanPath overrides (per-unit kinematics),
	 *      NOT by a nav class. General curve-fitting over the full path is not shipped.
	 *    - Threading a long tank through a corridor narrower than its bounding circle
	 *      (orientation-aware A* that tracks facing per node) -> a future USeinNavigation
	 *      subclass, NOT here on the base. */
	static FFixedPoint ResolveCollisionRadius(
		USeinWorldSubsystem* World,
		FSeinEntityHandle SelfHandle,
		const FSeinNavigationComponent* NavData);

	/** Footprint-radius cascade from already-resolved component pointers (no
	 *  world lookup): Extents (max per-shape bounding radius) -> NavComp
	 *  FallbackFootprintRadius -> 0. The World/handle overload above fetches
	 *  Extents then delegates here; hot loops that have hoisted their component
	 *  storage (e.g. avoidance) call this directly to skip the per-call lookup. */
	static FFixedPoint ResolveCollisionRadius(
		const FSeinExtentsComponent* Extents,
		const FSeinNavigationComponent* NavData);

	/** Footprint-aware passability check at a candidate position. True iff
	 *  the candidate's center AND every cached ring sample land on passable
	 *  cells (per `CachedCollisionRadius` / `CachedFootprintSamples`).
	 *  Subclasses use this for direction-clear probes — e.g., the unstick
	 *  state machine probes the reverse direction before committing to
	 *  Reversing, so a vehicle in a corner doesn't reverse INTO another
	 *  wall. When `CachedNumFootprintSamples == 0` (no Extents + no
	 *  FallbackFootprintRadius), degrades to legacy point-only check.
	 *
	 *  Public for the same reason `CacheFootprintFromContext` is: the move
	 *  action calls it — its hold-escape ladder probes the commanded direction
	 *  to tell a MECHANICAL block (footprint refused → escalate) from a policy
	 *  zero (a pivot-in-place — never escalate). Valid whenever the footprint
	 *  cache is (i.e. during a move). */
	bool IsFootprintPassable(const FFixedVector& Pos, USeinNavigation* Nav) const;

protected:

	// ----------------------------------------------------------------------
	// Footprint cache for ResolveNavCollision. Populated once per move
	// action by `CacheFootprintFromContext` (called from MoveToAction's
	// first-tick setup, before OnMoveBegin). Entity-stable for the duration
	// of the move — no per-tick component lookups.
	// ----------------------------------------------------------------------

	/** Effective collision radius (world units) from the
	 *  Extents → FallbackFootprintRadius → 0 cascade. 0 = point-only check. */
	UPROPERTY()
	FFixedPoint CachedCollisionRadius = FFixedPoint::Zero;

	/** Number of ring samples in CachedFootprintSamples. 0 when
	 *  CachedCollisionRadius == 0 (point-only). Otherwise 8 (45° spacing). */
	UPROPERTY()
	int32 CachedNumFootprintSamples = 0;

	/** Local-space XY offsets for footprint sampling. Computed from
	 *  CachedCollisionRadius once per move action; sampled via IsWorldPositionClear
	 *  at each ResolveNavCollision call. Fixed-size to avoid per-tick
	 *  allocations. */
	UPROPERTY()
	FFixedVector CachedFootprintSamples[8];

	/** Maximum traversable height difference between adjacent positions.
	 *  Prevents units from stepping up vertical walls whose top cells are
	 *  passable (connected to ground elsewhere). Default 75 world units —
	 *  allows slopes up to ~78° at typical per-tick step distances while
	 *  rejecting multi-meter wall step-ups. Set 0 to disable. */
	UPROPERTY()
	FFixedPoint CachedMaxStepHeight = FFixedPoint::FromInt(75);

	/** Agent's nav layer mask (`FSeinNavigationComponent::NavLayerMask`, default
	 *  0x01 = ground), cached alongside the footprint. Passed to the nav floor's
	 *  `IsWorldPositionClear` so a dynamic blocker only stops agents its authored
	 *  `BlockedNavLayerMask` intersects. */
	UPROPERTY()
	uint8 CachedNavLayerMask = 0x01;

	/** Extra configuration-space spacing authored on the Navigation component.
	 *  Retained with the rest of the profile for custom navigation probes and
	 *  exact checkpoint continuation. */
	UPROPERTY()
	int32 CachedNavWallPaddingCells = 0;

	/** Per-unit hard terrain exclusions cached with the other navigation
	 *  policy so the runtime movement floor cannot drift from its path request. */
	UPROPERTY()
	FGameplayTagContainer CachedBlockedTerrainTags;

	/** Requester identity used to exclude an entity's own dynamic blocker stamp
	 *  from runtime occupancy probes. */
	UPROPERTY()
	FSeinEntityHandle CachedNavRequester;

	FSeinNavAgentProfile BuildCachedNavAgentProfile() const;

	// ----------------------------------------------------------------------
	// Settle-facing cache (per movement instance). The idle settle resolves
	// "which formation slot is mine" ONCE per order — by exact-matching the
	// unit's last ordered goal (FSeinMovementComponent::TargetLocation, which
	// for ground moves IS the broker's persisted slot position, byte-identical
	// fixed-point) against the broker's SettledSlotPositions — and caches the
	// matched slot facing here, keyed by that goal. Derived deterministically
	// from hashed sim state, so an instance cache (not itself hashed) is safe.
	// ----------------------------------------------------------------------
	FFixedVector CachedSettleFacingKey = FFixedVector::ZeroVector;
	FFixedQuaternion CachedSettleFacing = FFixedQuaternion::Identity;
	bool bCachedSettleFacingResolved = false;
	bool bCachedSettleFacingFound = false;

	/** Whether this movement class turns idle units toward their formation slot's facing
	 *  while settling (gated globally by the Settle To Formation Facing project setting).
	 *  Default true. Override to false for classes that never rotate — Basic (translate-
	 *  only) opts out so its no-rotation contract holds while idle too. */
	virtual bool SettlesToSlotFacing() const { return true; }

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

	/** Assemble the unit's final rotation for a tick: tilt the given facing `Yaw`
	 *  to the ground slope at `Pos`, rate-limit the pitch/roll change toward it
	 *  (60°/sec), and persist the smoothed angles on `MovementData` so terrain
	 *  orientation stays continuous across ticks and orders. Returns the final
	 *  rotation. This is the slope step the default loop runs; shared with the
	 *  Apply Slope Tilt handle node so a custom BP_Tick can match it instead of
	 *  popping when it sets rotation from yaw alone. */
	FFixedQuaternion ApplySlopeTilt(
		const FFixedVector& Pos,
		FFixedPoint Yaw,
		FSeinMovementComponent* MovementData,
		USeinNavigation* Nav,
		FFixedPoint DeltaTime) const;

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
	 *  Corner handling: rely on the cluster-skip thinning above; for sharp-
	 *  corner anticipation, lean on a lower `LookAheadDistance` instead. (A
	 *  former `MaxCornerAngleRadians` cos-falloff knob was removed — it
	 *  interacted badly with off-path drift and never cleanly solved the
	 *  wheeled corner-cutting problem.) */
	static FFixedVector ResolveLookAheadPoint(
		const FFixedVector& AgentPos,
		const FSeinPath& Path,
		int32 CurrentWaypointIndex,
		FFixedPoint LookAhead);

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
	 *    1. Crossover (INCOMING direction): dot(AgentPos - W[i], W[i] - W[i-1]) > 0
	 *       → genuinely overshot PAST W[i] along the leg that was travelled toward
	 *       it. (Never the OUTGOING W[i]→W[i+1] direction — that fires for any
	 *       agent merely on the far side of W[i]'s plane, however far off to the
	 *       side, and skips whole detours; see the .cpp why-comment.)
	 *    2. Distance fallback: |AgentPos - W[i]| < CloseRadius → close enough.
	 *       (Sole trigger for W[0], which has no incoming leg.)
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

	/** Harness-standard advance: the same crossover + distance rules, with the standard close
	 *  radius (max(2 × TopSpeed × Dt, 50cm)) read straight from the context.
	 *
	 *  OWNERSHIP CONTRACT: waypoint advance is HARNESS mechanism. Tier-1 modes never touch it
	 *  (the base Tick runs it before every ComputeMotion). A Tier-2 Tick override MUST call one
	 *  of these two helpers rather than hand-rolling an advance loop — the incoming-direction
	 *  crossover test is load-bearing: a hand-rolled distance-only loop reintroduces the
	 *  overshoot-at-speed / backward-carrot bugs, and an outgoing-direction crossover skips
	 *  whole detours (the historic through-wall bug). */
	static void AdvanceWaypointAlongPath(const FSeinMovementContext& Ctx);


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

	/** A unit's effective top speed THIS TICK = authored `TopSpeed` × the context's
	 *  terrain speed multiplier (mud slows, road speeds). The single seam every movement
	 *  mode reads instead of `MovementData->TopSpeed` directly, so terrain speed applies
	 *  uniformly without each Tick re-deriving it. Returns 0 if MovementData is null.
	 *  Use it for the CRUISE/target speed only — leave stable "max-step reference" uses
	 *  (intermediate-waypoint arrival radius) at raw TopSpeed so a slowed tick never
	 *  fails to consume a waypoint it has effectively reached. */
	static FFixedPoint EffectiveTopSpeed(const FSeinMovementContext& Ctx);

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
	 *  Dynamic-AWARE — queries `Nav->IsWorldPositionClear` (static bake AND the
	 *  runtime dynamic-blocker list), so a unit can't slide its body through a
	 *  non-baked cover wall / deployable that A* correctly routed around. The
	 *  path is a steering force that can miss; this floor is the last defense,
	 *  and a static-only floor let bodies slip through dynamic walls (the
	 *  "moves through the wall" symptom). Uses the agent's own nav layer mask,
	 *  and by default units DON'T stamp nav — so this blocks against STRUCTURES,
	 *  not other units; unit-vs-unit push stays collision/penetration's job
	 *  ("don't walk through walls", not "don't walk through tanks"). */
	FFixedVector ResolveNavCollision(
		const FFixedVector& OldPos,
		const FFixedVector& NewPos,
		USeinNavigation* Nav,
		const FFixedVector* AuthoritativeDest = nullptr) const;

	/** Single short-step nav-collision resolve (escape valve, authoritative-dest exempt,
	 *  footprint + step-height gate, axis slides, hold-in-place). `ResolveNavCollision`
	 *  self-gates on step size and subdivides a long move into footprint-radius-sized hops,
	 *  calling THIS per hop — so a fast unit can't tunnel a thin blocker sitting between its
	 *  start and end footprints. Not for direct use; go through `ResolveNavCollision`. */
	FFixedVector ResolveNavCollisionStep(
		const FFixedVector& OldPos,
		const FFixedVector& NewPos,
		USeinNavigation* Nav,
		const FFixedVector* AuthoritativeDest = nullptr) const;

	/** Bend a NORMALIZED desired direction by this unit's precomputed local-
	 *  avoidance steer (`FSeinMovementComponent::AvoidanceOutput.SteerDir`, written
	 *  one-sided at PreTick by the active `USeinAvoidance`). This is the single
	 *  integration point every movement mode calls, so the general avoidance system
	 *  applies across all of them. Soft layer — the hard penetration floor remains the
	 *  no-overlap guarantee.
	 *
	 *  PURE READ: never query the spatial hash or read neighbour state here.
	 *  Movement runs through the insertion-ordered latent-action manager, so a
	 *  neighbour read at this point would be order-dependent → desync. Bit-exact
	 *  no-op when the steer is ~zero (returns the input direction UNCHANGED), so an
	 *  opted-out unit moves identically to a world with no avoidance. Input is
	 *  assumed unit-length; the steer is sized in that same unit space. */
	FFixedVector ApplyAvoidanceSteer(const FSeinMovementContext& Ctx, const FFixedVector& DesiredDir) const;

	/** This unit's precomputed avoidance SPEED-YIELD this tick
	 *  (`FSeinMovementComponent::AvoidanceOutput.SpeedScale`, written one-sided at
	 *  PreTick by the active `USeinAvoidance`): a [0,1] multiplier on cruise speed so a
	 *  model can make a unit give way by SLOWING, not only turning. 1 = no change. The
	 *  base RTS loop multiplies its cruise target by this; a custom BP_Tick reads it via
	 *  the Mover Handle. PURE READ — same one-sided / order-independent discipline as
	 *  ApplyAvoidanceSteer. Byte-identical no-op while the model leaves it at 1. */
	FFixedPoint GetAvoidanceSpeedScale(const FSeinMovementContext& Ctx) const;

	// CacheFootprintFromContext is declared in the public section above —
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

	/** Copy each field of the per-unit tuning UDS (FSeinMovementComponent::
	 *  MovementClassData) into the same-named instance UPROPERTY by reflection, so a
	 *  BP mode's graph reads its own variables and they reflect per-unit authoring.
	 *  Deterministic field copy; the instance vars are a cache of the hashed component
	 *  data. No-op when the tuning struct is empty. Called from the OnMoveBegin dispatch. */
	void HydrateTuningFromData(const struct FInstancedStruct& Tuning);

	/** Reusable BP-facing context wrapper, lazily created and repointed each
	 *  dispatch. Transient scratch — never hashed; its context pointer is valid
	 *  only during a single BP_Tick / hook dispatch. */
	UPROPERTY(Transient)
	TObjectPtr<USeinMoverHandle> CachedHandle;

	/** Reusable plan-time handle. PlanPath is const, so it's set via a localized const_cast (caching a
	 *  scratch handle is not movement-instance state). Its context/path pointers are valid only during
	 *  one PlanPath dispatch. */
	UPROPERTY(Transient)
	TObjectPtr<USeinPlannerHandle> CachedPlannerHandle;

};
