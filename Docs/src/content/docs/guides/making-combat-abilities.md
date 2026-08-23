---
title: Making Combat Abilities
description: Give a SeinARTS entity health, weapons, and an attack ability that fires at enemy targets on command.
---

This guide adds combat participation to an entity Blueprint by authoring vitals, weapons, and an attack ability. By the end, the entity can take damage, die, and fire back at enemies.

## What you will make

- A **Vitals Component** giving the entity health, armor, and optional regeneration.
- A **Weapon Component** with at least one configured weapon slot.
- An attack ability that fires every ready weapon at a commanded target.
- A command mapping that activates the attack on a right-click enemy order.

## Before you begin

- Complete [Creating an Entity Blueprint](/guides/creating-an-entity-blueprint/) or have an existing entity Blueprint based on `ASeinActor`.
- If you want the entity to move toward out-of-range targets before attacking, complete [Making a Movement Ability](/guides/making-a-movement-ability/) first.

## 1. Add the Vitals Component

1. Open your entity Blueprint and select the **SeinARTS Entity Bridge**.
2. Under **Component Data**, add a new entry and choose **Vitals Component**.
3. Configure the fields:
   - **Max Health**: the entity's health ceiling. Default is `100`.
   - **Health**: leave at `0`. The system seeds Health from Max Health at spawn when the authored value is zero or negative.
   - **Armor Tag**: optionally assign a tag under `SeinARTS.Combat.Armor` (for example, `SeinARTS.Combat.Armor.Light`). The damage formula receives this tag to compute damage modifiers.
   - **Regen Per Second**: health regenerated each simulation second. Leave at `0` for no regeneration.
   - **Invulnerable**: enable to make the entity refuse all damage. Useful for structures during construction.

Once an entity has a Vitals Component, it participates in the combat system. Other entities with weapons can acquire it as a target, and damage application checks against its health.

## 2. Add the Weapon Component

1. Still in **Component Data**, add a **Weapon Component** entry.
2. Expand the **Weapons** array and add a slot. Each slot is one weapon the entity can fire.
3. Configure the weapon slot:

| Field | Purpose | Suggested value |
|---|---|---|
| **Range** | Maximum firing distance in simulation units. | `1000` for a rifle. |
| **Arc Half Angle** | Firing arc as a half-angle in degrees. `180` means the weapon fires in all directions. `45` restricts it to a forward cone. | `180` for infantry. |
| **Cooldown Seconds** | Minimum time between shots. | `1.0` |
| **Magazine Size** | Shots before a reload. `0` disables the magazine system. | `0` for a simple weapon. |
| **Reload Seconds** | Time to reload an empty magazine. | `0` when Magazine Size is 0. |
| **Require Line of Sight** | Gates fire through the fog of war resolver. | Enabled. |
| **Delivery** | `Instant` applies damage immediately. `Projectile` spawns a travel-time entity. | `Instant` for a basic weapon. |
| **Projectile Speed** | Flight speed when Delivery is Projectile. | Irrelevant for Instant delivery. |
| **Projectile Class** | Optional entity Blueprint spawned as the visible projectile. | Leave empty for Instant. |

### Configure the damage payload

Each weapon slot carries a **Payload** that defines what a hit delivers.

- **Base Damage**: the raw damage number before the formula. Default is `10`.
- **Damage Type Tag**: a tag under `SeinARTS.Combat.Damage` (for example, `SeinARTS.Combat.Damage.Ballistic`). The damage formula receives this alongside the target's Armor Tag.
- **Area Radius**: splash radius around the impact point. `0` means single-target damage only.
- **Formula Class**: a `USeinDamageFormula` subclass that computes final damage from base damage, damage type, and armor. Leave empty to use the neutral built-in formula, which passes Base Damage through unchanged.

### Multiple weapons

Add more slots to the Weapons array for entities with multiple weapon systems. Each slot has its own range, cooldown, magazine, and payload. The attack ability iterates all slots each tick and fires any that are ready, so a unit can have a primary rifle and a secondary grenade launcher that operate on independent cooldowns.

## 3. Create or reuse the attack ability

SeinARTS ships a starter attack ability in C++ (`USeinAbility_Attack`) that fires every ready weapon at the commanded target until it dies. This is deliberately minimal — no target switching, no chasing, no engagement doctrine. For a first pass, it works as is.

### Option A: Use the shipped attack ability directly

The example project includes `SA_Attack` under `Content/SeinARTSExamples/Blueprints/Abilities/`. You can reference this asset directly in your entity's Granted Abilities, or duplicate it to customize.

### Option B: Create your own attack ability

1. In the Content Browser, create a new Blueprint class inheriting from **SeinAbility_Attack** (the C++ class) or from plain **SeinAbility** if you want full control.
2. Name it `SA_Attack`.
3. In Class Defaults, set:
   - **Target Type** to `Entity`. An attack targets another entity.
   - **Ability Name** to `Attack`.
4. If inheriting from `SeinAbility_Attack`, the On Tick logic is already implemented: each tick, it attempts to fire all weapons on the owner at the target entity. Override On Tick in Blueprint if you need custom engagement logic such as target switching, burst patterns, or facing requirements.
5. If building from plain `SeinAbility`, implement On Tick yourself using the **Try Fire Weapon At** function from the `SeinARTS > Combat` Blueprint library. Pass the owner entity, the weapon index, and the target entity. The function handles cooldown, range, line of sight, magazine, and delivery checks internally, returning whether the shot fired.

### Out-of-range behavior

On the ability's Class Defaults, find **Out Of Range Behavior** under **SeinARTS | Ability | Targeting**:

- **Reject** (default): the ability refuses activation if the target is beyond Max Range. The player sees no response.
- **Auto Move Then**: the entity moves toward the target until in range, then begins firing. This requires the entity to also have a movement ability.

For most RTS units, set this to **Auto Move Then** so attack-move works naturally.

## 4. Add the ability and command mapping

1. Open your entity Blueprint's **Ability Component** in Component Data.
2. Add `SA_Attack` to the **Granted Abilities** array (alongside your movement ability).
3. In the **Default Commands** array, add an entry:
   - **Required Context**: `SeinARTS.Command.Context.RightClick` and `SeinARTS.Command.Context.Target.Enemy`.
   - **Ability Tag**: the ability tag of your attack ability (auto-generated as `SeinARTS.Ability.Attack` with the `SA_` prefix).
   - **Priority**: `10`. Higher than the movement command's priority of `0`, so right-clicking an enemy attacks rather than moving.

### Command priority example

With both move and attack mappings in place, command resolution now works as:

| Right-click target | Context tags matched | Resolved ability |
|---|---|---|
| Ground | `RightClick`, `Target.Ground` | Move (priority 0) |
| Enemy unit | `RightClick`, `Target.Enemy` | Attack (priority 10) |
| Friendly unit | `RightClick`, `Target.Friendly` | Fallback (Move) |

Add more mappings to handle additional contexts. For example, a medic might map `RightClick + Target.Friendly` to a heal ability at priority 20.

## 5. Set up a custom damage formula (optional)

The neutral built-in formula passes Base Damage through as final damage. For armor-modified damage, create a formula class:

1. Create a new Blueprint inheriting from **SeinDamageFormula**.
2. Override the **Compute Damage** function. It receives:
   - **Base Damage** from the weapon payload.
   - **Damage Type Tag** from the weapon payload.
   - **Armor Tag** from the target's Vitals Component.
   - **Attacker** and **Target** entity handles for any additional component lookups.
3. Return the final damage value as an `FFixedPoint`.
4. Assign this class to the **Formula Class** field on the weapon slot's damage payload.

Damage formulas are stateless CDO policy objects. They are called once per hit and must not store mutable state.

## Common mistakes

- Adding a Weapon Component without a Vitals Component on the target. Damage requires the target to have vitals. The attacker does not need vitals to fire.
- Setting the attack ability's Target Type to `Point` instead of `Entity`. Attacks target entities, not locations.
- Setting command mapping priority lower than the movement mapping. The attack would never resolve because the move mapping matches first on any right-click.
- Using a Cooldown Seconds of `0`. This fires every sim tick, which is typically 20 times per second.
- Expecting the attack ability to chase. The shipped `SeinAbility_Attack` does not move. Set Out Of Range Behavior to Auto Move Then, or build a custom ability that manages pursuit.
- Storing mutable state on a damage formula. Formulas are stateless CDO queries.

## Finished checklist

- The entity has a **Vitals Component** with Max Health configured.
- The entity has a **Weapon Component** with at least one weapon slot.
- Each weapon slot has Range, Cooldown, Delivery, and a damage Payload.
- An attack ability is listed in the entity's Granted Abilities.
- A command mapping routes right-click enemy orders to the attack ability.
- The attack ability's Priority is higher than the movement mapping's Priority.
- The entity can take damage, die, and return fire.

You now have a combat-capable entity. It participates in damage exchange through its weapon slots and vitals, and responds to attack orders through the command system.
