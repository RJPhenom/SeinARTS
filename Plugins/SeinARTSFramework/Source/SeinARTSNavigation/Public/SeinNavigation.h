/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.h
 * @brief   Abstract base class for pluggable navigation implementations.
 *
 *          USeinNavigation owns the end-to-end nav problem for one world:
 *          bake pipeline, baked-asset storage, runtime pathfinding, reachability,
 *          and (optionally) debug visualization. It is the ONLY thing the
 *          framework's MoveTo action, editor bake button, and ability validation
 *          delegate talk to.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::NavigationClass`).
 *          The framework ships `USeinNavigationAStar` as a minimal 2D-grid +
 *          A* reference implementation; game teams can subclass or replace it
 *          entirely with navmesh-, waypoint-, or hierarchical-graph-based impls
 *          without touching any other framework code.
 *
 *          Decoupling contract:
 *          - The MoveTo action, BPFL, volume actor, and ability validation
 *            delegate call into this class's virtual surface only.
 *          - Concrete subclasses own their own data storage + bake strategy.
 *          - Baked data is stored in a USeinNavigationAsset subclass (impl-
 *            specific); ASeinNavVolume holds a polymorphic reference.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Types/Entity.h"
#include "Stamping/SeinStampShape.h"
#include "Components/SeinExtentsComponent.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.h"
#include "SeinNavigation.generated.h"

class UWorld;
class ASeinNavVolume;
class USeinNavigationAsset;
class IDetailLayoutBuilder;

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

/** Fired when the nav's baked data mutates (bake finished, asset re-loaded,
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
	// Bake (editor / dev-loop)
	// ----------------------------------------------------------------------

	/** The UDataAsset class this nav produces when baking. Subclasses return
	 *  their concrete USeinNavigationAsset subclass. Default: the abstract base
	 *  (use only if the subclass uses no serialized data — most don't). */
	virtual TSubclassOf<USeinNavigationAsset> GetAssetClass() const;

	/** Begin an async bake covering every ASeinNavVolume in World. Return true
	 *  if the bake kicked off (false if already running or no volumes found).
	 *  Subclasses own the async strategy — tick-driven, worker thread, whatever
	 *  fits their data. Progress reporting is the subclass's responsibility. */
	virtual bool BeginBake(UWorld* World) { return false; }

	/** True while an async bake is in progress. */
	virtual bool IsBaking() const { return false; }

	/** Request bake cancellation. Safe to call when not baking (no-op). */
	virtual void RequestCancelBake() {}

	// ----------------------------------------------------------------------
	// Runtime load — called on level begin-play
	// ----------------------------------------------------------------------

	/** Swap the loaded nav data. Passing nullptr clears runtime state.
	 *  Subclasses should call Super then broadcast OnNavigationMutated once
	 *  their own runtime arrays are updated — this base impl stores the
	 *  pointer but does NOT broadcast, so subclass mutations and the signal
	 *  stay in lockstep. */
	virtual void LoadFromAsset(USeinNavigationAsset* Asset) { LoadedAsset = Asset; }

	/** The currently-loaded baked asset, or nullptr if never loaded. */
	USeinNavigationAsset* GetLoadedAsset() const { return LoadedAsset; }

	/** True if the nav has usable runtime data (either from a baked asset or
	 *  procedurally initialized). Queries return no-path when false. */
	virtual bool HasRuntimeData() const { return LoadedAsset != nullptr; }

	// ----------------------------------------------------------------------
	// Queries — core pathing surface
	// ----------------------------------------------------------------------

	/** Synchronous path query. Subclasses override. Default: returns an invalid
	 *  path. Single entry point for path consumers (BPFL queries,
	 *  reachability checks, the budgeted
	 *  `USeinNavigationSubsystem::RequestPath` path). What the subclass
	 *  produces is its own concern — barebones nav returns a polyline,
	 *  vehicle-aware nav layers in wall padding / curve fitting / maneuver
	 *  prepend before returning. */
	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const { OutPath.Clear(); return false; }

	/** Cell-level path query — pure 2D pathfinding on the clearance grid.
	 *  Output is a cell-aware polyline: smoothed (LoS-collapsed) and
	 *  segment-derived as straight segments. Used directly by movement
	 *  modes that don't need curve-aware paths (`USeinBasicMovement`,
	 *  `USeinBasicUnitMovement`, `USeinInfantryMovement`), and as a
	 *  building block by vehicle-aware nav subclasses (which call this
	 *  internally from their `FindPath` override before layering on
	 *  curve fitting and other post-processing).
	 *
	 *  Default: clears OutPath and returns false. The default nav impl
	 *  (`USeinNavigationAStar`) overrides this with the real 2D
	 *  A* search. */
	virtual bool FindCellPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const
	{
		OutPath.Clear();
		return false;
	}

	/** Fast reachability check. Default: falls back to FindPath. Subclasses
	 *  should override with a cheaper reachability component / flood-fill if
	 *  the FindPath cost is prohibitive on the query hot path (ability
	 *  validation runs this per activation). */
	virtual bool IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const;

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

	/** Snap an arbitrary world-space point to the nearest walkable location.
	 *  Returns false if no walkable point is within the nav's reachable region.
	 *  Default: passes the point through unchanged + returns HasRuntimeData(). */
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
	{
		OutProjected = WorldPos;
		return HasRuntimeData();
	}

	/** Z-biased projection: snap to nearest walkable cell, preferring cells
	 *  whose stored height is close to `WorldPos.Z`. When the input lands at
	 *  a platform edge, regular `ProjectPointToNav` would snap to the
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
	 *  rasterize the line from `AgentPos` through `Waypoints[CurrentIdx..End]`
	 *  into grid cells; the ticker draws them as tinted overlays.
	 *
	 *  - OutRemainingCells: cells along the path ahead of the agent (excludes
	 *    the current-target cell to avoid double-draw).
	 *  - OutCurrentTargetCell: the single cell containing `Waypoints[CurrentIdx]`
	 *    (drawn with a distinct lead-marker color). Empty if unreachable.
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
	// Editor extensibility
	// ----------------------------------------------------------------------

#if WITH_EDITOR
	/** Optional hook for subclasses to extend ASeinNavVolume's details panel.
	 *  Called by the framework's `FSeinNavVolumeDetails` after it has added its
	 *  own "Bake Navigation" row. Subclasses may add custom rows (per-bake
	 *  options, per-volume diagnostics, multi-stage bake UI, etc.). Default:
	 *  no-op. The framework's bake button is unconditional — the abstract
	 *  `BeginBake` virtual dispatches to whatever subclass is active, so
	 *  designers see the same button regardless of which nav impl is selected. */
	virtual void CustomizeVolumeDetails(IDetailLayoutBuilder& /*DetailBuilder*/, ASeinNavVolume* /*Volume*/) {}
#endif

	// ----------------------------------------------------------------------
	// Events
	// ----------------------------------------------------------------------

	/** Broadcast after bake completion, asset swap, or dynamic obstacle mutation. */
	FSeinOnNavigationMutated OnNavigationMutated;

protected:

	/** The currently-loaded baked asset. Ownership stays with the volume / asset
	 *  registry — this is a non-owning pointer. */
	UPROPERTY(Transient)
	TObjectPtr<USeinNavigationAsset> LoadedAsset;
};
