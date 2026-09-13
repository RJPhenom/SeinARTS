---
title: Add Combat
description: Author health and weapon data, then script a repeating attack using fixed simulation time.
---

Complete [Add Movement](/guides/adding-movement/) first. This chapter gives the Soldier 100 health and an attack that deals 3 damage at 10 shots per second (600 RPM). Hits apply immediately; the animation remains the locomotion graph you already built.

## 1. Create health and weapon components

In `Content/Demo/Blueprints`, right-click and choose **SeinARTS Entity Component**. Name the asset `SC_DemoHealthComponent`. Open it and add these variables in My Blueprint:

| Variable | Type | Default |
| --- | --- | --- |
| Health | Fixed Point | `100` |
| MaxHealth | Fixed Point | `100` |

Make the variables Instance Editable so each entity can author its values. Compile to expose their defaults, enter the values, and save. The component compiler creates its simulation data struct; you do not create a separate Structure asset.

Create another **SeinARTS Entity Component**, `SC_DemoCombatComponent`, with Instance Editable variables:

| Variable | Type | Default |
| --- | --- | --- |
| Damage | Fixed Point | `3` |
| RateOfFire | Fixed Point | `10` |

RateOfFire means shots per simulation second. Compile and save.

Open `SU_Soldier` and add both components. Compile and save.

These values give 30 damage per second. A target with 100 health takes 34 hits to defeat, approximately 3.3 seconds from the first hit with uninterrupted fire.

## 2. Create the attack ability

In `Content/Demo/Blueprints`, create a **SeinARTS Ability** with **Generic Ability** as its parent. Name it `SA_Attack`.

Set its Class Defaults:

| Field | Value |
| --- | --- |
| Ability Name | Attack |
| Target Type | Entity |
| Max Range | `500` |
| Out Of Range Behavior | Auto Move Then |
| Requires Line Of Sight | Enabled |
| Granted Tags | `SeinARTS.Ability.Attack` |
| Cancel Abilities With Tag | `SeinARTS.Ability` |

The generated Ability Tag is `SeinARTS.Ability.Attack`. Automatic movement uses the Soldier's Movement Ability to approach an initially distant target. The attack graph will check range again before every hit.

Add a Fixed Point variable named `TimeUntilShot`. On **Event On Activate**, set it to `0`. This resets the timer whenever a new attack starts.

## 3. Read the simulation components

Create a function in `SA_Attack` named `TryShot`. This function performs one hit attempt and contains no latent nodes.

Use the framework's typed component getter for `SC_DemoCombatComponent`'s generated data struct, with **Entity Handle** = **Owner Entity**. Find it by searching the graph menu for your component's name. Break the returned struct to read Damage and RateOfFire. Use the getter's success output to branch; on failure, call **End Ability** and return.

Use the corresponding typed getter for `SC_DemoHealthComponent` on **Target Entity**. On failure, end the ability. Cache its Health output in a local Fixed Point variable named `OldHealth` before changing anything.

If Damage or RateOfFire is zero or negative, or OldHealth is zero or negative, end the ability and return. Use Fixed Point comparison and arithmetic nodes by dragging from these pins.

Component getters read simulation data. Do not use Get Component by Class on a rendered actor to read live health.

## 4. Check the target

In `TryShot`, add **Make Sein Target Query** and configure:

| Input | Value |
| --- | --- |
| Instigator | Owner Entity |
| Range | Max Range |
| Required Component | Generated data struct for `SC_DemoHealthComponent` |

The query uses the attacker's current simulation position, checks a full circle and requires line of sight. The default scorer rejects targets owned by the attacker.

Connect the query and **Target Entity** to **Check Target**. Switch on its result. Continue only on **Eligible**; every other result calls **End Ability**. In this demo, an attack ends if its target escapes range or sight. A new attack order approaches it again.

## 5. Apply a hit

On Eligible, call **Apply Field Delta**:

| Input | Value |
| --- | --- |
| Entity Handle | Target Entity |
| Struct Type | Generated data struct for `SC_DemoHealthComponent` |
| Field Name | `Health` |
| Delta | `0 - Damage`, using Fixed Point subtraction |
| Clamp Min | Enabled |

Branch on the return value. Failure ends the ability. On success, calculate `OldHealth - NewValue` and pass that amount to **Notify Damage Applied**, with Target = Target Entity and Source = Owner Entity.

Branch on **At Min**:

- True: call **Notify Death** with Dying = Target Entity and Killer = Owner Entity, then **Destroy Entity** on Target Entity, then **End Ability**.
- False: return from `TryShot`.

Keep the notification nodes before Destroy Entity. They supply presentation events; the health write and Destroy Entity perform the gameplay changes.

## 6. Run the firing timer

Use the supplied **Event On Tick**, whose Delta Time is Fixed Point simulation time:

1. Set `TimeUntilShot = TimeUntilShot - Delta Time`.
2. If TimeUntilShot is greater than zero, finish this tick.
3. Otherwise call `TryShot`.
4. Check the inherited **Is Active** value. If the shot ended the ability, finish this tick.
5. Read RateOfFire from Owner Entity's combat data again. Set `TimeUntilShot = TimeUntilShot + (1 / RateOfFire)`.

Use a Fixed Point `1` for the division. At the authored rate this gives a `0.1`-second firing interval, carrying the fraction of time left over between simulation ticks. Do not use an Unreal Timer, Delay, or Event Tick for this loop.

Compile and save `SA_Attack`.

## 7. Grant and map Attack

On `SU_Soldier`'s **Sein Abilities**, add `SA_Attack` to Granted Abilities. Keep `SA_Move`.

Add a Default Commands entry with Required Context containing both `SeinARTS.Command.Context.RightClick` and `SeinARTS.Command.Context.Target.Enemy`, Ability Tag `SeinARTS.Ability.Attack`, and Priority `10`.

Compile and save. Enemy clicks now select Attack; ground clicks still select Move.

## Checkpoint

- The Soldier authors health and weapon values through two entity components.
- Attack validates its target and reduces Health through Apply Field Delta.
- Its timer uses fixed simulation time and resets on each activation.
- A lethal hit notifies presentation, destroys the target, and ends the ability.

The next chapter creates a neutral mineral deposit that exercises the same hit loop and gives you an immediate income test. The later lobby chapter supplies the second human player for an enemy-versus-enemy test.

Continue with [Add Mineral Income](/guides/adding-mineral-income/).
