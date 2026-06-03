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

	/** Terrain tags this agent treats as impassable. Empty = no filter. */
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

	/** Agent collision footprint radius in world units. The pathfinder uses
	 *  this as a "preferred clearance" hint — paths are biased toward routes
	 *  that keep this many cells of breathing room from blocked cells. Big
	 *  vehicles get wider routes through open corridors; infantry (radius
	 *  near 0) ignores the bias and takes the shortest path. The exact
	 *  cost-shaping depends on the nav impl; the default A* impl converts
	 *  this to integer cell-clearance and adds a per-cell penalty
	 *  proportional to how many cells short of the preferred clearance each
	 *  candidate cell is. Default 0 disables the bias (legacy behavior —
	 *  paths hug walls). MoveToAction populates from
	 *  FSeinMovementData::FootprintRadius at request time. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedPoint AgentFootprintRadius;

	/** Extra cells of wall clearance (on top of the agent's footprint) the
	 *  planner should keep the path away from any wall. Best-effort — each
	 *  smoothed-polyline waypoint is shifted along the gradient of the nav's
	 *  WallDistance field until it sits at least `ceil(AgentFootprintRadius
	 *  / CellSize) + AgentWallPaddingCells` from the nearest wall, or the
	 *  gradient saturates (in narrow corridors waypoints land on the local
	 *  WallDistance maximum, i.e. the corridor centerline). Never fails the
	 *  path.
	 *
	 *  Capped at the planner's BFS expansion radius (default 64 cells);
	 *  values above the cap saturate at the cap (the WallDistance field
	 *  itself reads as the cap for cells further than that from any wall).
	 *
	 *  MoveToAction populates from `FSeinMovementData::WallPaddingCells`
	 *  (default 1 on the data struct). Applies to the destination waypoint
	 *  too: an order adjacent to a wall stops the unit N cells out in open
	 *  space. Abilities that want pinpoint stops (Garrison / Attack /
	 *  interact) zero this field on their move data so the push is a no-op. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	int32 AgentWallPaddingCells = 0;
};

/** Kind of motion a path segment represents. Growing the enum is additive;
 *  consumers default-handle unknown values as straights. */
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

/** Path query result. Waypoints are world-space, ordered Start → End. */
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
		const int32 N = Waypoints.Num();
		if (N < 2) return;
		Segments.Reserve(N - 1);
		for (int32 i = 0; i + 1 < N; ++i)
		{
			FSeinPathSegment Seg;
			Seg.Type = ESeinPathSegmentType::Straight;
			Seg.From = Waypoints[i];
			Seg.To   = Waypoints[i + 1];
			Segments.Add(MoveTemp(Seg));
		}
	}
};
