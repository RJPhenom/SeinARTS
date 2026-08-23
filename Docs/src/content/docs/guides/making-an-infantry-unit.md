---
title: Making an Infantry Unit
description: Assemble a complete infantry entity with a skeletal mesh, movement, combat, navigation, and an Animation Blueprint.
---

This guide takes a bare entity Blueprint and turns it into a walking, shooting infantry unit. It builds on the [Creating an Entity Blueprint](/guides/creating-an-entity-blueprint/) guide and references the [Making a Movement Ability](/guides/making-a-movement-ability/) and [Making Combat Abilities](/guides/making-combat-abilities/) guides for the ability details.

## What you will make

- An entity Blueprint with a Skeletal Mesh using the UE5 Mannequin.
- Simulation components for body, navigation, movement, vision, combat, and abilities.
- A command mapping table that routes right-click orders to move or attack.
- An Animation Blueprint driven by deterministic simulation state.

## Before you begin

- Complete [Creating an Entity Blueprint](/guides/creating-an-entity-blueprint/). You should have a compiled entity Blueprint such as `SU_Infantry_Officer` with an Identity Component in its Component Data.
- Have a movement ability (`SA_Move`) from [Making a Movement Ability](/guides/making-a-movement-ability/).
- Have an attack ability (`SA_Attack`) from [Making Combat Abilities](/guides/making-combat-abilities/).
- Import or locate a Skeletal Mesh and animation set for your unit. This guide uses the UE5 Mannequin (`SKM_Manny`) and its locomotion animations.

## 1. Add the visual representation

1. Open your entity Blueprint in the Blueprint Editor.
2. In the Components panel, add a **Skeletal Mesh Component** and attach it to the root.
3. Set its **Skeletal Mesh** to `SKM_Manny` (or your chosen mesh).
4. Assign an **Anim Class** — you will create this Animation Blueprint in step 8.
5. Position and rotate the mesh so it stands on the actor's origin. Infantry typically needs a small downward Z offset so feet meet the ground plane.

This mesh is part of the render layer. Its transform, animation, and material state are purely visual and never feed back into the simulation.

## 2. Add the Extents Component

The Extents Component defines the entity's simulation-side body for collision, navigation baking, and fog of war occlusion.

1. Select the **SeinARTS Entity Bridge** and open **Component Data**.
2. Add an **Extents Component** entry.
3. In the **Shapes** array, add one shape:
   - **Shape**: `Capsule`.
   - **Radius**: `40` for a standard infantry-sized body.
   - **Height**: `180` for a standing humanoid.
   - **Local Offset**: leave at zero unless the body needs vertical adjustment.
4. Enable **Collision Enabled**.
5. Set **Mobility** to `Movable`.
6. Set **Mass** to `100`. Heavier entities push lighter ones in collisions.
7. Leave **Bakes Into Nav** and **Blocks Nav** disabled. Moving infantry should not block the navigation grid.

## 3. Add the Navigation Component

The Navigation Component controls how the entity pathfinds.

1. Add a **Navigation Component** entry to Component Data.
2. Configure:
   - **Acceptance Radius**: `50`. How close the entity must be to its destination to count as arrived. Match this to the entity's body radius.
   - **Fallback Footprint Radius**: `50`. Used for pathfinding clearance when no Extents Component radius is available.
   - **Repath Mode**: `Interval`. The entity periodically recomputes its path to account for newly blocked or cleared areas.
   - **Repath Interval**: `0.25`. Repath every quarter-second of simulation time.
3. Leave **Blocked Terrain Tags** empty unless you want this unit to avoid specific terrain types (for example, a tag for deep water).

## 4. Add the Movement Component

The Movement Component defines how fast the entity moves and turns.

1. Add a **Movement Component** entry to Component Data.
2. Configure:
   - **Top Speed**: `500` for a jogging infantry unit. This is simulation units per second.
   - **Turn Rate**: `5`. Radians per second. Higher values make the unit snap to facing faster.
   - **Movement Class**: select `SeinInfantryMovement` from the Movement+ extension. This surfaces the **Infantry Movement Data** sub-data panel automatically, where you can tune acceleration and deceleration. If the Movement+ extension is not enabled, leave Movement Class empty to use the default Basic movement mode.
3. If using Infantry Movement, configure the sub-data that appears:
   - **Acceleration**: `750`. How quickly the unit ramps up to speed (world units per second²).
   - **Deceleration**: `750`. Brake rate and arrival deceleration.
4. Leave the reverse fields disabled. Infantry typically does not reverse.
5. Avoidance fields can stay at defaults:
   - **Avoidance Strength**: `1`. How strongly the unit steers around others.
   - **Avoidance Weight**: `0`. Units with higher weight displace lighter ones. Equal-weight units share the avoidance.

## 5. Add the Vitals Component

1. Add a **Vitals Component** entry to Component Data.
2. Set **Max Health** to the desired value (for example, `100`).
3. Leave **Health** at `0` so it seeds from Max Health at spawn.
4. Optionally set an **Armor Tag** under `SeinARTS.Combat.Armor`.

See [Making Combat Abilities](/guides/making-combat-abilities/) for full details on the vitals system.

## 6. Add the Weapon Component

1. Add a **Weapon Component** entry to Component Data.
2. Add one weapon slot to the **Weapons** array and configure:
   - **Range**: `1000`.
   - **Cooldown Seconds**: `1.0`.
   - **Delivery**: `Instant`.
   - **Payload > Base Damage**: `10`.
3. Add more slots for additional weapons.

See [Making Combat Abilities](/guides/making-combat-abilities/) for the full weapon configuration reference.

## 7. Add the Ability Component and command mappings

1. Add an **Ability Component** entry to Component Data.
2. Add `SA_Move` and `SA_Attack` to **Granted Abilities**.
3. Set **Fallback Ability Tag** to `SeinARTS.Ability.Move`.
4. In **Default Commands**, add two entries:

| Priority | Required Context | Ability Tag |
|---|---|---|
| `0` | `SeinARTS.Command.Context.RightClick`, `SeinARTS.Command.Context.Target.Ground` | `SeinARTS.Ability.Move` |
| `10` | `SeinARTS.Command.Context.RightClick`, `SeinARTS.Command.Context.Target.Enemy` | `SeinARTS.Ability.Attack` |

Right-clicking the ground moves. Right-clicking an enemy attacks. Any other right-click falls back to movement.

## 8. Add the Vision Component (optional)

If your project uses fog of war, add a **Vision Component** so the entity reveals terrain.

1. Add a **Vision Component** entry to Component Data.
2. Add one entry to the **Vision Stamps** array.
3. In the stamp's **Shape**, set:
   - **Shape**: `Radial` for a circular sight radius.
   - **Radius**: `1500` for a standard infantry sight range.
4. Set **Eye Height** to `180` on the Vision Component (the height for shadowcast line-of-sight checks).

## 9. Create the Animation Blueprint

The Animation Blueprint bridges the deterministic simulation with Unreal's animation system. It reads float-converted state from the simulation each frame.

1. Create a new **Animation Blueprint** for the Skeletal Mesh you assigned in step 1.
2. In the **Anim Graph**, add a **State Machine** named `Locomotion`.
3. Create at least two states: **Idle** and **Moving**.
4. In the **Event Graph**, compute the animation variables:
   1. **Get Owning Actor** → **Cast to SeinActor** → **Get Entity Handle**.
   2. Call **Get Movement State** (under `SeinARTS > Movement > State`) with the entity handle. This returns `FSeinMovementStateData`.
   3. Break the struct and promote the needed values to variables:
      - **Ground Speed** (float) — drives blendspace axes.
      - **Is Moving** (bool) — drives state transitions.
      - **Direction** (float) — for strafe blendspaces.
5. Set the **Idle → Moving** transition to fire when **Is Moving** is true.
6. Set the **Moving → Idle** transition to fire when **Is Moving** is false.
7. In the Moving state, use a 1D Blendspace driven by **Ground Speed** to blend between walk and run poses.

### Assign the Animation Blueprint

1. Return to the entity Blueprint.
2. Select the Skeletal Mesh Component.
3. Set **Anim Class** to the Animation Blueprint you just created.

## 10. Compile and test

1. **Compile** and **Save** the entity Blueprint.
2. Place the entity in a level that has the SeinARTS match setup (see [Setting Up a Playable Level](/guides/setting-up-a-playable-level/)).
3. Set the entity's **Player Slot** to your player index.
4. Play in Editor and verify:
   - Selecting the unit highlights it.
   - Right-clicking the ground moves the unit to that location.
   - The animation blends between idle and locomotion.
   - Right-clicking an enemy causes the unit to fire (check if the target takes damage and eventually dies).

## Common mistakes

- Adding a Skeletal Mesh but forgetting to set its Anim Class. The mesh renders in its bind pose.
- Mismatched Extents radius and Navigation acceptance radius. If the acceptance radius is smaller than the body radius, units stop short of their destination. If it is much larger, units appear to arrive far from the clicked point.
- Missing an Ability Component. Without it, the entity has no abilities and ignores all commands.
- Forgetting the Fallback Ability Tag. Without a fallback, right-clicking anything not covered by a command mapping does nothing.
- Setting the attack priority lower than or equal to the move priority. Every right-click resolves as movement because the move mapping matches first.
- Reading animation state from the actor's transform or velocity instead of calling Get Movement State. The actor transform is render-interpolated and may not match the simulation.
- Adding component types that the entity does not need. A barracks does not need a Movement Component. An abstract entity does not need an Extents Component. Start with only the components the entity requires.

## Component Data summary

A complete infantry entity's Component Data array contains:

| Component | Purpose |
|---|---|
| Identity Component | Unique tag and display metadata. Auto-generated from asset name. |
| Extents Component | Body shape, collision, mass. |
| Navigation Component | Pathfinding configuration. |
| Movement Component | Speed, turn rate, avoidance. |
| Vitals Component | Health and armor. |
| Weapon Component | Weapon slots with range, cooldown, and damage. |
| Ability Component | Granted abilities and command mappings. |
| Vision Component | Sight radius for fog of war (optional). |

## Finished checklist

- The entity Blueprint inherits from `ASeinActor` via the SeinARTS Entity Blueprint factory.
- A Skeletal Mesh Component with an Animation Blueprint is attached to the root.
- Component Data contains Extents, Navigation, Movement, Vitals, Weapon, and Ability components.
- Granted Abilities includes both a move and an attack ability.
- Default Commands maps ground clicks to move and enemy clicks to attack.
- The Animation Blueprint reads `Get Movement State` for locomotion blending.
- The entity compiles, saves, and responds to move and attack orders in Play in Editor.

You now have a fully functional infantry unit that moves, fights, and animates using the deterministic simulation as its single source of truth.
