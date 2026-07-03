/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.h
 * @brief   Abstract base class for pluggable navigation implementations.
 *
 *          USeinNavigation owns the runtime nav problem for one world:
 *          pathfinding, reachability, projection/placement queries, and
 *          (optionally) debug visualization. It is the ONLY thing the
 *          framework's MoveTo action and ability validation delegate talk to.
 *
 *          Baked data comes from the unified level-data pipeline
 *          (SeinARTSLevelData): a nav that participates registers as an
 *          ISeinLevelLayerProvider on the shared substrate (USeinLevelData)
 *          and loads its runtime grid from the baked channels via
 *          LoadFromSubstrate. The "Bake Level Data" button lives on
 *          ASeinLevelVolume; this class has no bake surface of its own.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::NavigationClass`).
 *          The framework ships `USeinNavigationAStar` as a minimal 2D-grid +
 *          A* reference implementation; game teams can subclass or replace it
 *          entirely with navmesh-, waypoint-, or hierarchical-graph-based impls
 *          without touching any other framework code.
 *
 *          LAYERING (read this before adding "path planning"):
 *            - Navigation = TOPOLOGY ("where can a unit go?"). Produces an FSeinPath.
 *              This class + subclasses; ONE concrete impl ships (USeinNavigationAStar) —
 *              there is no separate "planner nav" class.
 *            - FSeinPath  = the hand-off contract (a waypoint backbone + typed segments).
 *            - Movement   = KINEMATICS ("how does THIS unit drive it?"). Curve-fitting /
 *              Reeds-Shepp / reversing live in `USeinMovement::PlanPath` (per-unit), NOT
 *              in a nav class. A nav subclass is the right home ONLY for orientation-aware
 *              (facing-per-node) pathing — see SeinMovement.h ResolveCollisionRadius.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Types/Entity.h"
#include "Types/Random.h"
#include "Stamping/SeinStampShape.h"
#include "Components/SeinExtentsComponent.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.h"
#include "SeinNavigation.generated.h"

class UWorld;
class USeinLevelData;
class ISeinLevelLayerProvider;

/**
 * One runtime path blocker = one FSeinStampShape posed at an entity. Multiple
 * stamps on the same entity produce multiple FSeinDynamicBlocker entries,
 * each carrying the same Owner so pathing self-exclusion works uniformly.
 *
 * Carrying the entity's transform (not just the stamp's world position) lets
 * the consumer apply LocalOffset/YawOffset deterministically inside their own
 * cell-iteration pass — the stamping system doesn't pre-compute world poses,
 * so blocker re-stamping at FindPath time stays consistent.
 *
 * Owner identifies the blocking entity so per-FindPath self-exclusion can
 * skip a unit's own blocker stamps — without it, a tank pathing from inside
 * its own footprint would never find a path out.
 */
struct FSeinDynamicBlocker
{
	FSeinStampShape Shape;
	FFixedVector EntityCenter;
	FFixedQuaternion EntityRotation;
	FSeinEntityHandle Owner;

	/** Layer mask of agents this blocker affects. Pathfinding gates via
	 *  intersection with the requesting agent's NavLayerMask. Default 0xFF
	 *  (blocks everyone) if the owning entity didn't author a mask. */
	uint8 BlockedNavLayerMask = 0xFF;
};

/** Fired when the nav's baked data mutates (bake finished, substrate re-adopted,
 *  dynamic obstacle change). Cached plans must re-query on this signal. */
DECLARE_MULTICAST_DELEGATE(FSeinOnNavigationMutated);

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Navigation"))
class SEINARTSNAVIGATION_API USeinNavigation : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinNavigationSubsystem
	// ----------------------------------------------------------------------

	/** Called once when the subsystem instantiates this nav. Default: no-op. */
	virtual void OnNavigationInitialized(UWorld* World) {}

	/** Called once when the subsystem is tearing down this nav. Default: no-op. */
	virtual void OnNavigationDeinitialized() {}

	// ----------------------------------------------------------------------
	// Runtime data
	// ----------------------------------------------------------------------

	/** True if the nav has usable runtime data (loaded from the unified
	 *  substrate or procedurally initialized). Queries return no-path when
	 *  false. Default: false — subclasses report their own grid state. */
	virtual bool HasRuntimeData() const { return false; }

	// ----------------------------------------------------------------------
	// Unified level-data pipeline (CP1.1) — OPT-IN. A nav that registers as a
	// layer provider on the shared substrate (USeinLevelData) returns its provider
	// face here and loads its runtime grid from the baked channels. These hooks
	// default to "no" so the base stays agnostic of the substrate — a nav that
	// opts out simply has no baked data until something else initializes it.
	// ----------------------------------------------------------------------

	/** This nav's layer-provider face, or null if it contributes no channel to the
	 *  unified level-data bake. The nav subsystem registers a non-null result so the
	 *  shared bake runs this nav's BakeLayer. Default: null (doesn't participate). */
	virtual ISeinLevelLayerProvider* GetLevelDataProvider() { return nullptr; }

	/** Load the runtime grid from the unified substrate's baked channels + shared
	 *  height. Return false if this nav doesn't read the substrate — it then has
	 *  no baked data (FindPath returns no-path) until something else loads it.
	 *  Default: false. */
	virtual bool LoadFromSubstrate(const USeinLevelData& /*Substrate*/) { return false; }

	// ----------------------------------------------------------------------
	// Queries — core pathing surface
	// ----------------------------------------------------------------------

	/** Synchronous path query. Subclasses override. Default: returns an invalid
	 *  path. Single entry point for path consumers (BPFL queries,
	 *  reachability checks, the budgeted
	 *  `USeinNavigationSubsystem::RequestPath` path). What the subclass
	 *  produces is its own concern — barebones nav returns a polyline; a
	 *  subclass may layer in project-specific post-processing (extra wall
	 *  padding, maneuver shaping) before returning. */
	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const { OutPath.Clear(); return false; }

	/**
	 * Run a batch of path requests. The async-pathfinding drain calls this so a nav
	 * impl can parallelize the batch internally (per-thread scratch). Default: SERIAL
	 * — loops FindPath, so any nav works unchanged (a non-reentrant impl simply
	 * doesn't parallelize). USeinNavigationAStar overrides it to run the searches
	 * across worker threads. OutResults is sized to Requests and filled index-aligned;
	 * each result must be a pure function of its request + the immutable grid.
	 */
	virtual void RunPathBatch(const TArray<FSeinPathRequest>& Requests, TArray<FSeinPath>& OutResults) const
	{
		OutResults.SetNum(Requests.Num());
		for (int32 i = 0; i < Requests.Num(); ++i)
		{
			FindPath(Requests[i], OutResults[i]);
		}
	}

	/** Cell-level path query — pure 2D pathfinding on the clearance grid.
	 *  Output is a cell-aware polyline: smoothed (LoS-collapsed) and
	 *  segment-derived as straight segments. Used directly by movement
	 *  modes that don't need curve-aware paths (`USeinBasicMovement`,
	 *  `USeinBasicUnitMovement`, `USeinInfantryMovement`), and as a
	 *  building block by nav subclasses that call this internally from
	 *  their `FindPath` override before layering on project-specific
	 *  post-processing.
	 *
	 *  Default: clears OutPath and returns false. The default nav impl
	 *  (`USeinNavigationAStar`) overrides this with the real 2D
	 *  A* search. */
	virtual bool FindCellPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const
	{
		OutPath.Clear();
		return false;
	}

	/** Direction-query seam — the "pull" complement to FindPath's "push" route. Returns a
	 *  planar UNIT direction the agent at `Query.From` should head to progress toward
	 *  `Query.Goal`, or ZERO if it should stop (arrived / no direction / no data). This is
	 *  the seam a FIELD-shaped nav (flow field / continuum crowds) answers NATIVELY — it
	 *  samples its precomputed per-cell field at `From` instead of producing a per-agent
	 *  route — and the seam a field-follower movement mode consumes each tick. A
	 *  ROUTE-shaped nav answers it by routing and returning the first step's heading
	 *  (USeinNavigationAStar does this). `Query.GroupId` lets a field nav share ONE field
	 *  across an ordered group (key by GroupId + Goal); per-agent navs ignore it.
	 *
	 *  Base default: straight-line toward `Goal` (obstacle-BLIND) — a safe fallback so any
	 *  nav answers SOMETHING; override for obstacle-aware or field-based direction. */
	virtual FFixedVector QueryDirection(const FSeinDirectionQuery& Query) const;

	/** Fast reachability check. Default: falls back to FindPath. Subclasses
	 *  should override with a cheaper reachability component / flood-fill if
	 *  the FindPath cost is prohibitive on the query hot path (ability
	 *  validation runs this per activation). */
	virtual bool IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const;

	/** Find a random walkable world point within `Radius` of `Origin` that is
	 *  REACHABLE from `Origin` (same static nav region — uses the same component
	 *  notion as IsReachable). Deterministic: draws from the supplied FFixedRandom
	 *  stream and ADVANCES it, so the caller owns the seed (lockstep-safe as long
	 *  as the seed is deterministic — e.g. derived from entity handle + tick).
	 *  Best-effort: returns false if no reachable point turns up within the impl's
	 *  attempt budget (very sparse region / tiny radius) or the nav has no runtime
	 *  data. `OutPoint` is left untouched on false. Default: false (a nav that
	 *  doesn't support it degrades to "no point"). USeinNavigationAStar overrides
	 *  with disc rejection-sampling against the connectivity-component field. */
	virtual bool GetRandomReachablePoint(const FFixedVector& Origin, FFixedPoint Radius,
		FFixedRandom& Rng, FFixedVector& OutPoint) const { return false; }

	/** Trace a straight line on the STATIC nav grid from `From` to `To`. Returns true if
	 *  the line is BLOCKED before reaching `To`, with `OutHitPoint` set to the first
	 *  blocked point; returns false (clear) with `OutHitPoint = To` if the whole line is
	 *  traversable. A cheap grid walk — NOT a pathfind (no detour). Default: clear
	 *  (no data). USeinNavigationAStar overrides with a Bresenham + connectivity walk. */
	virtual bool NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const
	{
		OutHitPoint = To;
		return false;
	}

	/** The baked terrain-type index at a world position (0 = Default / off-grid / no
	 *  data). The neutral shared per-cell classification the level bake stamped —
	 *  movement reads it to scale traversal speed (via
	 *  `USeinARTSCoreSettings::GetTerrainSpeedMultiplier`); A* read the same type for
	 *  routing cost at BAKE time. Default: 0 (a nav with no terrain data is all-Default).
	 *  USeinNavigationAStar overrides with the runtime grid lookup. */
	virtual int32 GetTerrainTypeAt(const FFixedVector& WorldPos) const { return 0; }

	/** True if a world-space point is inside the nav's walkable region.
	 *  Default: false. Subclasses override. */
	virtual bool IsPassable(const FFixedVector& WorldPos) const { return false; }

	/** True if a unit could occupy this world position RIGHT NOW, considering BOTH
	 *  the static bake AND the current dynamic blockers (bBlocksNav stamps) whose
	 *  BlockedNavLayerMask intersects AgentNavLayerMask. Distinct from IsPassable
	 *  (static bake only) and from the per-FindPath dynamic overlay (transient).
	 *  Cover-slot selection uses this to reject slots sitting on a cell ANY wall —
	 *  baked OR runtime-dynamic — occupies, so units are never dispatched onto an
	 *  unreachable spot. Default: static-only (delegates to IsPassable);
	 *  USeinNavigationAStar overrides with dynamic-blocker awareness. */
	virtual bool IsWorldPositionClear(const FFixedVector& WorldPos, uint8 AgentNavLayerMask) const { return IsPassable(WorldPos); }

	/** World units per nav grid cell. Used by callers that need to align
	 *  to actual grid edges (e.g. extracting axis-aligned passable/impassable
	 *  boundary segments for wall-constraint generation).
	 *
	 *  Default: 100 (a sane fallback when the nav doesn't expose a real
	 *  grid). Subclasses with a known cell pitch should override. */
	virtual FFixedPoint GetCellSize() const { return FFixedPoint::FromInt(100); }

	/**
	 * Test whether a building footprint can be placed at the given pose without
	 * overlapping blocked cells. Used by the targeter's footprint placement
	 * resolver gate; ability validation calls this through the cross-module
	 * delegate registered on USeinWorldSubsystem (see FSeinFootprintPlacementResolver).
	 *
	 *   CenterWorld:    anchor world position the footprint occupies
	 *   YawDegrees:     rotation around vertical axis (drives Box footprint
	 *                   axes; ignored for Capsule)
	 *   Shape:          the FSeinExtentsShape that will occupy cells (Box or Capsule)
	 *   AgentLayerMask: nav layer mask used for blocking checks
	 *
	 * Default impl: walks every cell-center inside the shape's XY extent and
	 * tests IsPassable on each. Subclasses can override with cheaper grid-AABB
	 * or rasterization-based tests if the per-cell IsPassable cost is too high
	 * (typical buildings are 4×4..8×8 cells = 16..64 calls — not a hot path,
	 * but easy to specialize for nav impls that already rasterize).
	 *
	 * Returns true when every covered cell is walkable for the layer mask.
	 * False when any cell is blocked OR the nav has no runtime data (no bake →
	 * can't validate → conservative reject).
	 */
	virtual bool IsPlacementValid(const FFixedVector& CenterWorld, FFixedPoint YawDegrees,
		const FSeinExtentsShape& Shape, uint8 AgentLayerMask) const;

	/** Refresh the runtime dynamic-blocker set. Called by the nav stamping
	 *  system each PreTick from entities carrying FSeinExtentsComponent with
	 *  bBlocksNav set. Subclasses store the list and consume it during
	 *  FindPath (typically rebuilding a dynamic-blocked overlay per call
	 *  so the requester's own blocker can be excluded + the agent's layer
	 *  mask can filter out terrain that doesn't apply to this agent class).
	 *  Default: no-op — subclasses without dynamic blocker support are
	 *  unaffected. */
	virtual void SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& /*Blockers*/) {}

	/** Find a short "escape nudge" target for an agent that A* can't path out of
	 *  (stuck against walls / sitting in a sub-clearance cell). Returns a nearby
	 *  cell with more wall-clearance so the steering pipeline can drive the chassis
	 *  back into open space, after which normal pathing resumes. Consumed by
	 *  SeinMoveToAction's escape fallback.
	 *
	 *  Default: no nudge available — returns false and sets OutTargetWD = -1. A nav
	 *  without this support simply degrades to the action's sealed-pocket outcome
	 *  (the same terminal result as before this was a virtual). USeinNavigationAStar
	 *  overrides it with a WallDistance-gradient walk. */
	virtual bool FindEscapeNudgeTarget(const FFixedVector& /*AgentPos*/,
		FFixedVector& /*OutTarget*/, int32& OutTargetWD) const { OutTargetWD = -1; return false; }

	/** Snap a point to the nearest walkable cell — the PLAIN projection (for the elevation- and
	 *  occupancy-aware variants, see ProjectPointToNavOnElevation and ProjectPointToNavFree).
	 *  Returns false if no walkable point is within the nav's reachable region.
	 *  Default: passes the point through unchanged + returns HasRuntimeData(). */
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
	{
		OutProjected = WorldPos;
		return HasRuntimeData();
	}

	/** Snap to the nearest walkable cell AT the input's elevation — the elevation-aware step up
	 *  from ProjectPointToNav, preferring cells whose stored height is close to `WorldPos.Z`. When
	 *  the input lands at a platform edge, regular `ProjectPointToNav` would snap to the
	 *  nearest XY passable cell — which on most baked grids is the lower
	 *  floor cell because cells are indexed by XY only. This Z-biased
	 *  version expands the search to find a cell at the input's elevation,
	 *  falling back to plain XY projection only when no elevation-matching
	 *  cell exists in scan radius.
	 *
	 *  Used by formation slot resolution so a click on a raised platform
	 *  produces destinations ON the platform — without it, slots that
	 *  fan over the edge of the platform place members on the floor below,
	 *  producing stragglers running off cliffsides.
	 *
	 *  Default: falls back to `ProjectPointToNav` (no Z bias). Subclasses
	 *  whose grid stores per-cell heights override to do the actual
	 *  Z-tolerance scan. */
	virtual bool ProjectPointToNavOnElevation(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
	{
		return ProjectPointToNav(WorldPos, OutProjected);
	}

	/** Snap to the nearest walkable cell at the input's elevation that is ALSO FREE of the given
	 *  footprints — the occupancy-aware step up from ProjectPointToNavOnElevation. Rejects any
	 *  candidate cell whose centre is within `SelfRadius + AvoidRadii[j]` of `AvoidCentres[j]`,
	 *  i.e. the nearest walkable free cell. The formation layer feeds the already-placed slots as the
	 *  avoid set so an off-nav slot snaps onto the inside edge of the play area without piling onto a
	 *  peer (occupancy-aware nearest-free-cell). `AvoidCentres` / `AvoidRadii` are index-aligned; a
	 *  missing radius counts as zero. Falls back to a free-but-any-elevation cell, then to a plain
	 *  occupancy-blind projection, so a slot is never dropped.
	 *
	 *  Default: ignores avoidance and defers to ProjectPointToNavOnElevation. Subclasses whose grid
	 *  stores per-cell heights override to do the real occupancy-aware scan. */
	virtual bool ProjectPointToNavFree(
		const FFixedVector& WorldPos,
		FFixedPoint SelfRadius,
		const TArray<FFixedVector>& AvoidCentres,
		const TArray<FFixedPoint>& AvoidRadii,
		FFixedVector& OutProjected) const
	{
		return ProjectPointToNavOnElevation(WorldPos, OutProjected);
	}

	/** Sample the baked top-of-surface Z at a world-space XY.
	 *
	 *   `bWalkableOnly = true` (default): refuses on blocked cells (cube
	 *   tops, wall footprints, pruned islands). The refusal is deliberate
	 *   for ground movement: prevents units from Z-snapping onto wall tops
	 *   as they slide across blocked-cell slivers (path smoother corner-
	 *   cuts, vehicle turn arcs). Returning false leaves the unit's
	 *   previous-tick Z in place — keeps them on the floor instead of
	 *   popping onto the wall.
	 *
	 *   `bWalkableOnly = false`: returns the top of whatever's in the cell
	 *   regardless of passability — building roof, wall top, steep-slope
	 *   hit point. The bake already stores it per cell (one downward trace
	 *   per cell, top hit recorded). Used by flying movements: flyer Z =
	 *   surface + Altitude automatically clears anything in the cell.
	 *
	 *  Returns false in either mode for out-of-bounds XY. */
	virtual bool GetCellHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ, bool bWalkableOnly = true) const { return false; }

	// ----------------------------------------------------------------------
	// Debug
	// ----------------------------------------------------------------------

	/** Per-frame debug draw hook. Called each tick while `ShowFlags.Navigation`
	 *  is on in any viewport. Default: no-op — the framework's shipped cell
	 *  viz goes through `USeinNavDebugComponent` (scene-proxy backed, one
	 *  batched mesh, editor-visible without PIE). Override only if you need
	 *  ephemeral per-frame drawing on top of that. */
	virtual void DrawDebug(UWorld* World) const {}

	/** Collect the nav's cell geometry for scene-proxy rendering. Subclasses
	 *  emit one quad per cell (XY-plane, slightly above the cell height) with
	 *  a per-cell color (green = walkable, red = blocked). Default: no cells.
	 *
	 *  Called by `USeinNavDebugComponent::CreateSceneProxy` when `ShowFlags.
	 *  Navigation` is on. The proxy captures the returned arrays once, so
	 *  this is NOT a per-frame hot path — only runs on load / bake / mutation.
	 *
	 *  In `UE_BUILD_SHIPPING` subclass overrides become no-ops (method bodies
	 *  are guarded by `UE_ENABLE_DEBUG_DRAWING`). The declaration stays for
	 *  vtable / ABI consistency. */
	virtual void CollectDebugCellQuads(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const {}

	/** Collect per-cell geometry for an active move-to path. Called each frame
	 *  from the debug ticker while `ShowFlags.Navigation` is on. Subclasses
	 *  rasterize the FULL A* route (every waypoint segment WP[0]→…→last) into grid
	 *  cells; the ticker draws them as tinted overlays. Drawing the full route —
	 *  independent of the follower's live index — keeps the viz honest: a follower
	 *  that diverges shows against the true route instead of replacing it.
	 *
	 *  - OutRemainingCells: cells along the route (excludes the destination cell to
	 *    avoid double-draw with OutCurrentTargetCell).
	 *  - OutCurrentTargetCell: the destination cell (final waypoint), drawn with a
	 *    distinct marker color. Empty if unreachable.
	 *  - `AgentPos` / `CurrentWaypointIndex` are retained for the virtual's interface
	 *    contract but unused by the shipped full-route impl.
	 *
	 *  Same stripping convention as CollectDebugCellQuads. */
	virtual void CollectDebugPathCells(
		const FFixedVector& AgentPos,
		const TArray<FFixedVector>& Waypoints,
		int32 CurrentWaypointIndex,
		TArray<FVector>& OutRemainingCells,
		TArray<FVector>& OutCurrentTargetCell,
		float& OutHalfExtent) const {}

	/** Collect cells currently stamped by dynamic nav blockers (tanks,
	 *  vehicles, buildings under construction, etc.). Called per-frame from
	 *  the debug ticker while `ShowFlags.Navigation` is on so blocker stamps
	 *  appear live in the debug grid view. Emits one (Center, Color) pair
	 *  per cell — Color resolves from the blocker's BlockedNavLayerMask
	 *  against plugin-settings layer colors so the viz uniformly reflects
	 *  what each blocker is gating, regardless of whether the layer-
	 *  perspective filter is on. Subclasses with no dynamic-blocker support
	 *  emit nothing (default). Same shipping-strip convention as the other
	 *  debug collectors. */
	virtual void CollectDebugBlockerCells(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const {}

	// ----------------------------------------------------------------------
	// Events
	// ----------------------------------------------------------------------

	/** Broadcast after bake completion, substrate (re-)adoption, or dynamic
	 *  obstacle mutation. */
	FSeinOnNavigationMutated OnNavigationMutated;
};
