/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinNavigationComponent.h
 * @brief:   Pathfinding + nav-layer + repath authoring for movable entities.
 *           Extracted from the legacy `FSeinMovementData` as part of the
 *           Phase-5 module decomposition — nav-relevant authoring lives in
 *           the navigation module, movement-relevant authoring lives in the
 *           movement module (`FSeinMovementPayload`).
 *
 *           Designer authoring lives on the entity bridge's ComponentData
 *           array — pick `FSeinNavigationPayload` as an entry to author
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
#include "Components/SeinPayload.h"
#include "Components/SeinExtentsPayload.h"  // ESeinNavLayerBit
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "SeinNavigationPayload.generated.h"

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
	 *  polyline — cheaper and more reactive than Interval. The off-path
	 *  detector measures the unit against its path each tick and repaths
	 *  (budget-gated) once it strays past the threshold. */
	OffPathOnly     UMETA(DisplayName = "Off-Path Only")
};

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinNavigationPayload : public FSeinPayload
{
	GENERATED_BODY()

	/** Body radius (world units) the framework falls back to when this entity has no
	 *  Extents Component. Almost every RTS unit has an Extents Component — it drives FoW
	 *  occlusion, hit detection, and nav blocking — and when present THAT is the body-size
	 *  source of truth and this field is ignored.
	 *
	 *  Use it for prototype units or simple actors where you want a quick collision
	 *  footprint without authoring full Extents. When it IS used (no Extents on the
	 *  Blueprint) it drives path clearance (planner keeps the body clear of walls),
	 *  penetration (units never overlap footprints), and avoidance (perception radius for
	 *  nearby units). Resolution order, matching collision and pathfinding: (1) Extents
	 *  shapes — max bounding radius — preferred; (2) this field — fallback when Extents is
	 *  absent; (3) 0 — intangible (no collision, clearance, or avoidance). Rough guides:
	 *  infantry / small bipeds 50, wheeled vehicles 100, tracked vehicles 150. Default 50. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0", DisplayName = "Fallback Footprint Radius"))
	FFixedPoint FallbackFootprintRadius = FFixedPoint::FromInt(50);

	/** Extra wall clearance for this unit, in nav cells, on top of the geometric minimum.
	 *  The planner already keeps just enough room for the body to clear walls; this adds N
	 *  more cells of margin. 0 = just barely fit (the default).
	 *
	 *  The geometric minimum is Required = ceil(FootprintRadius / CellSize + 0.5), where
	 *  the +0.5 is the half-cell from cell-center to wall edge. 0 = tight corridors OK,
	 *  light scrapes possible on fast turns; 1 = a comfortable cell of slack for steering
	 *  bumps and avoidance; 2+ = wider corridors required, typical for tanks/vehicles that
	 *  want turn-radius slack without rubbing walls. Note: raising this can make a narrow
	 *  corridor UNREACHABLE if its width is below minimum + padding — correct (the unit
	 *  genuinely doesn't fit), but watch for paths suddenly routing the long way around.
	 *  Capped at 64 (the planner's clearance-BFS radius); useful range 0–8. Default 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0", ClampMax = "64", UIMin = "0", UIMax = "16",
				DisplayName = "Wall Padding (cells)"))
	int32 WallPadding = 0;

	/** The arrival acceptance a unit falls back to when it has no navigation component, or an
	 *  explicitly zero AcceptanceRadius. Single source of truth shared by the move-completion
	 *  check (SeinMoveToAction), the movement-trace settle classifier, and the idle re-seek
	 *  displacement floor - so the fallback and this field's own default can never drift apart. */
	static FORCEINLINE FFixedPoint DefaultArrivalAcceptance() { return FFixedPoint::FromInt(50); }

	/** How close (world units) the unit must get to its destination to count as arrived —
	 *  within this distance it stops and reports the move complete.
	 *
	 *  Tune higher for units that can't stop on a dime (wheeled vehicles especially, where
	 *  turn radius prevents tight arrivals); lower for infantry expected to land exactly on
	 *  a spot. Default 50 = half a 100cm cell. Per-unit and authoritative — there is no
	 *  per-call override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0"))
	FFixedPoint AcceptanceRadius = DefaultArrivalAcceptance();

	/** Which runtime blocker classes affect this unit. The unit is blocked by an
	 *  `FSeinExtentsPayload` dynamic-nav stamp only when at least one bit is
	 *  shared with that stamp's `BlockedNavLayerMask`. Static baked terrain is
	 *  deliberately separate; use Blocked Terrain Tags below for per-unit
	 *  ground restrictions. Default 0x01 (bit 0 = normal ground units).
	 *
	 *  Example: a temporary ground barricade stamps only the Default bit;
	 *  infantry keeps Default and routes around it, while a hover unit using a
	 *  distinct bit ignores it. Use Blocked Terrain Tags—not this mask—for baked
	 *  water, mud, road, or other ground classifications. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSCoreEntity.ESeinNavLayerBit"))
	uint8 NavLayerMask = 0x01;

	/** Baked terrain classes this unit treats as impassable. Matching is
	 *  hierarchical, so blocking `SeinARTS.Terrain.Water` also blocks authored
	 *  child types such as `SeinARTS.Terrain.Water.Deep`. The policy is carried through the
	 *  initial path, every repath, field-direction and escape query, the runtime
	 *  movement floor, collision/containment correction, and formation-slot
	 *  projection. Empty means no per-unit terrain restriction.
	 *
	 *  This is a hard topology rule, not a cost preference. Global terrain
	 *  `Nav Cost` and `Speed Multiplier` remain independent soft policies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (Categories = "SeinARTS.Terrain", DisplayName = "Blocked Terrain Tags"))
	FGameplayTagContainer BlockedTerrainTags;

	/** When a moving unit re-runs the pathfinder to react to world changes — turn drift,
	 *  blocker changes, new buildings appearing. Repathing keeps the unit honest. See
	 *  Repath Mode for the option descriptions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation")
	ESeinRepathMode RepathMode = ESeinRepathMode::Interval;

	/** Seconds between automatic repaths (Interval mode only). Smaller = more reactive to
	 *  world changes (new walls, destroyed gates) but more pathfinder work per second;
	 *  larger = cheaper but stale paths persist longer. Default 0.25s. Ignored unless
	 *  Repath Mode is Interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.05"))
	FFixedPoint RepathInterval = FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4);

	/** How many repaths may fail in a row before the move gives up entirely instead of
	 *  thrashing forever — e.g. when new blockers or a sealed destination keep returning
	 *  no-path. Each successful repath resets the count. Default 3 ≈ 0.75s at the default
	 *  0.25s interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "1"))
	int32 RepathFailureLimit = 3;

	/** How far (world units) the unit may drift from its planned path before Off-Path Only
	 *  mode triggers a fresh pathfind. Used only when Repath Mode is Off-Path Only.
	 *
	 *  Smaller = repaths on every minor avoidance bump (twitchy); larger = newly placed
	 *  obstacles don't trigger a recompute until the unit is well off course. Default 75cm
	 *  — a bit bigger than the default footprint so passive avoidance bumps don't thrash
	 *  the planner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0.0"))
	FFixedPoint OffPathThreshold = FFixedPoint::FromInt(75);

	/** Per-unit cap on pathfinder work (A* node expansions) for one path request, bounding
	 *  how hard the planner searches before returning its best-effort partial path to the
	 *  closest reachable cell. 0 = use the project default from plugin settings.
	 *
	 *  Smaller caps a unit's worst-case pathfind cost on huge / maze-like maps (cheap
	 *  skirmishers that should give up fast rather than stall the tick); larger lets an
	 *  important unit search harder for a long-range route. Hitting the cap is not a
	 *  failure — the unit still moves along the partial path toward the goal and (if
	 *  repathing) tries again from closer up. Default 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Navigation",
		meta = (ClampMin = "0", DisplayName = "Max Search Nodes"))
	int32 MaxSearchNodes = 0;

	// NOTE: preview opt-in is no longer sim data. Whether a unit draws destination
	// markers is a render-side choice: add a Formation Preview Component
	// (USeinFormationPreviewComponent, SeinARTSFramework) to the unit's Blueprint.
};

FORCEINLINE uint32 GetTypeHash(const FSeinNavigationPayload& C)
{
	uint32 H = GetTypeHash(C.FallbackFootprintRadius);
	H = HashCombine(H, GetTypeHash(C.WallPadding));
	H = HashCombine(H, GetTypeHash(C.AcceptanceRadius));
	H = HashCombine(H, GetTypeHash(C.NavLayerMask));
	{
		TArray<FGameplayTag> Tags;
		C.BlockedTerrainTags.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		});
		H = HashCombine(H, GetTypeHash(Tags.Num()));
		for (const FGameplayTag& Tag : Tags)
		{
			H = HashCombine(H, GetTypeHash(Tag));
		}
	}
	H = HashCombine(H, GetTypeHash(static_cast<uint8>(C.RepathMode)));
	H = HashCombine(H, GetTypeHash(C.RepathInterval));
	H = HashCombine(H, GetTypeHash(C.RepathFailureLimit));
	H = HashCombine(H, GetTypeHash(C.OffPathThreshold));
	H = HashCombine(H, GetTypeHash(C.MaxSearchNodes));
	return H;
}
