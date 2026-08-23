---
title: Setting Up a Playable Level
description: Configure a level with baked navigation, player slots, and the SeinARTS match pipeline so entities can spawn and accept commands.
---

This guide sets up a level so SeinARTS entities can spawn, pathfind, and receive player commands. Without this setup, placed entities have no navigation grid, no player ownership, and no match lifecycle.

## What you will make

- A level with a **Sein Level Volume** that defines the playable area and bakes navigation and fog of war data.
- Player slot assignments for placed entities.
- A working Play in Editor session where you can select units and issue orders.

## Before you begin

- Have at least one entity Blueprint ready. The [Creating an Entity Blueprint](/guides/creating-an-entity-blueprint/) guide covers the minimum, and [Making an Infantry Unit](/guides/making-an-infantry-unit/) produces a fully functional unit.
- Your project should have the SeinARTS Framework plugin enabled with default project settings.

## 1. Create or open a level

1. Create a new level or open an existing one.
2. Add ground geometry — a Landscape, a large Static Mesh plane, or BSP. Entities need a surface to stand on, and the navigation bake reads world collision to determine walkable areas.

## 2. Add a Sein Level Volume

The Sein Level Volume defines the rectangular area that SeinARTS bakes into its deterministic grid. Navigation, fog of war, and terrain data are all derived from this volume.

1. In the **Place Actors** panel, search for **Sein Level Volume**.
2. Drag it into the level.
3. Scale and position the volume so it covers the entire playable area. Entities outside this volume have no baked data.
4. In the Details panel under **SeinARTS**, configure:
   - **Cell Size**: the resolution of the navigation grid in simulation units. Default is `100` (1 meter). Smaller values increase fidelity and bake time.
   - Leave other settings at defaults unless you need specific terrain layers or fog of war configuration.

## 3. Bake level data

1. Select the **Sein Level Volume** in the level.
2. In the Details panel, click the **Bake Level Data** button. This runs the unified bake pipeline.
3. Wait for the bake to complete. The output is a set of generated assets stored under a `LevelData` folder within the level's content directory. These are regenerable build artifacts and are not checked into source control.

The bake reads world collision to produce:

- A navigation grid marking walkable and blocked cells.
- Fog of war layer data if a FoW provider is configured.
- Terrain type stamps if terrain classification is set up.

Re-bake after any change to the level's collision geometry, volume bounds, or cell size.

## 4. Place entities

1. Drag entity Blueprints from the Content Browser into the level.
2. Position and rotate each entity. SeinARTS bakes the placed actor's deterministic transform from the editor position.
3. Select each entity and in the Details panel, find **SeinARTS > Ownership > Player Slot**:
   - `0` — neutral, unowned. The entity belongs to no player.
   - `1` through `N` — player-owned. Assign to the player slot that should control this entity.

For a single-player test, assign your units to **Player Slot 1**.

## 5. Configure the game mode

SeinARTS provides a game mode that bootstraps the match lifecycle, player controller, camera, and HUD.

1. Open **World Settings** for the level (Window > World Settings, or the toolbar button).
2. Under **Game Mode**, set the **Game Mode Override** to your project's SeinARTS game mode class. The framework ships a base `ASeinGameMode` — use it directly or a Blueprint subclass.
3. If you have a custom player controller, camera, or HUD, assign them in the game mode's Class Defaults. The defaults provide a working RTS camera and selection system.

### Default player experience

With the default game mode, player controller, and HUD:

- The camera provides RTS-style panning, rotation, and zoom.
- Left-click selects entities. Drag to box-select.
- Right-click issues the resolved command (move to ground, attack enemy, or whatever the entity's command mappings dictate).
- Selected entities display their health bar and selection indicator.

## 6. Play in Editor

1. Set the PIE settings to **Selected Viewport** or your preferred mode.
2. Click **Play**.
3. Verify:
   - The SeinARTS simulation initializes (check the Output Log for `SeinARTS` category messages).
   - Entities spawn at their placed positions.
   - You can select entities with left-click and box selection.
   - Right-clicking the ground issues a move command (if the entity has a movement ability).
   - Right-clicking an enemy issues an attack command (if the entity has an attack ability).

If entities do not move, check that the level data is baked and the entities have Navigation and Movement components in their Component Data.

## Multiplayer testing

For local multiplayer testing in PIE:

1. In the PIE settings, set **Number of Players** to the desired count.
2. Set **Net Mode** to a multiplayer mode. SeinARTS handles lockstep synchronization through its net subsystem.
3. Assign entities to different player slots to control them from different PIE windows.

SeinARTS's lockstep transport ensures all peers see the same simulation state. Desync detection and state hashing run automatically.

## Common mistakes

- Forgetting to bake level data. Without the bake, there is no navigation grid and pathfinding fails.
- Placing entities outside the Sein Level Volume bounds. These entities have no baked data and cannot pathfind.
- Not setting a Player Slot on owned entities. Slot 0 entities are neutral and not selectable by any player.
- Using a non-SeinARTS game mode. The default Unreal game mode does not initialize the simulation subsystem, player controller, or command pipeline.
- Changing level geometry without re-baking. The navigation grid is stale until you bake again.
- Setting a very small Cell Size on a large level. This dramatically increases bake time and memory. Start with the default and decrease only where you need finer navigation resolution.

## Finished checklist

- The level has a **Sein Level Volume** covering the playable area.
- Level data has been baked (the Bake Level Data button has been run).
- Entity Blueprints are placed in the level within the volume bounds.
- Each owned entity has a non-zero **Player Slot**.
- The level's **Game Mode Override** is set to a SeinARTS game mode.
- Play in Editor shows the simulation running, entities responding to selection, and commands executing.

You now have a playable level. Combine this with [Making an Infantry Unit](/guides/making-an-infantry-unit/) to have units that move, fight, and animate on command.
