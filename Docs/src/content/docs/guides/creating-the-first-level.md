---
title: Create the First Level
description: Start with Unreal's Basic level and add the demo's player start, level-data bake, and initial simulation-content manifest.
---

Complete [Create the Gameplay Classes and Input](/guides/creating-gameplay-classes-and-input/) first. You now have your own game mode, camera, controller, HUD, and input assets. This guide creates their first playable environment. It uses no prebuilt demo map or units.

## 1. Create and save a Basic level

Choose **File > New Level > Basic**. This template supplies a floor and lighting. Save it as `LVL_DemoSkirmish` inside `Content/Demo`.

The asset path is `/Game/Demo/LVL_DemoSkirmish`. Use this new map throughout the guide.

The project should already have **World Settings Class** set to `SeinWorldSettings` and have been restarted. Open **World Settings** and verify:

| Setting | Value |
| --- | --- |
| Game Mode Override | `GMB_DemoSkirmishGameMode`, created in the preceding guide |
| Bootstrap Intent | Automatic Match |
| Auto-Start Sim | Enabled |

If the bootstrap settings are absent, correct the project's World Settings Class before proceeding with this new level.

## 2. Add the local player's start

Delete the template's ordinary **Player Start** if one is present.

Search Place Actors for **Sein Player Start** and add one near the center of the floor. Place it at approximately X `0`, Y `0`, Z `100`, with zero rotation.

Set **Player Slot** to `1`. Leave **Spawn Entity** empty: the scene has no unit Blueprint yet, and the camera pawn comes from the game mode rather than this field.

For this initial local scene, leave **Faction ID** and **Team ID** at their defaults. Player Slot `1` identifies the local player's seat; faction and team are separate values. The current standalone bootstrap can materialize this local player without a faction data asset.

Use exactly one participating Sein Player Start for this checkpoint. Do not add an ordinary Player Start in its place.

Save the map after positioning the start so its deterministic transform is stored.

## 3. Define the playable bounds and bake

Add a **Sein Level Volume** from Place Actors. Resize its box brush to fit inside the template floor's edges, with vertical bounds extending above and below the floor's top surface.

The floor must **block Visibility** so the bake can sample it. Keep the template floor's collision enabled.

With the volume selected, find **Bake** and press **Bake Level Data**. Wait for completion, verify **Baked Asset** is assigned, and save the map.

The unified bake generates the shared world substrate and registered layer data. Its destination is the **Level Data Save Folder** configured in Project Settings, initially `/Game/SeinARTS/LevelData`.

Rebake after changing relevant ground collision, static blockers, playable bounds, or bake settings. The level bake supplies navigation and fog data; it does not create units or input bindings.

## 4. Register the map and generate its manifest

Open **Project Settings > Plugins > SeinARTS**, find **Available Maps**, and add an entry:

| Setting | Value |
| --- | --- |
| Map | Your new `/Game/Demo/LVL_DemoSkirmish` asset |
| Display Name | Demo Skirmish |
| Slot Count | `1` |
| Team Count | `1` |

Slot Count describes the one participating start in this map. Team Count is a lobby UI setting; it does not alter the start's Team ID.

Compile and save all the gameplay Blueprints and Input assets. Finish the level bake and save the map before generating compatibility data.

In the same settings page, find **Manifest Save Folder**. Keep `/Game/SeinARTS` and press **Generate / Regenerate Manifest**. After a successful save, **Simulation Content Manifest** should point to the generated asset.

Resolve any reported missing, dirty, or uncompiled input before continuing. The manifest records saved simulation content and follows the configured map roots. Generating it before adding the gameplay map to Available Maps does not establish coverage for that map.

When you add gameplay assets in subsequent lessons, save them and regenerate the manifest before checking the new state in play.

## 5. Run the scene

In the editor play options, choose **one player** and networking mode **Play Standalone**. Start Play in Editor in the selected viewport, then focus the viewport.

Check:

- The ground is visible from the camera spawned by your game mode.
- W/A/S/D pan the camera.
- Q/E rotate it.
- The mouse wheel and Z/X change zoom distance.
- Dragging with the left mouse button displays a selection marquee.
- Releasing the button finishes the marquee; it does not remain stuck.

There are no units to select or move yet. This checkpoint tests the environment and controls you have actually created.

In the Output Log, check that navigation and fog grids load and the simulation starts. There should be no warning that your gameplay world is absent from the selected Simulation Content profile.

If the scene runs with a missing-coverage warning, revisit Available Maps and regenerate the manifest after saving the map. **Require Simulation Content Coverage** can be disabled while authoring, so a successful start alone does not prove the map was included.

## Checkpoint

Before authoring the Soldier:

- Your new level uses your new game mode and gameplay classes.
- Its ground supports the bake's trace channel.
- A Sein Player Start defines Player Slot `1` without requiring a unit asset.
- The level has an assigned, loadable Baked Asset.
- The saved gameplay map is included in the generated manifest.
- Camera controls and the empty selection marquee work in play.

Resolve a failure at this point before introducing unit configuration and movement behavior.
