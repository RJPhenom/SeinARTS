/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinNavigationComponent.h
 * @brief:   Pathfinding + nav-layer + repath authoring for movable entities.
 *           Extracted from the legacy `FSeinMovementData` as part of the
 *           Phase-5 module decomposition — nav-relevant authoring lives in
 *           the navigation module, movement-relevant authoring lives in the
 *           movement module (`FSeinMovementComponent`).
 *
 *           Designer authoring lives on the entity bridge's ComponentData
 *           array — pick `FSeinNavigationComponent` as an entry to author
 *           footprint / wall padding / acceptance radius / repath behaviour
 *           / nav-layer mask / preview toggle.
 *
 *           Cross-module coupling: `ESeinNavLayerBit` (the enum the nav
 *           layer mask references) lives on the extents data in
 *           SeinARTSCoreEntity because extents also uses it for nav
 *           blocking. The navigation module reads it via include.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Components/SeinExtentsComponent.h"  // ESeinNavLayerBit
#include "Types/FixedPoint.h"
#include "SeinNavigationComponent.generated.h"

/** When the move-to action recomputes its path during execution. The original
 *  path is computed once at move-start; long moves can drift off-path due to
 *  vehicle turn dynamics or world changes (gates, destroyed buildings,
 *  obstacles appearing). Repathing periodically keeps the unit honest. */
UENUM(BlueprintType)
enum class ESeinRepathMode : uint8
{
	/** Re-run FindPath every `RepathInterval` seconds from current position.
	 *  Robust + simple; cost is one A* search per unit per interval. */
	Interval        UMETA(DisplayName = "Interval"),

	/** Repath only when the unit has demonstrably drifted off the planned
	 *  polyline (cheaper, more reactive). Implementation is deferred —
	 *  selecting this currently behaves as no-repath until the off-path
	 *  detector is wired up. */
	OffPathOnly     UMETA(DisplayName = "Off-Path Only")
};

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinNavigationComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** **FALLBACK body radius (world units) — used ONLY when the entity has
	 *  no `FSeinExtentsComponent`.** Almost every unit in an RTS has an
	 *  Extents component (it drives FoW occlusion, hit detection, nav
	 *  blocking), and when it does, THAT'S the source of truth for body
	 *  size — this field is silently ignored.
	 *
	 *  ELI5: think of this as "what radius the framework should use if I
	 *  didn't bother authoring an Extents component." Useful for prototype
	 *  units or simple actors where you want a quick collision footprint
	 *  without the full Extents setup.
	 *
	 *  Resolution cascade (matches collision and pathfinding both):
	 *    1. Extents Shapes (max BoundingRadius across all shapes) — preferred
	 *    2. THIS field — fallback when Extents is absent
	 *    3. 0 — intangible (no collision, no path clearance, no avoidance)
	 *
	 *  When this field IS used (no Extents on the BP), it drives:
	 *    - Path clearance — planner keeps body clear of walls
	 *    - Penetration — units never overlap their footprints
	 *    - Avoidance — perception radius for nearby units
	 *
	 *  Rough guides for Extents-less units:
	 *    - Infantry / small bipeds: 50
	 *    - Wheeled vehicles (cars, trucks): 100
	 *    - Tracked vehicles (tanks): 150
	 *
	 *  Default 50. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0", DisplayName = "Fallback Footprint Radius"))
	FFixedPoint FallbackFootprintRadius = FFixedPoint::FromInt(50);

	/** **Extra breathing room beyond the geometric minimum, in nav cells.**
	 *  The planner ALREADY keeps enough space for the body to clear walls
	 *  (using the formula `Required = ceil(FootprintRadius/CellSize + 0.5)`,
	 *  where the `+0.5` accounts for the half-cell distance from cell-center
	 *  to wall edge). This field adds N MORE cells on top of that minimum.
	 *
	 *  ELI5:
	 *    - `WallPadding = 0` → default. "Just barely fit" — unit body has
	 *      the minimum possible clearance to walls (the geometric +0.5-cell
	 *      half-cell offset is already baked into the formula). Tight
	 *      corridors OK, light scrapes possible during turns at high speed.
	 *      Opt-in to anything higher if your map / vehicles need slack.
	 *    - `WallPadding = 1` → one full cell of extra space beyond the
	 *      body. Comfortable margin for steering bumps and avoidance.
	 *    - `WallPadding = 2+` → wider corridors required. Tanks/vehicles
	 *      often want this for turn-radius slack without rubbing walls.
	 *
	 *  Be aware: bumping this can make narrow corridors UNREACHABLE if their
	 *  width is below `geometric minimum + padding`. That's correct behavior
	 *  — the unit genuinely doesn't fit comfortably — but designers tuning
	 *  padding should watch for paths suddenly routing the long way around.
	 *
	 *  Capped at 64 (the planner's clearance-BFS expansion radius). Useful
	 *  range typically 0–8. Default 0 (opt-in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0", ClampMax = "64", UIMin = "0", UIMax = "16",
				DisplayName = "Wall Padding (cells)"))
	int32 WallPadding = 0;

	/** **How close (world units) the unit needs to be to its destination
	 *  to count as "arrived."** Below this distance the unit stops and
	 *  reports the move complete.
	 *
	 *  Tune higher for units that can't precisely stop on a dime — wheeled
	 *  vehicles especially, because turn radius prevents tight arrivals.
	 *  Tune lower for infantry / units expected to land exactly on a spot.
	 *
	 *  Default 50 = half a 100cm cell. (There is no per-call override today —
	 *  this per-unit value is authoritative; see API_Cleanup_Pass.md.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0"))
	FFixedPoint AcceptanceRadius = FFixedPoint::FromInt(50);

	/** **Which terrain "classes" this unit is affected by.** A bitmask:
	 *  each bit corresponds to a nav layer defined in plugin settings
	 *  (water, lava, no-build, etc.). The unit is blocked from a cell only
	 *  when AT LEAST ONE of its layer bits is also set in that cell's
	 *  blocker.
	 *
	 *  ELI5 example — "amphibious skips water":
	 *    - The water blocker is authored with the Default bit set.
	 *    - Normal infantry have NavLayerMask = Default → blocked by water.
	 *    - Amphibious unit drops Default and sets Amphibious → no overlap
	 *      with water blocker → walks through water freely.
	 *
	 *  Multi-class units (e.g. amphibious tank that blocks ground units AND
	 *  walks on water) set BOTH bits.
	 *
	 *  Default 0x01 = bit 0 = "Default" — normal ground units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSCoreEntity.ESeinNavLayerBit"))
	uint8 NavLayerMask = 0x01;

	/** **When a moving unit re-runs the pathfinder to react to world
	 *  changes.** Long moves can drift off-path (turn dynamics, blocker
	 *  changes, new buildings appearing). Repathing keeps the unit honest.
	 *  See `ESeinRepathMode` for option descriptions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation")
	ESeinRepathMode RepathMode = ESeinRepathMode::Interval;

	/** **Seconds between automatic repaths (Interval mode only).** Smaller
	 *  = more reactive to world changes (new walls, destroyed gates) but
	 *  costs more pathfinder work per second. Larger = cheaper but stale
	 *  paths persist longer.
	 *
	 *  Default 0.25s. Ignored when RepathMode isn't Interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.05"))
	FFixedPoint RepathInterval = FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4);

	/** **How many failed repaths in a row before giving up on the move.**
	 *  If the pathfinder keeps returning no-path (because of new blockers
	 *  or a sealed destination), after this many tries the move action
	 *  fails entirely rather than thrashing forever.
	 *
	 *  Each successful repath resets the counter. Default 3 ≈ 0.75s at
	 *  the default 0.25s interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "1"))
	int32 RepathFailureLimit = 3;

	/** **How far (world units) the unit can drift from its planned path
	 *  before OffPathOnly mode triggers a fresh pathfind.** Only used when
	 *  RepathMode == OffPathOnly.
	 *
	 *  Smaller = repaths on every minor avoidance bump (twitchy).
	 *  Larger = newly placed obstacles don't trigger recompute until the
	 *  unit is well off course.
	 *
	 *  Default 75cm — a bit bigger than the default footprint so passive
	 *  avoidance bumps don't thrash the planner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0"))
	FFixedPoint OffPathThreshold = FFixedPoint::FromInt(75);

	/** **Show the destination preview decal when this unit is selected.**
	 *  The framework draws a CoH-style "where will I stop" indicator on
	 *  cursor hover during move orders. Set false to suppress for unit
	 *  types where the preview doesn't add value (e.g. always-mobile
	 *  scouts, off-screen support units). Default true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation")
	bool bShowNavigationPreview = true;
};

FORCEINLINE uint32 GetTypeHash(const FSeinNavigationComponent& C)
{
	uint32 H = GetTypeHash(C.FallbackFootprintRadius);
	H = HashCombine(H, GetTypeHash(C.WallPadding));
	H = HashCombine(H, GetTypeHash(C.AcceptanceRadius));
	H = HashCombine(H, GetTypeHash(C.NavLayerMask));
	H = HashCombine(H, GetTypeHash(static_cast<uint8>(C.RepathMode)));
	H = HashCombine(H, GetTypeHash(C.RepathInterval));
	H = HashCombine(H, GetTypeHash(C.RepathFailureLimit));
	H = HashCombine(H, GetTypeHash(C.OffPathThreshold));
	H = HashCombine(H, GetTypeHash(C.bShowNavigationPreview));
	return H;
}
