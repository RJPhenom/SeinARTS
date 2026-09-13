---
title: Add Production and Research
description: Train Soldiers, set rally points, and unlock Factory construction through a queued research effect.
---

Complete [Build Production Buildings](/guides/building-production-buildings/) first. The Barracks will train Soldiers and research Factory access through the same production queue.

## 1. Make the Soldier producible

Open `SU_Soldier` and add **Sein Producible**. Set Build Time to `5`. Compile and save.

Create a Generic Ability named `SA_Produce_Soldier`:

| Field | Value |
| --- | --- |
| Ability Name | Train Soldier |
| Dispatch Mode | Single |
| Resource Cost > Amounts | `SeinARTS.Resource.Minerals` → `50` |
| Cost Timing | Production Queue |
| Blocked Tags | `SeinARTS.State.UnderConstruction` |

Its graph is **Event On Activate → Enqueue Production → End Ability**. Use self as the target of Enqueue Production and `SU_Soldier` as Producible Class.

Override **Can Activate**. Call Get Production Data on Owner Entity and return true only if the read succeeds and Queue length is less than Max Queue Size. End Ability ends the enqueue action; the production system continues the queued work.

Grant `SA_Produce_Soldier` to the Barracks. Compile and save.

## 2. Set a rally point

Create a Generic Ability named `SA_SetRallyPoint`, Ability Name `Set Rally Point`, and Target Type Point.

On Activate, make a Fixed Transform with Location = Target Location, identity rotation, and scale `(1, 1, 1)`. Pass it to the ability's **Set Rally Point** function on self, then End Ability.

Grant it on the Barracks and Factory. On each building, add a Default Commands entry requiring RightClick and Target.Ground, with the generated tag `SeinARTS.Ability.SetRallyPoint`.

Produced Soldiers spawn at Spawn Point Offset and use their Movement Ability to travel to the rally point. Compile and save.

## 3. Display the production queue

Add a Vertical Box named `ProductionQueue` to the HUD SelectionPanel. In RefreshHUD, call **Get Production Queue** on the primary view model.

For this small read-only queue, display its contents in a Text Block named `QueueText` inside ProductionQueue:

1. Iterate the returned queue items.
2. Append each Display Name to a local String, separated by newlines.
3. For the first item, also append Progress Percent multiplied by `100`, formatted with no fractional digits and a `%` suffix.
4. If Stalled At Completion is true, append `Waiting for resources or capacity`.
5. Set QueueText from that string; collapse ProductionQueue when the array is empty.

This display does not remove queue entries. The first item advances while later entries wait.

## 4. Create the unlock effect

Add the gameplay tag `SeinARTS.Tech.FactoryUnlocked` in Project Settings > Gameplay Tags.

In `Content/Demo/Blueprints`, create a **SeinARTS Effect** using its generic parent, named `SE_Unlock_Factory`. Configure:

| Field | Value |
| --- | --- |
| Scope | Player |
| Duration Mode | Persistent |
| Granted Tags | `SeinARTS.Tech.FactoryUnlocked` |
| Forbidden Prerequisite Tags | `SeinARTS.Tech.FactoryUnlocked` |

The effect needs no event graph for this unlock. While active, its Granted Tags belong to the researching player. Compile and save.

## 5. Create the research item and ability

Create a Generic Entity named `SR_Unlock_Factory`. Set its identity Display Name to `Factory Access`. Add **Sein Producible** with Is Research enabled and Granted Tech Effect `SE_Unlock_Factory`. Research takes ten seconds and applies the effect instead of spawning this entity.

Create a Generic Ability named `SA_Produce_Research_Factory`:

| Field | Value |
| --- | --- |
| Ability Name | Research Factory Access |
| Dispatch Mode | Single |
| Resource Cost > Amounts | `SeinARTS.Resource.Minerals` → `100` |
| Cost Timing | Production Queue |
| Blocked Tags | `SeinARTS.State.UnderConstruction` |

On Activate, **Enqueue Production** with Producible Class `SR_Unlock_Factory`, then End Ability.

Override Can Activate. Return true only when all these checks pass:

- Get Production Data on Owner Entity succeeds and Queue length is below Max Queue Size.
- **Player Has Tech Tag**, using Get Entity Owner and `SeinARTS.Tech.FactoryUnlocked`, is false.
- No Barracks owned by that player has `SR_Unlock_Factory` queued.

For the duplicate check, call **Lookup Entities By Tag** with `SeinARTS.Unit.Barracks`. For each returned entity whose Get Entity Owner matches the researching player's ID, read Get Production Data and inspect its Queue. Keep a local Boolean `AlreadyQueued`, initialized false before the loops and set true if an entry's Actor Class equals `SR_Unlock_Factory`. Return false when a match is found; otherwise evaluate the final return after all producers have been checked.

Grant the research ability on the Barracks. These Can Activate checks prevent the player paying twice, whether the research is completed or waiting in another Barracks. The effect's Forbidden Prerequisite Tags also describes the unlock restriction to callers of Get Tech Availability.

## 6. Gate Factory placement

Open `SA_Place_Factory` and add `SeinARTS.Tech.FactoryUnlocked` to **Required Player Tags**. Compile and save.

The existing Factory button remains visible but unavailable until research completes. The unlock applies to every Soldier owned by that player, including Soldiers produced later.

## 7. Test the loop

In a new session:

1. Build a Barracks. Train Soldier and Research Factory Access should be unavailable during construction.
2. After construction, right-click clear ground to set its rally point.
3. Train two Soldiers. Each costs `50`, appears after five seconds at the building's spawn offset, and moves to the rally point.
4. Queue Factory Access. Check its `100` cost and ten-second progress.
5. Select a Soldier before and after research. Build Factory should become available only after completion.
6. Build the Factory. It costs `200`; harvesting supplies any missing Minerals.

## Checkpoint

- Production costs are declared on abilities and build times on producible entities.
- The Barracks queue produces Soldiers and applies a research effect.
- A persistent player tag unlocks Factory placement.
- New units leave production through the same movement ability used for orders.

Continue with [Add the Transport Truck](/guides/adding-the-transport-truck/).
