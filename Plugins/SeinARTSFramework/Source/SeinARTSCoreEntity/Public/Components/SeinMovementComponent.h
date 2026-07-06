/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinMovementComponent.h
 * @brief:   Per-entity movement authoring + runtime state. Replaces the
 *           legacy `FSeinMovementData` (which co-mingled movement, navigation,
 *           and per-class tuning); the split is:
 *             - FSeinMovementComponent (this file) — top-line speed/turn
 *               knobs, movement-class picker, polymorphic per-class data,
 *               reverse settings, runtime velocity + arrival state.
 *             - FSeinNavigationComponent (SeinARTSNavigation module) —
 *               pathfinding + nav-layer + repath authoring.
 *             - FSeinInfantryMovementData / FSeinWheeledMovementData /
 *               FSeinTrackedMovementData / FSeinHoverMovementData /
 *               FSeinFlyingMovementData — per-movement-class tuning,
 *               surfaced in `MovementClassData` via the polymorphic-UDS
 *               auto-swap (custom details panel keys off the selected
 *               MovementClass's GetMovementDataStruct() virtual).
 *
 *           Designer authoring lives on the entity bridge's ComponentData
 *           array — designer picks `FSeinMovementComponent` as an entry and
 *           the component details panel surfaces the fields below + the
 *           per-class sub-data UDS that auto-populates from MovementClass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "UObject/SoftObjectPath.h"
#include "SeinMovementComponent.generated.h"

/** The per-tick OUTPUT of the local-avoidance layer (USeinAvoidance), produced at
 *  PreTick and consumed by the movement Tick. Two channels:
 *    - SteerDir   : a planar (XY) lateral nudge that BENDS the unit's desired heading,
 *                   consumed via USeinMovement::ApplyAvoidanceSteer. Strength-scaled +
 *                   temporally smoothed by the shipped model.
 *    - SpeedScale : a non-negative multiplier on cruise speed. < 1 = YIELD by slowing
 *                   (not only turning); > 1 = CATCH-UP boost (e.g. formation cohesion
 *                   closing a gap); 1 = bit-exact no change. The base RTS loop applies
 *                   it (USeinMovement::GetAvoidanceSpeedScale). The CONTRACT deliberately
 *                   does not bound the boost or pick a cohesion model — how much (if any)
 *                   > 1 a model writes, and how a movement mode physically honors it, are
 *                   CLASS opinions, not seam rules.
 *  Runtime sim state, hashed as a desync canary. A struct (not two loose fields) so the
 *  avoidance OUTPUT CONTRACT can grow additively without churning consumers. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAvoidanceOutput
{
	GENERATED_BODY()

	/** Planar lateral steer nudge (unit-direction space). Zero = no steering. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance")
	FFixedVector SteerDir = FFixedVector::ZeroVector;

	/** Cruise-speed multiplier, >= 0. 1 = full speed (bit-exact no-op); < 1 = yield
	 *  by braking; > 1 = catch-up boost. Unbounded above BY CONTRACT — magnitude
	 *  policy belongs to the producing avoidance class and the consuming movement
	 *  mode, never to this seam. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance",
		meta = (ClampMin = "0.0"))
	FFixedPoint SpeedScale = FFixedPoint::One;
};

FORCEINLINE uint32 GetTypeHash(const FSeinAvoidanceOutput& O)
{
	uint32 Hash = GetTypeHash(O.SteerDir);
	Hash = HashCombine(Hash, GetTypeHash(O.SpeedScale));
	return Hash;
}

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMovementComponent : public FSeinComponent
{
	GENERATED_BODY()

	// =========================================================================
	// Top-line authoring fields (apply to every movement class)
	// =========================================================================

	/** Maximum forward speed in world units per second. Default tuned for a
	 *  baseline foot-soldier; vehicles/designers override per entity class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint TopSpeed = FFixedPoint::FromInt(500);

	/** Turn rate in radians per second (general — applies regardless of
	 *  movement class). Per-class sub-data may override behaviour around
	 *  this; e.g. tracked vehicles' pivot-vs-arc decision branches off
	 *  TurnRate AND the tracked sub-data's PivotEntrySpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint TurnRate = FFixedPoint::FromInt(5);

	// =========================================================================
	// Movement-class selector + polymorphic per-class data
	// =========================================================================

	/** Which `USeinMovement` subclass drives this entity. Soft class path (not
	 *  TSubclassOf) because the movement classes live in modules that depend on
	 *  `SeinARTSCoreEntity` — the base + Basic/BasicUnit in `SeinARTSMovement`,
	 *  and the concrete modes (Infantry/Wheeled/Tracked/Hover/Flight) in the
	 *  `SeinARTSMovementPlus` extension. A direct TSubclassOf in CoreEntity would
	 *  flip the dep. Resolved to a UClass* at action-init time via TryLoadClass.
	 *
	 *  Null / invalid defaults to USeinBasicMovement (simple seek + arrive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (DisplayName = "Movement Class",
				MetaClass = "/Script/SeinARTSMovement.SeinMovement"))
	FSoftClassPath MovementClass;

	/** Per-movement-class tuning data. The struct type auto-swaps based on
	 *  the selected `MovementClass` — pick `USeinWheeledVehicleMovement` and
	 *  the UDS surfaces as `FSeinWheeledMovementData`; pick
	 *  `USeinTrackedVehicleMovement` and it swaps to `FSeinTrackedMovementData`,
	 *  and so on.
	 *
	 *  Mechanism: `USeinMovement` exposes a virtual `GetMovementDataStruct()`
	 *  returning the matching `UScriptStruct*`. The custom details panel
	 *  watches `MovementClass` changes via `PostEditChangeProperty` and
	 *  re-initialises `MovementClassData` with the matching struct type.
	 *  Movement subclasses without a sub-data struct (e.g. the bare
	 *  USeinBasicMovement) leave it empty.
	 *
	 *  Determinism: the per-class structs are `SeinDeterministic` and live
	 *  in sim storage like any other authored data — runtime queries route
	 *  through the same FInstancedStruct unwrap path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (BaseStruct = "/Script/SeinARTSCoreEntity.SeinComponent",
				ExcludeBaseStruct,
				SeinDeterministicOnly,
				SeinDataStructFromClass = "MovementClass,GetMovementDataStruct"))
	FInstancedStruct MovementClassData;

	// =========================================================================
	// Reverse (top-level, gated by bCanReverse)
	// =========================================================================

	/** When true, the unit can drive in reverse. Movements auto-engage
	 *  reverse for destinations BEHIND the unit (per ReverseEngageDotThreshold
	 *  + ReverseEngageDistanceThreshold), and explicit reverse abilities
	 *  become active. Off by default; designers opt in per unit type
	 *  (vehicles yes, infantry no). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bCanReverse = false;

	/** Maximum speed when reversing. Vehicles typically reverse slower than
	 *  forward. Set 0 to use TopSpeed / 2 as a fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseTopSpeed = FFixedPoint::Zero;

	/** Auto-reverse engages when `forward · normalize(toGoal)` is at or below
	 *  this dot threshold AND distance to goal is within
	 *  `ReverseEngageDistanceThreshold`. Default -0.5 ≈ 120° behind; lower
	 *  (more negative) is stricter. Range [-1, +1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseEngageDotThreshold = -FFixedPoint::Half;

	/** Auto-reverse only engages within this distance of the destination. A
	 *  far-away rear target would U-turn forward instead — reversing all the
	 *  way looks silly. Default 5m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseEngageDistanceThreshold = FFixedPoint::FromInt(500);

	// =========================================================================
	// Runtime state (BlueprintReadWrite, not authored)
	// =========================================================================

	/** Active move target world position (sim space) — the current order's resolved
	 *  per-member goal (formation slot / click point), written by USeinMoveToAction
	 *  each move tick alongside bHasTarget. A TRANSIENT per-order copy: the durable
	 *  slot layout is formation/broker-owned; which member fills which slot can be
	 *  re-decided on a re-form. Gate reads on bHasTarget (the value is stale after
	 *  arrival by design — "last ordered goal"). PreTick systems (avoidance
	 *  arrival-release, cohesion laggard detection) read it via component storage. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector TargetLocation = FFixedVector::ZeroVector;

	/** True when an active move-to action is in flight. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bHasTarget = false;

	/** Persistent world-space velocity vector (planar XY, world units / second).
	 *  Lives on the component, not the per-action movement instance, so
	 *  velocity carries smoothly across new move orders — issuing a new MoveTo
	 *  while in motion preserves momentum instead of snapping to zero.
	 *
	 *  Decoupled from facing: a unit's rotation lives on its FFixedTransform,
	 *  this vector lives here. For non-strafing movement subclasses (every
	 *  shipped one) the subclass's Tick maintains the invariant
	 *  `Velocity = Forward × Speed` end-of-tick so velocity stays parallel to
	 *  facing by construction.
	 *
	 *  Final-arrival logic zeros it (units come to rest at the destination);
	 *  cancellation/preemption intentionally leaves it set so the next order
	 *  picks up where the previous left off. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector Velocity = FFixedVector::ZeroVector;

	/** True when an active move action has entered the kinematic brake zone —
	 *  the unit is decelerating toward the final waypoint. Surfaces to AnimBPs
	 *  so animation graphs can blend into "approaching destination" anims
	 *  (slowing-down idle, vehicle brake-light, etc.) without re-deriving
	 *  from speed deltas. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bArrivalImminent = false;

	/** Smoothed pitch (radians, positive = nose up). Ground movements
	 *  rate-limit their per-tick pitch update toward the slope-sampled
	 *  target (USeinMovement::SmoothAngleToward) and store the smoothed
	 *  value here so it persists across ticks AND across move orders —
	 *  the unit's orientation is continuous regardless of order changes.
	 *  Without this, the visual orientation snapped instantly to whatever
	 *  the slope sampler returned each tick, which produced visible
	 *  twitches when terrain samples briefly crossed wall edges (the
	 *  "split-second sideways tilt" at narrow walls). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint SmoothedPitch = FFixedPoint::Zero;

	/** Smoothed roll (radians, positive = right side down, per
	 *  MakeFromEulers convention). Same rate-limit smoothing as
	 *  SmoothedPitch — see that field's comment. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint SmoothedRoll = FFixedPoint::Zero;

	/** Local-avoidance steering strength. The soft steering layer
	 *  (FSeinAvoidanceSystem, PreTick) scales each unit's computed lateral nudge by
	 *  this before the movement Tick consumes it. **0 = opt out** — the unit ignores
	 *  avoidance entirely and its motion is bit-identical to a world with no
	 *  avoidance (the hard penetration floor still applies). Per-class override:
	 *  tune low for ponderous units, higher for nimble ones. Default 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance",
		meta = (ClampMin = "0.0", DisplayName = "Avoidance Strength"))
	FFixedPoint AvoidanceStrength = FFixedPoint::FromInt(1);

	/** Avoidance priority WEIGHT. A unit's local-avoidance steer (FSeinAvoidanceSystem) only
	 *  YIELDS to neighbours whose weight QUALIFIES — equal-or-higher by default (see
	 *  bAvoidSameWeights). So a heavier unit plows straight through lighter ones: the lighter
	 *  ones dodge it, it ignores them. Set tanks high and infantry low for the classic
	 *  "infantry scatter, the tank doesn't flinch" feel. Higher = harder to push around. A
	 *  neighbour with no movement component carries no weight (a static obstacle) and is always
	 *  avoided. Default 0 (all units equal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance",
		meta = (DisplayName = "Avoidance Weight"))
	int32 AvoidanceWeight = 0;

	/** Whether a unit also avoids EQUAL-weight neighbours, or only strictly-heavier ones.
	 *  **true (default):** avoid weight ≥ self — equal-weight peers steer around each other
	 *  (everyone in a crowd gives way). **false:** avoid weight > self only — equal-weight peers
	 *  STOP dodging one another and resolve through the penetration floor instead, which kills
	 *  the same-class mutual-avoidance orbits that show up as spinning blobs. Pair with equal
	 *  AvoidanceWeight across a unit class to use it as a per-class "don't circle each other" switch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance",
		meta = (DisplayName = "Avoid Same Weights"))
	bool bAvoidSameWeights = true;

	/** This unit's current avoidance OUTPUT (lateral steer + speed-yield scale), written
	 *  by the active avoidance impl (USeinAvoidance) at PreTick and consumed by the
	 *  movement Tick (USeinMovement::ApplyAvoidanceSteer + GetAvoidanceSpeedScale).
	 *  Runtime sim state; hashed as a desync canary. Stays at its zero-steer / unity-scale
	 *  default for AvoidanceStrength = 0 units, which is what makes them a true no-op. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FSeinAvoidanceOutput AvoidanceOutput;

	/** World location at the previous avoidance PreTick sample — the active avoidance
	 *  model's serial pre-pass maintains this for every movement-carrying entity (idle
	 *  or ordered) so PreTick systems can measure the unit's ACTUAL per-tick world
	 *  displacement, collision included.
	 *
	 *  Velocity cannot serve that read: it is the unit's own commanded step (post
	 *  nav-floor, PRE body-collision — the collision resolver never writes it back),
	 *  so a body-blocked unit pressing into a crowd reads ~full commanded speed while
	 *  its body goes nowhere. ZeroVector = no sample yet (fresh spawn, or no avoidance
	 *  model active — consumers must fall back to the commanded-velocity test).
	 *  Runtime sim state, hashed. */
	UPROPERTY()
	FFixedVector PrevTickLocation = FFixedVector::ZeroVector;

	/** One-time spawn floor-snap latch. False on a freshly spawned/placed entity;
	 *  set true after the movement driver's first idle tick performs the initial
	 *  ground + slope snap (USeinMovement::TickIdle's first-contact branch — the
	 *  always-on FSeinMovementDriverSystem subsumed the retired
	 *  FSeinInitialSnapSystem in CP2.1), so a placed unit rests on the floor with
	 *  correct pitch/roll BEFORE its first move order. Move ticks and idle ticks
	 *  both re-snap every tick and ignore this flag — it exists purely to make
	 *  the INSTANT (unsmoothed) spawn-time snap run exactly once per entity. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bInitialGroundSnapDone = false;

	/** Idle re-seek HOME (muster pose) for an UN-BROKERED unit: the pose it came to rest at under its
	 *  own control, seeded ONCE at the first-contact ground-snap (see USeinMovement::TickIdle) and
	 *  consumed by the broker system's loose-home-return when the unit is shoved off it. A shove does
	 *  NOT update it — returning HERE is the whole point. Once the unit is ordered it gains a persistent
	 *  broker whose SettledSlotPositions is the authoritative home, and this per-unit home is no longer
	 *  consulted. Runtime sim state, hashed. */
	UPROPERTY()
	FFixedVector HomePos = FFixedVector::ZeroVector;

	/** Facing captured alongside HomePos (the muster-pose orientation). Hashed. */
	UPROPERTY()
	FFixedQuaternion HomeFacing = FFixedQuaternion::Identity;

	/** False until HomePos/HomeFacing are seeded (the first idle ground-snap tick after nav loads).
	 *  Gates the loose-home-return: an un-seeded unit has no home to return to. Hashed. */
	UPROPERTY()
	bool bHomeSeeded = false;

	/** Per-unit custom render/anim values a movement mode writes each tick for the render layer to read
	 *  (e.g. a hover's bank angle in slot 0, a tank's tread-speed delta in slot 1). Render-only: written
	 *  via Set Render Value (Sein Mover Handle), read via Get Movement Render Value (Sein Movement
	 *  Library). NOT part of the deterministic state hash — it's cosmetic output the mode computes
	 *  deterministically, so it never affects the sim. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Movement|State")
	TArray<FFixedPoint> RenderState;
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementComponent& C)
{
	uint32 Hash = GetTypeHash(C.TopSpeed);
	Hash = HashCombine(Hash, GetTypeHash(C.TurnRate));
	Hash = HashCombine(Hash, GetTypeHash(C.MovementClass));
	Hash = HashCombine(Hash, GetTypeHash(C.bCanReverse));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseTopSpeed));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseEngageDotThreshold));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseEngageDistanceThreshold));
	Hash = HashCombine(Hash, GetTypeHash(C.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(C.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(C.Velocity));
	Hash = HashCombine(Hash, GetTypeHash(C.bArrivalImminent));
	Hash = HashCombine(Hash, GetTypeHash(C.SmoothedPitch));
	Hash = HashCombine(Hash, GetTypeHash(C.SmoothedRoll));
	Hash = HashCombine(Hash, GetTypeHash(C.AvoidanceStrength));
	Hash = HashCombine(Hash, GetTypeHash(C.AvoidanceWeight));
	Hash = HashCombine(Hash, GetTypeHash(C.bAvoidSameWeights));
	Hash = HashCombine(Hash, GetTypeHash(C.AvoidanceOutput));
	Hash = HashCombine(Hash, GetTypeHash(C.PrevTickLocation));
	Hash = HashCombine(Hash, GetTypeHash(C.bInitialGroundSnapDone));
	Hash = HashCombine(Hash, GetTypeHash(C.HomePos));
	Hash = HashCombine(Hash, GetTypeHash(C.HomeFacing));
	Hash = HashCombine(Hash, GetTypeHash(C.bHomeSeeded));
	// MovementClassData is hashed by the framework attribute resolver's
	// reflection walk; skipped here (FInstancedStruct doesn't expose a
	// stable GetTypeHash for arbitrary inner structs).
	return Hash;
}
