---
title: Create the First Level
description: Start with Unreal's Basic level, add the demo's player start, bake level data, and test the camera and selection marquee.
---

Complete [Create the Gameplay Classes and Input](/guides/creating-gameplay-classes-and-input/) first. You now have your own game mode, camera, controller, HUD, and input assets. This guide creates their first playable environment. It uses no prebuilt demo map or units.

## 1. Create and save a Basic level

Choose **File > New Level > Basic**. This template supplies a floor and lighting. Save it as `LVL_DemoSkirmish` inside `Content/Demo`.

The asset path is `/Game/Demo/LVL_DemoSkirmish`. Use this new map throughout the guide.

Open **World Settings** and set **Game Mode Override** to `GMB_DemoSkirmishGameMode`, created in the preceding guide.

## 2. Add the local player's start

Delete the template's ordinary **Player Start** if one is present.

Search Place Actors for **Sein Player Start** and add one near the center of the floor. Position its actor origin at the floor's top surface, with zero rotation and unit scale. For a floor surface at Z `0`, use approximately X `0`, Y `0`, Z `0`.

Set **Player Slot** to `1`. This identifies the local player's seat. The camera pawn comes from the game mode; you will assign a starting unit in the next chapter.

Use exactly one participating Sein Player Start for this checkpoint. Do not add an ordinary Player Start in its place.

Save the map after positioning the start so its deterministic transform is stored.

## 3. Define the playable bounds and bake

Add a **Sein Level Volume** from Place Actors. Resize its box brush to fit inside the template floor's edges, with vertical bounds extending above and below the floor's top surface.

The floor must **block Visibility** so the bake can sample it. Keep the template floor's collision enabled.

With the volume selected, find **Bake** and press **Bake Level Data**. Wait for completion, verify **Baked Asset** is assigned, and save the map.

The unified bake generates the shared world substrate and registered layer data. Its destination is the **Level Data Save Folder** configured in Project Settings, initially `/Game/SeinARTS/LevelData`.

Rebake after changing relevant ground collision, static blockers, playable bounds, or bake settings. The level bake supplies navigation and fog data; it does not create units or input bindings.

## 4. Run the scene

Compile and save your gameplay Blueprints and save the map.

In the editor play options, choose **one player** and networking mode **Play Standalone**. Start Play in Editor in the selected viewport, then focus the viewport.

Check:

- The ground is visible from the camera spawned by your game mode.
- W/A/S/D pan the camera.
- Q/E rotate it.
- The mouse wheel and Z/X change zoom distance.
- Dragging with the left mouse button displays a selection marquee.
- Releasing the button finishes the marquee; it does not remain stuck.

There are no units to select or move yet. This checkpoint tests the environment and controls you have actually created.

In the Output Log, check that navigation and fog grids load and the simulation starts.

## Checkpoint

Before authoring the Soldier:

- Your new level uses your new game mode and gameplay classes.
- Its ground supports the bake's trace channel.
- A Sein Player Start defines Player Slot `1` without requiring a unit asset.
- The level has an assigned, loadable Baked Asset.
- Camera controls and the empty selection marquee work in play.

Resolve a failure at this point before introducing unit configuration and movement behavior.

Continue with [Create the Soldier](/guides/creating-the-soldier/).
