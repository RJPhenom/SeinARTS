---
title: Create the Soldier
description: Author the first unit Blueprint, give it a mesh and idle animation, and select it in the demo level.
---

Complete [Create the First Level](/guides/creating-the-first-level/) first. You now have a running scene and camera controls. This chapter adds a Soldier you can see and select. You will give it a movement ability next.

## 1. Create the unit Blueprint

In `Content/Demo/Blueprints`, right-click and choose **SeinARTS Entity Blueprint** from the SeinARTS asset category. Choose **Generic Entity** in the parent picker and name the asset `SU_Soldier`.

Open it. Its parent is the native **SeinActor**, and it already has an inherited **EntityBridge** component. The bridge connects this actor to its simulation entity. Add the unit's data through the Blueprint's **Components** panel.

Add **Sein Identity**, select it, and enter:

| Field        | Value                 |
| ------------ | --------------------- |
| Display Name | Soldier               |
| Description  | A basic soldier unit. |

The entity factory generates `SeinARTS.Unit.Soldier` from the asset name. This tag identifies the unit type; Display Name is the readable label you will use in the UI.

Compile and save.

## 2. Add the mannequin mesh

Use Unreal's Third Person content for the Soldier's model and animations. In the Content Browser, choose **Add > Add Feature or Content Pack**, select **Third Person** in the Blueprint category, and choose **Add to Project**.

The pack adds its mannequin assets under `Content/Characters/Mannequins`. Your existing demo game mode and controller remain the classes used by `LVL_DemoSkirmish`.

Return to `SU_Soldier`. Add a **Skeletal Mesh** component, name it `SoldierMesh`, and assign `SKM_Manny_Simple` from `Content/Characters/Mannequins/Meshes`.

Set the mesh's relative **Yaw** to `-90`.

This keeps the feet at the actor's origin and aligns the mannequin with the unit's forward direction.

## 3. Create the idle animation Blueprint

Create `Content/Demo/Animations` now. Inside it, create an **Animation Blueprint**, choose the imported mannequin's `SK_Mannequin` skeleton, and name it `ABP_Soldier`.

Open its **AnimGraph**. In the Asset Browser, find `MM_Idle`, drag it into the graph as an animation sequence player, and connect its pose output to **Output Pose**. Select the sequence player and enable **Loop Animation**.

Compile and save `ABP_Soldier`.

Return to `SU_Soldier` and select `SoldierMesh`. Set **Animation Mode** to **Use Animation Blueprint**, then assign `ABP_Soldier` as **Anim Class**. The preview should show the mannequin standing in its idle animation.

This is the starting pose graph. When you add movement, you will extend this Animation Blueprint to read the unit's simulation movement state and animate its locomotion.

## 4. Give the unit a selection shape

In `SU_Soldier`, add **Sein Extents**. Select it and add one entry to **Shapes**. The new entry is a capsule with radius `40` and height `180`.

The capsule describes the unit's body for the framework's spatial queries, including selection. Its Height extends upward from Local Offset Z, so this shape starts at the Soldier's feet.

The default **Unrestricted** selection policy allows normal selection, and **Include in Drag Selection** includes the unit in a marquee. You can select this Soldier without adding an Unreal collision capsule to the actor.

## 5. Add sight

Add **Sein Vision** to the Blueprint. Its **Vision Stamps** array starts with one enabled radial stamp with radius `1000` on the normal sight layer. That stamp makes the Soldier a source of vision for its owning player. You can adjust its radius and shape in **Vision Stamps** as you develop the unit.

Compile and save `SU_Soldier`.

## 6. Set the player's starting entity

Open `LVL_DemoSkirmish` and select the **Sein Player Start** you created earlier. Set **Spawn Entity** to `SU_Soldier`. Its **Player Slot** remains `1`.

Position the start inside the baked playable bounds, with its actor origin at the floor's top surface. If the floor surface is at Z `0`, set the start's Location Z to `0`. The Soldier's feet are at its origin, and the starting entity uses the start's saved transform directly. Use zero rotation and unit scale for this checkpoint.

Save the map. When the match starts, the framework spawns the Soldier at this start and assigns it to Player Slot `1`. The map defines the player's starting presence through **Spawn Entity**; subsequent units will come from gameplay such as production.

## 7. Test selection

Compile and save the unit and animation Blueprints, then save the map.

Run the scene with one player in **Play Standalone**, as in the previous chapter. Pan to the Soldier and confirm that it stands on the floor and plays its idle animation.

To check selection before building a selection display, open the Unreal console and run these two toggle commands once each:

```text
Sein.Commands.ShowLog
Sein.Commands.ShowLog.Observer
```

The first shows the command log overlay. The second includes observer commands, which contain local selection changes.

Close the console, then:

1. Left-click the Soldier. The overlay should report `SelectionChanged` with `1 ents`.
2. Left-click clear ground. It should report `SelectionChanged` with `0 ents`.
3. Drag a marquee around the Soldier and release. Selection should return to `1 ents`.

These commands toggle their settings, so run them again to turn the diagnostics off after the check. The Soldier has no movement ability yet; this chapter's test is selection.

## Checkpoint

- `SU_Soldier` is your own Blueprint derived from SeinActor.
- Its mesh uses the imported mannequin and your `ABP_Soldier` idle graph.
- Sein Extents contains a capsule, and Sein Vision contains a radial stamp.
- The Sein Player Start references `SU_Soldier` as Spawn Entity, and the match spawns it for Player Slot `1` at the start's saved transform.
- Clicking, clearing selection, and marquee selection produce the expected selection counts.

Continue with [Add Movement](/guides/adding-movement/) to author `SA_Move`, connect right-click commands, and animate locomotion.
