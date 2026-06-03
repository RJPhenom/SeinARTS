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
#include "UObject/SoftObjectPath.h"
#include "SeinMovementComponent.generated.h"

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

	/** Acceleration rate (world units per second²) — how quickly current speed
	 *  ramps UP toward target. Drives the smoothstep speed model in BOTH the
	 *  vehicle movements AND infantry: USeinInfantryMovement::Tick feeds this
	 *  into StepSpeedToward (and Deceleration into the arrival brake), so high
	 *  accel/decel give snappy infantry. (Older "infantry ignores it" note was
	 *  stale.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Acceleration = FFixedPoint::FromInt(750);

	/** Deceleration rate (world units per second²) — how quickly current speed
	 *  ramps DOWN toward target. Also the kinematic arrival-brake rate (units
	 *  slow into the final waypoint). Typically >= Acceleration so units brake
	 *  at least as hard as they accelerate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Deceleration = FFixedPoint::FromInt(750);

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
				SeinDeterministicOnly))
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
	// Position keeping (passive re-seek) — opt-in, infantry-oriented
	// =========================================================================

	/** When true, this unit passively holds its position: if it goes idle and
	 *  gets shoved off its last move target (`DesiredPosition`) — a formation slot,
	 *  cover slot, or a plain move point — it re-paths back on its own. Intended
	 *  for infantry (formation / cover keeping); leave OFF for vehicles, which
	 *  shouldn't auto-return when bumped. Off by default — designers opt in per
	 *  unit type (same convention as `bCanReverse`). When false the position-keep
	 *  system skips this entity entirely: no `DesiredPosition` claim and no
	 *  re-seek, so it costs nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bMaintainPosition = false;

	// =========================================================================
	// Runtime state (BlueprintReadWrite, not authored)
	// =========================================================================

	/** Active move target world position (sim space). */
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

	/** Persistent "home" the unit passively returns to when it's idle and gets
	 *  displaced (its formation slot / cover slot / last move target). Set when a
	 *  move starts; the position-keeping pass re-seeks it after penetration or
	 *  collision bumps the unit off. Also the hook for AI "hold this cover" — set
	 *  it to a slot and the unit settles there and stays. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector DesiredPosition = FFixedVector::ZeroVector;

	/** True once DesiredPosition is meaningful (a move has established a home).
	 *  The position-keeper ignores units that have never been ordered anywhere. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bHasDesiredPosition = false;
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementComponent& C)
{
	uint32 Hash = GetTypeHash(C.TopSpeed);
	Hash = HashCombine(Hash, GetTypeHash(C.Acceleration));
	Hash = HashCombine(Hash, GetTypeHash(C.Deceleration));
	Hash = HashCombine(Hash, GetTypeHash(C.TurnRate));
	Hash = HashCombine(Hash, GetTypeHash(C.MovementClass));
	Hash = HashCombine(Hash, GetTypeHash(C.bCanReverse));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseTopSpeed));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseEngageDotThreshold));
	Hash = HashCombine(Hash, GetTypeHash(C.ReverseEngageDistanceThreshold));
	Hash = HashCombine(Hash, GetTypeHash(C.bMaintainPosition));
	Hash = HashCombine(Hash, GetTypeHash(C.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(C.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(C.Velocity));
	Hash = HashCombine(Hash, GetTypeHash(C.bArrivalImminent));
	Hash = HashCombine(Hash, GetTypeHash(C.SmoothedPitch));
	Hash = HashCombine(Hash, GetTypeHash(C.SmoothedRoll));
	Hash = HashCombine(Hash, GetTypeHash(C.DesiredPosition));
	Hash = HashCombine(Hash, GetTypeHash(C.bHasDesiredPosition));
	// MovementClassData is hashed by the framework attribute resolver's
	// reflection walk; skipped here (FInstancedStruct doesn't expose a
	// stable GetTypeHash for arbitrary inner structs).
	return Hash;
}
