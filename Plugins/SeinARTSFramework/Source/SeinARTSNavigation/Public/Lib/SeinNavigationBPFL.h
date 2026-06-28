/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.h
 * @brief   Blueprint API for navigation queries. Routes through the active
 *          USeinNavigation instance; impl-agnostic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinPathTypes.h"
#include "SeinNavigationBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Navigation Library"))
class SEINARTSNAVIGATION_API USeinNavigationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Finds a path from Start to End for a unit, routing around obstacles. Returns the path; check Is
	 *  Path Valid before reading its waypoints.
	 *
	 *  Runs the active navigation's pathfinder. Requester identifies the unit (so its body size is
	 *  accounted for); Blocked Terrain Tags are terrain classes this unit can't cross (e.g. water for
	 *  infantry). The result may be partial — a best-effort route toward an unreachable goal — and Is
	 *  Path Valid is still true for a partial path. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Find Path"))
	static FSeinPath SeinFindPath(
		const UObject* WorldContextObject,
		FFixedVector Start,
		FFixedVector End,
		FSeinEntityHandle Requester,
		FGameplayTagContainer BlockedTerrainTags);

	/** Asks the navigation "from here, which way to the goal?" — returns a single planar direction.
	 *
	 *  The pull-style complement to Find Path: instead of a whole route it returns the unit direction to
	 *  head this tick (zero = stop / arrived / no route). Field-based navigation (flow fields) answers this
	 *  cheaply every tick; the shipped grid nav answers by routing and returning the first step (so for the
	 *  grid nav, prefer Find Path for ordinary movement). Group Id lets a shared-field nav reuse one field
	 *  for an ordered group (0 = lone unit). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Nav Direction", AdvancedDisplay = "GroupId"))
	static FFixedVector SeinQueryNavDirection(
		const UObject* WorldContextObject,
		FFixedVector From,
		FFixedVector Goal,
		FSeinEntityHandle Requester,
		FGameplayTagContainer BlockedTerrainTags,
		int64 GroupId = 0);

	/** Can a unit get from one point to another at all? Returns true if a route exists.
	 *
	 *  A fast connectivity check, cheaper than Find Path — it answers reachability without building the
	 *  route. Agent Tags select which terrain the unit treats as blocked. Use it to gate an order ("can
	 *  this unit even reach there?") before committing to a full path. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Location Reachable"))
	static bool SeinIsLocationReachable(
		const UObject* WorldContextObject,
		FFixedVector From,
		FFixedVector To,
		FGameplayTagContainer AgentTags);

	/** Picks a random walkable point within Radius of Origin that the origin can actually reach.
	 *
	 *  Deterministic: the same Seed always returns the same point, so it is lockstep-safe — derive Seed
	 *  from sim state (e.g. entity id + tick), never from wall-clock. Returns false (and leaves Out
	 *  Point at Origin) when nothing suitable is found: a sparse area, a tiny radius, or no baked nav. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Random Reachable Point"))
	static bool SeinGetRandomReachablePoint(
		const UObject* WorldContextObject,
		FFixedVector Origin,
		FFixedPoint Radius,
		int64 Seed,
		FFixedVector& OutPoint);

	/** Tests a straight line across the static navigation; returns whether it is blocked.
	 *
	 *  Returns true if something blocks the line before reaching To (Out Hit Point is the first blocked
	 *  spot, otherwise To). Cheap — no pathfinding. Use it to check a straight shortcut before routing
	 *  the long way around. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Nav Raycast"))
	static bool SeinNavRaycast(
		const UObject* WorldContextObject,
		FFixedVector From,
		FFixedVector To,
		FFixedVector& OutHitPoint);

	/** The ground height at a world position. (Z is this engine's up axis.) Returns false if there is
	 *  no navigation data there.
	 *
	 *  Walkable Only samples only walkable cells — use it for ground units, so they ignore blocked
	 *  slivers; turn it off to read the top of any cell — use it for flyers, so they ride over
	 *  buildings. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cell Height At"))
	static bool SeinGetCellHeightAt(
		const UObject* WorldContextObject,
		FFixedVector WorldPos,
		bool bWalkableOnly,
		FFixedPoint& OutHeight);

	/** The terrain type index under a world position (0 = the default / no terrain).
	 *
	 *  Identifies which authored terrain class covers that spot — the value that drives routing cost,
	 *  traversal speed, and vision. Map the index to your terrain set to read it meaningfully. Returns
	 *  0 where there is no terrain data. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Terrain Type At"))
	static int32 SeinGetTerrainTypeAt(const UObject* WorldContextObject, FFixedVector WorldPos);

	/** The terrain tag at a world position — the friendly identifier for the terrain class there.
	 *
	 *  The named version of Get Terrain Type At: it maps the terrain index to its tag in your terrain
	 *  set (e.g. Terrain.Road, Terrain.Mud). Returns an empty tag where there is no terrain. Branch on
	 *  this to make terrain-aware decisions by name instead of by index. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Terrain Tag At"))
	static FGameplayTag SeinGetTerrainTagAt(const UObject* WorldContextObject, FFixedVector WorldPos);

	/** Whether a unit could stand at a world position right now — walkable ground and not blocked.
	 *
	 *  Considers the static navigation plus any current dynamic blockers, so it answers "is this spot
	 *  free?" at this moment. Use it to validate a destination before ordering a unit there, or a spot
	 *  before placing something. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Position Clear"))
	static bool SeinIsPositionClear(const UObject* WorldContextObject, FFixedVector WorldPos);

	/** The size of one navigation grid cell, in world units.
	 *
	 *  The granularity of the nav grid — useful when reasoning about how finely a hand-built or
	 *  smoothed path needs its points spaced. Returns 0 when there is no navigation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cell Size"))
	static FFixedPoint SeinGetCellSize(const UObject* WorldContextObject);

	// ---- Path accessors (operate on a path you already have) ----

	/** Whether the path actually reached a destination and is safe to follow. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Is Path Valid"))
	static bool SeinIsPathValid(const FSeinPath& Path) { return Path.bIsValid; }

	/** The path's points in order, from start to destination. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Waypoints"))
	static TArray<FFixedVector> SeinGetPathWaypoints(const FSeinPath& Path) { return Path.Waypoints; }

	/** The path's typed segments — the typed stretch between each pair of waypoints (Straight today).
	 *
	 *  Where Get Path Waypoints gives the turn points, this gives how to travel between them: read a
	 *  segment's type to drive a path by segment (follow a curve, brake into an arc). Empty for simple
	 *  paths that never derived segments; otherwise one segment per consecutive waypoint pair. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Segments"))
	static TArray<FSeinPathSegment> SeinGetPathSegments(const FSeinPath& Path) { return Path.Segments; }

	/** How many points the path has — a count, NOT a world distance. For travel distance use Get Path Cost. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Length"))
	static int32 SeinGetPathLength(const FSeinPath& Path) { return Path.Waypoints.Num(); }

	/** The total planar length of the path in world units (the sum of its segments).
	 *
	 *  Use it to compare routes by travel distance — e.g. which of several reachable targets is nearest
	 *  by path rather than by straight line. Does not include terrain cost weighting. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Cost"))
	static FFixedPoint SeinGetPathCost(const FSeinPath& Path) { return Path.TotalCost; }
};
