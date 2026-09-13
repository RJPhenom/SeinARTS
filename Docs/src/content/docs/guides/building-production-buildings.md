---
title: Build Production Buildings
description: Create Barracks and Factory entities, pay for placement, and complete construction over simulation time.
---

Complete [Build the HUD](/guides/building-the-hud/) first. The Soldier will place a Barracks for 100 Minerals and a Factory for 200. Buildings take ten seconds to become operational.

Before adding buildings, make room on `LVL_DemoSkirmish`: scale the existing template floor to cover at least `6000 × 6000` world units, keeping its top surface at the same height. Expand the Sein Level Volume inside those edges, press Bake Level Data, and save. Keep the player start and deposits on the floor.

## 1. Create the Barracks

In `Content/Demo/Blueprints`, create a **SeinARTS Entity**, parent **Generic Entity**, named `SU_Barracks`. Set Display Name on Sein Identity to `Barracks`.

Add the following components:

| Component | Configuration |
| --- | --- |
| Static Mesh | Engine Cube; relative Location `(0, 0, 100)`, Scale `(4, 4, 2)` |
| Sein Vision | Default |
| Sein Abilities | Default |
| Sein Construction | Default |
| Sein Extents | One Box: Forward half extent `200`, Right half extent `200`, Height `200`; Collision enabled; Mobility Static; Blocks Nav enabled |
| SC_DemoHealthComponent | Health `500`, MaxHealth `500` |
| Sein Production | Spawn Point Offset location `(350, 0, 0)` |

The spawn point sits outside the building's footprint. Runtime navigation blocking lets a placed building obstruct movement and lets its destruction clear the obstruction.

Compile and save.

## 2. Create the Factory

Create another Generic Entity named `SU_Factory`. Set Display Name to `Factory`.

Use the same component set with these changes:

| Setting | Factory value |
| --- | --- |
| Cube relative Location | `(0, 0, 125)` |
| Cube Scale | `(6, 5, 2.5)` |
| Box forward/right half extents | `300` / `250` |
| Box Height | `250` |
| Health and MaxHealth | `750` |
| Spawn Point Offset location | `(500, 0, 0)` |

Compile and save.

## 3. Complete construction

Create a Generic Ability named `SA_CompleteConstruction`. Enable **Is Passive**.

On **Event On Tick**:

1. Call **Is Under Construction** with Owner Entity.
2. If false, End Ability.
3. Otherwise call **Add Construction Progress**, Entity = Owner Entity, Amount = Delta Time.
4. Check Is Under Construction again. If false, End Ability.

This makes each building advance its own ten-second construction timer. Add Construction Progress removes the construction component when finished and releases the framework's UnderConstruction tag.

Grant `SA_CompleteConstruction` on both buildings. Do not add UnderConstruction to the passive ability's Blocked Tags: it needs to run during construction.

Compile and save all three assets.

## 4. Create Barracks placement

Create a Generic Ability named `SA_Place_Barracks`:

| Field | Value |
| --- | --- |
| Ability Name | Build Barracks |
| Target Type | Point |
| Dispatch Mode | Single |
| Max Range | `500` |
| Out Of Range Behavior | Auto Move Then |
| Requires Free Footprint | Enabled |
| Resource Cost > Amounts | `SeinARTS.Resource.Minerals` → `100` |
| Cancel Abilities With Tag | `SeinARTS.Ability` |
| Targeter Spec | Point + Facing Targeter Spec |

Expand Targeter Spec. Set **Building Class** to `SU_Barracks`, **Rotation Step Degrees** to `90`, and **Reject Click When Blocked** to enabled. The default preview reads the building's mesh and extents.

On **Event On Activate**:

1. Check that **Targeter Points** has an element at index `0`. If not, Cancel Ability.
2. Get element `0` and call **Get Targeter Point Transform**.
3. Call **Spawn Entity** with Actor Class `SU_Barracks`, Spawn Transform from that node, and Owner Player ID from **Get Entity Owner** on Owner Entity.
4. Check the returned handle with **Is Entity Alive**. On success, End Ability. On failure, Cancel Ability so the activation payment is refunded.

The ability's Resource Cost takes payment. Do not add a second Deduct node to the graph.

Compile and save. Grant `SA_Place_Barracks` to the Soldier. Its HUD now includes Build Barracks.

## 5. Create Factory placement

Create `SA_Place_Factory` through the Generic Ability factory. Use the same settings and graph, changing Ability Name to `Build Factory`, resource amount to `200`, Targeter Spec's Building Class to `SU_Factory`, and Spawn Entity's Actor Class to `SU_Factory`.

Grant it to the Soldier. In the next chapter, research will gate this ability. Compile and save.

## 6. Display construction progress

Add a Progress Bar named `ConstructionBar` to the HUD's SelectionPanel, above ActionButtons. In RefreshHUD, use the primary view model's Entity handle:

- If **Is Under Construction** is true, show ConstructionBar and set Percent from **Get Construction Percent**, converted with **Fixed To Float**.
- Otherwise collapse ConstructionBar.

These are read operations. No construction mutation belongs in the HUD.

## 7. Test placement

Run Play Standalone. Select the Soldier and click Build Barracks. Move the preview onto clear ground. Press the right mouse button to lock its location, drag to choose facing, and release to place it.

Check that Minerals drops by `100`, the Barracks appears at the preview's location and orientation, and selecting it shows a construction bar reaching completion after ten seconds. Build a Factory and check its `200` cost.

Try an overlapping placement and a placement outside the playable bounds. They should not produce a building or charge a completed placement. Keep space around the spawn-point side of each building.

## Checkpoint

- The Soldier creates buildings through placement abilities.
- Placement validates a footprint and uses the captured transform.
- Construction advances through simulation callbacks.
- The HUD displays construction progress.

Continue with [Add Production and Research](/guides/adding-production-and-research/).
