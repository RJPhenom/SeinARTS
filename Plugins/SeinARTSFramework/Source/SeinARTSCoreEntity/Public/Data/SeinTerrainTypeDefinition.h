/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainTypeDefinition.h
 * @brief   Plugin-settings row defining one designer-configurable TERRAIN TYPE —
 *          the neutral per-cell classification the unified level bake stamps and
 *          each system interprets (nav → cost in BASE; cover → quality in the Cover
 *          EXTENSION, keyed by TerrainTag).
 *
 *          The per-cell baked channel stores a uint8 INDEX. Index 0 is the reserved
 *          implicit "Default" (cost 1, no tag) and is NOT in the array — exactly like
 *          nav layers reserve bit 0. The array holds the ADDITIONAL types, at stored
 *          indices 1..N (array position i → stored index i+1). A cell that matches no
 *          physical material and sits under no terrain volume is Default.
 *
 *          Index stability: unlike nav-layer BIT order (which breaks saves/replays if
 *          shifted), terrain-type INDEX order only lives in the regenerable baked data,
 *          so reordering/inserting is more forgiving — it just needs a RE-BAKE, never
 *          breaks a save. "Append or rename" stays the easy habit.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "Types/FixedPoint.h"
#include "SeinTerrainTypeDefinition.generated.h"

/**
 * One designer-configurable terrain type. Authored in plugin settings
 * (`USeinARTSCoreSettings::TerrainTypes`), alongside the resource catalog and the
 * nav / vision layer definitions.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinTerrainTypeDefinition
{
	GENERATED_BODY()

	/** Identity tag (author under `SeinARTS.Terrain.*`). The STABLE key extensions
	 *  map their own interpretation from — e.g. the Cover extension maps this tag to
	 *  a cover quality (Road → Negative). Nav reads NavCost directly, no tag needed.
	 *  Also the key terrain volumes reference to stamp this type. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FGameplayTag TerrainTag;

	/** ROUTING dial — A* path cost. 1 = normal; higher = the pathfinder AVOIDS this
	 *  terrain (routes around it when a cheaper way exists). This is independent of
	 *  speed: it changes WHICH path, not how fast the unit traverses. A cell is NEVER
	 *  made impassable via cost — use blocking geometry or a per-agent BlockedTerrainTags
	 *  filter for that, so it's clamped to [1, 254] (0/255 are the bake's blocked
	 *  sentinels). Example: a road that gives negative cover → low SpeedMultiplier impact
	 *  but a HIGH NavCost so units prefer to skirt it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain",
		meta = (ClampMin = "1", ClampMax = "254"))
	int32 NavCost = 1;

	/** SPEED dial — traversal speed multiplier, INDEPENDENT of NavCost. 1 = normal;
	 *  <1 slower (mud, deep snow), >1 faster (roads). A unit's effective top speed while
	 *  on this terrain = its authored TopSpeed × this. Floored at 0.05 at runtime so it
	 *  can never freeze a unit. Routing (NavCost) and speed are separate on purpose: you
	 *  don't walk slower on a road, but you might still route around it. Default 1. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FFixedPoint SpeedMultiplier = FFixedPoint::One;

	/** VISION dial — scales the SIGHT RADIUS of a unit standing on this terrain (fog of
	 *  war). 1 = normal; <1 cuts vision (a unit in a forest sees less far), >1 extends it
	 *  (a hilltop / watchtower terrain). Floored at 0.05 at runtime. Independent of the
	 *  other dials. NOTE: this is "reduced own sight while on the terrain," not line-of-
	 *  sight occlusion through it (forests blocking sight of what's behind them would be a
	 *  separate FoW-occlusion feature). Default 1. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FFixedPoint VisionMultiplier = FFixedPoint::One;

	/** Physical materials that resolve to this terrain type at bake. Paint a landscape
	 *  layer (or assign a mesh material) whose PhysMaterial is listed here and the
	 *  shared down-trace classifies those cells as this type — native painting, no
	 *  custom tool. A terrain volume always OVERRIDES this material-derived type.
	 *
	 *  Stored as soft PATHS (not TSoftObjectPtr<UPhysicalMaterial>) so this sim-module
	 *  settings struct does NOT pull a PhysicsCore link dependency for the reflected
	 *  inner type; the editor picker is still filtered to physical materials via
	 *  AllowedClasses, and the bake (level-data module, which does own physics) compares
	 *  a trace hit's phys-material path against these — no asset load needed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain",
		meta = (AllowedClasses = "/Script/PhysicsCore.PhysicalMaterial"))
	TArray<FSoftObjectPath> PhysicalMaterials;

	/** Color used by the terrain debug overlay (and a sensible default tint source for
	 *  extensions that visualize types). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FLinearColor DebugColor = FLinearColor::White;
};
