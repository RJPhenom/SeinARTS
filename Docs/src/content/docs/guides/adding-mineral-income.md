---
title: Add Mineral Income
description: Create a neutral mineral deposit and reward the attacking player for each successful hit.
---

Complete [Add Combat](/guides/adding-combat/) first. A mineral deposit will hold 1,000 stock. Each hit removes up to 3 stock and grants the same number of Minerals to the attacking player.

## 1. Define Minerals

Open **Project Settings > Gameplay Tags** and add `SeinARTS.Resource.Minerals` to the project's gameplay-tag list. Save the tag to the project's tag configuration.

Open **Project Settings > Plugins > SeinARTS**. Under **Resource Catalog**, add:

| Field | Value |
| --- | --- |
| Resource Tag | `SeinARTS.Resource.Minerals` |
| Display Name | Minerals |
| Default Starting Value | `500` |

The stockpile is uncapped. Restart Play after changing the catalog so new players receive the starting balance.

## 2. Create the deposit data

In `Content/Demo/Blueprints`, create a **SeinARTS Entity Component** named `SC_DemoResourceComponent`.

Add an Instance Editable Fixed Point variable named `Remaining`, default `1000`. Compile and save.

Create a **SeinARTS Entity** using **Generic Entity** as its parent, named `SBP_Resource_Minerals`. On Sein Identity, set Display Name to `Mineral Deposit`.

Add a Static Mesh component using the engine Cube. Set its relative Location to `(0, 0, 75)` and Scale to `(2, 2, 1.5)`. This gives a 200-by-200 footprint with its base at the actor origin.

Add **Sein Extents** with one Box shape: Half Extent (Forward) `100`, Half Extent (Right) `100`, and Height `150`. Enable Collision, set Mobility to Static, and enable Blocks Nav. This creates a runtime obstacle that depletion can remove. Add `SC_DemoResourceComponent`. Compile and save.

## 3. Place map resources

Open `LVL_DemoSkirmish`. Place three `SBP_Resource_Minerals` instances on the floor inside the baked bounds, leaving room to walk around each one. These map-placed entities are neutral.

Put one deposit within `400` world units of the starting Soldier, so the first test does not depend on approaching an unseen target. Save the map. These deposits are authored map resources; player units continue to spawn from their player starts and production buildings.

## 4. Create Harvest

Create a Generic **SeinARTS Ability** named `SA_Harvest`, with Ability Name `Harvest`, Target Type **Entity**, Max Range `500`, Out Of Range Behavior **Auto Move Then**, and Requires Line Of Sight enabled.

Set Granted Tags to `SeinARTS.Ability.Harvest` and Cancel Abilities With Tag to `SeinARTS.Ability`. This lets a new Harvest replace the previous run and lets Move interrupt harvesting.

Add `TimeUntilShot` as a Fixed Point variable. Set it to zero on On Activate and build the same On Tick timer from the combat chapter. Use the Soldier's Damage and RateOfFire as the extraction amount and frequency: `3` Minerals per hit at `10` hits per second, or `30` Minerals per second while harvesting continuously.

Create `TryHarvest` as a non-latent function. Read the owner's combat data and the target's `SC_DemoResourceComponent` data through typed getters. End Ability if either read fails, Remaining is not positive, or Damage or RateOfFire is not positive. Cache Remaining as local `OldRemaining`.

In `TryHarvest`, add **Make Sein Target Query** and configure:

| Input | Value |
| --- | --- |
| Instigator | Owner Entity |
| Range | Max Range |
| Required Component | Generated data struct for `SC_DemoResourceComponent` |

Connect the query and **Target Entity** to **Check Target**. Switch on its result. Continue only on **Eligible**; every other result calls **End Ability**.

Apply **Apply Field Delta** to Target Entity, with that resource struct, Field Name `Remaining`, Delta `0 - Damage`, and Clamp Min enabled at `0`. End Ability on failure.

Calculate `Extracted = OldRemaining - NewValue`. If it is positive:

1. Add **Make Sein Resource Cost**.
2. Drag from its **Amounts** input and choose **Make Map**. The connection supplies the Gameplay Tag key and Fixed Point value types. Add one pair: `SeinARTS.Resource.Minerals` → Extracted.
3. Call **Grant Income**, with Player ID from **Get Entity Owner** on Owner Entity, and Amount from the struct you made.

Using the actual decrease prevents a final hit from awarding more Minerals than the deposit contains. The income belongs to the attacker, not the neutral deposit owner.

If Apply Field Delta reports At Min, destroy Target Entity and end the ability. Otherwise return.

Compile and save.

## 5. Map neutral clicks

Grant `SA_Harvest` on the Soldier's Sein Abilities. Add a Default Commands entry:

| Field | Value |
| --- | --- |
| Required Context | `SeinARTS.Command.Context.RightClick` and `SeinARTS.Command.Context.Target.Neutral` |
| Ability Tag | `SeinARTS.Ability.Harvest` |
| Priority | `10` |

The resource-component check makes clicks on other neutral entities fail without granting income. Compile and save.

## 6. Check the extraction loop

Run Play Standalone and order the Soldier to harvest the nearby deposit. It should remain in range until the deposit is exhausted. Right-click ground during harvesting; it should stop extracting and move.

For a short depletion test, set that placed deposit's Remaining to `25` before Play. Eight hits should extract `3` each, and the ninth should extract the remaining `1`; then the deposit disappears. Reset Remaining to its inherited default after the test. The HUD in the next chapter makes the balance visible and lets you verify that it rises from `500` to `525`.

## Checkpoint

- Minerals exists in the resource catalog with a starting balance of 500.
- Neutral deposits contain their own Remaining stock.
- Harvest grants only the amount successfully removed from the target.
- Cancelling the order stops income, and depleted deposits disappear.

Continue with [Build the HUD](/guides/building-the-hud/).
