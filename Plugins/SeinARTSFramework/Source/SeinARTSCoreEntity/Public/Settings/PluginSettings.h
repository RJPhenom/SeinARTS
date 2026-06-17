/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		PluginSettings.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Global plugin settings for SeinARTS.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"            // FDirectoryPath
#include "UObject/SoftObjectPath.h"
#include "Types/FixedPoint.h"
#include "Data/SeinResourceTypes.h"
#include "Data/SeinVisionLayerDefinition.h"
#include "Data/SeinNavLayerDefinition.h"
#include "Data/SeinTerrainTypeDefinition.h"
#include "Data/SeinCollisionChannelDefinition.h"
#include "Data/SeinLobbyMapEntry.h"
#include "Data/SeinTagPrefixMapping.h"
#include "StructUtils/InstancedStruct.h"
#include "PluginSettings.generated.h"

class USeinCommandBrokerResolver;
class USeinFaction;
class USeinFactionService;
class USeinAIController;

/**
 * What happens when a player disconnects mid-match and the slot's grace
 * period expires (`DroppedToAITakeoverSeconds`). Per-project policy: an
 * RTS demanding match completion picks `BasicAI`, a competitive ladder
 * map could pick `RemovePlayer` for forfeits, a casual coop project could
 * pick `KeepUnitsAlive` to leave the dropped player's units idle until
 * teammates can shepherd them.
 *
 *   KeepUnitsAlive — flip lifecycle to AITakeover, no AI registered.
 *                    Units sit idle (no commands issued on the slot's behalf
 *                    beyond the framework's empty heartbeats). Closest to
 *                    pre-Phase-4 behavior.
 *   BasicAI        — instantiate `DefaultAIControllerClass`, register it
 *                    with the sim. Default; framework ships
 *                    `USeinNullAIController` as a no-op fallback so the
 *                    pipeline is exercised even before designers author
 *                    their own AI subclass.
 *   RemovePlayer   — (RESERVED — not yet implemented) destroy the slot's
 *                    units on AI-takeover transition. Forfeit semantics.
 *                    Currently behaves as `KeepUnitsAlive` until the unit-
 *                    teardown path lands.
 */
UENUM(BlueprintType)
enum class ESeinSlotDropPolicy : uint8
{
	KeepUnitsAlive UMETA(DisplayName = "Keep Units Alive (no AI)"),
	BasicAI        UMETA(DisplayName = "Auto-Spawn Default AI Controller"),
	RemovePlayer   UMETA(DisplayName = "Remove Player on Drop (forfeit) [RESERVED]"),
};

/**
 * Global settings for SeinARTS.
 * Configure these in Project Settings > Plugins > SeinARTS.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SeinARTS"))
class SEINARTSCOREENTITY_API USeinARTSCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USeinARTSCoreSettings();

	/** Reconciles the VisionLayers array after config load. DefaultEngine.ini
	 *  files saved before the 6-slot contract / DebugColor field existed
	 *  can load back at len < 6 or with white-default colors on older
	 *  entries — this hook pads the array to 6 slots and backfills
	 *  per-slot debug colors on any slot whose color is still the struct-
	 *  default white. Idempotent. */
	virtual void PostInitProperties() override;

	// Simulation Settings
	// ====================================================================================================

	/**
	 * Simulation tick rate (ticks per second).
	 * Higher tick rates = smoother simulation but higher CPU cost.
	 * Default: 30 ticks per second.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "120", UIMin = "10", UIMax = "60"))
	int32 SimulationTickRate;

	/**
	 * Maximum number of simulation ticks to process per frame.
	 * Prevents "spiral of death" when frame rate drops below tick rate.
	 * Default: 5 ticks per frame maximum.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "30", UIMin = "1", UIMax = "10"))
	int32 MaxTicksPerFrame;

	// Economy Settings — Resource Catalog
	// ====================================================================================================

	/**
	 * Project-wide resource catalog. Each entry declares a resource by gameplay
	 * tag (under SeinARTS.Resource.*) with its display name, icon, caps,
	 * overflow/spend behavior, cost-direction, and production-deduction timing.
	 *
	 * Factions reference catalog entries by tag in their ResourceKit. See
	 * the resource catalog + production cost model for authoritative semantics.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (TitleProperty = "ResourceTag"))
	TArray<FSeinResourceDefinition> ResourceCatalog;

	/**
	 * Project-wide faction registry. Every `USeinFaction` data asset that should
	 * be playable / available for resource-kit lookup must be listed here.
	 *
	 * Why settings-driven (not auto-discovered): determinism. Both server and
	 * client read from the same `DefaultEngine.ini`, so iteration order +
	 * registered set is bit-identical. `USeinWorldSubsystem::RegisterFactionsFromSettings`
	 * loads + registers in a single pass at world-init time on each peer; without
	 * this, a server that registers factions via game-side code while clients do
	 * not produces an empty `Factions` map on clients → empty `ResourceKit` lookup
	 * → divergent starting resources → state-hash desync from tick 1.
	 *
	 * Designers add their faction assets here (or via a Project Settings panel
	 * once Phase 3c lobby UI ships). Empty array is fine for projects that don't
	 * use the faction-driven `ResourceKit` path — `StartingResources` on the
	 * GameMode CDO covers the common case.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (DisplayName = "Registered Factions"))
	TArray<TSoftObjectPtr<USeinFaction>> RegisteredFactions;

	// Effects
	// ====================================================================================================

	// Command Brokers (DESIGN §5)
	// ====================================================================================================

	/**
	 * Default resolver class instantiated for every spawned CommandBroker. Framework
	 * ships `USeinDefaultCommandBrokerResolver` (capability-map filtered + uniform
	 * grid positions); designers override here to ship a project-wide dispatch
	 * policy (tight ranks, class clusters, wedge formations, etc.). If empty, the
	 * framework default is used.
	 *
	 * Default value points at SeinARTSCover's `USeinCoverAwareDefaultBrokerResolver`
	 * via soft-class-path — when the cover module is loaded the framework opts into
	 * cover-snap behavior out of the box, and when the cover module is stripped the
	 * soft path resolves to null and the framework falls back to the plain
	 * `USeinDefaultCommandBrokerResolver`. Designers wanting plain non-cover
	 * dispatch can clear this field in Project Settings.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Command Brokers",
		meta = (DisplayName = "Default Broker Resolver Class"))
	TSoftClassPtr<USeinCommandBrokerResolver> DefaultBrokerResolverClass;

	// DefaultSquadDispatchResolverClass removed — squad dispatch resolver
	// selection is owned by the SeinARTSSquad extension module. Per-squad
	// override via FSeinSquadComponent::DispatchResolverClass.

	/**
	 * Dev-mode warning threshold for per-entity active-effect count. When an
	 * entity's effect count crosses this threshold (previously below, now at-or-above)
	 * a warning is logged once per crossing — catches runaway apply loops at design
	 * time. Zero runtime cost in shipping; the warning path is compiled out outside
	 * `!UE_BUILD_SHIPPING`. Default: 256.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Effects", meta = (ClampMin = "1", UIMin = "32", UIMax = "1024"))
	int32 EffectCountWarningThreshold;

	// Navigation Settings (DESIGN §13)
	// ====================================================================================================

	/**
	 * **Which navigation implementation drives pathfinding.** The framework's
	 * MoveTo action, editor bake button, and ability validation all route
	 * through this class — the rest of the plugin doesn't care what's
	 * underneath.
	 *
	 * Ships with `USeinNavigationAStar` as the default: footprint-aware
	 * (configuration-space) 2D grid A* + line-of-sight smoothing. Paths
	 * route only through cells where the unit's body physically fits — no
	 * post-process push needed. Suitable as a generic RTS default.
	 *
	 * Game teams can subclass for project-specific behavior (curve-fitting
	 * for vehicles, flow fields for mass movement, etc.) without touching
	 * any framework code.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Navigation Class",
				MetaClass = "/Script/SeinARTSNavigation.SeinNavigation"))
	FSoftClassPath NavigationClass;

	/**
	 * The level-data substrate class (CP1.1 unified bake — nav + Fog-of-War layers
	 * read from one baked grid). Empty / invalid → framework default
	 * `USeinLevelDataDefault`. Swappable wholesale, same soft-path pattern as
	 * `NavigationClass` / `FogOfWarClass` so SeinARTSCoreEntity stays decoupled from
	 * SeinARTSLevelData (planning/Decisions.md D12).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Level Data Class",
				MetaClass = "/Script/SeinARTSLevelData.SeinLevelData"))
	FSoftClassPath LevelDataClass;

	/**
	 * **Size of one nav grid square, in world units.** The pathfinder works
	 * on a 2D grid; this sets how big each grid cell is.
	 *
	 * ELI5: pick this to match your smallest unit's body size.
	 *  - Infantry-centric RTS (CoH/AoE feel): ~100 (1m cells)
	 *  - Massive-unit RTS (Supreme Commander feel): ~800 (8m cells)
	 *
	 * Trade-off: smaller cells = finer detail, more memory, slower bakes.
	 * Larger cells = coarser paths, faster bakes.
	 *
	 * Per-volume override available: set `bOverrideCellSize` on a
	 * `ASeinLevelVolume` to use a different cell size in that volume only.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	FFixedPoint CellSize;

	/**
	 * **Tallest vertical step a unit can climb in one stride, in world
	 * units.** Used at bake time to decide whether two adjacent cells are
	 * connected: if their height difference exceeds this, the connection
	 * is dropped (treated as a wall edge).
	 *
	 * ELI5: think "stair height." A 30cm value lets units climb curbs but
	 * not walls; an 80cm value lets them climb up small ledges; 200cm+
	 * effectively disables the step check.
	 *
	 * Typical: about half the CellSize.
	 *
	 * Per-volume override available on `ASeinLevelVolume`.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	FFixedPoint MaxStepHeight;

	/**
	 * **Where baked level data assets are saved.** When you click "Bake Level
	 * Data" on a `ASeinLevelVolume`, the resulting `.uasset` (the shared
	 * height field + every layer's channel — nav, fog of war, …) is written
	 * here as `LevelData_<LevelName>.uasset` and auto-assigned to every level
	 * volume on the level.
	 *
	 * ELI5: "where do my baked levels go?" Default `/Game/LevelData/`. Use the
	 * content-browser picker to choose any folder under any content mount —
	 * `/Game/` for project content, `/<PluginName>/` for plugin content. The
	 * framework auto-creates the folder if it doesn't exist.
	 *
	 * Baked level data is a regenerable build artifact (and gitignored by
	 * default) — re-bake after changing this; existing bakes don't auto-move.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Level Data Save Folder", ContentDir))
	FDirectoryPath LevelDataSaveFolder;

	/**
	 * Collision channel the unified bake's shared per-cell down-trace uses to find the
	 * ground surface (the pass that produces the height field + surface normal). Default
	 * ECC_Visibility. Point it at a project-specific "ground" channel if your level
	 * geometry isn't on Visibility. Re-bake after changing. (Consumed by the default
	 * substrate USeinLevelDataDefault; a custom substrate may read or ignore it.)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Bake Trace Channel"))
	TEnumAsByte<ECollisionChannel> BakeTraceChannel = ECC_Visibility;

	/**
	 * Designer-configurable N-layers for the agent/blocker nav layer mask.
	 * Slot 0 → bit 1, slot 6 → bit 7. Exactly 7 slots, framework-enforced.
	 *
	 * The framework-default "Default" layer is NOT in this array — it's
	 * reserved as bit 0 and always present. These 7 slots are exclusively
	 * for additional agent classes a game needs beyond generic pathing:
	 * Amphibious, HeavyVehicle, FriendlyFaction, InfantryOnly, etc. All 7
	 * ship disabled — opt in by naming + enabling slots your game uses.
	 *
	 * Pathing is gated by intersection: `(AgentMask & BlockedMask) != 0`
	 * → blocked. So an amphibious unit whose NavLayerMask drops the
	 * Default bit and adds the "Amphibious" bit ignores a water blocker
	 * authored as Default-only; multi-class agents OR multiple bits.
	 *
	 * Renaming a slot is safe. Reordering / inserting mid-array shifts
	 * every higher-slot bit → breaks replays + saves. Only append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, EditFixedSize, Category = "Navigation",
		meta = (TitleProperty = "LayerName"))
	TArray<FSeinNavLayerDefinition> NavLayers;

	/**
	 * **Terrain types** — the neutral per-cell classification the unified level bake
	 * stamps once and each system interprets: navigation maps a type to a movement
	 * COST (here, `NavCost`); the Cover extension maps the same type's `TerrainTag` to
	 * a cover quality (Road → Negative) — so one authored road region drives both
	 * faster movement and negative cover without double-authoring, and the base never
	 * learns about cover.
	 *
	 * Authored two ways (both bake into the same per-cell type): list a type's
	 * `PhysicalMaterials` and the shared down-trace classifies cells whose surface uses
	 * that phys material (paint a landscape layer / assign a mesh material — native, no
	 * tool); or drop an `ASeinTerrainVolume` (its `TerrainType` tag + Priority OVERRIDES
	 * the material-derived type in its footprint).
	 *
	 * The baked per-cell channel stores a uint8 INDEX. Index 0 = reserved implicit
	 * "Default" (cost 1, no tag), NOT in this array — array position i → stored index
	 * i+1 (mirrors nav layers reserving bit 0). Reordering/deleting only needs a
	 * RE-BAKE (indices live in regenerable baked data, never in saves). Append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain",
		meta = (TitleProperty = "TerrainTag"))
	TArray<FSeinTerrainTypeDefinition> TerrainTypes;

	/** Movement-cost multiplier for a baked per-cell terrain-type INDEX (0 = Default).
	 *  Centralizes the reserved-0 / array-offset mapping: index 0 (or out of range) →
	 *  1; index N → TerrainTypes[N-1].NavCost clamped to [1, 254]. */
	int32 GetTerrainNavCost(int32 StoredTypeIndex) const
	{
		if (StoredTypeIndex <= 0 || StoredTypeIndex > TerrainTypes.Num()) return 1;
		return FMath::Clamp(TerrainTypes[StoredTypeIndex - 1].NavCost, 1, 254);
	}

	/** Baked stored INDEX for a terrain `Tag` (0 = Default / not found). The inverse of
	 *  the reserved-0 mapping above — used by the bake (terrain volume tag → index) and
	 *  by nav's per-agent BlockedTerrainTags filter (tag → index to gate). */
	int32 GetTerrainTypeIndex(const FGameplayTag& Tag) const
	{
		if (!Tag.IsValid()) return 0;
		for (int32 i = 0; i < TerrainTypes.Num(); ++i)
		{
			if (TerrainTypes[i].TerrainTag == Tag) return i + 1;
		}
		return 0;
	}

	/** Traversal SPEED multiplier for a baked per-cell terrain-type INDEX (0 = Default).
	 *  The movement step scales a unit's effective top speed by this. Index 0 / out of
	 *  range → 1 (no change). Floored at 0.05 so a misauthored 0/negative can never freeze
	 *  a unit. Independent of GetTerrainNavCost (routing) — the two dials are separate. */
	FFixedPoint GetTerrainSpeedMultiplier(int32 StoredTypeIndex) const
	{
		if (StoredTypeIndex <= 0 || StoredTypeIndex > TerrainTypes.Num()) return FFixedPoint::One;
		const FFixedPoint M = TerrainTypes[StoredTypeIndex - 1].SpeedMultiplier;
		const FFixedPoint Floor = FFixedPoint::One / FFixedPoint::FromInt(20); // 0.05
		return (M < Floor) ? Floor : M;
	}

	/** VISION (sight-radius) multiplier for a baked per-cell terrain-type INDEX
	 *  (0 = Default). Fog of war scales a unit's vision radius by this while it stands on
	 *  the terrain. Index 0 / out of range → 1; floored at 0.05. Independent of the other
	 *  dials. */
	FFixedPoint GetTerrainVisionMultiplier(int32 StoredTypeIndex) const
	{
		if (StoredTypeIndex <= 0 || StoredTypeIndex > TerrainTypes.Num()) return FFixedPoint::One;
		const FFixedPoint M = TerrainTypes[StoredTypeIndex - 1].VisionMultiplier;
		const FFixedPoint Floor = FFixedPoint::One / FFixedPoint::FromInt(20); // 0.05
		return (M < Floor) ? Floor : M;
	}

	/** Identity tag for a baked per-cell terrain-type INDEX (0 = Default → invalid tag).
	 *  The inverse of GetTerrainTypeIndex — used by extensions that interpret terrain by
	 *  tag (e.g. the Cover extension maps a terrain tag → a cover quality). */
	FGameplayTag GetTerrainTag(int32 StoredTypeIndex) const
	{
		if (StoredTypeIndex <= 0 || StoredTypeIndex > TerrainTypes.Num()) return FGameplayTag();
		return TerrainTypes[StoredTypeIndex - 1].TerrainTag;
	}

	/**
	 * **How many path searches the planner will run per simulation tick.**
	 * If more units ask for paths in the same tick than the budget allows,
	 * the extras wait one tick and try again.
	 *
	 * ELI5: think of this as the planner's "speed limit." A budget of 32
	 * means up to 32 units can start a move in the same tick instantly;
	 * a 50-unit selection would stagger across 2 ticks (~67ms).
	 *
	 * Tuning:
	 *  - **Default 32** — covers typical RTS group sizes without visible
	 *    stagger.
	 *  - Lower (4-8) — hard cap on per-tick CPU cost. Useful on low-spec
	 *    targets or huge maps where individual searches are expensive.
	 *  - Higher (128+) — effectively disables throttling. Use if your game
	 *    has frequent large simultaneous moves and you've measured the
	 *    real cost is acceptable.
	 *
	 * Note: BPFL one-off queries (`SeinFindPath`) and reachability checks
	 * bypass this budget — only auto-pathing units are throttled.
	 *
	 * Lockstep-safe (the order of consumption matches across clients).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1", ClampMax = "1024", UIMin = "1", UIMax = "256",
				DisplayName = "Path Requests Per Tick Budget"))
	int32 PathRequestsPerTickBudget;

	/**
	 * **A* search speed-vs-optimality dial, as a percent.** The pathfinder
	 * uses `f(n) = g(n) + (h(n) * Weight) / 100`. At 100 it's pure A*
	 * (always optimal, slow on big maps); above 100 it's "weighted A*"
	 * (faster, paths can be up to Weight% longer than optimal).
	 *
	 * ELI5: 100 = "find the shortest path no matter what."
	 *       150 = "find a path quickly, I'm OK with up to 50% longer."
	 *
	 * Tuning:
	 *  - **Default 125** — paths at most 25% longer than optimal. Visually
	 *    indistinguishable on most maps. 5-10× faster search on
	 *    obstacle-rich terrain than pure A*.
	 *  - 100 — pure A*. Slowest but always optimal.
	 *  - 200+ — very fast but paths visibly suboptimal (zig-zags).
	 *
	 * Only used when the shipped A*-family nav is selected (gated below).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "100", ClampMax = "300", UIMin = "100", UIMax = "200",
				DisplayName = "A* Heuristic Weight (%)",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 AStarHeuristicWeightPercent;

	/**
	 * **Hard cap on how much work one path search can do.** A* explores
	 * cells one at a time; this caps the total number of cells it'll look
	 * at before giving up. On cap-exceeded, A* returns the best partial
	 * path it found (closest-to-goal cell it reached) — same behavior as
	 * an unreachable destination.
	 *
	 * ELI5: think "patience limit." Higher = the planner will work harder
	 * on tough paths; lower = fails fast on impossible paths.
	 *
	 * Tuning:
	 *  - **Default 10000** — covers any legitimate path on a 1km² map with
	 *    100cm cells.
	 *  - Raise (50000+) for very large maps or fine-grained grids.
	 *  - Lower for tighter performance bounds on huge maps with many
	 *    unreachable clicks.
	 *
	 * Only used when the shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "256", ClampMax = "1000000", UIMin = "1000", UIMax = "100000",
				DisplayName = "A* Max Iterations",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 AStarMaxIterations;

	/**
	 * **How strict the planner is about elevation matching when snapping
	 * a destination onto walkable ground.** When the user clicks somewhere
	 * (or formations place slots), the planner snaps the click to the
	 * nearest passable cell. This tolerance is the maximum Z difference
	 * between input height and candidate cell height — beyond it, the
	 * scan keeps looking.
	 *
	 * ELI5: prevents the "stragglers running off cliffs" bug. If you click
	 * on a raised platform, slots that fan over the edge would otherwise
	 * snap to the floor below; the elevation gate forces them to find a
	 * platform cell instead.
	 *
	 * Tuning:
	 *  - **Default 100** — matches the default 100cm cell size.
	 *  - Tighter (50) — for maps with closely-spaced mezzanine levels you
	 *    want to keep distinct.
	 *  - Looser (200+) — for maps with tall step-ups you want considered
	 *    the same level.
	 *
	 * Only used when the shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1.0", ClampMax = "10000.0", UIMin = "10.0", UIMax = "1000.0",
				DisplayName = "Nav Projection Elevation Tolerance",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	FFixedPoint NavProjectionElevationTolerance;

	/**
	 * **How far the planner searches for a fallback cell when the clicked
	 * spot isn't directly walkable.** Click on a wall, in the ocean, or on
	 * an impassable mountain — the planner scans outward ring-by-ring
	 * looking for the nearest walkable cell. This caps how far it'll go
	 * before giving up.
	 *
	 * ELI5: think "search radius for 'walk near here instead'." A click on
	 * water with radius=30 → unit walks to the nearest shore within 30
	 * cells; with radius=5 → fails if no shore within 5 cells.
	 *
	 * Tuning:
	 *  - **Default 30** — ≈30m at the default 100cm grid.
	 *  - Raise for sparse walkable regions where clicks legitimately need
	 *    to scan far for a valid cell.
	 *  - Lower if you want clicks past the walkable area to fail-fast
	 *    instead of "walking to the nearest land."
	 *
	 * Cost scales with R², so very large values are expensive.
	 *
	 * Only used when the shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1", ClampMax = "200", UIMin = "5", UIMax = "60",
				DisplayName = "Nav Projection Max Ring Radius (cells)",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 NavProjectionMaxRingRadius;

	/** **Formation spread (opt-in).** When OFF (default), a group move order sends
	 *  every selected unit to the SAME projected destination — the AoE/SC2/CoH
	 *  model — and the hard collision floor packs them into a no-overlap cluster on
	 *  arrival. When ON, the broker fans members out across a grid formation around
	 *  the target. All the formation plumbing (grid layout, anti-cross slot match,
	 *  destination preview) stays wired either way; this only switches the DEFAULT
	 *  dispatch. Real per-unit-class / per-order formation modes layer on top later. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement",
		meta = (DisplayName = "Formation Spread Enabled"))
	bool bFormationSpreadEnabled = false;

	// ── Local avoidance (FSeinAvoidanceSystem) ──
	// Model-shape constants shared by ALL movers (the avoidance model's "feel").
	// Per-UNIT dials — AvoidanceStrength / AvoidanceWeight / bAvoidSameWeights —
	// live on FSeinMovementComponent. Defaults equal the former inline literals,
	// so motion is unchanged until tuned. All fixed-point → bit-identical.

	/** Perception look-ahead: a moving unit perceives neighbours out to
	 *  2×footprint + Speed×this. Default 0.5s. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Lookahead Seconds", ClampMin = "0.0"))
	FFixedPoint AvoidanceLookaheadSeconds = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** Speed (uu/s) at or below which a unit counts as not-moving and skips avoidance. Default 10. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Moving Speed Floor", ClampMin = "0.0"))
	FFixedPoint AvoidanceMovingSpeedFloor = FFixedPoint::FromInt(10);

	/** Influence range as a multiple of combined footprint (steer fades to 0 by this ×). Default 5. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Falloff Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceFalloffRadii = FFixedPoint::FromInt(5);

	/** Temporal smoothing: fraction of the previous steer kept each tick (0..1). Default 0.7. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Smooth Keep", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceSmoothKeep = FFixedPoint::FromInt(7) / FFixedPoint::FromInt(10);

	/** Floor on the head-on encounter weight (a with-flow / perpendicular neighbour
	 *  still carries at least this). Default 0.1. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Head-On Base", ClampMin = "0.0"))
	FFixedPoint AvoidanceHeadOnBase = FFixedPoint::One / FFixedPoint::FromInt(10);

	/** Arrival fade: steering releases within this × footprint of the goal so the
	 *  collision floor + path-attraction own the endgame. Default 3. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Arrival Release Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceArrivalReleaseRadii = FFixedPoint::FromInt(3);

	/** Clamp on the accumulated lateral nudge (unit-direction space) before
	 *  strength-scale + smoothing. Default 2. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement|Avoidance",
		meta = (DisplayName = "Max Steer Magnitude", ClampMin = "0.0"))
	FFixedPoint AvoidanceMaxSteerMagnitude = FFixedPoint::FromInt(2);

	/** EditCondition helper for the A*-family-specific properties above.
	 *  True when `NavigationClass` is the shipped A* nav (or — once
	 *  shipped — a known A* subclass like the planner variant).
	 *
	 *  Listed explicitly rather than via `IsChildOf<USeinNavigationAStar>`
	 *  because SeinARTSCoreEntity (where this lives) deliberately does NOT
	 *  depend on SeinARTSNavigation. When a new A*-family nav class is
	 *  introduced (e.g. `SeinNavigationPlannerAStar` for vehicle turn
	 *  planning), append its path to the OR chain here. Projects with
	 *  fully custom nav classes don't see the A*-specific tunables — which
	 *  is correct, since those settings wouldn't apply. */
	UFUNCTION()
	bool IsUsingShippedAStar() const
	{
		const FString Path = NavigationClass.GetAssetPathString();
		return Path == TEXT("/Script/SeinARTSNavigation.SeinNavigationAStar");
		// Future: || Path == TEXT("/Script/SeinARTSNavigation.SeinNavigationPlannerAStar");
	}

	// Collision Settings — Channel Registry
	// ====================================================================================================

	/**
	 * Project-wide collision-channel registry (object types). Each entry declares
	 * a channel a collider can BE (its Object Type) and can RESPOND to (its
	 * response matrix), with a per-channel DefaultResponse. Analogous to Unreal's
	 * object channels (WorldStatic, Pawn, Vehicle, …) but fully data-driven and
	 * **independent of navigation** — a nav blocker need not be a collider, and a
	 * collider need not block nav.
	 *
	 * Colliders (FSeinExtentsComponent's collision section) reference channels by
	 * Name: renaming a channel is safe; reordering is cosmetic for authored data
	 * (the runtime rebuilds its index layout from this list each session, identically
	 * on every peer). Holds ADDITIONAL channels only — the reserved "Default"
	 * channel lives outside this array (always present, can't be removed; like the
	 * nav "Default" layer / vision "Normal"). Enumerate via GetAllCollisionChannels().
	 * Object Type is a separate axis from Mobility (what a collider IS vs how it moves).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Collision",
		meta = (TitleProperty = "Name"))
	TArray<FSeinCollisionChannelDefinition> CollisionChannels;

	/** Framework-reserved collision channel — always present and NOT stored in the
	 *  CollisionChannels array (so it can't be removed), like the nav "Default"
	 *  layer / vision "Normal" layer. Block-responds by default. */
	static FName GetDefaultCollisionChannelName() { return FName(TEXT("Default")); }

	/** All collision channels: the reserved "Default" first, then the designer-
	 *  authored CollisionChannels (unnamed / duplicate-"Default" entries skipped).
	 *  Use everywhere channels are enumerated (resolver defaults, response matrix,
	 *  Object Type dropdown, debug tint) so "Default" is always available even when
	 *  the editable array is empty. */
	TArray<FSeinCollisionChannelDefinition> GetAllCollisionChannels() const;

	/** Mass-ratio cutoff for Block separation. When two Movable colliders overlap
	 *  and the heavier one's Mass is at least this many times the lighter one's,
	 *  the heavier is treated as IMMOVABLE for that pair — the lighter body absorbs
	 *  the entire separation (so infantry can never shove a tank, however many pile
	 *  on). Below the cutoff the push is mass-weighted. Integer ratio (8 = 8:1);
	 *  set very high to effectively disable the cutoff and always mass-weight. */
	UPROPERTY(Config, EditAnywhere, Category = "Collision", meta = (ClampMin = "1"))
	int32 CollisionMassRatioCutoff = 8;

	// Network / Lockstep Settings (DESIGN §TBD — Phase 0 spike)
	// ====================================================================================================

	/**
	 * Master enable for the lockstep network layer. When false, the
	 * `SeinARTSNet` module's USeinNetSubsystem becomes a no-op even if a
	 * NetDriver is present — useful for shipping a single-player build that
	 * still has the module compiled in. Independent of the world's NetMode:
	 * the subsystem additionally requires `World->GetNetMode() != NM_Standalone`
	 * before any RPC traffic flows.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bNetworkingEnabled;

	/**
	 * Network turn rate in Hz. The lockstep layer aggregates player commands
	 * into "turns" at this cadence; sim ticks are unaffected and continue at
	 * `SimulationTickRate`. A turn is `SimulationTickRate / TurnRate` sim
	 * ticks long (must divide evenly — runtime asserts).
	 *
	 * Defaults: 30 Hz sim / 10 Hz turns = 3 sim ticks per turn (~100 ms turn
	 * length). RTS-genre standard. Lower turn rate = lower bandwidth + higher
	 * input latency floor; higher = the inverse.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "60", UIMin = "5", UIMax = "30"))
	int32 TurnRate;

	/**
	 * Input delay measured in turns. Locally captured commands target turn
	 * `current + InputDelayTurns`, hiding network latency by deferring sim
	 * effect. UX target: 200–300 ms (= 2–3 turns at 10 Hz). Players don't
	 * notice the delay if local feedback (audio cue, ground marker, selection
	 * ring) is fired immediately on input — that's the framework's job, not
	 * this setting's.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "10", UIMin = "2", UIMax = "5"))
	int32 InputDelayTurns;

	/**
	 * Maximum simultaneous players a single lockstep session supports. Drives
	 * slot allocation in `USeinNetSubsystem` and bounds the per-turn command
	 * gather. Bumping past 8 needs validation that the host's RPC bandwidth
	 * still fits an Unreal channel — peer-relay traffic scales O(N²).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "16", UIMin = "2", UIMax = "8"))
	int32 MaxPlayers;

	/**
	 * Pluggable relay actor class. Spawned once on the host at session start
	 * (replicated to clients via `bAlwaysRelevant`); carries the
	 * Server_SubmitCommands + Multicast_BroadcastTurn RPCs. Designers can
	 * subclass to layer custom telemetry / encryption / per-game packet
	 * shaping without touching framework code.
	 *
	 * Soft path so SeinARTSCoreEntity stays decoupled from SeinARTSNet (same
	 * pattern as `NavigationClass` / `FogOfWarClass`). Empty path = framework
	 * default `ASeinNetRelay`.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Relay Actor Class",
				MetaClass = "/Script/SeinARTSNet.SeinNetRelay"))
	FSoftClassPath RelayActorClass;

	/**
	 * Periodic determinism gossip enable. When true, every Nth turn
	 * (`DeterminismCheckIntervalTurns`) every client hashes its world state
	 * and sends the digest to the host; the host fans the digest back so
	 * peers can compare. Mismatch → desync alarm broadcast to every peer with
	 * the full per-slot hash table (see USeinNetSubsystem state-hash gossip).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bDeterminismChecksEnabled;

	/**
	 * Cadence of state-hash gossip in turns. Lower = catches desyncs sooner
	 * but more bandwidth + CPU on the hash. 10 turns (= ~1 s at 10 Hz) is a
	 * sane starting point; tighten to 1 during desync hunts.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "300", UIMin = "1", UIMax = "60", EditCondition = "bDeterminismChecksEnabled"))
	int32 DeterminismCheckIntervalTurns;

	// Drop-in / drop-out policy (Phase 4)
	// ----------------------------------------------------------------------------------------------------

	/**
	 * What the server does when a player disconnects mid-match and the
	 * `DroppedToAITakeoverSeconds` grace period expires. See `ESeinSlotDropPolicy`
	 * for the per-mode semantics.
	 *
	 * Default: `BasicAI` — auto-spawn `DefaultAIControllerClass` and register
	 * it with the sim. Framework ships `USeinNullAIController` as a no-op
	 * fallback if no project-specific AI is configured, so the pipeline is
	 * exercised end-to-end even before designers author real strategic AI.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Slot Drop Policy"))
	ESeinSlotDropPolicy SlotDropPolicy;

	/**
	 * AI controller class instantiated for a slot on the `Dropped → AITakeover`
	 * transition when `SlotDropPolicy == BasicAI`. Filtered to `USeinAIController`
	 * subclasses. Empty path falls back to the framework-shipped
	 * `USeinNullAIController` (no-op idle controller — the slot's units stand
	 * still but the registration pipeline is exercised, useful for shaking
	 * out wiring before authoring real AI).
	 *
	 * Soft path because game projects ship their AI in their own module; this
	 * settings module deliberately doesn't depend on game code.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Default AI Controller Class",
				MetaClass = "/Script/SeinARTSCoreEntity.SeinAIController",
				EditCondition = "SlotDropPolicy == ESeinSlotDropPolicy::BasicAI"))
	FSoftClassPath DefaultAIControllerClass;

	/**
	 * Seconds a slot can stay in `Dropped` state before the server transitions
	 * it to `AITakeover` (per `SlotDropPolicy`). The grace period exists for
	 * brief network blips — a stutter under this threshold lets the player's
	 * relay resume submitting without ever flipping to AI.
	 *
	 * Set to 0 for instant AI takeover (useful for testing); higher values give
	 * more reconnect leeway. While the slot is `Dropped` the server is already
	 * injecting empty heartbeats so the lockstep gate doesn't stall, so this
	 * value is purely "how long do we hope the human comes back."
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Dropped → AI Takeover Seconds",
				ClampMin = "0.0", UIMin = "0.0", UIMax = "120.0"))
	double DroppedToAITakeoverSeconds;

	/**
	 * DEBUG ONLY. When nonzero, USeinNetSubsystem uses this exact value as
	 * the lockstep SessionSeed instead of generating a fresh one from wall-
	 * clock at each PIE Play. Lets you reproduce identical sim runs across
	 * PIE sessions for desync investigation — anything PRNG-driven becomes
	 * bit-identical run-to-run, so any remaining variance you see is caused
	 * by something other than the random seed (level data, sim ordering,
	 * float drift, etc.).
	 *
	 * Leave at 0 in production. Each match must have a fresh seed for
	 * variety + replay uniqueness.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Debug",
		meta = (DisplayName = "Debug Fixed Session Seed (0 = random)"))
	int64 DebugFixedSessionSeed;

	// Fog Of War Settings
	// ====================================================================================================

	/**
	 * Active fog-of-war implementation. The framework's reader BPFL, editor
	 * bake button, and cross-module LOS resolver route through this class —
	 * the rest of the plugin is wholly decoupled from fog-of-war semantics.
	 *
	 * Ships with `USeinFogOfWarDefault` as the default: single-layer 2D grid
	 * with symmetric-shadowcast visibility + lampshade height model (CoH
	 * TrueSight behavior). Game teams can subclass or replace entirely without
	 * touching any other framework code — fog is independent of nav.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (DisplayName = "Fog Of War Class", MetaClass = "/Script/SeinARTSFogOfWar.SeinFogOfWar"))
	FSoftClassPath FogOfWarClass;

	/**
	 * Default fog-of-war grid cell edge in world units (per-volume override
	 * supported on `ASeinLevelVolume`). Independent of nav cell size — the
	 * fog grid is typically coarser than nav because vision doesn't need
	 * sub-meter granularity; at bake it snaps to an integer multiple of the
	 * shared level-data grid. Smaller values = crisper fog edges + higher
	 * memory; larger = cheaper stamps + chunkier edges.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War")
	FFixedPoint VisionCellSize;

	/**
	 * Designer-configurable N-layers for the EVNNNNNN cell bitfield. Slot 0 →
	 * bit 2 (N0), slot 5 → bit 7 (N5). Exactly 6 slots, framework-enforced.
	 *
	 * The framework-default "Normal" layer is NOT in this array — it's reserved
	 * as the V bit (bit 1) and always present. These 6 slots are exclusively
	 * for additional channels a game needs beyond generic visibility: Stealth,
	 * Thermal, Radar, DetectorPerception, Acoustic, Infrared, etc. All 6 ship
	 * disabled — opt in by naming + enabling the slots your game uses.
	 *
	 * Layer semantics (per-bit, EVNNNNNN bitfield):
	 *  - A vision stamp emits into the layers set in its `FSeinVisionStamp.LayerMask`
	 *    (V = generic visibility; N0..N5 = these designer slots).
	 *  - An entity is seen on the layers set in its
	 *    `FSeinFogVisibilityComponent.FogVisibilityLayerMask`.
	 *  - Entity E is visible to observer O iff some stamp owned by O covers E's
	 *    cell with a bit also set in E's FogVisibilityLayerMask (the masks
	 *    intersect on a bit O's VisionGroup has lit for that cell).
	 *  (There is no name-based PerceptionLayers/EmissionLayers model — names are
	 *   only for BP queries + the debug viewer; matching is by bit.)
	 *
	 * Renaming a slot is safe. Reordering / inserting mid-array shifts every
	 * higher-slot bit → breaks replays + saves. Only append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, EditFixedSize, Category = "Fog Of War",
		meta = (TitleProperty = "LayerName"))
	TArray<FSeinVisionLayerDefinition> VisionLayers;

	/**
	 * Vision tick cadence in sim-ticks. `VisionTickInterval = N` means vision
	 * recomputes every N-th sim tick (e.g. N=3 at 30 Hz sim → 10 Hz fog).
	 * Higher values = cheaper (vision stamp math runs less often) but more
	 * perceptual latency on units entering/exiting vision. Below ~15 Hz the
	 * latency is imperceptible; above ~5 Hz it starts to feel stuttery. Must
	 * be deterministic across clients, so this is plugin-scoped — never
	 * per-machine.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (ClampMin = "1", ClampMax = "60", UIMin = "1", UIMax = "10"))
	int32 VisionTickInterval;

	/**
	 * Render-side fog tick rate in Hz. Independent of `VisionTickInterval` —
	 * this governs only the debug-viz + UI readback cadence, not sim state.
	 * The sim-tick path is always deterministic; the render path runs on
	 * wall-clock and can lag freely.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "5.0", UIMax = "30.0"))
	float FogRenderTickRate;

	// Debug Visualization Settings
	// ====================================================================================================

	/**
	 * Maximum world-space distance from the active camera at which debug viz
	 * is still rendered. Applies to:
	 *   - Per-entity DrawDebug* sites (active-move path overlays, steering
	 *     vectors, etc.) — culls beyond this distance from the camera the
	 *     local PC owns.
	 *   - Scene-proxy grid cells (nav + fog-of-war debug viz) — culls cells
	 *     beyond this distance from the per-view camera (`View->ViewLocation`).
	 *
	 * Default: 10000 (~100m). Tune up for tactical zoom-out perspectives
	 * (40000+ for 1km² maps) or down to focus debug viz tightly on the active
	 * engagement.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization",
		meta = (ClampMin = "100.0", UIMin = "1000.0", UIMax = "100000.0"))
	float DebugDrawMaxDistance;

	/**
	 * Per-bucket cap on debug-viz draws. Applies to:
	 *   - Per-entity DrawDebug* sites — global per-frame cap on entity
	 *     overlays drawn (closest-to-camera-first selection).
	 *   - Scene-proxy grid cells (nav + fog-of-war) — per-bucket cap on cells
	 *     emitted to the mesh builder (closest-to-camera-first within each
	 *     color bucket: walkable, blocked, blocker buckets, vision buckets).
	 *
	 * Default: 50000. High enough to render a generous in-view grid even on
	 * fine cell sizes (50cm) at typical zoom-out levels, and never bites
	 * at typical per-entity viz counts. Set to a small value (50-200) to
	 * focus debug viz on the immediate area around the camera; set higher
	 * to effectively disable the cap.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization",
		meta = (ClampMin = "1", UIMin = "10", UIMax = "100000"))
	int32 DebugDrawMaxEntities;

	/**
	 * When true, debug-viz candidates outside the active camera view are
	 * culled in addition to the distance check. Two implementations under
	 * the same gate:
	 *   - Per-entity DrawDebug* sites: cone test (dot product + radius),
	 *     intentionally wider than the visible rectangle so glancing-angle
	 *     entities aren't lost.
	 *   - Scene-proxy grid cells: full plane-by-plane frustum test
	 *     (`FConvexVolume::IntersectBox`) per cell against the per-view
	 *     frustum. Tighter than the entity cone — cells just off-screen
	 *     get culled.
	 *
	 * Default: true. Disable if you want debug viz visible from any camera
	 * angle (e.g. minimap-driven debug captures).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization")
	bool bDebugDrawFrustumCullEnabled;

	// Lobby Settings (subcategory of Network — survives map travel via the
	// game-instance subsystems). Everything here is OPTIONAL — the lobby
	// works without any of these set, falling back to framework defaults.
	// ====================================================================================================

	/**
	 * Playable maps available in the lobby's map dropdown. Each entry is a
	 * `(Map, DisplayName, SlotCount, Thumbnail)` tuple. Host picks one, the
	 * lobby resizes its slots to `SlotCount`, and `Sein.Net.StartMatch`
	 * server-travels to `Map`.
	 *
	 * Order matters: the FIRST entry is the default selected on lobby boot.
	 * Designers list maps in the order they want presented in the dropdown.
	 *
	 * Empty array = no map dropdown / no playable maps configured. Lobby
	 * falls back to seeding `MaxPlayers` Open slots, and StartMatch uses
	 * the runtime override (`USeinLobbySubsystem::GameplayMap`) or
	 * `DefaultGameplayMap` setting.
	 *
	 * `SlotCount` is designer-declared per entry (matches the SeinPlayerStart
	 * count in the level) — kept manual to avoid asset-load cost at lobby
	 * boot. Set it to the same value as the level's number of placed
	 * `SeinPlayerStart`s with `PlayerSlot > 0`.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Available Maps", TitleProperty = "DisplayName"))
	TArray<FSeinLobbyMapEntry> AvailableMaps;

	/**
	 * Framework default match-settings extensions. Designers populate this
	 * array with whatever rule structs their game uses
	 * (`FSeinBasicMatchSettings` for common RTS knobs, custom structs
	 * otherwise). At StartMatch the lobby snapshot copies these into
	 * `FSeinMatchSettings::Extensions` alongside the runtime slot manifest.
	 *
	 * Slots are NOT here — they're dynamic runtime state on the lobby
	 * actor (seeded from the selected `AvailableMaps` entry's `SlotCount`,
	 * or `MaxPlayers` if no map is selected). Per-level slot manifest for
	 * PIE-direct comes from `ASeinPlayerStart` actors in the level via
	 * `ASeinPlayerStart::SynthesizeMatchSettingsFromLevel`.
	 *
	 * Framework code does NOT prescribe what's in this array — it's an
	 * `FInstancedStruct` collection the lobby/game mode forwards to the
	 * sim unchanged. Designer BP scripts read entries via
	 * `FindMatchExtension<T>` (see `SeinMatchSettingsBPFL.h`).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Default Match Extensions"))
	TArray<FInstancedStruct> DefaultMatchExtensions;

	/**
	 * Pluggable faction-discovery service. Default impl scans the AssetRegistry
	 * for `USeinFaction` data assets and exposes them to the lobby UI + claim
	 * validation. Designers override to layer in player-designed factions,
	 * modded folders, or network-imported faction definitions.
	 *
	 * Soft path so this module stays decoupled from project code (same pattern
	 * as `NavigationClass` / `FogOfWarClass` / `RelayActorClass`). Empty path
	 * = framework default `USeinFactionService` (AssetRegistry scan only).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Faction Service Class",
				MetaClass = "/Script/SeinARTSCoreEntity.SeinFactionService"))
	FSoftClassPath FactionServiceClass;

	/**
	 * Default gameplay map the host travels to on `Sein.Net.Lobby.StartMatch`
	 * (when travel is requested). The lobby publishes the snapshot first; the
	 * destination map's GameMode reads it from `USeinLobbySubsystem` in
	 * InitGame.
	 *
	 * Two-tier override: `USeinLobbySubsystem::GameplayMap` (runtime, settable
	 * from a Widget BP) takes precedence; this plugin setting is the ship-time
	 * fallback. Empty path = host runs the match in-place (no travel).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Default Gameplay Map"))
	TSoftObjectPtr<UWorld> DefaultGameplayMap;

	/**
	 * Map the local player travels to after `SeinRequestLeaveLobby` disconnects
	 * them from the session. Empty path = no auto-travel (project handles
	 * post-disconnect routing via UE's NetworkFailure delegates).
	 *
	 * Designer use: ship a main-menu map here so leaving a lobby returns to
	 * the front-end UI instead of dropping into a disconnected gameplay map.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Main Menu Map"))
	TSoftObjectPtr<UWorld> MainMenuMap;

	/**
	 * Seconds a disconnected lobby slot stays reserved for its previous
	 * claimant before reverting to a fully Open slot. Lobby-side reconnect
	 * window: a player who drops + rejoins within this grace gets their
	 * original slot/faction back via `FUniqueNetIdRepl` lookup.
	 *
	 * 0 = no grace (slot opens immediately on disconnect; no reconnect path).
	 * Default 60s covers brief network blips + alt-tab freezes; competitive
	 * games often shorten to 15-30s.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Lobby Reconnect Grace (seconds)",
				ClampMin = "0.0", UIMin = "0.0", UIMax = "300.0"))
	float LobbyReconnectGraceSeconds;

	// Cover + Squad extension settings moved to their own UDeveloperSettings
	// pages owned by the extension plugins:
	//   - USeinARTSCoverSettings  (SeinARTSCover module)  → "SeinARTS Cover Extension"
	//   - USeinARTSSquadSettings  (SeinARTSSquad module)  → "SeinARTS Squad Extension"
	// The base framework settings carry no cover/squad fields so the extensions
	// stay fully strippable with no orphaned settings here.

	// Tag Semantics — Auto-Generation of Tags from Asset Names
	// ====================================================================================================

	/**
	 * Master switch for the auto-tag-generation pipeline. When false, the
	 * SeinARTS factories no longer stamp tags on new assets, the asset-rename
	 * hook no longer updates tags, and the Regenerate buttons become no-ops.
	 * Existing `bAutoGeneratedTag` flags on existing assets are preserved
	 * untouched (turning the system back on resumes from where you left off).
	 *
	 * Use this if your team prefers fully manual tag authoring or you want to
	 * disable the system temporarily without uninstalling.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Semantics",
		meta = (DisplayName = "Enable Auto Tag Generation"))
	bool bEnableAutoTagGeneration;

	/**
	 * Root namespace stamped on every auto-generated tag, before the category
	 * and name segments. Default `SeinARTS` produces `SeinARTS.Ability.Move`,
	 * `SeinARTS.Unit.Infantry`, etc. Leave empty for un-namespaced tags
	 * (`Ability.Move`).
	 *
	 * Downstream teams set this to their own project namespace (e.g. `MyGame`)
	 * to produce `MyGame.Ability.Move`. Typically paired with adding custom
	 * prefix mappings (e.g. `MA → Ability`) so the team's own asset-name
	 * conventions feed the same generator.
	 *
	 * Changing this value invalidates all existing auto-generated tags; the
	 * settings panel prompts to regenerate when the field is edited.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Semantics",
		meta = (DisplayName = "Tag Prefix"))
	FString TagPrefix;

	/**
	 * Asset-name-prefix to tag-category-segment mapping table. Asset named
	 * `<Prefix>_<Name>` produces tag `<TagPrefix>.<Category>.<Name>`. Ships
	 * with the SeinARTS conventions:
	 *
	 *   SA  → Ability   (SA_Move          → SeinARTS.Ability.Move)
	 *   SU  → Unit      (SU_Infantry      → SeinARTS.Unit.Infantry)
	 *   SE  → Effect    (SE_Boost         → SeinARTS.Effect.Boost)
	 *   SR  → Research  (SR_VehicleDepot  → SeinARTS.Research.VehicleDepot) — the producible
	 *   ST  → Tech      (ST_VehicleDepot  → SeinARTS.Tech.VehicleDepot)      — the granted player tag
	 *   SBP → Entity    (SBP_Smoke        → SeinARTS.Entity.Smoke)
	 *
	 * SR and ST are paired: the SR_ producible's GrantedTechEffect applies an
	 * effect whose EffectTag/GrantedTags include a corresponding ST_-derived
	 * Tech tag. Designer authoring is straight-line: build SR_VehicleDepot →
	 * effect grants ST_VehicleDepot to player → SA_PlaceVehicleDepot ability's
	 * RequiredPlayerTags includes ST_VehicleDepot → unlock UX completes.
	 *
	 * Add entries to support alternative prefixes (e.g. `MA → Ability` for a
	 * `MyGame.*`-namespaced project that uses MA_ asset names). Multiple
	 * prefixes can map to the same category — they all produce the same tag
	 * category root, which is fine.
	 *
	 * Changing entries invalidates existing auto-generated tags; the settings
	 * panel prompts to regenerate when the array is edited.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Semantics",
		meta = (DisplayName = "Prefix Category Mappings",
				TitleProperty = "AssetPrefix"))
	TArray<FSeinTagPrefixMapping> PrefixCategoryMappings;

	/**
	 * When true, additional underscores in the asset name become tag-hierarchy
	 * separators:
	 *
	 *   SE_Movement_SprintBoost → SeinARTS.Effect.Movement.SprintBoost   (layered)
	 *   SA_Production_BuildBarracks → SeinARTS.Ability.Production.BuildBarracks
	 *   SU_Infantry_Officer → SeinARTS.Unit.Infantry.Officer
	 *
	 * When false, only the prefix split applies; remaining underscores are
	 * preserved as-is in the name segment:
	 *
	 *   SE_Movement_SprintBoost → SeinARTS.Effect.Movement_SprintBoost   (flat)
	 *
	 * Layering lets designers express deep hierarchies via folder-style asset
	 * naming, which is the whole point of `MatchesTag` hierarchy descent.
	 * Defaults to true.
	 *
	 * Changing this value invalidates existing auto-generated tags; the
	 * settings panel prompts to regenerate when the toggle changes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Semantics",
		meta = (DisplayName = "Allow Tag Layering"))
	bool bAllowTagLayering;

	// Editor Settings — Content Browser Factory Visibility
	// ====================================================================================================

#if WITH_EDITORONLY_DATA
	/** If true, the SeinARTS Ability factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Ability in Basic Category"))
	bool bShowAbilityInBasicCategory;

	/** If true, the SeinARTS Component factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Component in Basic Category"))
	bool bShowComponentInBasicCategory;

	/** If true, the SeinARTS Effect factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Effect in Basic Category"))
	bool bShowEffectInBasicCategory;

	/** If true, the SeinARTS Entity Blueprint factory appears in the default
	 *  (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Entity Blueprint in Basic Category"))
	bool bShowEntityInBasicCategory;

	/** If true, the View Model Widget factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show View Model Widget in Basic Category"))
	bool bShowWidgetInBasicCategory;
#endif

	// User Interface
	// ====================================================================================================
	// The UI Toolkit ships with the base framework (not a strippable extension), so its
	// defaults live here under the shared "SeinARTS" page rather than a separate settings
	// page. USeinMinimapViewModel seeds its per-instance properties from these on init;
	// widgets may override any at runtime via the view-model's BlueprintReadWrite properties.

	/** Square edge of the minimap fog-overlay texture, in texels. Higher = finer fog edges
	 *  at higher rebuild cost. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Texture Resolution", ClampMin = "16", ClampMax = "512"))
	int32 MinimapFogTextureResolution = 256;

	/** Cadence (in sim-tick refreshes) between minimap fog-overlay rebuilds. 1 = every tick. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Update Interval", ClampMin = "1"))
	int32 MinimapFogUpdateInterval = 4;

	/** Box-blur radius (texels) applied to the minimap fog overlay to soften the hard
	 *  per-cell edges. 0 = off. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Blur Radius", ClampMin = "0", ClampMax = "8"))
	int32 MinimapFogBlurRadius = 0;

	/** Minimap overlay color for explored-but-not-currently-visible cells (alpha darkens terrain). */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Explored Color"))
	FColor MinimapFogExploredColor = FColor(0, 0, 0, 120);

	/** Minimap overlay color for never-explored cells (alpha near-opaque hides terrain). */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Unexplored Color"))
	FColor MinimapFogUnexploredColor = FColor(2, 2, 4, 255);

	// UDeveloperSettings Interface
	// ====================================================================================================

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
