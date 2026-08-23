---
title: Making a Movement Ability
description: Create a Blueprint ability that moves an entity to a clicked destination using SeinARTS pathfinding and the Move To latent action.
---

This guide creates a movement ability from scratch, wires it into an entity through command mappings, and connects it to an Animation Blueprint so the unit visually responds to its simulation movement state.

## What you will make

- A Blueprintable `USeinAbility` subclass that moves its owner to a target location.
- A command mapping that activates the ability on a right-click ground order.
- An Animation Blueprint that reads deterministic movement state from the simulation.

## Before you begin

- Complete [Creating an Entity Blueprint](/guides/creating-an-entity-blueprint/) or have an existing entity Blueprint based on `ASeinActor`.
- Your entity needs a **Movement Component**, **Navigation Component**, and **Extents Component** in its Component Data. If you are following the [Making an Infantry Unit](/guides/making-an-infantry-unit/) guide, these are already in place.

## 1. Create the ability Blueprint

1. In the Content Browser, right-click an empty area and choose **SeinARTS Ability** from the **SeinARTS** asset category.
2. In **Pick Parent Class for Ability**, choose the base **SeinAbility** class.
3. Name the asset `SA_Move`. The `SA_` prefix auto-generates the ability tag `SeinARTS.Ability.Move`.
4. The asset opens in the Blueprint Editor with **On Activate**, **On Tick**, and **On End** event nodes pre-wired.

## 2. Configure the ability defaults

1. Open `SA_Move` in the Blueprint Editor.
2. In the **Class Defaults** panel, find the **SeinARTS | Ability** section and set:
   - **Ability Name** to `Move`.
   - **Target Type** to `Point`. The ability captures a world location from the player's click.
   - **Is Move Ability** to enabled. This tells the command system this ability satisfies ground-move orders.
3. Leave **Cooldown**, **Resource Cost**, and **Targeting** fields at their defaults. A basic move has no cost, no cooldown, and no range limit.

## 3. Add the Move To latent action

1. Open the **Event Graph**.
2. Find the **On Activate** event node. This fires once when the ability starts.
3. From **On Activate**, drag out and search for **Move To**. Place the `SeinARTS > Movement > Move To` async node.
4. The **Ability** pin auto-fills with `self`. Connect the **Destination** pin to **Get Target Location**, which returns the point the player clicked.
5. Connect the output execution pins:
   - **On Completed** — the entity reached its destination. Call **End Ability** here.
   - **On Failed** — pathfinding failed or the entity was destroyed. Call **End Ability** here as well.
   - **On Cancelled** — another ability or order interrupted the move. The system handles cancellation, so leave this unconnected or use it for cleanup effects.

The **On Waypoint Reached** and **On Path Recomputed** pins are informational. You can leave them unconnected for a basic move.

### What the Move To node does

Move To is a latent action. It requests a path from the navigation system, drives the entity through its movement instance each sim tick, and fires completion pins when the entity arrives or fails. The ability stays active until one of these pins fires and you call End Ability.

## 4. Handle the On Tick event (optional)

For a basic movement ability, **On Tick** can remain empty. The Move To latent action drives movement internally. Use On Tick only if you need per-tick logic such as updating a visual indicator or checking a custom abort condition.

## 5. Add the ability to an entity

1. Open your entity Blueprint (for example, `SU_Infantry_Officer`).
2. Select the **SeinARTS Entity Bridge** and expand **Component Data**.
3. Find or add an **Ability Component** entry.
4. In the Ability Component, add `SA_Move` to the **Granted Abilities** array.

## 6. Set up the command mapping

Command mappings connect player input contexts to abilities. When the player right-clicks, the framework builds a context tag set describing what was clicked and resolves it against the entity's mappings.

1. Still in the **Ability Component**, find the **Default Commands** array.
2. Add an entry with:
   - **Required Context**: add the tags `SeinARTS.Command.Context.RightClick` and `SeinARTS.Command.Context.Target.Ground`.
   - **Ability Tag**: set to the ability tag of `SA_Move` (auto-generated as `SeinARTS.Ability.Move` from the `SA_` prefix).
   - **Priority**: `0`. Ground movement is typically the lowest priority command so more specific mappings (attack, heal) take precedence.
3. Set **Fallback Ability Tag** to the same `SeinARTS.Ability.Move` tag. This ensures a right-click with no matching context still falls back to movement.

### How command resolution works

On a right-click, the player controller assembles context tags such as `SeinARTS.Command.Context.RightClick`, `SeinARTS.Command.Context.Target.Ground`, `SeinARTS.Command.Context.Target.Enemy`, or `SeinARTS.Command.Context.Target.Friendly`. The ability component walks Default Commands sorted by descending Priority. The first mapping whose Required Context tags are all present in the actual context wins, and that mapping's Ability Tag is activated. If nothing matches, the Fallback Ability Tag fires.

## 7. Connect an Animation Blueprint

The simulation drives movement in fixed-point deterministic space. To animate the unit, the Animation Blueprint reads derived float state from the simulation each frame.

1. Create or open the Animation Blueprint for your entity's Skeletal Mesh.
2. In the **Anim Graph**, use a **Locomotion** state machine or blendspace driven by speed and direction.
3. In the **Event Graph** or an **Anim Node Function**, get the movement state:
   1. Call **Get Owning Actor** to obtain the owning actor.
   2. Cast to **SeinActor**.
   3. Call **Get Entity Handle** on the SeinActor to get the entity's simulation handle.
   4. Call **Get Movement State** (under `SeinARTS > Movement > State`). Pass the entity handle. This returns an `FSeinMovementStateData` struct.
4. Break the struct to access animation-ready values:
   - **Speed** — signed forward speed in units per second. Negative when reversing.
   - **Ground Speed** — unsigned planar speed magnitude. Use this for blendspace axes.
   - **Direction** — angle in degrees from facing to velocity, in the range [-180, 180]. Drives strafe blendspaces.
   - **Is Moving** — true when the absolute speed exceeds 1 cm/s. Use this as a state machine transition condition.
   - **Is Reversing** — true when Speed is negative.
   - **Arrival Imminent** — true when the unit is in its kinematic brake zone, useful for triggering deceleration animations.

### Locomotion blendspace setup

For a simple infantry unit, create a 1D Blendspace with **Ground Speed** on the horizontal axis, blending between idle, walk, and run poses. If your unit can strafe, use a 2D Blendspace with **Direction** and **Ground Speed**.

## Common mistakes

- Forgetting to call **End Ability** on the Completed and Failed pins. The ability stays active forever, blocking other commands.
- Setting Target Type to `Entity` instead of `Point`. Movement targets a location, not another entity. Chase behavior would be a separate ability.
- Placing movement logic in On Tick instead of using the Move To node. Move To manages pathfinding, repaths, and arrival internally.
- Adding a movement ability but not adding a **Movement Component** or **Navigation Component** to the entity's Component Data. The Move To action requires both.
- Casting to a specific actor class in the Animation Blueprint instead of `SeinActor`. All SeinARTS entities share the same actor base class.

## Finished checklist

- The ability asset uses the `SA_` prefix and inherits from `SeinAbility`.
- Target Type is `Point` and Is Move Ability is enabled.
- On Activate calls Move To with the target location.
- On Completed and On Failed both call End Ability.
- The ability is listed in the entity's Granted Abilities.
- A command mapping routes right-click ground orders to this ability's tag.
- The Animation Blueprint reads movement state through Get Entity Handle and Get Movement State.

You now have a working movement ability. The entity pathfinds to the clicked location, animates its locomotion, and frees itself for the next command on arrival.
