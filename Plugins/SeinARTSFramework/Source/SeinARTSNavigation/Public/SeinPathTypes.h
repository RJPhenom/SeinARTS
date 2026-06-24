/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPathTypes.h
 * @brief   Shared path query / result structs. Kept impl-agnostic — any
 *          USeinNavigation subclass consumes FSeinPathRequest and produces
 *          FSeinPath.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.generated.h"

/** Outcome of a budgeted path request via `USeinNavigationSubsystem::RequestPath`.
 *  Distinguishes "couldn't get a path right now, try again next tick" (Throttled)
 *  from "no path exists" (NotFound) — the move action treats the former as wait,
 *  the latter as fail. Direct `USeinNavigation::FindPath` callers never see
 *  Throttled; that path is unbudgeted. */
UENUM(BlueprintType)
enum class ESeinPathResult : uint8
{
	/** Path found. OutPath valid; check `bIsPartial` for partial / best-effort. */
	Found            UMETA(DisplayName = "Found"),

	/** Search completed and no path exists (start blocked, unreachable region,
	 *  or out-of-bounds). OutPath invalid. Caller should fail the order. */
	NotFound         UMETA(DisplayName = "Not Found"),

	/** Per-tick budget exhausted. OutPath invalid. Caller should retry next
	 *  sim tick. Counter resets on `USeinWorldSubsystem::OnSimTickCompleted`. */
	Throttled        UMETA(DisplayName = "Throttled"),

	/** Navigation subsystem unavailable / nav data unloaded. OutPath invalid.
	 *  Caller should fail the order. */
	NoNavigation     UMETA(DisplayName = "No Navigation"),
};

/** Pathfinding query. Impl-specific fields (cost caps, agent class) live on
 *  the nav subclass; keep this struct small + engine-agnostic. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPathRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedVector Start;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedVector End;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FSeinEntityHandle Requester;

	/** Terrain classes this agent treats as impassable. The shipped USeinNavigationAStar
	 *  honors this as a HARD filter: cells whose baked terrain type maps to any listed tag
	 *  are excluded from both the A* topology and the line-of-sight smoother for this
	 *  request (e.g. an amphibious-only unit that lists "Water"). Matching is hierarchical —
	 *  listing a parent tag bars its child types. Pass/block only; there is no per-tag soft
	 *  routing COST (that is the separate, unbuilt cost-region work). A custom
	 *  USeinNavigation may interpret it differently. Empty = no filter. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FGameplayTagContainer BlockedTerrainTags;

	/** Agent layer mask — which layer bits identify the requesting agent
	 *  for nav-blocker intersection. The pathfinder skips dynamic blockers
	 *  whose `BlockedNavLayerMask & AgentNavLayerMask == 0`. Default 0xFF
	 *  (matches all blockers — preserves single-layer behavior when the
	 *  caller doesn't fill this in). MoveToAction populates from
	 *  FSeinMovementData::NavLayerMask at request time. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	uint8 AgentNavLayerMask = 0xFF;

	/** Agent body radius in world units. The default A* converts this to a whole-cell
	 *  clearance requirement and runs the search in the unit's CONFIGURATION SPACE: only
	 *  cells where the body physically fits (WallDistance >= ceil(radius / CellSize + 0.5)
	 *  + AgentWallPaddingCells) are expanded, so big vehicles route through wider corridors
	 *  while infantry (radius near 0) take the shortest line. It is a hard topology gate,
	 *  not a soft cost — a unit is never routed through a gap its body can't clear (with a
	 *  carve-out that lets a unit escape a too-tight START cell, and stop AT an adjacent
	 *  destination). Exact treatment is impl-defined. Default 0 = no clearance requirement
	 *  (paths may hug walls). MoveToAction populates this from the unit's resolved footprint
	 *  (Extents max bounding radius, else the Navigation Component's Fallback Footprint
	 *  Radius) at request time. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedPoint AgentFootprintRadius;

	/** Extra cells of wall clearance (on top of the agent's footprint) the planner keeps
	 *  the path away from walls. Feeds the configuration-space clearance gate alongside
	 *  the footprint (Required = ceil(AgentFootprintRadius / CellSize + 0.5) +
	 *  AgentWallPaddingCells), so raising it both tightens which cells A* will route
	 *  through AND pushes smoothed waypoints further from walls along the nav's
	 *  WallDistance gradient. Best-effort on the push (in narrow corridors waypoints
	 *  settle on the corridor centerline); never fails the path.
	 *
	 *  Capped at the planner's BFS expansion radius (default 64 cells); values above the
	 *  cap saturate at it. Applies to the destination waypoint too: an order adjacent to
	 *  a wall stops the unit N cells out in open space. MoveToAction populates this from
	 *  the Navigation Component's Wall Padding (default 0). Abilities that want pinpoint
	 *  stops (Garrison / Attack / interact) zero it on their move data so the push is a
	 *  no-op. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	int32 AgentWallPaddingCells = 0;

	/** When true, `End` is an AUTHORITATIVE destination (e.g. a cover slot) that
	 *  OVERRULES the coarse nav bake. The planner honors `End` as the exact final
	 *  waypoint even on a partial path (when a reachable cell is adjacent to it),
	 *  and skips the wall-push on that final waypoint; the mover is allowed to
	 *  stand on it even if its cell is bake-blocked. Default false — nav decides
	 *  reachability and a partial path stops at the nearest reachable cell. The
	 *  destination is an INPUT, not an opinion nav may relocate (root CLAUDE.md #6). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	bool bAuthoritativeDestination = false;

	/** Per-request cap on A* node expansions. 0 = use the project default
	 *  (USeinARTSCoreSettings::AStarMaxIterations). Set a smaller value to bound an
	 *  expensive / long-range pathfind — A* returns a best-effort partial path
	 *  (bIsPartial) if the cap is hit rather than searching the whole grid. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path", meta = (ClampMin = "0"))
	int32 AgentMaxSearchNodes = 0;
};

/** Kind of motion a path segment represents. Only `Straight` is built today — the
 *  enum is intentionally a one-value extensibility seam. New kinds are added by whatever
 *  PRODUCES the path: the nav (`FindPath`) for topology kinds (e.g. link / jump hops over
 *  off-mesh edges), a movement planner (`USeinMovement::PlanPath`) for kinematic kinds
 *  (e.g. arc curve-fitting). Consumers default-handle unknown values as straights, so
 *  growing it stays additive. */
UENUM(BlueprintType)
enum class ESeinPathSegmentType : uint8
{
	/** Straight-line segment from `From` to `To`. One segment per
	 *  `Waypoints[i] → Waypoints[i+1]` pair. */
	Straight UMETA(DisplayName = "Straight"),
};

/** Single typed segment in a planned path. One segment per consecutive
 *  `Waypoints[i] → Waypoints[i+1]` pair, derived by
 *  `DeriveSegmentsFromWaypoints` after the path is committed. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPathSegment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	ESeinPathSegmentType Type = ESeinPathSegmentType::Straight;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedVector From;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedVector To;
};

/** Path query result — a route carried as TWO complementary views:
 *    - `Waypoints` : the world-space polyline the built-in follower drives (Start → End).
 *    - `Segments`  : the same route as TYPED segments (Straight today) — an extensibility
 *                    seam for arc / link / jump kinds emitted by whatever produces the path
 *                    (the nav for topology kinds, a movement planner for kinematic kinds).
 *  The follower drives the waypoint backbone; segment-aware driving is opt-in for a custom
 *  movement mode (read them via the Mover Handle's Get Segment / Get Path Segments nodes). */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPath
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	TArray<FFixedVector> Waypoints;

	/** Typed segment representation of the path. One `Straight` segment per
	 *  consecutive `Waypoints[i] → Waypoints[i+1]` pair, derived by
	 *  `DeriveSegmentsFromWaypoints` after the path is committed. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	TArray<FSeinPathSegment> Segments;

	/** Total planar (XY) world-space length of the path — the sum of segment
	 *  lengths, set by `DeriveSegmentsFromWaypoints` when the path is committed.
	 *  Use it to compare routes by travel distance (not the waypoint count).
	 *  Does NOT include terrain cost weighting. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedPoint TotalCost;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	bool bIsValid = false;

	/** True if the path does not reach the exact destination (partial / best-effort). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	bool bIsPartial = false;

	void Clear()
	{
		Waypoints.Reset();
		Segments.Reset();
		TotalCost = FFixedPoint::Zero;
		bIsValid = false;
		bIsPartial = false;
	}

	int32 GetWaypointCount() const { return Waypoints.Num(); }

	FFixedVector GetWaypoint(int32 Index) const
	{
		return Waypoints.IsValidIndex(Index) ? Waypoints[Index] : FFixedVector();
	}

	/** Populate `Segments` from `Waypoints` as a sequence of `Straight`
	 *  segments — one per consecutive waypoint pair. Resets and re-fills;
	 *  call AFTER all waypoint mutations (smoothing, wall push) are complete.
	 *  No-op when `Waypoints.Num() < 2`. */
	void DeriveSegmentsFromWaypoints()
	{
		Segments.Reset();
		TotalCost = FFixedPoint::Zero;
		const int32 N = Waypoints.Num();
		if (N < 2) return;
		Segments.Reserve(N - 1);
		for (int32 i = 0; i + 1 < N; ++i)
		{
			FSeinPathSegment Seg;
			Seg.Type = ESeinPathSegmentType::Straight;
			Seg.From = Waypoints[i];
			Seg.To   = Waypoints[i + 1];
			// Accumulate planar (XY) length — the travel-distance convention the
			// mover uses (Z is terrain-follow, not travel).
			FFixedVector Delta = Waypoints[i + 1] - Waypoints[i];
			Delta.Z = FFixedPoint::Zero;
			TotalCost += Delta.Size();
			Segments.Add(MoveTemp(Seg));
		}
	}
};
