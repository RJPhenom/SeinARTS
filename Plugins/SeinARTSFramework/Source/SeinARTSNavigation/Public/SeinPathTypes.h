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
	 *  request (e.g. a ground-only unit that lists `SeinARTS.Terrain.Water`). Matching is hierarchical —
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
	 *  FSeinNavigationComponent::NavLayerMask at request time. */
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
	 *  (USeinNavigationAStar::AStarMaxIterations, on the nav class CDO). Set a smaller value to bound an
	 *  expensive / long-range pathfind — A* returns a best-effort partial path
	 *  (bIsPartial) if the cap is hit rather than searching the whole grid. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path", meta = (ClampMin = "0"))
	int32 AgentMaxSearchNodes = 0;

	/** Group / region key for navs that SHARE planning work across many agents — a
	 *  flow-field or hierarchical nav keys ONE field / abstract route per (GroupId, End)
	 *  and reuses it for every member, rather than planning each request independently.
	 *  0 = none (treat as a lone agent). The shipped per-agent A* nav IGNORES this. This
	 *  is a CONTRACT seam only: the framework defines + carries the key (movement stamps
	 *  it from the order's cohesion group); BUILDING the shared-field/route cache keyed by
	 *  it is the nav impl's job. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	int64 GroupId = 0;
};

/** Direction-query input — the "pull" complement to FSeinPathRequest's "push" route.
 *  Asks the nav "from HERE, which way do I head toward Goal?" once, for one agent, this
 *  tick (see USeinNavigation::QueryDirection). A FIELD-shaped nav answers by sampling its
 *  precomputed field at From (cheap, shared across a group); a ROUTE-shaped nav answers by
 *  routing and returning the first step's heading. Lean by design — a per-tick query, not
 *  a full plan — so a field-follower movement mode can poll it every tick. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinDirectionQuery
{
	GENERATED_BODY()

	/** The querying agent's current world position. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	FFixedVector From;

	/** The destination the agent is heading toward. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	FFixedVector Goal;

	/** The querying agent (self-exclusion / per-agent field lookup). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	FSeinEntityHandle Requester;

	/** Terrain classes this agent treats as impassable — same semantics as
	 *  FSeinPathRequest::BlockedTerrainTags. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	FGameplayTagContainer BlockedTerrainTags;

	/** Agent nav-layer mask (dynamic-blocker filtering). Default 0xFF. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	uint8 AgentNavLayerMask = 0xFF;

	/** Agent body radius (clearance), world units. Default 0. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	FFixedPoint AgentFootprintRadius;

	/** Extra whole-cell wall spacing — same semantics as
	 *  FSeinPathRequest::AgentWallPaddingCells. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	int32 AgentWallPaddingCells = 0;

	/** Group / region key for shared-field navs (see FSeinPathRequest::GroupId). 0 = lone.
	 *  A field nav keys ONE field per (GroupId, Goal) and samples it for every member;
	 *  per-agent navs ignore it. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Direction")
	int64 GroupId = 0;
};

/** Escape-target query input — asks the nav "I am mechanically stuck HERE; where is nearby
 *  open space I can physically walk to?" (see USeinNavigation::QueryEscapeTarget). Fired by
 *  the move action's hold-escape ladder when a unit's applied step has been ~zero against a
 *  blocked footprint for a sustained window. Lean by design — a rare per-escalation query,
 *  not a per-tick poll. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinEscapeQuery
{
	GENERATED_BODY()

	/** The stuck agent's current world position. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Escape")
	FFixedVector From;

	/** The querying agent (self-exclusion for impls that consult dynamic state).
	 *  Reserved — the shipped A* impl ignores it. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Escape")
	FSeinEntityHandle Requester;

	/** Terrain classes this agent treats as impassable — same semantics as
	 *  FSeinPathRequest::BlockedTerrainTags. A returned target never sits on one. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Escape")
	FGameplayTagContainer BlockedTerrainTags;

	/** Agent nav-layer mask (dynamic-blocker filtering for target validation). Default 0xFF. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Escape")
	uint8 AgentNavLayerMask = 0xFF;

	/** Agent body radius (clearance), world units. Default 0. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Escape")
	FFixedPoint AgentFootprintRadius;
};

/** Kind of motion a path segment represents. `Straight` is the only kind the shipped
 *  nav / movement PRODUCE today; the rest are CONTRACT seams for non-polyline planners to
 *  emit. New kinds are added by whatever PRODUCES the path: the nav (`FindPath`) for
 *  topology / field kinds, a movement planner (`USeinMovement::PlanPath`) for kinematic
 *  kinds (e.g. arc curve-fitting). Consumers default-handle unknown values as straights,
 *  so growing it stays additive. */
UENUM(BlueprintType)
enum class ESeinPathSegmentType : uint8
{
	/** Straight-line segment from `From` to `To`. One segment per
	 *  `Waypoints[i] → Waypoints[i+1]` pair. */
	Straight UMETA(DisplayName = "Straight"),

	/** Hierarchical ABSTRACT edge (HPA*-style): a hop between two abstract-graph nodes
	 *  (cluster portals) that has NOT been refined to ground waypoints. `From`/`To` are the
	 *  portal anchors. A follower refines it ON DEMAND (route `From`→`To` for the next
	 *  segment as it streams toward it) instead of eagerly expanding the whole route — the
	 *  scaling win for many-agent / long-range planning. Produced by a hierarchical
	 *  `USeinNavigation`; not emitted by the shipped A* nav. */
	AbstractEdge UMETA(DisplayName = "Abstract Edge"),

	/** FIELD-follow segment (flow-field / continuum-crowd style): the route is NOT a
	 *  polyline but a shared DIRECTION FIELD owned by the nav. `From` = the anchor/region,
	 *  `To` = the goal. A follower IGNORES the waypoint backbone for this segment and
	 *  instead samples the nav each tick via `USeinNavigation::QueryDirection` (which reads
	 *  the field), so one field serves a whole group (keyed by `FSeinPathRequest::GroupId`).
	 *  Produced by a field-shaped `USeinNavigation`; not emitted by the shipped A* nav. */
	Field UMETA(DisplayName = "Field"),

	/** Drivable CIRCULAR ARC segment — a Dubins / Reeds-Shepp corner or curve. `From` and
	 *  `To` are the arc endpoints; `Center` is the arc center, `Radius` its radius, and
	 *  `SweepAngle` the SIGNED swept angle (sign = handedness, magnitude = extent in radians).
	 *  `From` + `To` + `Center` + `SweepAngle` fully determine the arc. The built-in follower
	 *  drives it as a fine polyline via `FlattenToWaypoints`; a curve-aware Tier-2 mode may
	 *  pure-pursuit the exact arc by reading the segment (Mover Handle `Get Segment`). Emitted
	 *  by a movement planner (`USeinMovement::PlanPath` via the Planner Handle's `Add Arc
	 *  Segment`) or a curve-planning nav; not emitted by the shipped A* nav. */
	Arc UMETA(DisplayName = "Arc"),

	/** Discrete HOP / link segment — a ballistic jump, a jump-jet leap, a cliff nav-link. The
	 *  unit travels from `From` (launch) to `To` (land) along a mode-defined trajectory rather
	 *  than the ground polyline; the apex/arc is derived by the driving mode, so no extra
	 *  geometry is carried here. The built-in follower falls back to a straight `From`→`To`
	 *  hop; an airborne / jump-capable mode reads the segment and plays its own trajectory.
	 *  Emitted by a nav that produces links or a movement planner; not emitted by the shipped
	 *  A* nav. */
	Jump UMETA(DisplayName = "Jump"),
};

/** Single typed segment in a planned path. For a `Straight` route there is one segment per
 *  consecutive `Waypoints[i] → Waypoints[i+1]` pair (derived by `DeriveSegmentsFromWaypoints`).
 *  For a typed route (containing `Arc`, etc.) the segments are the AUTHORED truth and the
 *  `Waypoints` polyline is a driven approximation of them (see `FSeinPath::FlattenToWaypoints`) —
 *  so segments are NOT 1:1 with waypoint pairs there. The geometry fields below are meaningful
 *  only for the kinds that name them (`Arc`); they stay zero for the others. */
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

	/** ARC center (world). Meaningful only for `Arc`; zero otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedVector Center;

	/** ARC radius (world units). Meaningful only for `Arc`; zero otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedPoint Radius = FFixedPoint::Zero;

	/** SIGNED swept angle of the arc, radians — sign = handedness (positive = counter-clockwise,
	 *  negative = clockwise), magnitude = extent. Meaningful only for `Arc`; zero otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedPoint SweepAngle = FFixedPoint::Zero;

	/** Drive DIRECTION for this segment: true = travel it in REVERSE (backward). Lets one path
	 *  alternate forward and reverse at cusps (Reeds-Shepp), which a single mode-global reverse
	 *  latch cannot express. Carrying the flag is the base contract; ACTING on it (reverse
	 *  kinematics, cusp settle) is the driving mode's job. Default false = forward. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	bool bReverse = false;
};

/** Path query result — a route carried as TWO complementary views:
 *    - `Waypoints` : the world-space polyline the built-in follower drives (Start → End).
 *    - `Segments`  : the same route as TYPED segments — Straight / AbstractEdge / Field / Arc /
 *                    Jump — emitted by whatever produces the path (the nav for topology / field
 *                    kinds, a movement planner for kinematic kinds like Arc).
 *  For a plain Straight route the two views agree 1:1 (`DeriveSegmentsFromWaypoints`). For a
 *  TYPED route the `Segments` are the authored truth and `Waypoints` is a drivable approximation
 *  of them: call `FlattenToWaypoints` once at commit so the built-in follower can drive an arc /
 *  link path with no follower-loop changes. A curve-aware movement mode instead reads the exact
 *  `Segments` (via the Mover Handle's Get Segment / Get Path Segments nodes) and ignores the
 *  approximated backbone. */
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

	/** DEBUG-ONLY: the EXACT A* cell chain (cell centers, world-space) this route was built from,
	 *  captured BEFORE the string-pull smoother collapses it into turn-point `Waypoints`. The nav
	 *  path debug viz draws these 1:1, so the yellow cells match the logical cell path pathfinding
	 *  actually chose. Deliberately NOT a UPROPERTY — never reflected / hashed / serialized, so it
	 *  carries no sim state and cannot affect determinism; populated only in non-shipping builds. */
	TArray<FFixedVector> DebugCellPath;

	void Clear()
	{
		Waypoints.Reset();
		Segments.Reset();
		TotalCost = FFixedPoint::Zero;
		bIsValid = false;
		bIsPartial = false;
		DebugCellPath.Reset();
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

	/** Rebuild `Waypoints` from typed `Segments` into a drivable polyline the built-in follower
	 *  can drive with no follower-loop changes: each `Arc` is expanded into fixed-point samples
	 *  bounded by `MaxChordError` (the maximum allowed sagitta between the true arc and a sampled
	 *  chord); every other kind (Straight / AbstractEdge / Field / Jump) collapses to its
	 *  `From`→`To` endpoints. `Segments` is PRESERVED as the authored typed truth (render + a
	 *  curve-aware mode read it), so after this call `Waypoints` is a fine polyline no longer 1:1
	 *  with `Segments`. Endpoints are copied exactly (no fixed-point drift at joins); the terminal
	 *  waypoint stays the exact destination, preserving the destination-preview invariant.
	 *
	 *  SELF-GUARDING and DETERMINISTIC: a no-op when `Segments` holds only Straight kinds (the
	 *  shipped case) — `Waypoints` is left untouched — so this is safe to call unconditionally on
	 *  any committed path. When it does expand, it uses fixed-point LUT trig and a FIXED chord
	 *  tolerance so every peer produces an identical waypoint count and identical positions; pass
	 *  a compile-time / config constant for `MaxChordError`, never a per-run tunable that could
	 *  drift across machines. Defined out-of-line in SeinPathTypes.cpp (keeps the trig include out
	 *  of this widely-included header). */
	void FlattenToWaypoints(FFixedPoint MaxChordError);

	/** True if any segment is a non-`Straight` kind (Arc / Jump / Field / AbstractEdge) — i.e.
	 *  the route needs `FlattenToWaypoints` (or a segment-aware mode) rather than plain waypoint
	 *  following. False for the shipped all-Straight case. */
	bool HasTypedSegments() const
	{
		for (const FSeinPathSegment& Seg : Segments)
		{
			if (Seg.Type != ESeinPathSegmentType::Straight) return true;
		}
		return false;
	}
};
