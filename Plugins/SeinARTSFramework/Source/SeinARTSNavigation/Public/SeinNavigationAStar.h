/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStar.h
 * @brief   Reference implementation of USeinNavigation — single-layer 2D grid
 *          baked from downward line-traces, with synchronous A* pathfinding and
 *          line-of-sight path smoothing. Minimal on purpose; serves as the
 *          default nav plus an example for custom subclasses.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNavigation.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationAStar.generated.h"

class UWorld;
class USeinNavigationAStarAsset;

UCLASS(BlueprintType, meta = (DisplayName = "Sein Nav (A*)"))
class SEINARTSNAVIGATION_API USeinNavigationAStar : public USeinNavigation
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Designer config (edit on the nav CDO via class defaults — class is
	// instantiated from plugin settings, so these values apply per-project.)
	// ----------------------------------------------------------------------

	/** Maximum walkable slope angle in degrees. Surfaces steeper than this are
	 *  treated as blocked at bake time. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxWalkableSlopeDegrees = 45.0f;

	/** Vertical extent (world units) above the tallest NavVolume that the bake
	 *  traces start from. Bump this if your walkable surfaces sit near the top
	 *  of the volume and you want a margin. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float BakeTraceHeadroom = 200.0f;

	/** Emit cell quads (green = walkable, red = blocked) for the nav debug
	 *  scene proxy. Gated by `ShowFlags.Navigation` / `Sein.Nav.Show`. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawCellsInDebug = true;

	// ----------------------------------------------------------------------
	// USeinNavigation overrides
	// ----------------------------------------------------------------------

	virtual TSubclassOf<USeinNavigationAsset> GetAssetClass() const override;
	virtual bool BeginBake(UWorld* World) override;
	virtual bool IsBaking() const override { return bBaking; }
	virtual void RequestCancelBake() override { bCancelRequested = true; }

	virtual void LoadFromAsset(USeinNavigationAsset* Asset) override;
	virtual bool HasRuntimeData() const override { return CellCost.Num() > 0; }

	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;
	virtual bool FindCellPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;
	virtual bool IsPassable(const FFixedVector& WorldPos) const override;
	virtual bool IsWorldPositionClear(const FFixedVector& WorldPos, uint8 AgentNavLayerMask) const override;
	virtual FFixedPoint GetCellSize() const override { return CellSize; }
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const override;
	virtual bool ProjectPointToNavOnElevation(const FFixedVector& WorldPos, FFixedVector& OutProjected) const override;
	virtual bool GetCellHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ, bool bWalkableOnly = true) const override;
	virtual void SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& InBlockers) override;

	// Debug collectors — declarations stay in all build configs (ABI); bodies
	// are compiled out in shipping via UE_ENABLE_DEBUG_DRAWING in the .cpp.
	virtual void CollectDebugCellQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const override;
	virtual void CollectDebugPathCells(
		const FFixedVector& AgentPos,
		const TArray<FFixedVector>& Waypoints,
		int32 CurrentWaypointIndex,
		TArray<FVector>& OutRemainingCells,
		TArray<FVector>& OutCurrentTargetCell,
		float& OutHalfExtent) const override;
	virtual void CollectDebugBlockerCells(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

	/** Find a "nudge target" world position for an agent stuck at `AgentPos`.
	 *  Scans the 8 cells around the agent's current cell, picks the one with
	 *  the highest static `WallDistance` value that is also passable AND
	 *  reachable from the agent's cell via the bake's connection bits.
	 *  Returns the cell CENTER as a world position in `OutTarget`, with
	 *  `OutTargetWD` set to the chosen neighbor's WD; returns false (and
	 *  leaves outputs untouched) if no passable neighbor exists in any
	 *  direction (chassis is in a sealed pocket).
	 *
	 *  Static WD only — dynamic-blocker influence is intentionally ignored
	 *  for this query so a single transient obstacle doesn't trap an agent
	 *  in escape mode forever. Connection bits ARE checked so the chosen
	 *  neighbor is physically reachable (no slope/step gate violations).
	 *
	 *  Consumed by SeinMoveToAction's escape-nudge fallback when A* can't
	 *  find a path from the agent's current cell. The action overrides
	 *  `Path.Waypoints = [OutTarget]` and lets the normal carrot/steering
	 *  pipeline drive the chassis toward it. Once the chassis reaches a
	 *  cell in C-space (WD ≥ Required), normal pathing resumes. */
	bool FindEscapeNudgeTarget(
		const FFixedVector& AgentPos,
		FFixedVector& OutTarget,
		int32& OutTargetWD) const;

protected:

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
		int32 WallPaddingCells) const;

	// ----------------------------------------------------------------------
	// Runtime grid (populated by LoadFromAsset / bake)
	// ----------------------------------------------------------------------
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSize = FFixedPoint::FromInt(100);
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Per-cell cost. 0 = blocked, 1..254 = passable with cost multiplier, 255 = impassable. */
	TArray<uint8> CellCost;

	/** Per-cell center-height (world-space Z) — snapped-to placement for units. */
	TArray<FFixedPoint> CellHeight;

	/** Per-cell 8-direction connectivity bitmask (baked). Bit N is set iff a
	 *  unit can traverse from this cell to its neighbor at direction index N.
	 *  Queried directly by A* + path smoother — no live slope math at query
	 *  time, so the rules applied at bake are guaranteed to match the rules
	 *  enforced at runtime. */
	TArray<uint8> CellConnections;

	/** Per-cell Chebyshev distance to the nearest blocked cell, computed at
	 *  LoadFromAsset time via multi-source BFS. Values clamp to
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
	 *  the asset. Recomputed whenever the grid changes (load / bake / asset
	 *  swap). */
	TArray<uint8> WallDistance;

	/** Runtime list of dynamic blockers, refreshed each PreTick by the
	 *  nav-blocker stamping system. FindPath rebuilds the per-call
	 *  DynamicBlocked overlay from this list (excluding the requester so
	 *  a unit can path out of its own footprint). */
	TArray<FSeinDynamicBlocker> DynamicBlockers;

	/** Per-cell flag (1 = dynamically blocked for this FindPath, 0 = clear).
	 *  Mutable so it can be rebuilt inside the const FindPath. Single-threaded
	 *  sim guarantees no concurrent FindPath; the buffer is reused across
	 *  calls to avoid per-call allocations. */
	mutable TArray<uint8> DynamicBlocked;

	/** AABB of cells written into `DynamicBlocked` by the previous
	 *  `BuildDynamicBlockedOverlay` call (inclusive bounds, in grid
	 *  coordinates). Next overlay rebuild clears only this rect instead of
	 *  the full grid — huge savings on large maps with localized blocker
	 *  clusters (e.g. a 1km² map with a few vehicles in one corner used to
	 *  pay 1MB of Memzero per FindPath × 4 paths/tick).
	 *
	 *  `Min.X > Max.X` is the "empty / invalid" sentinel: previous overlay
	 *  wrote nothing, no clear needed. Mutable for the same reason as
	 *  DynamicBlocked. */
	mutable FIntRect LastOverlayDirtyRect = FIntRect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);

	/** Fingerprint of the last DynamicBlockers list pushed via SetDynamicBlockers.
	 *  XOR-fold of per-blocker pose + mask + shape hash. SetDynamicBlockers
	 *  only broadcasts OnNavigationMutated when the new hash differs — gates
	 *  the debug scene-proxy rebuild to actual mutations instead of every
	 *  per-tick push. */
	uint32 LastBlockerHash = 0;

	// ----------------------------------------------------------------------
	// A* search state — lazy-validated via CellGen / CurrentSearchGen
	// ----------------------------------------------------------------------
	//
	// The three core search arrays (GCosts, Parents, Closed) used to be
	// allocated + initialized inside every AStarSearch call — on a 1km² grid
	// that's ~9 MB of memzero per call, plus the alloc itself. With the
	// per-tick path budget driving 4+ FindPath calls per tick at 30Hz, the
	// init cost was dominating the actual A* work on large grids.
	//
	// Lazy validation pattern: the arrays are sized once when the grid
	// changes, never re-initialized at the start of a search. A parallel
	// `CellGen` array stores the search-generation that last touched each
	// cell. When `CellGen[i] != CurrentSearchGen` the cell's GCosts /
	// Parents / Closed entries are treated as "fresh" (INT32_MAX / -1 /
	// false) — uninitialized values never read without the gen check.
	// `++CurrentSearchGen` per search; on uint16 wraparound (every ~65k
	// searches) we do one full CellGen reset to keep the invariant.
	//
	// Per-call cost now scales with **expanded nodes** (hundreds-to-low-
	// thousands) instead of grid size. All mutable so they can be modified
	// inside the const FindPath / AStarSearch. Single-threaded sim
	// guarantees no concurrent search, so no synchronization needed.

	mutable TArray<int32> SearchGCosts;
	mutable TArray<int32> SearchParents;
	mutable TArray<uint8> SearchClosed;
	mutable TArray<uint16> SearchCellGen;
	mutable uint16 CurrentSearchGen = 0;

	// ----------------------------------------------------------------------
	// Dynamic-WD lazy cache — per-request, gen-tagged just like the A*
	// search state. Caches the Chebyshev distance from each visited cell
	// to its nearest dyn-blocked cell, capped at a per-request RequiredClearance.
	//
	// Why lazy: the dyn-blocker influence region inflated by RequiredClearance
	// can cover thousands of cells per request, but A* / Push / Smoother
	// only actually read clearance at a fraction of them. Precomputing the
	// full inflated field would touch every cell in the halo whether or not
	// the path goes near it; lazy compute only pays for cells we read.
	//
	// Fast paths:
	//  - Cell outside `LastOverlayDirtyRect` inflated by RequiredClearance →
	//    no blocker can be within range, return WallDistance immediately.
	//    Single rect check, ~95% of A*-visited cells hit this in sparse-
	//    blocker maps (units clustered in a few squads).
	//  - Cache hit on prior visit in this request → O(1) lookup, no scan.
	//
	// Slow path: Chebyshev ring scan outward from the cell, exits early
	// at the first dyn-blocked cell encountered. Worst-case O(R²) cells
	// scanned; typical case much less since most query cells are near a
	// blocker (otherwise the rect check skipped them).
	mutable TArray<uint8> DynamicWDCache;
	mutable TArray<uint16> DynamicWDCacheGen;
	mutable uint16 CurrentDynamicWDGen = 0;

	/** Open-list node, kept as a class-private nested type so `Open` can live
	 *  on the impl as a `mutable` member. Was previously in an anonymous
	 *  namespace inside the .cpp; moved here strictly to preserve the heap's
	 *  allocated memory across FindPath calls (long paths grow Open to 1000s
	 *  of entries and the local-array version reallocated from scratch on
	 *  the next search). */
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

	/** Heap-ordered open list, member-scoped so allocation persists across
	 *  searches. `Open.Reset()` at the top of each AStarSearch keeps the
	 *  capacity that previous calls grew to — saves the realloc-from-128
	 *  pattern when consecutive searches both expand to thousands of nodes. */
	mutable TArray<FAStarNode> Open;

	// ----------------------------------------------------------------------
	// Bake state
	// ----------------------------------------------------------------------
	bool bBaking = false;
	bool bCancelRequested = false;

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
	 *  this directly. */
	FORCEINLINE bool IsCellPassableForPath(int32 X, int32 Y) const
	{
		if (!IsCellPassable(X, Y)) return false;
		if (DynamicBlocked.Num() == 0) return true;
		return DynamicBlocked[CellIndex(X, Y)] == 0;
	}

	/** Stamp DynamicBlockers (skipping `Exclude` + filtering to those whose
	 *  BlockedNavLayerMask intersects `AgentNavLayerMask`) into DynamicBlocked.
	 *  Called at the top of FindPath so the overlay matches BOTH the
	 *  requester (self-exclusion) AND the agent's layer (water blocker
	 *  doesn't stamp for amphibious agents). */
	void BuildDynamicBlockedOverlay(FSeinEntityHandle Exclude, uint8 AgentNavLayerMask) const;

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
	int32 GetEffectiveWD(int32 X, int32 Y, int32 MaxR) const;

private:
	/** Slow-path ring scan for `GetEffectiveWD` — exits early on first
	 *  encountered dyn-blocked cell. Caller guarantees (X, Y) is valid. */
	int32 ComputeDynamicWDRingScan(int32 X, int32 Y, int32 MaxR) const;

protected:

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;
	FFixedVector GridToWorld(int32 X, int32 Y) const;

	/** Bresenham grid line-of-sight — true if every cell from (X0,Y0) to (X1,Y1)
	 *  is passable AND, when `RequiredClearance > 0`, every cell on the line
	 *  EXCEPT the anchor (X0,Y0) has `WallDistance >= RequiredClearance`. The
	 *  anchor is exempt because the unit may legitimately start near a wall
	 *  (just spawned, or pushed there by another system) and we still need
	 *  smoothing to find a line OUT. Used for path smoothing; the clearance
	 *  gate keeps smoothed segments from collapsing across wall corners that
	 *  A* carefully routed around. */
	bool HasLineOfSight(int32 X0, int32 Y0, int32 X1, int32 Y1, int32 RequiredClearance = 0) const;

	/** String-pull smoothed polyline from an A* cell chain. Forwards
	 *  `RequiredClearance` into `HasLineOfSight` so the smoother refuses to
	 *  collapse waypoints across low-clearance cells. Default 0 = no
	 *  clearance enforcement (backward-compatible). */
	void BuildSmoothedPath(const TArray<FIntPoint>& CellPath, FSeinPath& OutPath, int32 RequiredClearance = 0) const;

	/** Core A* search — configuration-space (footprint-aware) variant.
	 *
	 *  Topology is constrained by `RequiredClearance`: a neighbor cell N is
	 *  reachable from current cell U iff
	 *    `WallDistance[N] >= min(RequiredClearance, WallDistance[U] + 1)`
	 *
	 *  Why this rule:
	 *   - **Normal case** (`WallDistance[U] >= RequiredClearance`): neighbor
	 *     must also satisfy `>= RequiredClearance`. The unit stays in
	 *     "configuration space" — cells where the full footprint clears walls.
	 *   - **Start-cell escape** (`WallDistance[U] < RequiredClearance`): a
	 *     unit that spawned next to a wall, or was pushed against one by
	 *     another system, has a starting cell with too-low clearance. The
	 *     `min(Required, U+1)` form requires strict improvement (+1 clearance
	 *     per step) while escaping, until we reach a cell with full clearance.
	 *     Without this, the search would fail from any low-clearance start.
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
	 */
	TArray<FIntPoint> AStarSearch(FIntPoint Start, FIntPoint End, bool& bOutPartial,
		int32 HeuristicWeightPercent, int32 MaxIterations, int32 RequiredClearance) const;

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

	/** Recompute WallDistance via multi-source BFS from all blocked cells.
	 *  Run once at LoadFromAsset; not a hot path. */
	void RebuildWallDistanceField();

	// ----------------------------------------------------------------------
	// Bake pipeline (synchronous, editor-blocking slow-task progress)
	// ----------------------------------------------------------------------
	bool DoSyncBake(UWorld* World, USeinNavigationAStarAsset*& OutAsset);
	void ApplyAssetData(const USeinNavigationAStarAsset* Asset);

#if WITH_EDITOR
	USeinNavigationAStarAsset* CreateOrLoadAsset(UWorld* World, const FString& AssetName) const;
	bool SaveAssetToDisk(USeinNavigationAStarAsset* Asset) const;
#endif
};
