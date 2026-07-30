/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStar.h
 * @brief   Reference implementation of USeinNavigation — single-layer 2D grid
 *          with synchronous A* pathfinding and line-of-sight path smoothing.
 *          Participates in the unified level-data bake (SeinARTSLevelData) as
 *          the "Nav" layer provider: BakeLayer reproduces per-cell cost +
 *          connectivity from the shared substrate surface data, and the runtime
 *          grid loads from the baked channel via LoadFromSubstrate. Serves as
 *          the default nav and the reference for custom subclasses; the
 *          footprint-clearance (C-space) layer is the non-trivial part.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNavigation.h"
#include "SeinLevelLayerProvider.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationAStar.generated.h"

class UWorld;
class USeinLevelData;

#if WITH_DEV_AUTOMATION_TESTS
namespace UE::SeinARTSTests
{
	struct FNavigationAStarTestAccess;
	struct FNavigationCanonicalStateTestAccess;
}
#endif

/**
 * Works out how units walk from A to B: it lays a 2D grid over the level, finds a route around
 * walls and steep ground, then straightens that route into clean waypoints. This is the navigation
 * used out of the box, and the reference other nav classes copy.
 *
 * Uses A* SEARCH over a single-layer square grid. A* explores cells cheapest-route-first, guided by
 * a straight-line distance estimate to the goal, so it finds a short path without checking every
 * cell. Routing weight is per-cell (higher = costlier ground, so the search prefers cheaper terrain);
 * cells are blocked by baked walls, over-steep slopes, or dynamic blockers stamped in each tick. The
 * search is footprint-aware (configuration-space): it keeps the whole unit clear of walls by the
 * agent's radius plus a padding margin, measured against a per-cell distance-to-nearest-wall field,
 * so different unit sizes get different routes from the same bake. After the search, a line-of-sight
 * string-pull smooths the cell chain into a short waypoint list, then a wall-push pass nudges those
 * waypoints toward corridor centers. Genuinely unreachable goals return a partial path to the closest
 * reachable cell rather than failing. Searches run synchronously on the game thread; when batched and
 * Sein.Sim.Parallel is on, each worker gets its own scratch so results stay identical to the serial
 * path. Participates in the unified level-data bake as the "Nav" layer provider: it reproduces
 * per-cell cost and connectivity from the shared level substrate and loads its runtime grid from that
 * baked channel.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Sein Nav (A*)"))
class SEINARTSNAVIGATION_API USeinNavigationAStar : public USeinNavigation, public ISeinLevelLayerProvider
{
	GENERATED_BODY()

private:

#if WITH_DEV_AUTOMATION_TESTS
	friend struct UE::SeinARTSTests::FNavigationAStarTestAccess;
	friend struct UE::SeinARTSTests::FNavigationCanonicalStateTestAccess;
#endif

	/** Open-list node, kept as a class-private nested type so the search heap
	 *  can live inside FAStarScratch. Was previously in an anonymous namespace
	 *  inside the .cpp; moved here strictly to preserve the heap's allocated
	 *  memory across FindPath calls (long paths grow Open to 1000s of entries
	 *  and the local-array version reallocated from scratch on the next
	 *  search). */
	struct FAStarNode
	{
		int32 CellIdx = 0;
		int32 FCost = 0;
		int32 GCost = 0;
		int32 Tiebreak = 0; // insertion order; keeps heap order deterministic

		bool operator<(const FAStarNode& Other) const
		{
			if (FCost != Other.FCost) return FCost < Other.FCost;
			if (GCost != Other.GCost) return GCost > Other.GCost; // prefer higher G (closer to goal)
			return Tiebreak < Other.Tiebreak;
		}
	};

	/** All mutable per-search scratch, bundled so the A* path search is
	 *  REENTRANT: each concurrent search carries its own FAStarScratch, so the
	 *  search methods take it by reference instead of touching shared instance
	 *  members. The serial path uses a single persistent `MainScratch` instance
	 *  (below), preserving the lazy-gen / persistent-buffer behavior the old
	 *  mutable members had — the arrays size once and survive across calls.
	 *  Holds ONLY mutable scratch; read-only grid data (CellCost, WallDistance,
	 *  CellConnections, DynamicBlockers, …) stays on the class, shared and
	 *  immutable during a search. */
	struct FAStarScratch
	{
		/** Per-cell flag (1 = dynamically blocked for this FindPath, 0 = clear).
		 *  Rebuilt at the top of each FindPath; the buffer is reused across calls
		 *  to avoid per-call allocations (per-scratch, so concurrent searches
		 *  don't clash). */
		TArray<uint8> DynamicBlocked;

		/** AABB of cells written into `DynamicBlocked` by the previous
		 *  `BuildDynamicBlockedOverlay` call (inclusive bounds, in grid
		 *  coordinates). Next overlay rebuild clears only this rect instead of
		 *  the full grid — huge savings on large maps with localized blocker
		 *  clusters (e.g. a 1km² map with a few vehicles in one corner used to
		 *  pay 1MB of Memzero per FindPath × 4 paths/tick).
		 *
		 *  `Min.X > Max.X` is the "empty / invalid" sentinel: previous overlay
		 *  wrote nothing, no clear needed. */
		FIntRect LastOverlayDirtyRect = FIntRect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);

		/** Overlay-REUSE signature (perf; bit-identical). BuildDynamicBlockedOverlay is SKIPPED when the
		 *  current request would rebuild the SAME overlay this scratch already holds: same agent mask,
		 *  no blocker/grid mutation since it was built, and no relevant self-exclusion
		 *  (`bOverlayReuseValid`). Exact blocker changes and substrate reloads explicitly invalidate the
		 *  persistent MainScratch; worker scratches are local to one immutable batch. This lets a fresh
		 *  per-worker scratch build the overlay ONCE per async
		 *  batch and reuse it for the rest of that batch's same-mask, non-self-blocker requests, instead
		 *  of re-stamping every blocker per request (the cover-wall batch cost). The reused overlay
		 *  equals the per-request one (exclusion only removes the requester's OWN cells, which a
		 *  non-blocker requester has none of). Only the overlay BYTES are reused; the MaxR-capped
		 *  dynamic-WD cache is still re-derived per request (it is not overlay-pure). */
		bool   bOverlayReuseValid      = false;
		uint8  OverlayReuseMask        = 0;

		/** Per-request dynamic-WD lazy cache (Chebyshev distance from each visited
		 *  cell to its nearest dyn-blocked cell, capped at the per-request
		 *  RequiredClearance), gen-tagged just like the A* search state. Bumped at
		 *  the top of each FindCellPath so consecutive requests don't share stale
		 *  dynamic distances. Per-scratch for reentrancy. */
		TArray<uint8> DynamicWDCache;
		TArray<uint16> DynamicWDCacheGen;
		uint16 CurrentDynamicWDGen = 0;

		/** Per-FindCellPath terrain-tag gate (built in FindCellPath from
		 *  Request.BlockedTerrainTags). RequestBlockedType is a 256-entry lookup
		 *  (1 = that terrain-type index is barred for this agent);
		 *  bRequestHasBlockedTypes guards the hot-path check so an agent that
		 *  blocks no terrain pays nothing. Per-scratch for reentrancy. */
		TArray<uint8> RequestBlockedType;
		bool bRequestHasBlockedTypes = false;

		// A* search state — lazy-validated via SearchCellGen / CurrentSearchGen.
		// The three core search arrays (GCosts, Parents, Closed) are sized once
		// when the grid changes and never re-initialized at the start of a
		// search; a parallel SearchCellGen array stores the search-generation
		// that last touched each cell, so stale entries read as "fresh"
		// (INT32_MAX / -1 / false) without a memzero. Per-scratch for reentrancy.
		TArray<int32> SearchGCosts;
		TArray<int32> SearchParents;
		TArray<uint8> SearchClosed;
		TArray<uint16> SearchCellGen;
		uint16 CurrentSearchGen = 0;

		/** Heap-ordered open list, scratch-scoped so allocation persists across
		 *  searches. `Open.Reset()` at the top of each AStarSearch keeps the
		 *  capacity that previous calls grew to — saves the realloc-from-128
		 *  pattern when consecutive searches both expand to thousands of nodes. */
		TArray<FAStarNode> Open;

		/** Reusable cell-path buffer. AStarSearch fills this (via Reset, not
		 *  realloc) with the reconstructed chain Start→End (or Start→best-H for a
		 *  partial), and FindCellPathInternal reads it. Scratch-scoped so the
		 *  allocation survives across searches instead of a fresh per-call
		 *  TArray-by-value return; per-worker on the parallel batch path, so
		 *  concurrent searches never share it. */
		TArray<FIntPoint> CellPath;
	};

	/** Persistent scratch for the serial (single-threaded) path. The virtual
	 *  FindPath / FindCellPath overrides forward this; its arrays persist across
	 *  calls exactly as the old `mutable` members did, so serial behavior is
	 *  byte-identical. Mutable because the search methods are `const`. */
	mutable FAStarScratch MainScratch;

public:

	// ----------------------------------------------------------------------
	// Designer config (edit on the nav CDO via class defaults — class is
	// instantiated from plugin settings, so these values apply per-project.)
	// ----------------------------------------------------------------------

	/** Maximum walkable slope angle in degrees. Surfaces steeper than this are
	 *  treated as blocked at bake time. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxWalkableSlopeDegrees = 45.0f;

	/** Vertical extent (world units) above the tallest Sein Level Volume that
	 *  the bake traces start from. Bump this if your walkable surfaces sit near
	 *  the top of the volume and you want a margin. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float BakeTraceHeadroom = 200.0f;

	/** Block "obstacle-top" cells at bake — walkable surfaces that perch above the
	 *  lower ground they're disconnected from (wall / cube tops). They are unreachable
	 *  AND invalid standing positions, so IsPassable / placement queries must reject
	 *  them. Legacy nav left them walkable-but-isolated (pathing-correct, but placement-
	 *  wrong, and they show as floating walkable cells when a tall play volume encloses
	 *  them). Detection is local-maximum: a connected walkable component every one of
	 *  whose disconnected walkable 8-neighbours is more than a step LOWER. A same-level
	 *  disjoint play region (D11) keeps a same-level/higher disconnected neighbour and is
	 *  never flagged. Off = legacy behaviour (tops stay walkable-isolated). */
	UPROPERTY(EditAnywhere, Category = "Bake")
	bool bBlockElevatedObstacleTops = true;

	/** Emit cell quads (green = walkable, red = blocked) for the nav debug
	 *  scene proxy. Gated by `ShowFlags.Navigation` / `Sein.Nav.Show`. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawCellsInDebug = true;

	/** Speed-versus-optimality dial for the A* search, as a percent: 100 means "find the shortest
	 *  path no matter what," higher means "find a good path faster, accepting up to that-much longer."
	 *  The search scores cells as f = g + (h * Weight) / 100, so raising Weight biases it harder toward
	 *  the goal and expands fewer cells. Default 125 keeps paths at most 25% longer than optimal
	 *  (visually indistinguishable) for a 5-10x speedup on obstacle-rich terrain; 100 is pure,
	 *  always-optimal A* (slowest); 200 and up is very fast but produces visible zig-zags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "A* Heuristic Weight (%)", ClampMin = "100", ClampMax = "300", UIMin = "100", UIMax = "200"))
	int32 AStarHeuristicWeightPercent = 125;

	/** Hard cap on how much work one path search may do — the planner's patience limit. A* explores
	 *  cells one at a time; once it has expanded this many it gives up and returns the best partial
	 *  path it found (the closest-to-goal cell reached), the same as for a genuinely unreachable goal.
	 *  Default 10000 covers any legitimate path on a 1 square-km map at 100 cm cells. Raise it (50000
	 *  and up) for very large maps or fine grids; lower it for a tighter bound on huge maps with many
	 *  unreachable clicks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "A* Max Iterations", ClampMin = "256", ClampMax = "1000000", UIMin = "1000", UIMax = "100000"))
	int32 AStarMaxIterations = 10000;

	// ----------------------------------------------------------------------
	// USeinNavigation overrides
	// ----------------------------------------------------------------------

	virtual bool HasRuntimeData() const override { return CellCost.Num() > 0; }
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override;
	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override;

	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;
	virtual bool FindCellPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;

	/** Obstacle-aware "which way from here": routes From→Goal (cell path) and returns the
	 *  heading to the first waypoint past the start. This is the PULL-equivalent of FindPath
	 *  for the shipped route-shaped nav — note it runs a search per call, so route-shaped
	 *  movement should consume the PATH api, not poll this every tick; the seam exists so a
	 *  FIELD-shaped nav can answer cheaply. Falls back to the base straight-line on no path. */
	virtual FFixedVector QueryDirection(const FSeinDirectionQuery& Query) const override;

	/** Run a batch of path requests, parallelized across worker threads when
	 *  Sein.Sim.Parallel is on — each worker gets its own FAStarScratch, so the
	 *  searches run race-free and each result is identical to the serial path.
	 *  Falls back to a serial MainScratch loop when parallelism is off / N==1. */
	virtual void RunPathBatch(const TArray<FSeinPathRequest>& Requests, TArray<FSeinPath>& OutResults) const override;

private:
	/** Reentrant body of FindCellPath. The virtual override is a thin wrapper
	 *  that forwards `MainScratch`; this helper takes the per-search scratch
	 *  by reference so the same code can run concurrently on multiple threads,
	 *  each with its own FAStarScratch. Serial behavior is byte-identical to
	 *  the pre-refactor member-scratch version (MainScratch's arrays persist
	 *  across calls exactly as the old mutable members did). */
	bool FindCellPathInternal(const FSeinPathRequest& Request, FSeinPath& OutPath, FAStarScratch& Scratch) const;

	/** Reentrant body of FindPath (cell A* + wall-push + post-validation) operating
	 *  on the supplied scratch. The virtual FindPath passes MainScratch; RunPathBatch's
	 *  parallel path passes a per-worker scratch so concurrent searches don't collide. */
	bool FindPathInternal(const FSeinPathRequest& Request, FSeinPath& OutPath, FAStarScratch& Scratch) const;

public:
	virtual bool IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const override;
	virtual bool GetRandomReachablePoint(const FFixedVector& QueryOrigin, FFixedPoint Radius, FFixedRandom& Rng, FFixedVector& OutPoint) const override;
	virtual bool IsPassable(const FFixedVector& WorldPos) const override;
	virtual bool IsWorldPositionClear(const FFixedVector& WorldPos, uint8 AgentNavLayerMask) const override;
	virtual bool IsPlacementValid(const FFixedVector& CenterWorld, FFixedPoint YawDegrees,
		const FSeinExtentsShape& Shape, uint8 AgentLayerMask) const override;
	virtual FFixedPoint GetCellSize() const override { return CellSize; }
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const override;
	virtual bool ProjectPointToNavOnElevation(const FFixedVector& WorldPos, FFixedVector& OutProjected) const override;
	virtual bool ProjectPointToNavFree(
		const FFixedVector& WorldPos,
		FFixedPoint SelfRadius,
		const TArray<FFixedVector>& AvoidCentres,
		const TArray<FFixedPoint>& AvoidRadii,
		FFixedVector& OutProjected) const override;
	virtual bool GetCellHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ, bool bWalkableOnly = true) const override;
	virtual bool NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const override;
	virtual int32 GetTerrainTypeAt(const FFixedVector& WorldPos) const override;
	// ----------------------------------------------------------------------
	// ISeinLevelLayerProvider (CP1.1 nav port) — nav contributes a "Nav" channel
	// (per-cell Cost + Connections) to the unified level-data bake, reproduced
	// from the shared substrate surface data + nav's own connectivity midpoint
	// traces. Its runtime reads this channel + the shared height; pathing logic
	// stays unchanged.
	// ----------------------------------------------------------------------
	virtual FName GetLayerId() const override;
	virtual void BakeLayer(const USeinLevelData& Substrate, UWorld* World, TArray<uint8>& OutData) override;

	// Unified-pipeline participation (CP1.1): this nav IS a layer provider, and at
	// runtime loads its grid from the baked "Nav" channel + shared height whenever
	// the substrate carries data (the only baked-data path; see subsystem).
	virtual ISeinLevelLayerProvider* GetLevelDataProvider() override { return this; }

	// Debug collectors — declarations stay in all build configs (ABI); bodies
	// are compiled out in shipping via UE_ENABLE_DEBUG_DRAWING in the .cpp.
	virtual void CollectDebugCellQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const override;
	virtual void CollectDebugPathCells(
		const TArray<FFixedVector>& CellPathWorld,
		TArray<FVector>& OutRouteCells,
		TArray<FVector>& OutDestCell,
		float& OutHalfExtent) const override;
	virtual void CollectDebugBlockerCells(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

	/** Escape-target query (base contract: USeinNavigation::QueryEscapeTarget).
	 *  Walks UP the static `WallDistance` gradient from the agent's cell — up to
	 *  6 cells, greedy highest-WD unvisited neighbor per step, each hop gated by
	 *  the bake's connection bits (no slope/step violations) and static
	 *  passability — then returns the FURTHEST cell of that walk which also
	 *  passes the CONTRACT validation (walking BACK along the chain until one
	 *  does): candidate + agent footprint ring clear of dynamic blockers (per
	 *  Query.AgentNavLayerMask / AgentFootprintRadius), no blocked terrain
	 *  (Query.BlockedTerrainTags), AND the straight From→candidate segment clear
	 *  at half-cell samples — the consumer walks a straight leg the greedy
	 *  chain may have bent around. Multi-cell on purpose: a single-step target lands inside
	 *  typical AcceptanceRadius and "arrives" instantly, escaping nothing.
	 *  Returns the cell CENTER as a world position; false (outputs untouched)
	 *  when the agent's cell is a sealed pocket or no walked cell validates.
	 *
	 *  Consumed by SeinMoveToAction's hold-escape ladder, which steers the unit
	 *  to the target as a short internal leg and then resumes normal pathing. */
	virtual bool QueryEscapeTarget(
		const FSeinEscapeQuery& Query,
		FFixedVector& OutTarget) const override;

protected:

	virtual FSeinStaticEnvironmentAdoptionResult LoadFromSubstrateImpl(
		const USeinLevelData& Substrate) override;

	/**
	 * Reusable shipped-A* coverage for native subclasses that explicitly opt
	 * into the inherited grid/query contract. An override may return this
	 * directly when it adds no future-affecting state, or fold this digest into
	 * a subclass-specific digest with its additional state.
	 */
	bool ComputeAStarStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const;

	/** Reusable exact-state claim for a native subclass that explicitly adds
	 *  no future-affecting mutable state beyond inherited shipped A* state. */
	bool ComputeAStarStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const;

	/** Walk each waypoint along the WallDistance gradient until it sits in
	 *  a cell whose distance-to-wall is at least
	 *  `ceil(AgentFootprintRadius/CellSize) + WallPaddingCells`. Keeps the
	 *  raw cell-A* polyline clear enough of walls that a unit at full
	 *  footprint doesn't clip when following the line. No-op when both
	 *  the footprint and the explicit pad are zero. Called from FindPath
	 *  after smoothing. */
	void PushWaypointsAwayFromWalls(
		FSeinPath& Path,
		FFixedPoint AgentFootprintRadius,
		int32 WallPaddingCells,
		FAStarScratch& Scratch) const;

private:
	// ----------------------------------------------------------------------
	// Runtime grid (populated by LoadFromSubstrate)
	// ----------------------------------------------------------------------
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSize = FFixedPoint::FromInt(100);
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Cached digest of the large immutable grid payload. Rebuilt once on
	 *  successful substrate adoption so per-root StateContract revalidation
	 *  hashes only this digest plus the small live query-tuning surface. */
	FGuid StaticGridDigest;

	/** Per-cell A* routing weight + passability. 0 = blocked, 255 = impassable;
	 *  1..254 = passable, and the value IS the terrain cost multiplier the A*
	 *  step cost reads (higher = costlier to cross, so routing prefers cheaper
	 *  ground). Baked from terrain NavCost. (Dynamic/runtime cost regions are a
	 *  separate, unbuilt item.) */
	TArray<uint8> CellCost;

	/** Per-cell center-height (world-space Z) — snapped-to placement for units. */
	TArray<FFixedPoint> CellHeight;

	/** Per-cell terrain-type index (0 = Default), loaded from the shared substrate at
	 *  LoadFromSubstrate. Read only by the per-request BlockedTerrainTags gate in
	 *  IsCellPassableForPath so an agent can be barred from terrain types its tags list
	 *  (e.g. amphibious-only "Water"). The terrain COST itself is baked into CellCost at
	 *  bake time — not read from here at runtime. */
	TArray<uint8> CellTerrainType;

	/** Per-cell 8-direction connectivity bitmask (baked). Bit N is set iff a
	 *  unit can traverse from this cell to its neighbor at direction index N.
	 *  Queried directly by A* + path smoother — no live slope math at query
	 *  time, so the rules applied at bake are guaranteed to match the rules
	 *  enforced at runtime. */
	TArray<uint8> CellConnections;

	/** Per-cell Chebyshev distance to the nearest blocked cell, computed at
	 *  grid-load time via multi-source BFS. Values clamp to
	 *  [0, WallDistanceCap] — 0 = blocked cell itself, 1 = adjacent to wall,
	 *  WallDistanceCap = "no nearby walls within scan radius."
	 *
	 *  Read by the wall-padding post-process (`PushWaypointsAwayFromWalls`):
	 *  each smoothed waypoint walks its cell along the gradient of this
	 *  field — `max(WallDistance among 8-neighbors)` — until the cell sits
	 *  `FootprintCells + WallPaddingCells` from the nearest wall, or the
	 *  gradient saturates (no neighbor improves). Best-effort; in narrow
	 *  corridors waypoints converge to the local maximum (corridor
	 *  centerline) rather than failing the path.
	 *
	 *  Also read by AugmentInitialHeading's side-clearance scoring (which
	 *  side of the bicycle U-turn arc has more breathing room).
	 *
	 *  Runtime-only — derived from CellCost at load time, not serialized to
	 *  the baked data. Recomputed whenever the grid changes (substrate load /
	 *  rebake). */
	TArray<uint8> WallDistance;

	/** Per-cell static connectivity-component label, computed at grid-load time
	 *  via flood-fill over CellConnections (`RebuildConnectivityComponents`).
	 *  -1 = blocked / unlabeled; 0..K = component id. Two passable cells share a
	 *  label iff they're mutually reachable through the STATIC bake (set
	 *  connection bit + passable neighbor — the same relation A* traverses at
	 *  zero clearance, and the same one the bake-time island prune uses).
	 *
	 *  Backs the O(1) `IsReachable` override: project both endpoints to cells,
	 *  compare labels. Replaces the base's full-A* reachability fallback on the
	 *  command-validation hot path. Does not model the diagonal-squeeze
	 *  anti-tunnel or per-footprint clearance (it can over-report at thin
	 *  diagonal pinch corners / for oversized agents — both degrade to a
	 *  graceful partial path, never a hard failure) and ignores dynamic
	 *  blockers by design (a transient obstacle doesn't make ground
	 *  fundamentally unreachable).
	 *
	 *  Runtime-only — derived from CellCost/CellConnections at load time, not
	 *  serialized. Recomputed on every grid load alongside WallDistance. */
	TArray<int32> CellComponent;

	/** Runtime list of dynamic blockers, refreshed each PreTick by the
	 *  nav-blocker stamping system. FindPath rebuilds the per-call
	 *  DynamicBlocked overlay from this list (excluding the requester so
	 *  a unit can path out of its own footprint). Shared, immutable during a
	 *  search — NOT part of FAStarScratch. */
	TArray<FSeinDynamicBlocker> DynamicBlockers;

protected:
	virtual void SetDynamicBlockers(
		const TArray<FSeinDynamicBlocker>& InBlockers) override;

	// ----------------------------------------------------------------------
	// Grid helpers
	// ----------------------------------------------------------------------
	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
	FORCEINLINE bool IsCellPassable(int32 X, int32 Y) const
	{
		if (!IsValidCoord(X, Y)) return false;
		const uint8 C = CellCost[CellIndex(X, Y)];
		return C > 0 && C < 255;
	}

	/** Effective passability for a FindPath in progress: static cell + the
	 *  current request's dynamic-blocked overlay. DynamicBlocked is rebuilt
	 *  at the top of FindPath (with the requester excluded + agent mask
	 *  applied), so callers inside that scope (A*, HasLineOfSight) can use
	 *  this directly. Reads the per-search scratch (DynamicBlocked +
	 *  RequestBlockedType + the blocked-types flag) so it is reentrant across
	 *  concurrent searches that each carry their own FAStarScratch. */
	FORCEINLINE bool IsCellPassableForPath(int32 X, int32 Y, const FAStarScratch& Scratch) const
	{
		if (!IsCellPassable(X, Y)) return false;
		const int32 Idx = CellIndex(X, Y);
		// Per-agent terrain-tag gate (BlockedTerrainTags). Guarded so a no-blocked-terrain
		// agent skips it; when active, CellTerrainType is grid-sized and RequestBlockedType
		// is 256 entries, so both indexings are in range.
		if (Scratch.bRequestHasBlockedTypes && Scratch.RequestBlockedType[CellTerrainType[Idx]]) return false;
		if (Scratch.DynamicBlocked.Num() == 0) return true;
		return Scratch.DynamicBlocked[Idx] == 0;
	}

	/** Stamp DynamicBlockers (skipping `Exclude` + filtering to those whose
	 *  BlockedNavLayerMask intersects `AgentNavLayerMask`) into DynamicBlocked.
	 *  Called at the top of FindPath so the overlay matches BOTH the
	 *  requester (self-exclusion) AND the agent's layer (water blocker
	 *  doesn't stamp for amphibious agents). */
	void BuildDynamicBlockedOverlay(FSeinEntityHandle Exclude, uint8 AgentNavLayerMask, FAStarScratch& Scratch) const;

	/** Effective wall-distance at (X, Y): `min(static WallDistance, dynamic-
	 *  blocker Chebyshev distance)`, capped at `MaxR` for the dynamic side.
	 *
	 *  Returns the static WallDistance if (a) the dynamic overlay is empty,
	 *  (b) the cell is outside `LastOverlayDirtyRect` inflated by MaxR (no
	 *  dyn blocker can be within range), or (c) a ring scan found no
	 *  blocker within MaxR. Otherwise returns `min(StaticWD, dist_to_nearest_
	 *  blocked_cell)`.
	 *
	 *  Cached per-request via `DynamicWDCacheGen` — A* re-visits and
	 *  cardinal-WD lookups across diagonal anti-squeeze checks hit the cache
	 *  rather than re-scanning. Gen is bumped at the top of each FindCellPath
	 *  call so consecutive requests don't share stale dynamic distances.
	 *
	 *  Called from A* C-space gate, HasLineOfSight clearance gate, and
	 *  PushWaypointsAwayFromWalls. All three observe the same effective
	 *  clearance — so dynamic blockers act on path topology, smoothing,
	 *  AND waypoint push, exactly like static walls do. */
	int32 GetEffectiveWD(int32 X, int32 Y, int32 MaxR, FAStarScratch& Scratch) const;

private:
	/** Slow-path ring scan for `GetEffectiveWD` — exits early on first
	 *  encountered dyn-blocked cell. Caller guarantees (X, Y) is valid. */
	int32 ComputeDynamicWDRingScan(int32 X, int32 Y, int32 MaxR, FAStarScratch& Scratch) const;

	/** Shared O(8R)-per-ring outward scan around (StartX, StartY) used by the three
	 *  ProjectPointToNav* publics. Checks the start cell first, then each ring at
	 *  radius 1..MaxR (top + bottom rows including corners, then left + right
	 *  columns excluding corners — identical visitation order to the old inline
	 *  copies). `Accept(CX, CY)` is the per-cell acceptance predicate; on the first
	 *  accepted cell, writes its world center to OutProjected and returns true.
	 *  Returns false if no cell is accepted within MaxR. Defined in the .cpp; all
	 *  instantiations are local to that TU. */
	template <typename FAcceptPred>
	bool RingScanForCell(int32 StartX, int32 StartY, int32 MaxR, FAcceptPred&& Accept, FFixedVector& OutProjected) const;

	/** Required configuration-space clearance, in whole cells, for an agent of the
	 *  given footprint radius + wall-padding:
	 *    Required = ceil(FootprintRadius / CellSize + 0.5) + WallPaddingCells
	 *  (clamped to WallDistanceCap). Single source of truth for both the A* C-space
	 *  gate (FindCellPathInternal) and the wall-push pass (PushWaypointsAwayFromWalls).
	 *  Deterministic fixed-point ceil. Returns 0 when CellSize is non-positive. */
	int32 ComputeRequiredClearance(FFixedPoint FootprintRadius, int32 WallPaddingCells) const;

#if !UE_BUILD_SHIPPING
	/** Non-shipping diagnostic reporters extracted from their host search/path
	 *  functions so those read at their real logic size. Each gates on its log
	 *  channel's activity BEFORE doing any grid work; pure observation, never
	 *  mutates path results. Declarations compiled out in shipping. */
	void ReportAStarPartial(FIntPoint Start, FIntPoint End, int32 StartIdx, int32 EndIdx,
		int32 BestCellIdx, int32 Iterations, int32 IterCap, int32 RequiredClearance, FAStarScratch& Scratch) const;
	void ReportUnreachableSegments(const FSeinPathRequest& Request, const FSeinPath& OutPath, FAStarScratch& Scratch) const;
	void ReportCellPathClearance(const TArray<FIntPoint>& CellPath, const FSeinPath& OutPath,
		int32 RequiredClearance, bool bPartial) const;
#endif

protected:

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;
	FFixedVector GridToWorld(int32 X, int32 Y) const;

	/** Supercover grid line-of-sight — true if every cell from (X0,Y0) to (X1,Y1)
	 *  is passable AND, when `RequiredClearance > 0`, every cell on the line
	 *  EXCEPT the anchor (X0,Y0) has `WallDistance >= RequiredClearance`. The
	 *  anchor is exempt because the unit may legitimately start near a wall
	 *  (just spawned, or pushed there by another system) and we still need
	 *  smoothing to find a line OUT. Used for path smoothing; the clearance
	 *  gate keeps smoothed segments from collapsing across wall corners that
	 *  A* carefully routed around. */
	bool HasLineOfSight(int32 X0, int32 Y0, int32 X1, int32 Y1, FAStarScratch& Scratch, int32 RequiredClearance = 0) const;

	/** String-pull smoothed polyline from an A* cell chain. Forwards
	 *  `RequiredClearance` into `HasLineOfSight` so the smoother refuses to
	 *  collapse waypoints across low-clearance cells. Default 0 = no
	 *  clearance enforcement (backward-compatible). */
	void BuildSmoothedPath(const TArray<FIntPoint>& CellPath, FSeinPath& OutPath, FAStarScratch& Scratch, int32 RequiredClearance = 0) const;

	/** Core A* search — configuration-space (footprint-aware) variant.
	 *
	 *  Topology is constrained by `RequiredClearance`: a neighbor cell N is
	 *  reachable from current cell U iff
	 *    `WallDistance[N] >= min(RequiredClearance, WallDistance[U])`
	 *
	 *  Why this rule:
	 *   - **Normal case** (`WallDistance[U] >= RequiredClearance`): neighbor
	 *     must also satisfy `>= RequiredClearance`. The unit stays in
	 *     "configuration space" — cells where the full footprint clears walls.
	 *   - **Start-cell escape** (`WallDistance[U] < RequiredClearance`): a
	 *     unit that spawned next to a wall, or was pushed against one by
	 *     another system (a crowd shove at a corner), has a starting cell with
	 *     too-low clearance. The `min(Required, U)` form requires only
	 *     NON-DECREASING clearance (`>= current WD`) while escaping: the unit
	 *     may traverse level low-clearance cells and climb when it can, locks
	 *     into configuration space the instant it reaches a full-clearance
	 *     cell, and never steps to a tighter cell. (An earlier `U+1`
	 *     "strict +1 climb per step" form stranded units on a flat low-
	 *     clearance plateau — no orthogonal neighbor strictly higher, the one
	 *     higher diagonal squeeze-blocked by its low-clearance flanks. See the
	 *     corner-orphan diagnosis 2026-06-14.) Normal (in-C-space) starts are
	 *     UNAFFECTED: `min(Required, U) == Required` whenever `U >= Required`.
	 *
	 *  Diagonal squeezes: a diagonal step from U requires both cardinal
	 *  neighbors that flank it to also satisfy the clearance rule, mirroring
	 *  the existing connectivity-bit anti-squeeze.
	 *
	 *  Behavior:
	 *   - Goal reachable through configuration space within iteration cap
	 *     → returns cell chain to End, `bOutPartial = false`.
	 *   - Goal NOT in configuration space (click landed in a tight spot the
	 *     unit can't physically fit), OR goal unreachable, OR iteration cap
	 *     hit → returns chain to best-H cell visited, `bOutPartial = true`.
	 *   - Start invalid / blocked → returns empty (caller's responsibility to
	 *     pre-project Start onto a passable cell).
	 *
	 *  RequiredClearance is computed once per FindPath call from
	 *  `Request.AgentFootprintRadius` + `Request.AgentWallPaddingCells`.
	 *  Different unit sizes get different paths from the same baked
	 *  `WallDistance` field — no per-footprint rebake needed.
	 *
	 *  HeuristicWeightPercent: f(n) = g(n) + (h(n) * Weight) / 100. Values
	 *    >100 produce weighted A* (suboptimal but faster). 100 = admissible.
	 *  MaxIterations: hard cap on node expansions.
	 *
	 *  The reconstructed cell chain is written into `Scratch.CellPath` (filled via
	 *  Reset, not realloc — pooled across calls); empty there means no path /
	 *  invalid start. The caller reads `Scratch.CellPath` after the call.
	 */
	void AStarSearch(FIntPoint Start, FIntPoint End, bool& bOutPartial,
		int32 HeuristicWeightPercent, int32 MaxIterations, int32 RequiredClearance, FAStarScratch& Scratch) const;

	/** Constant used to mark cells as "no nearby wall" in WallDistance. Sets
	 *  the maximum BFS expansion radius: any cell further than this many
	 *  cells from the nearest wall reads as the cap. Drives the practical
	 *  upper bound on the wall-padding push pass — anything above
	 *  `CapCells × CellSize` of total required clearance becomes
	 *  indistinguishable from `CapCells × CellSize`, because the push-pass
	 *  gradient-walk saturates once it reaches a cell that reads the cap.
	 *
	 *  Bumped from 16 to 64 because designers tuning `WallPadding` for
	 *  large vehicles want headroom above 1600cm; 64 cells = 6400cm gives
	 *  64m of expressible clearance preference. uint8 can carry up to 255
	 *  so the type is safe; only cost is the per-bake BFS frontier expands
	 *  64 cells instead of 16, which is a one-shot bake step, not hot. */
	static constexpr uint8 WallDistanceCap = 64;

	/** Disc rejection-sampling budget for GetRandomReachablePoint. Each attempt
	 *  draws one uniform point in the radius and tests passability + same
	 *  component. 32 is ample when a meaningful fraction of the disc is reachable
	 *  (≈96% hit at 10% reachable area); a region too sparse to hit in 32 draws
	 *  returns false (best-effort contract — widen the radius if the caller needs
	 *  a guaranteed result on sparse maps). */
	static constexpr int32 RandomReachableMaxAttempts = 32;

	/** Recompute WallDistance via multi-source BFS from all blocked cells.
	 *  Run once at LoadFromSubstrate; not a hot path. */
	void RebuildWallDistanceField();

	/** Recompute the CellComponent labels via flood-fill over CellConnections
	 *  (set bit + passable neighbor). Run once at LoadFromSubstrate alongside
	 *  RebuildWallDistanceField; not a hot path. Backs the O(1) IsReachable. */
	void RebuildConnectivityComponents();
};
