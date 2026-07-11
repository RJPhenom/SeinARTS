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
#include "Formations/SeinFormation.h"
#include "PluginSettings.generated.h"

class USeinCommandBrokerResolver;
class USeinFaction;
class USeinFactionService;
class USeinAIController;
class USeinFormation;
class UWorld;

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
 *
 * Property DECLARATION order below follows the intended Project Settings category
 * order (Simulation → Level Data → Terrain → Economy → Collision → Navigation → Fog
 * Of War → Network → UI → Debug Visualization → Editor Preferences). That order is
 * additionally pinned explicitly in FSeinARTSCoreSettingsDetails::CustomizeDetails
 * (via SetSortOrder) so the panel reads top-to-bottom the same way regardless of how
 * the engine would otherwise sort categories — keep the two in sync when editing.
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

#if WITH_EDITOR
	/** Live-apply the Simulation|Performance toggles to their backing cvars when edited in the
	 *  Project Settings panel, so designers see the effect without an editor restart. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Push the Parallel Simulation / Parallel Min Batch / Async Pathfinding settings to their
	 *  backing console variables (`Sein.Sim.Parallel` / `Sein.Sim.ParallelMinBatch` /
	 *  `Sein.Sim.AsyncPathfinding`). Called from PostInitProperties (after config load) and on
	 *  editor property change. Sets at `ECVF_SetByProjectSetting` priority so a manual console
	 *  override (e.g. typing `Sein.Sim.Parallel 0` for live A/B determinism testing) still wins. */
	void ApplySimPerformanceCvars() const;

	// Simulation
	// ====================================================================================================

	/**
	 * How many times per second the deterministic simulation advances. Higher is smoother but
	 * costs more CPU, and every client in a match must use the same value. Default 30.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "120", UIMin = "10", UIMax = "60"))
	int32 SimulationTickRate;

	/**
	 * The most simulation ticks the game will run in one rendered frame. This caps catch-up work
	 * so a momentary frame-rate dip can't spiral into an ever-growing backlog of ticks. Default 5.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "30", UIMin = "1", UIMax = "10"))
	int32 MaxTicksPerFrame;

	/**
	 * How many active effects one entity can stack up before a development-build warning fires.
	 * The warning logs once each time an entity crosses this count from below — a cheap tripwire
	 * for a runaway effect-apply loop while authoring. It compiles out of shipping builds entirely,
	 * so it costs nothing at ship. Default 256.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", UIMin = "32", UIMax = "1024"))
	int32 EffectCountWarningThreshold;

	// Simulation — Performance (deterministic multithreading)
	// ----------------------------------------------------------------------------------------------------
	// These three drive the `Sein.Sim.*` console variables; the console still wins at runtime for
	// live A/B testing. Parallel passes are designed BIT-IDENTICAL to serial, so toggling them is
	// deterministic (verify via Sein.Sim.StateHash.Log). Exception: Async Pathfinding shifts WHEN a
	// path arrives by one tick — see its note; it must match across clients in a match.

	/**
	 * Master switch for spreading the simulation's per-entity work across CPU worker threads. When
	 * on, the parallel passes — local avoidance, the idle-movement driver, nav containment, fog-of-war
	 * stamping, collision (with the Parallel resolver selected), and pathfinding (with Async
	 * Pathfinding on) — run multithreaded. Each parallel pass is built to produce bit-identical
	 * results to the single-threaded path, so it is safe to toggle and even safe for different
	 * machines in one match to differ. Drives the Sein.Sim.Parallel console variable. Default on.
	 * Turn it off to force the whole sim single-threaded — the reference path, and the first thing
	 * to try when hunting a desync.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation|Performance",
		meta = (DisplayName = "Parallel Simulation"))
	bool bParallelSimulation = true;

	/**
	 * The smallest batch of entities a pass will bother sending to worker threads. Below this count
	 * the pass just runs single-threaded, because dispatching a few dozen entities to threads costs
	 * more than it saves. Only matters when Parallel Simulation is on. Drives the
	 * Sein.Sim.ParallelMinBatch console variable. Default 64.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation|Performance",
		meta = (DisplayName = "Parallel Min Batch", ClampMin = "1", UIMin = "8", UIMax = "512",
				EditCondition = "bParallelSimulation"))
	int32 ParallelMinBatch = 64;

	/**
	 * Gather path requests and solve them as one deterministic parallel batch the tick after they
	 * are made, instead of solving each inline the moment it is asked for. This is how large-scale
	 * RTS engines pathfind at scale — it lifts the single most expensive sim system off the critical
	 * tick. The path a unit gets is unchanged; the only cost is about one tick (~33 ms at 30 Hz) of
	 * delay before a move order produces its path, which is imperceptible in play. The batch runs in
	 * parallel when Parallel Simulation is on and byte-identically serial when off, so the deferred
	 * timing is the same regardless of that per-machine toggle.
	 *
	 * Sim-affecting and lockstep-critical: because it shifts WHICH tick a unit receives its path on,
	 * every client in a multiplayer match must use the same value — treat it as a build-wide default
	 * like the tick rate, never a per-machine knob. Drives the Sein.Sim.AsyncPathfinding console
	 * variable.
	 *
	 * ON by default (2026-07-06): the async batch now keys each cached result by its request IDENTITY
	 * (destination + agent params, deliberately NOT the per-tick-resampled Start) and rejects any
	 * cached path whose destination no longer matches the live request. So a re-ordered unit never
	 * consumes a prior order's path, a group given one order can't split, and a stale NotFound can't
	 * fail a valid move; a stale pending request self-clears via key-overwrite, and dead/consumed
	 * results via the per-drain reset. The timing is fixed-1-tick-deferred, drained and consumed
	 * within one tick before the StateHash, so it is bit-deterministic across peers: validate via the
	 * Sein.Sim.Parallel 0-vs-1 state-hash gate with async on, then peer/replay agreement.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Async Pathfinding"))
	bool bAsyncPathfinding = true;

	// Level Data
	// ====================================================================================================

	/**
	 * Which level-data substrate the whole game bakes and reads from. This is the shared baked grid
	 * that navigation and fog-of-war both draw their layers out of, so there is one trace of the
	 * level, not one per system. Pick your own subclass to replace the substrate wholesale; nothing
	 * else in the framework needs to change.
	 *
	 * Set this to None to turn the level substrate OFF — WYSIWYG, but heavy: the Bake Level Data button
	 * does nothing, and navigation, baked fog occluders, and the minimap all lose their data. A
	 * one-time on-screen warning fires while it is off (suppress it in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Level Data Class",
				MetaClass = "/Script/SeinARTSLevelData.SeinLevelData"))
	FSoftClassPath LevelDataClass;

	/**
	 * Where baked level-data assets are written. When you press Bake Level Data on a Sein Level
	 * Volume, the result — the shared height field plus every layer's channel (nav, fog of war, and
	 * so on) — is saved here as LevelData_<LevelName> and auto-assigned to every level volume on the
	 * level.
	 *
	 * Default /Game/LevelData/. Use the content-browser picker to choose any folder under any content
	 * mount (/Game/ for project content, /<PluginName>/ for plugin content); it is created if missing.
	 * Baked level data is a regenerable, gitignored build artifact, so re-bake after changing this —
	 * existing bakes do not move themselves.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Level Data Save Folder", ContentDir))
	FDirectoryPath LevelDataSaveFolder;

	/**
	 * Which collision channel the bake's downward ground trace tests against when it samples each
	 * cell's floor height and surface normal. Default Visibility. Point it at a project-specific
	 * "ground" channel if your level geometry does not block Visibility. Re-bake after changing. The
	 * framework default substrate uses this; a custom substrate may read or ignore it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Level Data",
		meta = (DisplayName = "Bake Trace Channel"))
	TEnumAsByte<ECollisionChannel> BakeTraceChannel = ECC_Visibility;

	// Terrain
	// ====================================================================================================

	/**
	 * The palette of terrain types the level bake stamps onto every cell once, for other systems to
	 * read however they like. Navigation turns a type into a movement cost (the Nav Cost field on each
	 * type); the Cover extension turns the same type's tag into a cover quality (a road becomes negative
	 * cover) — so one authored road region drives both faster movement and worse cover with no
	 * double-authoring, and the base framework never learns what cover is.
	 *
	 * Author a type two ways, both of which bake into the same per-cell result: list its physical
	 * materials and the ground trace classifies any cell whose surface uses one of them (paint a
	 * landscape layer or assign a mesh material — native, no special tool); or drop a Sein Terrain
	 * Volume, whose terrain tag and priority override the material-derived type inside its footprint.
	 *
	 * Each cell stores a one-byte index. Index 0 is the reserved implicit "Default" type (cost 1, no
	 * tag) and is NOT in this array — array slot i is stored as index i+1, mirroring how nav layers
	 * reserve bit 0. Reordering or deleting entries only needs a re-bake (the indices live in
	 * regenerable baked data, never in saved games), so append or rename freely.
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

	// Economy
	// ====================================================================================================

	/**
	 * The project-wide list of resources the economy knows about. Each entry declares one resource by
	 * gameplay tag (under SeinARTS.Resource.*) along with its display name, icon, caps, overflow and
	 * spend behaviour, cost direction, and when production deducts it. Factions reference these entries
	 * by tag in their resource kit.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (TitleProperty = "ResourceTag"))
	TArray<FSeinResourceDefinition> ResourceCatalog;

	/**
	 * The list of faction assets that are playable or available for resource-kit lookup. Every faction
	 * a match might use must appear here.
	 *
	 * This is a hand-authored list rather than an auto-scan for determinism: server and client both
	 * read the same registry from config, so the set of factions and their order is identical on every
	 * peer. If the server knew about a faction the clients did not, the clients would look up an empty
	 * resource kit and start the match with different resources — a state-hash desync from the very
	 * first tick.
	 *
	 * Add your faction assets here. An empty list is fine for projects that do not use faction-driven
	 * resource kits — starting resources on the game mode cover the common case.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (DisplayName = "Registered Factions"))
	TArray<TSoftObjectPtr<USeinFaction>> RegisteredFactions;

	// Collision
	// ====================================================================================================

	/**
	 * The project-wide set of collision channels — the object types a collider can be and can respond
	 * to. Each entry names a channel, gives it a default response, and carries the response matrix for
	 * how it reacts to the other channels. This is the framework's own data-driven version of Unreal's
	 * object channels (WorldStatic, Pawn, Vehicle, and so on), kept independent of navigation: a nav
	 * blocker need not be a collider, and a collider need not block nav.
	 *
	 * Colliders reference channels by name, so renaming a channel is safe and reordering is purely
	 * cosmetic (the runtime rebuilds its index layout from this list each session, identically on
	 * every peer). This holds ADDITIONAL channels only — the reserved "Default" channel lives outside
	 * the array, is always present, and cannot be removed (like the nav Default layer or the vision
	 * Normal layer). A collider's object type is a separate axis from its mobility: what it is versus
	 * how it moves.
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

	/**
	 * How lopsided two bodies' masses must be before the heavier one stops yielding at all. When two
	 * movable colliders overlap and the heavier is at least this many times the lighter's mass, the
	 * heavier is treated as immovable for that pair and the lighter body absorbs the whole separation
	 * — so infantry can never shove a tank, no matter how many pile on. Below the ratio, the push is
	 * split by mass. This is a whole-number ratio (8 means 8:1); set it very high to effectively
	 * disable the cutoff and always split by mass.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Collision", meta = (ClampMin = "1"))
	int32 CollisionMassRatioCutoff = 8;

	/**
	 * Which collision resolver separates overlapping bodies and emits overlap events for the whole
	 * game, once per tick. The collision-resolution system just delegates to the class you pick here;
	 * nothing else in the framework cares how it works underneath.
	 *
	 * The default is Sein Collision Resolver (Default), a Gauss-Seidel relaxation that pushes
	 * overlapping solid bodies apart along their shortest-separation axis with mass-weighting, treats
	 * walls and statics as immovable, and holds a body at a barrier rather than shoving it through a
	 * wall or off the grid. The framework also ships Sein Collision Resolver (Parallel), a Jacobi
	 * variant that spreads the work across threads for large unit counts. Pick either, or your own
	 * subclass (impulse-based, position-based dynamics, and so on), without touching any other
	 * framework code. Set this to None to turn collision resolution OFF: solid bodies overlap freely
	 * and overlap events stop firing. A one-time on-screen warning fires while it is off (suppress it
	 * in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Collision",
		meta = (DisplayName = "Collision Resolver Class",
				MetaClass = "/Script/SeinARTSCoreEntity.SeinCollisionResolver"))
	FSoftClassPath CollisionResolverClass;

	// Navigation
	// ====================================================================================================
	// Top-level navigation properties first, then the Avoidance subcategory, then the Formation
	// subcategory (both nested at the bottom of the Navigation section in Project Settings). The
	// movement-mode determinism toggle and the command-broker resolver live here as direct nav
	// properties. (Squad dispatch resolver selection is owned by the SeinARTSSquad extension, per
	// FSeinSquadComponent::DispatchResolverClass — not a base setting.)

	/**
	 * Which navigation system drives pathfinding for the whole game. Movement orders, the level bake,
	 * and order validation all route through the class you pick here; nothing else in the framework
	 * cares how it works underneath.
	 *
	 * The default is the shipped A* navigation: a footprint-aware 2D-grid A* with line-of-sight
	 * smoothing that only routes a unit through cells its body actually fits in — a solid,
	 * general-purpose RTS default. Pick your own subclass instead (custom routing, flow fields for
	 * mass movement, and so on) without touching any other framework code.
	 *
	 * Set this to None to turn navigation OFF — WYSIWYG, but heavy: every Move order fails and the nav
	 * wall-barrier stops blocking (collision can then push units through baked walls), so only do it if
	 * your game supplies its own pathing. A one-time on-screen warning fires while it is off (suppress
	 * it in Editor Preferences). A set-but-unloadable class is treated as a mistake, not off, and falls
	 * back to the shipped A* default with a logged error.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Navigation Class",
				MetaClass = "/Script/SeinARTSNavigation.SeinNavigation"))
	FSoftClassPath NavigationClass;

	/**
	 * How big each navigation grid square is, in world units. The pathfinder works on a 2D grid; pick
	 * this to roughly match your smallest unit's body size.
	 *
	 * Rough guides: an infantry-centric RTS wants about 100
	 * (1 m cells); a massive-unit RTS wants about 800 (8 m cells). Smaller
	 * cells give finer paths but cost more memory and slower bakes; larger cells are coarser but
	 * cheaper. A Sein Level Volume can override this for its own area only (Override Cell Size on the
	 * volume).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	FFixedPoint CellSize;

	/**
	 * The tallest vertical step a unit can climb in one stride, in world units — think "stair height."
	 * At bake time, two adjacent cells whose heights differ by more than this are treated as
	 * disconnected (a wall edge), so units won't path up the step.
	 *
	 * A 30 cm value lets units mount curbs but not walls; about 80 cm lets them climb small ledges;
	 * 200 cm and up effectively disables the step check. Typical is about half the Cell Size. A Sein
	 * Level Volume can override it for its own area.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	FFixedPoint MaxStepHeight;

	/**
	 * Extra navigation layers for the agent/blocker mask, so different unit classes can path through
	 * different obstacles. There are exactly seven slots (slot 0 is bit 1 up to slot 6 as bit 7), a
	 * count the framework enforces.
	 *
	 * The built-in "Default" layer is NOT one of these — it is reserved as bit 0 and is always present.
	 * These seven slots are purely for additional agent classes a game needs beyond generic pathing:
	 * Amphibious, Heavy Vehicle, Friendly Faction, Infantry Only, and the like. All seven ship
	 * disabled; opt in by naming and enabling the slots your game uses.
	 *
	 * Pathing is blocked when an agent's mask and a blocker's mask share any bit. So an amphibious unit
	 * whose mask drops the Default bit and adds an Amphibious bit walks straight through a water blocker
	 * authored as Default-only, while a multi-class agent simply sets several bits.
	 *
	 * Renaming a slot is safe. Reordering or inserting in the middle shifts every higher bit and breaks
	 * replays and saves — only append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, EditFixedSize, Category = "Navigation",
		meta = (TitleProperty = "LayerName"))
	TArray<FSeinNavLayerDefinition> NavLayers;

	/**
	 * How many path searches the planner runs per simulation tick — the planner's speed limit. If more
	 * units ask for paths in one tick than the budget allows, the extras wait a tick and try again, so
	 * a big selection staggers its searches instead of spiking the CPU.
	 *
	 * Default 32 covers typical RTS group sizes with no visible stagger (a 50-unit selection spreads
	 * over about 2 ticks). Lower it (4-8) to hard-cap per-tick path cost on low-spec targets or huge
	 * maps; raise it (128 and up) to effectively disable throttling if you have frequent large
	 * simultaneous moves and have measured the cost is fine. One-off Blueprint queries (Find Path) and
	 * reachability checks bypass the budget — only auto-pathing units are throttled. Lockstep-safe:
	 * consumption order matches across clients.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1", ClampMax = "1024", UIMin = "1", UIMax = "256",
				DisplayName = "Path Requests Per Tick Budget"))
	int32 PathRequestsPerTickBudget;

	/**
	 * Speed-versus-optimality dial for the A* search, as a percent: 100 means "find the shortest path
	 * no matter what," higher means "find a good path faster, I'll accept up to that-much longer." The
	 * search scores cells as f = g + (h * Weight) / 100, so raising Weight biases it harder toward the
	 * goal and expands fewer cells.
	 *
	 * Default 125 keeps paths at most 25% longer than optimal — visually indistinguishable on most
	 * maps, and 5-10x faster than pure A* on obstacle-rich terrain. 100 is pure, always-optimal A*
	 * (slowest); 200 and up is very fast but produces visibly sub-optimal zig-zags. Only used when the
	 * shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "100", ClampMax = "300", UIMin = "100", UIMax = "200",
				DisplayName = "A* Heuristic Weight (%)",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 AStarHeuristicWeightPercent;

	/**
	 * Hard cap on how much work one path search may do — the planner's patience limit. A* explores
	 * cells one at a time; once it has expanded this many it gives up and returns the best partial path
	 * it found (the closest-to-goal cell it reached), the same as it does for a genuinely unreachable
	 * destination.
	 *
	 * Default 10000 covers any legitimate path on a 1 square-km map at 100 cm cells. Raise it (50000
	 * and up) for very large maps or fine grids where long routes need more search; lower it for a
	 * tighter performance bound on huge maps with many unreachable clicks. Only used when the shipped
	 * A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "256", ClampMax = "1000000", UIMin = "1000", UIMax = "100000",
				DisplayName = "A* Max Iterations",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 AStarMaxIterations;

	/**
	 * How close in height a candidate cell must be to count as "the same level" when the planner snaps
	 * a destination onto walkable ground. Clicks (and formation slots) snap to the nearest passable
	 * cell; this is the maximum height difference allowed before the search keeps looking for a
	 * better-matched cell.
	 *
	 * It prevents the "stragglers run off the cliff" bug: click on a raised platform and slots that fan
	 * over the edge would otherwise snap to the floor below — the elevation gate keeps them on the
	 * platform. Default 100 matches the default 100 cm cell. Tighten it (50) for maps with
	 * closely-stacked mezzanine levels you want kept distinct; loosen it (200 and up) for tall step-ups
	 * that should still read as one level. Only used when the shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1.0", ClampMax = "10000.0", UIMin = "10.0", UIMax = "1000.0",
				DisplayName = "Nav Projection Elevation Tolerance",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	FFixedPoint NavProjectionElevationTolerance;

	/**
	 * How far (in cells) the planner searches for a fallback spot when the clicked point isn't directly
	 * walkable — the "walk near here instead" radius. Click a wall, the ocean, or an impassable
	 * mountain and the planner scans outward ring by ring for the nearest walkable cell, giving up past
	 * this distance.
	 *
	 * At radius 30 a click on water sends the unit to the nearest shore within 30 cells; at 5 it fails
	 * if no shore is that close. Default 30 is about 30 m on a 100 cm grid. Raise it for sparse walkable
	 * regions where clicks legitimately need to scan far; lower it if you'd rather clicks past the
	 * playable area fail fast than "walk to the nearest land." Cost scales with the radius squared, so
	 * keep large values in check. Only used when the shipped A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1", ClampMax = "200", UIMin = "5", UIMax = "60",
				DisplayName = "Nav Projection Max Ring Radius (cells)",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 NavProjectionMaxRingRadius;

	/**
	 * Smallest connected walkable region the nav bake keeps, in cells. After baking, the A* nav floods
	 * the grid into connected walkable regions and discards any region smaller than this — junk islands
	 * like wall tops, cube tops, and slivers of floating geometry units can't reach anyway.
	 *
	 * Raise it to prune larger stray patches (e.g. a 5x5 rooftop you don't want pathable); lower it
	 * toward 1 to keep small legitimate platforms. Re-bake after changing. Only used when the shipped
	 * A*-family nav is selected.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (ClampMin = "1", ClampMax = "1024", UIMin = "1", UIMax = "256",
				DisplayName = "Nav Min Walkable Island (cells)",
				EditCondition = "IsUsingShippedAStar",
				EditConditionHides))
	int32 NavMinWalkableIslandCells;

	/**
	 * Which resolver every spawned command broker uses to decide how a group order fans out to its
	 * units and where each unit is sent. The framework default filters by unit capability and lays
	 * units out on a uniform grid; override it to ship a project-wide dispatch policy — tight ranks,
	 * class clusters, wedge formations, and the like.
	 *
	 * This is a soft class path, so it can name a class in an optional extension without a hard
	 * dependency. This project's config points it at the Cover extension's cover-aware resolver, which
	 * snaps loose units into nearby cover on a move order; strip the cover module and it simply falls
	 * back to the plain default. Set this to None to turn loose-unit group dispatch OFF: a right-click
	 * on a multi-unit selection no longer fans the order out to members. Single units and squads are
	 * unaffected — squads use their own dispatch setting. A one-time on-screen warning fires while it
	 * is off (suppress it in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Default Broker Resolver Class"))
	TSoftClassPtr<USeinCommandBrokerResolver> DefaultBrokerResolverClass;

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

	// Navigation — Avoidance (a Navigation SUBCATEGORY)
	// ----------------------------------------------------------------------------------------------------
	// The MODEL is pluggable (AvoidanceClass below); the tuning constants that follow are the shipped
	// USeinAvoidanceDefault model's "feel". Per-UNIT dials — AvoidanceStrength / AvoidanceWeight /
	// bAvoidSameWeights — live on FSeinMovementComponent. Defaults equal the former inline literals,
	// so motion is unchanged until tuned. All fixed-point → bit-identical.

	/**
	 * Which local-avoidance model runs the soft per-tick steering that keeps moving units from crowding
	 * into each other. The avoidance system delegates to the class you pick here; nothing else in the
	 * framework cares how it works underneath. The shipped default is a lateral-steer boids model.
	 * Pick your own subclass instead — a different flocking model, a
	 * flow-field follower's separation pass, and so on — without touching any other framework code.
	 * Set this to None to turn avoidance OFF: units still move and collide, they just stop softly
	 * flowing around each other, so crowds grind at chokepoints. A one-time on-screen warning fires
	 * while it is off (suppress it in Editor Preferences). (This is soft steering, not ORCA-style velocity
	 * obstacles, which do not scale to RTS unit counts.)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Avoidance Class",
				MetaClass = "/Script/SeinARTSMovement.SeinAvoidance"))
	FSoftClassPath AvoidanceClass;

	/** How far ahead in time a moving unit looks for neighbours to steer around. It perceives others
	 *  out to twice its footprint plus its speed times this many seconds, so faster units watch farther
	 *  ahead. Default 0.5 seconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Lookahead Seconds", ClampMin = "0.0"))
	FFixedPoint AvoidanceLookaheadSeconds = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** The speed, in world units per second, at or below which a unit counts as stopped and skips
	 *  avoidance steering entirely. Default 10. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Moving Speed Floor", ClampMin = "0.0"))
	FFixedPoint AvoidanceMovingSpeedFloor = FFixedPoint::FromInt(10);

	/** How far a neighbour's influence reaches, measured in multiples of the two units' combined
	 *  footprint. Steering fades to zero by this multiple. Default 5. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Falloff Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceFalloffRadii = FFixedPoint::FromInt(5);

	/** How much of last tick's steering direction is carried into this tick, from 0 to 1, to keep
	 *  motion smooth instead of jittery. Higher is smoother but slower to react. Default 0.7. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Smooth Keep", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceSmoothKeep = FFixedPoint::FromInt(7) / FFixedPoint::FromInt(10);

	/** The minimum weight given to a neighbour that is not coming head-on. A unit moving with the flow
	 *  or crossing perpendicular still counts at least this much, so it is never ignored entirely.
	 *  Default 0.1. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Head-On Base", ClampMin = "0.0"))
	FFixedPoint AvoidanceHeadOnBase = FFixedPoint::One / FFixedPoint::FromInt(10);

	/** How close to its goal a unit stops avoidance-steering, measured in footprints. Inside this
	 *  radius the collision resolver and path attraction take over the final approach, so units settle
	 *  onto their destination instead of shuffling. Default 3. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Arrival Release Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceArrivalReleaseRadii = FFixedPoint::FromInt(3);

	/** The cap on how strong the accumulated sideways nudge can get before per-unit strength scaling
	 *  and smoothing are applied. Keeps a crowded unit from being shoved sideways too hard in one tick.
	 *  Default 2. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Max Steer Magnitude", ClampMin = "0.0"))
	FFixedPoint AvoidanceMaxSteerMagnitude = FFixedPoint::FromInt(2);

	/** The tightest avoidance may bend a unit's heading away from the straight line to its current
	 *  goal, given as the cosine of the maximum bend angle.
	 *
	 *  After steering, the bent heading is clamped so it never points more than this far off the goal
	 *  bearing. That keeps a unit always making some forward progress toward its goal, which is what
	 *  stops it circling a churning crowd forever (an orbit needs the unit to spend part of each loop
	 *  heading away from its goal - this cap forbids that). Given as a cosine so it needs no trig:
	 *  1 = no bend allowed (pure goal-seek), 0 = up to 90 degrees, -1 = OFF (any bend, including
	 *  backward). Default about 80 degrees - wide enough for a clean slide-past, tight enough to break
	 *  melee orbits. Set to -1 to disable (bit-identical to no cap). Only the side choice is preserved;
	 *  this never flips which way a unit dodges. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Bend Cap (Cos)", ClampMin = "-1.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceBendCapCos = FFixedPoint::FromInt(17) / FFixedPoint::FromInt(100);

	/** How firmly a MOVING unit steers around a stationary unit that's in its way.
	 *
	 *  0 (default) = the mover plows straight through parked units and lets the collision layer shove
	 *  them aside. Above 0, a mover instead weaves around an idle unit whose Avoidance Weight qualifies
	 *  (heavier-or-equal) - so moving infantry route around an idle tank, while a moving tank still
	 *  plows through idle infantry (the lighter idler is ignored). Higher = firmer weave. The mover
	 *  can't circle the parked unit - the Bend Cap guarantees it keeps making forward progress. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Idle Resolve Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceIdleResolveStrength = FFixedPoint::Zero;

	/** How strongly an IDLE unit steps aside for an approaching mover that's about to run it over.
	 *
	 *  0 (default) = idle units never move on their own (a mover plows through them, the collision
	 *  layer shoves them). Above 0, an idle unit shuffles sideways to make a lane for an approaching
	 *  mover whose Avoidance Weight qualifies (heavier-or-equal) - so idle infantry step aside for a
	 *  passing tank, while an idle tank holds its ground for passing infantry. Only active while Idle
	 *  Re-Seek is on: the re-form owns walking the unit back to its slot once the mover has passed. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Idle Dodge Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceIdleDodgeStrength = FFixedPoint::Zero;

	/** How fast (world units per second) a dodging idle unit shuffles aside.
	 *
	 *  Keep it slow so a step-aside reads as a shuffle, not a sprint, and the collision layer resolves
	 *  any minor idler-into-idler overlap gracefully. Inert while Idle Dodge Strength is 0. Default 40. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Idle Dodge Step Speed", ClampMin = "0.0"))
	FFixedPoint AvoidanceIdleDodgeStepSpeed = FFixedPoint::FromInt(40);

	/** How hard a unit brakes when it is swerving hard through traffic. 0 = never brake.
	 *
	 *  Yield-by-slowing, layered on top of yield-by-turning: steering saturation is the congestion
	 *  signal, and at full saturation the unit's cruise speed is multiplied by (1 - this), scaling
	 *  linearly in between. Applies only while a unit is actively avoiding; a clear unit always runs
	 *  full cruise. Default 0.25 - a unit swerving at its steering cap drops to 75 percent cruise,
	 *  so congestion reads as units slowing into the weave instead of sliding through at full tilt. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Brake Strength", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceBrakeStrength = FFixedPoint::One / FFixedPoint::FromInt(4);

	/** How much a unit that has pulled ahead of its formation slows down so the group stays
	 *  together. 0 = leaders never wait.
	 *
	 *  At full deviation ahead of the group's mean progress, the front-runner's cruise speed is
	 *  multiplied by (1 - this). The group is the unit's command broker (its formation); lone units
	 *  are unaffected. Pairs with Cohesion Catch-Up Boost (the behind-the-group side) and Cohesion
	 *  Range (how much lag counts as full deviation). Default 0.5 - front-runners ease to half
	 *  speed until the group catches up. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Cohesion Hold-Back", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceCohesionHoldBack = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** How much a unit that has fallen behind its formation speeds up to close the gap.
	 *  1 = laggards never hurry.
	 *
	 *  The cruise multiplier a lagging member ramps toward at full deviation behind the group's
	 *  mean progress. Values above 1 push a unit past its authored top speed - how a movement mode
	 *  physically honors that is the mode's own policy. Pairs with Cohesion Hold-Back and Cohesion
	 *  Range. Default 2 - stragglers sprint at up to double cruise to rejoin their formation. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Cohesion Catch-Up Boost", ClampMin = "1.0"))
	FFixedPoint AvoidanceCohesionCatchUpBoost = FFixedPoint::FromInt(2);

	/** How strung out a formation must get, measured in bodies, before catch-up and hold-back
	 *  reach full strength.
	 *
	 *  Multiples of a unit's footprint radius, so the response is the same on a short hop and a
	 *  long march. A member starts reacting once it deviates from the group's mean progress by
	 *  about 15 percent of this range and responds fully at the whole range. Default 8 - a unit
	 *  with a 50 cm footprint starts reacting around 60 cm of lag and responds fully at 400 cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Cohesion Range (Footprints)", ClampMin = "1.0"))
	FFixedPoint AvoidanceCohesionRangeRadii = FFixedPoint::FromInt(8);


	/** How strongly two units on a genuine crossing course slide past each other.
	 *
	 *  When two movers are heading opposite ways and their goals are on opposite sides, they pick
	 *  opposite sides and curve past instead of running into each other or circling. This scales
	 *  that sideways slide. 0 turns the crossing slide-past off entirely (units fall back to the
	 *  basic side-step, which is what causes the head-on lock and orbiting). Default 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Do-Si-Do Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceDoSiDoStrength = FFixedPoint::One;

	/** How far apart two units' goals must be before the engine treats them as genuinely crossing.
	 *
	 *  Measured as a multiple of how far apart the two units currently are. Higher means the slide-
	 *  past only kicks in for units that really are trading places, so a crowd converging on one
	 *  spot still packs tightly instead of shoving sideways. Lower makes units more eager to treat a
	 *  near-miss as a crossing. Combined with the opposite-directions test. Default 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Avoidance",
		meta = (DisplayName = "Crossing Goal Divergence", ClampMin = "0.0"))
	FFixedPoint AvoidanceCrossingGoalDivergence = FFixedPoint::One;

	/** Whether units turn to face their formation's direction after arriving on a slot.
	 *
	 *  When on, a unit delivered to a formation slot rotates at its own Turn Rate to the slot's
	 *  facing while it settles, so an arrived formation ends up facing formation-forward instead
	 *  of frozen at each unit's last travel heading. Applies only to slot-delivered ground moves
	 *  (lone moves, entity-targeted orders, and squad-authored dispatches keep the travel heading);
	 *  movement classes that never rotate are exempt. Off = every unit keeps its travel heading. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Settle To Formation Facing"))
	bool bSettleToFormationFacing = true;

	/** Whether idle formations automatically re-form after being shoved apart. Off by default.
	 *
	 *  When on, a formation whose order queue is empty and whose members are all idle and settled
	 *  checks about twice a second whether anyone has been displaced more than the Re-Seek
	 *  Displacement Threshold from the formation's settled slots (collision shoves, passing
	 *  traffic, and the like). If so, the formation issues ONE internal ground order to re-fill
	 *  its own slots - members are re-matched to slots so the re-form crosses as little as
	 *  possible, not returned to their exact old spots. Applies to formations dispatched by the
	 *  default broker resolver; squad-authored dispatches are unaffected. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Idle Re-Seek"))
	bool bIdleReseek = false;

	/** How far a settled unit must be pushed off its formation before the formation re-forms.
	 *
	 *  World units, measured to the unit's ASSIGNED slot after members are re-matched to slots.
	 *  Must comfortably exceed the arrival acceptance radius plus ordinary collision-settle
	 *  jitter, or formations re-form forever. A structural floor enforces this at runtime: the
	 *  effective trigger is never less than twice a unit's arrival acceptance, so a too-low value
	 *  here is quietly raised rather than causing an endless shuffle. Default 150. Only read while
	 *  Idle Re-Seek is on. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Re-Seek Displacement Threshold", ClampMin = "0.0"))
	FFixedPoint ReseekDisplacementThreshold = FFixedPoint::FromInt(150);

	/** How often an idle formation checks whether it has been shoved apart, in seconds.
	 *
	 *  The cold watch cadence: while nothing is displaced and no re-form is running, each
	 *  formation scans at this interval. Larger = cheaper and slower to notice displacement;
	 *  smaller = snappier. Converted to whole sim ticks (minimum one tick). Default 0.5.
	 *  Only read while Idle Re-Seek is on. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Re-Seek Watch Interval", ClampMin = "0.0"))
	FFixedPoint ReseekWatchInterval = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** How often soldiers are released during an active re-form, in seconds. 0 = every sim tick.
	 *
	 *  The hot cadence: while a re-form episode is running, displaced soldiers whose personal
	 *  stagger delay has matured and whose path home is clear of traffic release at this
	 *  sampling rate. Coarser values quantize releases into visible waves (soldiers whose
	 *  gates opened in the same window fire together); 0 samples every tick so each soldier
	 *  releases the moment its own gates open - the most organic setting. Default 0.
	 *  Only read while Idle Re-Seek is on. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Re-Seek Release Interval", ClampMin = "0.0"))
	FFixedPoint ReseekReleaseInterval = FFixedPoint::Zero;

	/** Safety cap on how long one re-form episode may run before it gives up, in seconds.
	 *
	 *  A dense crowd can enter a slow shuffle where each unit's return home nudges a neighbour off
	 *  its slot, which returns and nudges the next - a loop that never fully settles on its own.
	 *  When an episode has run this long without finishing, the formation is declared good-enough:
	 *  it stops re-forming and rests where it is, then waits a moment before checking again. This
	 *  is a backstop, not the primary defence (the displacement floor above is), so it should
	 *  rarely fire. 0 disables the cap (not recommended). Default 4. Only read while Idle Re-Seek
	 *  is on. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Re-Seek Max Episode Seconds", ClampMin = "0.0"))
	FFixedPoint ReseekMaxEpisodeSeconds = FFixedPoint::FromInt(4);

	// Navigation — Formation (a Navigation SUBCATEGORY, nested below Avoidance)
	// ----------------------------------------------------------------------------------------------------
	// The order-formation system: the shape a selection forms for a move. The drag gesture and a plain
	// click both resolve through the command broker resolver to a USeinFormation.

	/** Whether a plain (non-drag) right-click spreads the selection into the Default Formation at the
	 *  cursor — with the destination preview showing it — instead of every unit converging on the
	 *  single clicked point. Off by default, which is the classic single-destination click. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Formation",
		meta = (DisplayName = "Enable Single-Click Formations"))
	bool bEnableSingleClickFormations = false;

	/** Master switch for the on-ground destination preview. Off, the preview subsystem still exists
	 *  and answers queries (for example cover snapping) but draws nothing. */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Formation",
		meta = (DisplayName = "Enable Formation Preview"))
	bool bEnableFormationPreview = true;

	/** Which actor the preview subsystem spawns to draw the per-unit destination markers. The framework
	 *  default uses flat mesh quads that stay crisp under temporal anti-aliasing. Two alternatives ship:
	 *  a decal version that conforms to terrain but smears under TAA while you drag, and an
	 *  instanced-mesh version that draws a whole formation in one call and scales to huge selections.
	 *  Subclass any of them in Blueprint to restyle the look (preview mesh, material, quality tints), or
	 *  override the element hooks for a fully custom backend. Set it to None to turn the destination
	 *  preview render OFF: no on-ground markers are drawn (a one-time on-screen note appears while off,
	 *  suppress it in Editor Preferences). */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Formation",
		meta = (DisplayName = "Formation Preview Actor Class",
				MetaClass = "/Script/SeinARTSFramework.SeinFormationPreviewActor"))
	FSoftClassPath FormationPreviewActorClass;

	/** The formation a move order uses when it does not name one itself. It drives both the
	 *  right-click-drag default and, when Enable Single-Click Formations is on, the plain-click
	 *  formation. Default is Box. Squads ignore this and lay out their own authored slots. Box wants
	 *  the width you get from a drag, so on a plain click it forms a roughly square block — pick Grid,
	 *  Ring, Wedge, or Square if you want a wider single-click spread. Set it to None for no default
	 *  shape: loose move orders then arrive as a blob (everyone converges on the click point), a valid
	 *  mode; a one-time on-screen note appears while it is None (suppress it in Editor Preferences). */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Formation",
		meta = (DisplayName = "Default Formation"))
	TSubclassOf<USeinFormation> DefaultFormation;

	// Fog Of War
	// ====================================================================================================

	/**
	 * Which fog-of-war implementation the game uses. The fog reader library, the editor bake button,
	 * and the cross-module line-of-sight resolver all route through this class; the rest of the
	 * framework knows nothing about how fog works.
	 *
	 * The default is a single-layer 2D grid with symmetric-shadowcast visibility and a height-aware
	 * true-line-of-sight model (elevation-aware true-sight). Subclass it or replace it entirely
	 * without touching any other framework code — fog is independent of navigation.
	 *
	 * Set this to None to turn fog OFF: nothing is hidden and every unit is always visible. A one-time
	 * on-screen warning fires while it is off (suppress it in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (DisplayName = "Fog Of War Class", MetaClass = "/Script/SeinARTSFogOfWar.SeinFogOfWar"))
	FSoftClassPath FogOfWarClass;

	/**
	 * How big each fog-of-war grid cell is, in world units (a Sein Level Volume can override it for its
	 * own area). This is independent of the nav cell size — the fog grid is usually coarser, because
	 * vision does not need sub-metre detail, and at bake time it snaps to a whole-number multiple of the
	 * shared level grid. Smaller cells give crisper fog edges at higher memory cost; larger cells are
	 * cheaper to stamp but chunkier.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War")
	FFixedPoint VisionCellSize;

	/**
	 * Extra vision channels beyond plain sightlines, so a game can model things like thermal or radar
	 * detection separately. There are exactly six slots, a count the framework enforces.
	 *
	 * The built-in "Normal" visibility layer is NOT one of these — it is always present. These six
	 * slots are for additional channels a game needs: Stealth, Thermal, Radar, Detector, Acoustic,
	 * Infrared, and the like. All six ship disabled; opt in by naming and enabling the slots you use.
	 *
	 * How the layers match up: a vision stamp emits onto whichever layers its mask sets (plain
	 * visibility, plus any of these six); an entity is seen on whichever layers its own visibility mask
	 * sets; and an entity is visible to an observer when one of that observer's stamps covers the
	 * entity's cell on a layer both masks share. In other words, matching is by bit, not by name — the
	 * names are only for Blueprint queries and the debug viewer.
	 *
	 * Renaming a slot is safe. Reordering or inserting in the middle shifts every higher bit and breaks
	 * replays and saves — only append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, EditFixedSize, Category = "Fog Of War",
		meta = (TitleProperty = "LayerName"))
	TArray<FSeinVisionLayerDefinition> VisionLayers;

	/**
	 * How often vision is recomputed, in simulation ticks. A value of N means fog updates every Nth
	 * tick — for example 3, at a 30 Hz sim, gives 10 Hz fog. Higher values are cheaper because the
	 * vision math runs less often, at the cost of a little more lag as units enter and leave sight.
	 * Around 15 Hz and up the lag is imperceptible; below about 5 Hz it starts to feel stuttery. Every
	 * client must use the same value, so this is a build-wide setting, never per-machine.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (ClampMin = "1", ClampMax = "60", UIMin = "1", UIMax = "10"))
	int32 VisionTickInterval;

	/**
	 * How often the render-side fog readback updates, in hertz. This is separate from Vision Tick
	 * Interval: it only drives the debug visualization and UI readback, never sim state. The simulation
	 * path stays deterministic; this render path runs on wall-clock time and can lag freely.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fog Of War",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "5.0", UIMax = "30.0"))
	float FogRenderTickRate;

	// Network / Lockstep
	// ====================================================================================================

	/**
	 * Master switch for the lockstep networking layer. Off, the net subsystem does nothing even if a
	 * net driver is present — handy for a single-player build that still has the module compiled in.
	 * This is separate from the world's net mode: the subsystem also requires the world to be networked
	 * (not standalone) before any traffic flows.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bNetworkingEnabled;

	/**
	 * How many lockstep turns happen per second. The network layer batches players' commands into turns
	 * at this cadence; the sim itself keeps ticking at its own rate. One turn lasts the tick rate
	 * divided by this, which must divide evenly (the runtime checks).
	 *
	 * At the defaults — 30 Hz sim and 10 Hz turns — a turn is 3 sim ticks, about 100 ms, the RTS-genre
	 * standard. A lower turn rate means less bandwidth but a higher floor on input latency; a higher
	 * rate trades the other way.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "60", UIMin = "5", UIMax = "30"))
	int32 TurnRate;

	/**
	 * How many turns ahead a local command is scheduled. A command you issue targets the current turn
	 * plus this many, which hides network latency by deferring when it takes effect in the sim. Aim for
	 * about 200-300 ms (2-3 turns at 10 Hz). Players do not feel the delay as long as local feedback —
	 * an audio cue, a ground marker, a selection ring — fires immediately on input, which is the
	 * framework's job, not this setting's.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "10", UIMin = "2", UIMax = "5"))
	int32 InputDelayTurns;

	/**
	 * The most players one lockstep session supports. It sizes the session's player slots and bounds
	 * the per-turn command gather. Going above 8 needs a check that the host's bandwidth still fits an
	 * Unreal channel, since peer-relay traffic grows with the square of the player count.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "16", UIMin = "2", UIMax = "8"))
	int32 MaxPlayers;

	/**
	 * Which relay actor carries the lockstep traffic. One is spawned on the host at session start and
	 * replicated to every client; it holds the RPCs that submit commands to the server and broadcast
	 * each completed turn back out. Subclass it to layer in custom telemetry, encryption, or per-game
	 * packet shaping without touching framework code. Set it to None to turn the relay OFF: no relay
	 * spawns, so lockstep networking can't send or receive commands — only do this if you are not using
	 * the built-in net layer. A one-time on-screen warning fires while off (suppress it in Editor
	 * Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Relay Actor Class",
				MetaClass = "/Script/SeinARTSNet.SeinNetRelay"))
	FSoftClassPath RelayActorClass;

	/**
	 * Whether clients periodically cross-check that their simulations still agree. When on, every few
	 * turns (set by Determinism Check Interval) each client hashes its world state and sends the digest
	 * to the host, which fans the digests back so peers can compare. Any mismatch raises a desync alarm
	 * to every peer with the full per-slot hash table.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bDeterminismChecksEnabled;

	/**
	 * Whether to verify, when a client joins, that its sim-affecting settings match the host's before
	 * the match starts. On (default), a joining client sends a fingerprint of its determinism-relevant
	 * settings (the pluggable class pickers, tick/turn cadence, nav/collision/avoidance tuning, and the
	 * nav/terrain/collision/vision/resource registries); the host compares it to its own and rejects a
	 * mismatched client with a reason rather than letting it silently desync every tick. Render-only,
	 * transport, lobby, editor, and debug settings are excluded, so they may differ per machine. Turn
	 * it off only if you deliberately run clients with divergent sim settings (you will desync).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Check Settings Parity On Join"))
	bool bConfigParityCheckEnabled = true;

	/**
	 * How often the determinism cross-check runs, in turns. Lower catches a desync sooner but spends
	 * more bandwidth and CPU on hashing. 10 turns (about 1 second at 10 Hz) is a sensible baseline;
	 * drop it to 1 while hunting a desync.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "1", ClampMax = "300", UIMin = "1", UIMax = "60", EditCondition = "bDeterminismChecksEnabled"))
	int32 DeterminismCheckIntervalTurns;

	// Drop-in / drop-out policy (Phase 4)
	// ----------------------------------------------------------------------------------------------------

	/**
	 * What the server does with a player's slot when they disconnect mid-match and the grace period
	 * below runs out. Each mode's behaviour is described in the dropdown.
	 *
	 * The default, Auto-Spawn Default AI Controller, instantiates the AI controller set below and
	 * registers it with the sim. The framework ships a no-op controller as the fallback, so the
	 * takeover pipeline runs end-to-end even before you author real strategic AI.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Slot Drop Policy"))
	ESeinSlotDropPolicy SlotDropPolicy;

	/**
	 * Which AI controller takes over a dropped slot when the drop policy is set to auto-spawn AI.
	 * Restricted to Sein AI Controller subclasses. The framework ships a no-op controller (the default)
	 * whose units just stand still but which still exercises the whole takeover-and-registration path —
	 * useful for shaking out the wiring before you have real AI. It is a soft path because games ship
	 * their AI in their own module and this settings module deliberately does not depend on game code.
	 * Set it to None to turn drop-takeover AI OFF: a dropped player's slot gets no controller and its
	 * units simply idle. A one-time on-screen note appears while off (suppress it in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Default AI Controller Class",
				MetaClass = "/Script/SeinARTSCoreEntity.SeinAIController",
				EditCondition = "SlotDropPolicy == ESeinSlotDropPolicy::BasicAI"))
	FSoftClassPath DefaultAIControllerClass;

	/**
	 * How many seconds a dropped slot waits before the server hands it to AI (per the drop policy). The
	 * grace period covers brief network blips — a stutter shorter than this lets the player's relay
	 * resume without ever flipping to AI.
	 *
	 * Set it to 0 for instant takeover (handy for testing); larger values give more room to reconnect.
	 * While a slot is dropped the server is already injecting empty heartbeats so the lockstep gate
	 * never stalls, so this value is purely how long to keep hoping the human comes back.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network",
		meta = (DisplayName = "Dropped → AI Takeover Seconds",
				ClampMin = "0.0", UIMin = "0.0", UIMax = "120.0"))
	double DroppedToAITakeoverSeconds;

	/**
	 * Debug only. When non-zero, the net subsystem uses this exact value as the lockstep session seed
	 * instead of drawing a fresh one from the wall-clock at each Play. That makes every run reproduce
	 * identically, so anything driven by the random number generator becomes bit-for-bit the same run
	 * to run — any variance left over must come from something other than the seed (level data, sim
	 * ordering, float drift, and so on), which is what you want when chasing a desync.
	 *
	 * Leave it at 0 in production; every real match needs a fresh seed for variety and replay
	 * uniqueness.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Debug",
		meta = (DisplayName = "Debug Fixed Session Seed (0 = random)"))
	int64 DebugFixedSessionSeed;

	// Network — Lobby (a Network SUBCATEGORY — survives map travel via the game-instance subsystems).
	// Everything here is OPTIONAL — the lobby works without any of these set, falling back to framework
	// defaults.
	// ----------------------------------------------------------------------------------------------------

	/**
	 * The maps offered in the lobby's map dropdown. Each entry pairs a map with its display name, its
	 * player-slot count, and a thumbnail. The host picks one, the lobby resizes to that slot count, and
	 * starting the match travels everyone to that map.
	 *
	 * Order matters: the first entry is selected when the lobby opens, so list them in the order you
	 * want them shown.
	 *
	 * An empty list means no map dropdown — the lobby just seeds Max Players open slots, and Start Match
	 * uses the runtime override or the Default Gameplay Map below.
	 *
	 * The slot count is declared per entry by hand (to avoid loading the level just to count starts at
	 * lobby boot); set it to the number of Sein Player Starts placed in that level with a player slot
	 * greater than zero.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Available Maps", TitleProperty = "DisplayName"))
	TArray<FSeinLobbyMapEntry> AvailableMaps;

	/**
	 * The default match rules a new match starts with. Fill this with whatever rule structs your game
	 * uses — the built-in basic match settings for common RTS knobs, or your own structs. When a match
	 * starts, the lobby copies these into the match settings alongside the live slot list.
	 *
	 * Player slots are not here; they are dynamic state on the lobby (seeded from the chosen map's slot
	 * count, or Max Players if no map is picked). When you Play a level directly, the slot list is
	 * synthesized from the Sein Player Start actors in that level instead.
	 *
	 * The framework does not care what goes in this array — it is just a collection of structs the lobby
	 * and game mode forward to the sim untouched, which your Blueprint scripts read back by type.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Default Match Extensions"))
	TArray<FInstancedStruct> DefaultMatchExtensions;

	/**
	 * Which service discovers the factions the lobby can offer. The default scans the asset registry
	 * for faction assets and exposes them to the lobby UI and claim validation. Override it to add
	 * player-designed factions, modded folders, or factions imported over the network. The default does
	 * the asset-registry scan only. Set it to None to turn faction discovery OFF: the lobby has no
	 * faction service and offers no factions to pick. A one-time on-screen note appears while off
	 * (suppress it in Editor Preferences).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Faction Service Class",
				MetaClass = "/Script/SeinARTSCoreEntity.SeinFactionService"))
	FSoftClassPath FactionServiceClass;

	/**
	 * The map the host travels everyone to when a lobby match starts, if no map was chosen at runtime.
	 * The lobby publishes its snapshot first, and the destination map's game mode reads it back as the
	 * match begins. A runtime value set from a widget takes precedence over this; this setting is the
	 * ship-time fallback. Leave it empty to run the match in place without travelling.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Default Gameplay Map"))
	TSoftObjectPtr<UWorld> DefaultGameplayMap;

	/**
	 * The map the local player returns to after leaving a lobby. Point it at your front-end menu so
	 * leaving a lobby lands on the main menu instead of a disconnected gameplay map. Leave it empty for
	 * no auto-travel, in which case your project handles post-disconnect routing itself.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Main Menu Map"))
	TSoftObjectPtr<UWorld> MainMenuMap;

	/**
	 * How many seconds a disconnected lobby slot stays reserved for the player who left before it opens
	 * back up. Rejoin within this window and you get your original slot and faction back. 0 means no
	 * grace — the slot opens the instant someone drops. Default 60 seconds covers brief blips and
	 * alt-tab freezes; competitive games often shorten it to 15-30.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network|Lobby",
		meta = (DisplayName = "Lobby Reconnect Grace (seconds)",
				ClampMin = "0.0", UIMin = "0.0", UIMax = "300.0"))
	float LobbyReconnectGraceSeconds;

	// Cover + Squad extension settings live on their own UDeveloperSettings pages owned by the extension
	// plugins (USeinARTSCoverSettings / USeinARTSSquadSettings). The base framework settings carry no
	// cover/squad fields so the extensions stay fully strippable with no orphaned settings here.

	// UI
	// ====================================================================================================
	// The UI Toolkit ships with the base framework (not a strippable extension), so its defaults live
	// here under the shared "SeinARTS" page rather than a separate settings page. USeinMinimapViewModel
	// seeds its per-instance properties from these on init; widgets may override any at runtime via the
	// view-model's BlueprintReadWrite properties.

	/**
	 * How marquee (drag-box) selection measures each unit's on-screen footprint. The HUD projects a
	 * unit's body to the screen and tests that 2D silhouette against the drag rectangle.
	 *
	 * On (default), it tests the unit's actual authored extents — exact box corners, and capsules with
	 * real hemispherical end-caps — so the selection matches the red extents visualizer one to one and
	 * the box grabs exactly what you see.
	 *
	 * Off is the legacy fast path: it approximates each body by the hull of its extent's top and bottom
	 * rings, a flat-capped cylinder. It is cheaper, but under an angled RTS camera the near edge of the
	 * base ring balloons below the unit, so the hull over-covers tall or large capsules and the box can
	 * grab units it never visually touched (worst on big units). Toggle the Sein.Marquee.Debug.Show
	 * overlay to compare the two live.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "UI",
		meta = (DisplayName = "Use Sein Extents For Marquee"))
	bool bUseSeinExtentsForMarquee = true;

	/** The edge length, in texels, of the square texture that holds the minimap's fog overlay. Higher
	 *  gives finer fog edges but costs more to rebuild. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Texture Resolution", ClampMin = "16", ClampMax = "512"))
	int32 MinimapFogTextureResolution = 256;

	/** How many sim-tick refreshes pass between minimap fog rebuilds. 1 rebuilds every tick. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Update Interval", ClampMin = "1"))
	int32 MinimapFogUpdateInterval = 4;

	/** The box-blur radius, in texels, applied to the minimap fog to soften its hard per-cell edges.
	 *  0 turns the blur off. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Blur Radius", ClampMin = "0", ClampMax = "8"))
	int32 MinimapFogBlurRadius = 0;

	/** The minimap overlay colour for cells you have explored but cannot currently see; its alpha
	 *  darkens the terrain underneath. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Explored Color"))
	FColor MinimapFogExploredColor = FColor(0, 0, 0, 120);

	/** The minimap overlay colour for cells you have never explored; its near-opaque alpha hides the
	 *  terrain underneath. */
	UPROPERTY(Config, EditAnywhere, Category = "UI|Minimap", meta = (DisplayName = "Fog Unexplored Color"))
	FColor MinimapFogUnexploredColor = FColor(2, 2, 4, 255);

	// Debug Visualization
	// ====================================================================================================

	/**
	 * How far from the camera, in world units, debug visualization still draws. Beyond this, both the
	 * per-entity overlays (move paths, steering vectors, and the like) and the nav and fog grid-cell
	 * visualizers are culled. Default 10000, about 100 m. Raise it for zoomed-out tactical views (40000
	 * and up on a square-kilometre map) or lower it to focus debug viz tightly on the current fight.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization",
		meta = (ClampMin = "100.0", UIMin = "1000.0", UIMax = "100000.0"))
	float DebugDrawMaxDistance;

	/**
	 * A cap on how many debug-viz elements are drawn, applied per bucket. It limits the per-frame count
	 * of entity overlays and, for the nav and fog grids, the cells emitted in each colour bucket
	 * (walkable, blocked, blockers, vision) — always keeping the ones closest to the camera. Default
	 * 50000, high enough to draw a generous in-view grid even at fine 50 cm cells and never a problem at
	 * typical entity counts. Drop it to something small (50-200) to focus on the area right around the
	 * camera, or raise it to effectively remove the cap.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization",
		meta = (ClampMin = "1", UIMin = "10", UIMax = "100000"))
	int32 DebugDrawMaxEntities;

	/**
	 * Whether debug viz outside the camera's view is culled as well as beyond the distance limit. Entity
	 * overlays use a cone test kept a little wider than the visible rectangle so glancing-angle entities
	 * are not lost; the nav and fog grid cells use a tighter, exact frustum test, so cells just
	 * off-screen are dropped. Default on. Turn it off to keep debug viz visible from any angle — for
	 * example a minimap-driven debug capture.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Visualization")
	bool bDebugDrawFrustumCullEnabled;

	// Editor Preferences
	// ====================================================================================================

	/**
	 * Whether to hide the on-screen warnings that appear when a pluggable system's class-picker is set
	 * to None (any of the navigation, avoidance, collision, fog of war, level data, broker resolver,
	 * default formation, net relay, AI controller, faction service, or formation preview pickers).
	 * Those warnings remind you a system is intentionally off; tick this once you know. They are
	 * development-only and never appear in shipping builds regardless of this setting.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences",
		meta = (DisplayName = "Suppress Disabled-System Warnings"))
	bool bSuppressDisabledSystemWarnings = false;

	// Editor Preferences — Tag Semantics (auto-generation of tags from asset names)
	// ----------------------------------------------------------------------------------------------------

	/**
	 * Master switch for automatically deriving gameplay tags from asset names. Off, the SeinARTS
	 * factories stop stamping tags on new assets, the rename hook stops updating them, and the
	 * Regenerate buttons do nothing. Existing auto-generated-tag flags are left untouched, so turning it
	 * back on resumes where you left off. Use this if your team prefers to author every tag by hand, or
	 * to pause the system without uninstalling it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences",
		meta = (DisplayName = "Enable Auto Tag Generation"))
	bool bEnableAutoTagGeneration;

	/**
	 * The root namespace put in front of every auto-generated tag, before its category and name. The
	 * default, SeinARTS, produces tags like SeinARTS.Ability.Move and SeinARTS.Unit.Infantry; leave it
	 * empty for un-namespaced tags like Ability.Move. Set it to your own project name (say MyGame) to
	 * get MyGame.Ability.Move — usually paired with adding your own prefix mappings below so your
	 * asset-naming feeds the same generator. Changing this invalidates every existing auto-generated
	 * tag, and the panel offers to regenerate them when you edit it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences",
		meta = (DisplayName = "Tag Prefix"))
	FString TagPrefix;

	/**
	 * The table that turns an asset-name prefix into a tag category. An asset named Prefix_Name becomes
	 * the tag TagPrefix.Category.Name. Ships with the SeinARTS conventions:
	 *
	 *   SA  becomes Ability   (SA_Move         becomes SeinARTS.Ability.Move)
	 *   SU  becomes Unit      (SU_Infantry     becomes SeinARTS.Unit.Infantry)
	 *   SE  becomes Effect    (SE_Boost        becomes SeinARTS.Effect.Boost)
	 *   SR  becomes Research  (SR_VehicleDepot becomes SeinARTS.Research.VehicleDepot) — the producible
	 *   ST  becomes Tech      (ST_VehicleDepot becomes SeinARTS.Tech.VehicleDepot)     — the granted player tag
	 *   SBP becomes Entity    (SBP_Smoke       becomes SeinARTS.Entity.Smoke)
	 *
	 * SR and ST are a pair: an SR_ producible grants a tech effect whose tags include the matching ST_
	 * tech tag, so authoring runs in a straight line — build SR_VehicleDepot, its effect grants
	 * ST_VehicleDepot to the player, and an SA_ ability that requires ST_VehicleDepot unlocks.
	 *
	 * Add entries for your own prefixes (say MA becomes Ability for a project that names abilities MA_).
	 * Several prefixes may map to the same category, which is fine. Changing entries invalidates existing
	 * auto-generated tags, and the panel offers to regenerate when you edit the table.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences",
		meta = (DisplayName = "Prefix Category Mappings",
				TitleProperty = "AssetPrefix"))
	TArray<FSeinTagPrefixMapping> PrefixCategoryMappings;

	/**
	 * Whether extra underscores in an asset name become tag-hierarchy separators. On, the name nests:
	 *
	 *   SE_Movement_SprintBoost     becomes SeinARTS.Effect.Movement.SprintBoost
	 *   SA_Production_BuildBarracks becomes SeinARTS.Ability.Production.BuildBarracks
	 *   SU_Infantry_Officer         becomes SeinARTS.Unit.Infantry.Officer
	 *
	 * Off, only the prefix is split and the rest of the underscores stay in the name:
	 *
	 *   SE_Movement_SprintBoost     becomes SeinARTS.Effect.Movement_SprintBoost
	 *
	 * Layering lets you express deep hierarchies through folder-style naming, which is the whole point
	 * of tag hierarchy matching. Default on. Changing it invalidates existing auto-generated tags, and
	 * the panel offers to regenerate when you toggle it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences",
		meta = (DisplayName = "Allow Tag Layering"))
	bool bAllowTagLayering;

	// Editor Preferences — Factory Visibility (Content Browser)
	// ----------------------------------------------------------------------------------------------------

#if WITH_EDITORONLY_DATA
	/** Whether the SeinARTS Ability factory appears in the Content Browser's default (Basic) create menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Ability in Basic Category"))
	bool bShowAbilityInBasicCategory;

	/** Whether the SeinARTS Component factory appears in the Content Browser's default (Basic) create menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Component in Basic Category"))
	bool bShowComponentInBasicCategory;

	/** Whether the SeinARTS Effect factory appears in the Content Browser's default (Basic) create menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Effect in Basic Category"))
	bool bShowEffectInBasicCategory;

	/** Whether the SeinARTS Entity Blueprint factory appears in the Content Browser's default (Basic)
	 *  create menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Entity Blueprint in Basic Category"))
	bool bShowEntityInBasicCategory;

	/** Whether the View Model Widget factory appears in the Content Browser's default (Basic) create menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show View Model Widget in Basic Category"))
	bool bShowWidgetInBasicCategory;
#endif

	// Pluggable-system helpers
	// ====================================================================================================

	/**
	 * Emit a one-time on-screen + log warning that a pluggable system is disabled because its
	 * settings class-picker is None. No-op when Suppress Disabled-System Warnings is set, and compiled
	 * out of shipping builds. Render/editor-side only (touches GEngine on-screen messages) — it reads
	 * no hashed sim state and never feeds back into it. SystemName is the display label (e.g.
	 * "Navigation"); Detail is a short consequence sentence appended after it. bHighSeverity picks the
	 * on-screen colour: red for systems whose off-state breaks a core flow (nav, level data, broker,
	 * net relay), orange for benign "no-X" modes. The log line is deduped per system per session, so
	 * it is safe to call from per-order code (the broker / formation).
	 */
	static void ReportDisabledSystem(const TCHAR* SystemName, const TCHAR* Detail, bool bHighSeverity);

	/**
	 * Deterministic content hash of the SIM-AFFECTING settings — the fields that must be byte-identical
	 * across every client in a lockstep session (the pluggable class pickers, tick/turn cadence, the
	 * nav/collision/avoidance tuning, and the nav/terrain/collision/vision/resource registries). Render,
	 * transport, lobby, editor, and debug settings are excluded so they may differ per machine. Used by
	 * the net layer's opt-in config-parity check at join (see Check Settings Parity On Join). Value-based
	 * and stable across machines/builds (reflection ExportText + FCrc), so two peers with identical
	 * settings produce the same value.
	 *
	 * The value ALSO folds in any EXTENSION settings registered via FSeinConfigFingerprintRegistry
	 * (e.g. the Squad extension's Pace Squads Together / dispatch-resolver class), sorted by stable id
	 * so it stays load-order independent — this is how an extension's sim-affecting settings join the
	 * parity check without the base depending on the extension. NOTE: the value is NOT wire-stable
	 * across framework versions (the field set evolves); a lockstep session already requires matched
	 * builds, so there is nothing to migrate.
	 */
	int32 ComputeConfigFingerprint() const;

	// UDeveloperSettings Interface
	// ====================================================================================================

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
