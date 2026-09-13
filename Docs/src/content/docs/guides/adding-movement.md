---
title: Add Movement
description: Create a movement ability, map right-click commands, and animate the Soldier from simulation state.
---

Complete [Create the Soldier](/guides/creating-the-soldier/) first. You have a selectable Soldier spawned by the player's start. This chapter gives it a movement ability and a walking animation.

## 1. Add movement and navigation

Open `SU_Soldier`. In the Components panel, add **Sein Movement** and **Sein Navigation**.

Select **Sein Movement**. Set **Movement Class** to **SeinBasicUnitMovement**. This is the framework's basic unit mover. Its starting **Top Speed** is `500` world units per second and **Turn Rate** is `5` radians per second.

Select **Sein Navigation**. Its **Acceptance Radius** starts at `50` world units: the unit can finish its move once it reaches that distance from the destination. The capsule you authored on Sein Extents supplies its body size for navigation clearance.

Compile and save. These components supply movement and pathfinding data. The ability graph you create next tells the unit when and where to move.

## 2. Create the movement ability

In `Content/Demo/Blueprints`, right-click and choose **SeinARTS Ability**. Choose **Generic Ability** in the parent picker. Name it `SA_Move` and open it.

In **Class Defaults**, set:

| Field         | Value |
| ------------- | ----- |
| Ability Name  | Move  |
| Target Type   | Point |

The factory generates **Ability Tag** as `SeinARTS.Ability.Move`. This tag is the identifier that command mappings use to activate the ability. A Point target supplies the destination; the default All dispatch mode sends the order to each eligible selected unit.

Set **Granted Tags** to `SeinARTS.Ability.Move` and **Cancel Abilities With Tag** to `SeinARTS.Ability`. This lets a new Move replace the current activation. Later continuous actions use the same parent tag so Move can interrupt them.

Compile and save.

## 3. Script Move To

Open `SA_Move`'s Event Graph. Use the **Event On Activate** supplied by the factory. This event runs when the simulation activates this ability.

Add the SeinARTS **Move To** node. Connect the event's execution output to it. The node's **Ability** is `self`, the current `SA_Move` instance.

Add a getter for the inherited **Target Location** variable and connect it to **Destination**. Both pins use the framework's Fixed Vector type; no Unreal Vector conversion is needed.

Wire the terminal outputs:

| Move To output | Connection                 |
| -------------- | -------------------------- |
| Completed      | End Ability, target `self` |
| Failed         | End Ability, target `self` |
| Cancelled      | End Ability, target `self` |

You can connect all three outputs to the same **End Ability** node. Leave the waypoint, partial-path, and path-recomputed outputs unconnected for this movement graph; those events report progress rather than ending the move.

```text
Event On Activate
  → Move To (Ability = self, Destination = Target Location)
      Completed → End Ability
      Failed    → End Ability
      Cancelled → End Ability
```

Move To stays active while the unit travels. End Ability releases the ability when that work finishes. Connecting End Ability directly after activation would end the movement before it can run.

Compile and save `SA_Move`.

## 4. Grant the ability to the Soldier

Return to `SU_Soldier` and add **Sein Abilities**. Select it, add one entry to **Granted Abilities**, and choose `SA_Move`.

Select **SeinMovement**. Set **Movement Ability** to `SA_Move`.

This selects the ability used for automatic movement, such as moving to a rally point or into range of a target.

Compile and save the Soldier.

## 5. Map right-click ground commands

Select the Soldier's **Sein Abilities** component. Add one entry to **Default Commands**:

| Field            | Value                                                                              |
| ---------------- | ---------------------------------------------------------------------------------- |
| Required Context | `SeinARTS.Command.Context.RightClick` and `SeinARTS.Command.Context.Target.Ground` |
| Ability Tag      | `SeinARTS.Ability.Move`                                                            |

Add both Required Context tags to the same entry. They must both be present for this mapping to match. Your controller already handles command press and release from the input chapter; the framework supplies these context tags when the player right-clicks ground.

This mapping makes that click activate `SA_Move`.

Compile and save. Start `LVL_DemoSkirmish` in **Play Standalone**, select the spawned Soldier, and right-click reachable ground inside the baked bounds. It should travel toward the point and stop. Right-click another point while it is moving to replace the order. Stop Play before editing the animation Blueprint.

At this point the mesh still uses the idle animation from the previous chapter. The next steps make its pose follow its movement.

## 6. Create the locomotion Blend Space

In `Content/Demo/Animations`, create a **Blend Space** using the imported `SK_Mannequin` skeleton. Name it `BS_Idle_Walk_Run`.

Open it and configure its axes:

| Axis       | Name      | Minimum | Maximum | Grid Divisions |
| ---------- | --------- | ------- | ------- | -------------- |
| Horizontal | Direction | `-180`  | `180`   | `8`            |
| Vertical   | Speed     | `0`     | `600`   | `6`            |

The Third Person content imported in the Soldier chapter includes the animations under `Content/Characters/Mannequins/Anims/Unarmed/Walk` and `Jog`.

Drag the animations into the graph at these sample coordinates. Select each sample to enter its exact Direction and Speed values:

| Direction | Animation at Speed `300`    | Animation at Speed `600`   |
| --------- | --------------------------- | -------------------------- |
| `-180`    | `MF_Unarmed_Walk_Bwd`       | `MF_Unarmed_Jog_Bwd`       |
| `-135`    | `MF_Unarmed_Walk_Bwd_Left`  | `MF_Unarmed_Jog_Bwd_Left`  |
| `-90`     | `MF_Unarmed_Walk_Left`      | `MF_Unarmed_Jog_Left`      |
| `-45`     | `MF_Unarmed_Walk_Fwd_Left`  | `MF_Unarmed_Jog_Fwd_Left`  |
| `0`       | `MF_Unarmed_Walk_Fwd`       | `MF_Unarmed_Jog_Fwd`       |
| `45`      | `MF_Unarmed_Walk_Fwd_Right` | `MF_Unarmed_Jog_Fwd_Right` |
| `90`      | `MF_Unarmed_Walk_Right`     | `MF_Unarmed_Jog_Right`     |
| `135`     | `MF_Unarmed_Walk_Bwd_Right` | `MF_Unarmed_Jog_Bwd_Right` |
| `180`     | `MF_Unarmed_Walk_Bwd`       | `MF_Unarmed_Jog_Bwd`       |

The backward animation appears at both ends because `-180` and `180` represent the same direction. Idle will remain a separate state in the Animation Blueprint. Save the Blend Space.

## 7. Read movement into the Animation Blueprint

Open `ABP_Soldier`. Create three variables:

| Variable    | Type    |
| ----------- | ------- |
| GroundSpeed | Float   |
| Direction   | Float   |
| IsMoving    | Boolean |

In its Event Graph, add **Get Owning Actor** and the SeinARTS **Get Animation Movement State** node. Connect the owning actor to the node's **Actor** input. Break its **Out State** struct to expose **Ground Speed**, **Direction**, and **Is Moving**.

Use **Event Blueprint Update Animation** to execute the three variable setters in sequence. Feed each setter from the corresponding movement-state output:

```text
Get Owning Actor → Get Animation Movement State → Break Movement State
                                                  Ground Speed → GroundSpeed
                                                  Direction    → Direction
                                                  Is Moving    → IsMoving

Event Blueprint Update Animation
  → Set GroundSpeed → Set Direction → Set IsMoving
```

The getter reads the simulation entity behind the owning actor. Its values are zero when no movement state is available, including an unbound editor preview. The Animation Blueprint uses these values for presentation; it does not move the unit or write back to the simulation.

Compile the Animation Blueprint so its variables are available in the AnimGraph.

## 8. Connect idle and locomotion

In the AnimGraph, replace the direct idle-to-Output Pose connection with a **State Machine** named `Locomotion`, connected to **Output Pose**. Open the state machine.

Create two states:

- **Idle:** connect a looping `MM_Idle` sequence player to the state's output pose, as in the previous chapter. Connect Entry to Idle.
- **WalkRun:** add a player for your `BS_Idle_Walk_Run`, enable looping, and connect it to the state's output pose. Connect getters for `Direction` and `GroundSpeed` to its Direction and Speed inputs.

Add a transition from Idle to WalkRun. Open its rule and connect `IsMoving` to **Can Enter Transition**.

Add the return transition from WalkRun to Idle. Its rule is **NOT IsMoving**.

Compile and save `ABP_Soldier`. The Soldier's skeletal mesh already uses this Animation Blueprint, so its Anim Class does not need another assignment.

## 9. Test movement and animation

Save the Blueprints and map, then run one player in **Play Standalone**:

1. Select the Soldier spawned by the Sein Player Start.
2. Right-click clear ground within the playable bounds. It should move and play locomotion.
3. Let it arrive. It should stop and return to idle.
4. Issue another move, then right-click a different reachable point before arrival. The new order should replace the first.
5. Hold Shift and right-click two reachable points. Release Shift; the Soldier should visit them in order.

If a click does nothing, check that the Soldier is selected, `SA_Move` is in Granted Abilities, and the Default Commands entry contains both context tags. If movement starts but cannot find a route, check the level volume's bake and that the start and target lie on navigable ground. If the unit moves but stays in idle, check the Animation Blueprint's movement-state getter, variable setters, and transition rules.

## Checkpoint

- `SU_Soldier` has Sein Movement, Sein Navigation, and Sein Abilities.
- You created `SA_Move`; On Activate calls Move To with Target Location, and every terminal output ends the ability.
- The Soldier grants `SA_Move` and selects it as Movement Ability.
- One Default Commands entry maps right-click ground context to `SeinARTS.Ability.Move`.
- Your locomotion Blend Space uses the imported mannequin animations, and `ABP_Soldier` reads simulation movement state.
- The spawned Soldier moves, changes destination, follows queued orders, and returns to idle on arrival.

Continue with [Add Combat](/guides/adding-combat/).
