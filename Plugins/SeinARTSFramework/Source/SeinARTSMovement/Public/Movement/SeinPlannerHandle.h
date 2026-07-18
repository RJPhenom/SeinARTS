/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlannerHandle.h
 * @brief   The Blueprint-facing view of a path-planning request. Custom movement modes use this
 *          inside their Plan Path event to build the route a unit will follow.
 *
 *          Like the Mover Handle is to a tick, this wraps the plan-time context and the output path
 *          for the duration of one Plan Path dispatch. A mode can read the start and destination,
 *          ask for a normal A* path and then reshape it, probe the navigation, or build a path from
 *          scratch. Valid only during the dispatch; a graph must not store it for later.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinPathTypes.h"   // ESeinPathResult (BP return type)
#include "GameplayTagContainer.h"  // FGameplayTag (Get Terrain Tag At return)
#include "SeinPlannerHandle.generated.h"

struct FSeinPlanPathContext;
struct FSeinPath;
class USeinMovement;

UCLASS(BlueprintType, meta = (DisplayName = "Sein Planner Handle", SeinDeterministic))
class SEINARTSMOVEMENT_API USeinPlannerHandle : public UObject
{
	GENERATED_BODY()

public:

	// C++-only wiring: the owning USeinMovement binds the context + output path for one dispatch.
	void SetContext(const FSeinPlanPathContext* InCtx, FSeinPath* InOutPath) { Ctx = InCtx; OutPath = InOutPath; }

	/** True when this handle is driving a real plan request (it has a context and an output path). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Is Valid Planner"))
	bool IsValidPlanner() const;

	// ---- What you're planning ----

	/** Where the unit is starting from. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Start Location"))
	FFixedVector GetStartLocation() const;

	/** Where the unit was ordered to go. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Destination"))
	FFixedVector GetDestination() const;

	/** How wide the unit is — the clearance its path needs to keep from walls. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Footprint Radius"))
	FFixedPoint GetFootprintRadius() const;

	/** The tightest turn this mode can make — useful if you smooth the path into drivable curves. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Min Turn Radius"))
	FFixedPoint GetMinTurnRadius() const;

	// ---- Building the path ----

	/** Empties the path so you can build a fresh one. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Clear Path"))
	void ClearPath();

	/** Adds one point to the end of the path. Build a route by adding points in order from start to
	 *  destination, then call Finalize Path. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Add Waypoint"))
	void AddWaypoint(const FFixedVector& Waypoint);

	/** Finishes a hand-built path so the unit can follow it. Call once after adding all waypoints.
	 *  Set Is Partial to true if the path stops short of the exact destination (best effort). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Finalize Path"))
	void FinalizePath(bool bIsPartial);

	// ---- Building a TYPED path (arcs / drivable curves) ----
	// A vehicle mode rounds the nav polyline into drivable curves here: add alternating straight +
	// arc segments, then call Finalize Typed Path. The built-in follower drives the flattened
	// backbone; a curve-aware mode reads the exact segments (Mover Handle Get Segment).

	/** Appends a drivable ARC segment (a Dubins / Reeds-Shepp curve) to the path: it runs From ->
	 *  To about Center with the given Radius and SIGNED Sweep (sign = handedness, magnitude =
	 *  extent in radians); Reverse marks a segment driven backward (a Reeds-Shepp cusp). Also
	 *  pushes the endpoints onto the waypoint backbone so the path stays coherent for a plain
	 *  follower. Chain segments end-to-end (each From = the previous To). Call Finalize Typed Path
	 *  (NOT Finalize Path) once all segments are added. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Add Arc Segment"))
	void AddArcSegment(const FFixedVector& From, const FFixedVector& To,
		const FFixedVector& Center, FFixedPoint Radius, FFixedPoint Sweep, bool bReverse);

	/** Appends a STRAIGHT typed segment (From -> To) — the tangent legs between arcs when building
	 *  a mixed straight + arc route by hand. Reverse marks it driven backward. Also pushes the
	 *  endpoints onto the waypoint backbone. Call Finalize Typed Path once done. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Add Straight Segment"))
	void AddStraightSegment(const FFixedVector& From, const FFixedVector& To, bool bReverse);

	/** Finishes a hand-built TYPED path (arc / straight segments added above). Unlike Finalize
	 *  Path, this PRESERVES the typed segments you authored instead of rederiving them as plain
	 *  straights, and sets total cost from the true segment lengths (an arc contributes its arc
	 *  length). Set Is Partial if the route stops short of the exact destination. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Finalize Typed Path"))
	void FinalizeTypedPath(bool bIsPartial);

	/** Replaces the path with a straight line from start to destination, and reports success.
	 *
	 *  The flying default — a unit that ignores obstacles just goes straight. For ground units use
	 *  Request Nav Path instead so they route around walls. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Build Straight Line Path"))
	ESeinPathResult BuildStraightLinePath();

	/** Runs the normal pathfinder to the destination and fills the path; reports the outcome.
	 *
	 *  This is what ground units do by default: a budgeted A* search using this unit's footprint and
	 *  navigation settings. The result is Found, Not Found, Throttled (out of search budget this
	 *  tick, try again), or No Navigation. After it returns Found you can read the waypoints with Get
	 *  Path Waypoint and rebuild a smoothed version if you want a custom driving line. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Request Nav Path"))
	ESeinPathResult RequestNavPath();

	// ---- Reading the path back (to post-process it) ----

	/** How many points the current path has. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Path Waypoint Count"))
	int32 GetPathWaypointCount() const;

	/** The path point at a given index, or zero if out of range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Path Waypoint"))
	FFixedVector GetPathWaypoint(int32 Index) const;

	// ---- Navigation probes ----

	/** Tests a straight line across the static navigation; returns whether it is blocked.
	 *
	 *  Returns true if something blocks the line (Out Hit Point is the first blocked spot), false if
	 *  it is clear. Use it to decide whether a straight shortcut is possible before pathing around. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Nav Raycast"))
	bool NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const;

	/** The ground height at a world position. (Z is this engine's up axis.)
	 *
	 *  Returns false if there is no navigation data there. Walkable Only ignores blocked cells (use
	 *  it for ground units); turn it off to read the top of any cell (use it for flyers). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Sample Ground Height"))
	bool SampleGroundHeight(const FFixedVector& WorldPos, bool bWalkableOnly, FFixedPoint& OutHeight) const;

	/** Snaps a world point onto the nearest navigable spot; returns whether it succeeded.
	 *
	 *  Use it when building a custom path so a hand-placed or smoothed waypoint lands on reachable
	 *  ground (Out Projected is the snapped point). Returns false (Out Projected left unchanged) when
	 *  there is no navigation data. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Project To Nav"))
	bool ProjectToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const;

	/** The terrain type index under a world position (0 = default / no terrain). Drives routing cost,
	 *  traversal speed, and vision; map the index to your terrain set to read it meaningfully. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Terrain Type At"))
	int32 GetTerrainTypeAt(const FFixedVector& WorldPos) const;

	/** The terrain tag at a world position — the friendly identifier for the terrain class there (e.g.
	 *  Terrain.Road). The named version of Get Terrain Type At; empty where there is no terrain. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Terrain Tag At"))
	FGameplayTag GetTerrainTagAt(const FFixedVector& WorldPos) const;

	/** Whether a unit could stand at a world position right now — walkable and not blocked (static
	 *  navigation plus current dynamic blockers). Use it to validate a waypoint before adding it. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Is Position Clear"))
	bool IsPositionClear(const FFixedVector& WorldPos) const;

	/** The size of one navigation grid cell, in world units — the grid granularity to space smoothed
	 *  waypoints against. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Planner", meta = (DisplayName = "Get Cell Size"))
	FFixedPoint GetCellSize() const;

private:

	USeinMovement* GetOwningMovement() const;

	const FSeinPlanPathContext* Ctx = nullptr;
	FSeinPath* OutPath = nullptr;
};
