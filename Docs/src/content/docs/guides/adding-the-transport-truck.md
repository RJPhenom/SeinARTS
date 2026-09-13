---
title: Add the Transport Truck
description: Produce a four-seat Truck and author embarkation and unloading with the containment primitives.
---

Complete [Add Production and Research](/guides/adding-production-and-research/) first. The Factory will produce a Truck for 150 Minerals. It carries four Soldiers, loads instantly, and unloads into open ground beside it.

## 1. Expose containment for authoring

The containment runtime provides container and passenger data, but these two types currently have no Add Component entries. Add two small authoring components to the game's C++ module so their data can be configured on your Blueprints.

Close the editor. In `Source/SeinDemo/SeinDemo.Build.cs`, add `SeinARTSCoreEntity` to PublicDependencyModuleNames alongside the existing Core, CoreUObject, and Engine dependencies.

Create `Source/SeinDemo/DemoTransportComponents.h` with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Authoring/SeinEntityComponent.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "DemoTransportComponents.generated.h"

/** Authors the container data used by Enter Container and Exit Container. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS),
    meta = (BlueprintSpawnableComponent))
class SEINDEMO_API UDemoTransportComponent : public USeinEntityComponent
{
    GENERATED_BODY()
public:
    /** Capacity, accepted passengers, and behavior when this transport dies. */
    UPROPERTY(EditAnywhere, Category = "SeinARTS",
        meta = (ShowOnlyInnerProperties))
    FSeinContainmentData Transport;

    virtual const UScriptStruct* GetPayloadStruct() const override
    {
        return FSeinContainmentData::StaticStruct();
    }

    virtual bool WritePayload(FInstancedStruct& Out) const override
    {
        Out.InitializeAs<FSeinContainmentData>(Transport);
        return true;
    }
};

/** Authors passenger size; the simulation maintains its container reference. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS),
    meta = (BlueprintSpawnableComponent))
class SEINDEMO_API UDemoPassengerComponent : public USeinEntityComponent
{
    GENERATED_BODY()
public:
    /** Space occupied by this entity when it enters a transport. */
    UPROPERTY(EditAnywhere, Category = "SeinARTS",
        meta = (ShowOnlyInnerProperties))
    FSeinContainmentMemberData Passenger;

    virtual const UScriptStruct* GetPayloadStruct() const override
    {
        return FSeinContainmentMemberData::StaticStruct();
    }

    virtual bool WritePayload(FInstancedStruct& Out) const override
    {
        Out.InitializeAs<FSeinContainmentMemberData>(Passenger);
        return true;
    }
};
```

Create `Source/SeinDemo/DemoTransportComponents.cpp` containing:

```cpp
#include "DemoTransportComponents.h"
```

Regenerate the project's Visual Studio files, build **Development Editor / Win64**, then reopen the project. **Demo Transport** and **Demo Passenger** should appear in Add Component. Their parent handles baking the authored values for simulation; neither component needs a tick or event graph.

## 2. Create the Truck

Create a Generic SeinARTS Entity named `SU_Truck`. Set its identity Display Name to `Truck`.

Add the following components:

| Component | Configuration |
| --- | --- |
| Static Mesh | Engine Cube; relative Location `(0, 0, 75)`, Scale `(4, 2, 1.5)` |
| Sein Navigation | Default |
| Sein Vision | Default |
| Sein Extents | Box: Forward half extent `200`, Right half extent `100`, Height `150`; Collision enabled |
| Sein Movement | Top Speed `600`; Movement Class SeinBasicUnitMovement |
| SC_DemoHealthComponent | Health and MaxHealth `300` |
| Sein Abilities | Grant `SA_Move`; map RightClick + Target.Ground to its tag |
| Sein Producible | Build Time `8` |
| Demo Transport | Total Capacity `4` |

On Demo Transport, edit Accepted Entity Query to require the Soldier's identity tag, `SeinARTS.Unit.Soldier`. Set Movement Ability to `SA_Move`. Compile and save.

On `SU_Soldier`, add **Demo Passenger**. Compile and save.

## 3. Produce Trucks

Create a Generic Ability named `SA_Produce_Truck`. Set Ability Name `Build Truck`, Dispatch Mode Single, Cost Timing Production Queue, and Resource Cost to 150 Minerals. Block it with `SeinARTS.State.UnderConstruction`.

Use the same queue-capacity Can Activate function as Train Soldier. On Activate, Enqueue Production with `SU_Truck`, then End Ability.

Grant it to the Factory. Its existing rally-point ability also works for Trucks. Compile and save.

## 4. Embark a Soldier

Create a Generic Ability named `SA_Embark`, Ability Name `Embark`, Target Type Entity, Max Range `350`, and Out Of Range Behavior Auto Move Then.

Set Cancel Abilities With Tag to `SeinARTS.Ability` so embarking can replace movement, attack, or harvesting.

Set Valid Target Tags to a query requiring `SeinARTS.Unit.Truck`. The ability checks these conditions on Activate:

1. Owner Entity and Target Entity are alive.
2. Get Entity Owner returns the same player ID for both.
3. **Is Contained** on Owner Entity is false.
4. **Can Accept Entity**, with Container = Target Entity and Entity = Owner Entity, is true.

If any check fails, End Ability. Otherwise call **Enter Container** with Entity = Owner Entity and Container = Target Entity, then End Ability. The containment operation checks capacity again, so simultaneous loading cannot exceed four seats.

Grant SA_Embark to the Soldier. Add a Default Commands entry requiring RightClick and Target.Friendly, Ability Tag `SeinARTS.Ability.Embark`, Priority `10`. The target-tag check rejects friendly entities that are not Trucks.

## 5. Keep passengers inactive and hidden

Override Can Activate on `SA_Move`, `SA_Attack`, `SA_Harvest`, `SA_Place_Barracks`, and `SA_Place_Factory` to return **NOT Is Contained(Owner Entity)**. If a graph already contains an eligibility rule, AND this condition with it. This also applies to movement orders issued while a Soldier remains selected after loading.

In `SU_Soldier`'s actor Event Graph, use Event Tick only for presentation:

1. Call **Get Entity Handle** on self.
2. Call Is Contained with that handle.
3. Set the skeletal mesh's Visibility to the inverse result.
4. Set its Collision Enabled to No Collision while contained and Query Only when outside.

The containment system removes hidden passengers from spatial queries and vision contribution. Hiding the mesh separately makes the visual representation agree. Keep this graph free of simulation writes.

Compile and save.

## 6. Unload passengers

Create a Generic Ability named `SA_Disembark`, Ability Name `Unload`. Grant it to the Truck.

On Activate, copy **Get Occupants(Owner Entity)** into a local entity-handle array. Iterate that copy with a For Each Loop; the live occupant array changes when passengers leave.

For each occupant at Array Index `i`:

1. Make a Fixed Vector local exit offset `(-300, -150 + 100 * i, 0)`.
2. Get the Truck's Fixed Transform with **Get Entity Transform**. Use Fixed Transform **Transform Position** to convert the local offset to a world position.
3. Call **Is Location Reachable** from the Truck's current location to that point. If false, leave this passenger aboard.
4. If true, call **Exit Container**, Entity = the occupant, Exit Location = the calculated world point.

Call End Ability from the loop's Completed output. Keep the four exit positions clear of other units and obstacles when testing. This simple reachability check does not reserve space against other units; the collision system handles local separation after a successful exit.

## 7. Show occupancy

Add a Text Block `OccupancyText` to the HUD SelectionPanel. In RefreshHUD, call **Get Containment Data** with the primary model's Entity. On success, display `Current Load / Total Capacity`. Otherwise collapse the text block.

## 8. Test transport

Produce a Truck, then move it onto open floor. Select four Soldiers and right-click it. Check occupancy reaches `4 / 4`, their meshes disappear, and their individual movement orders cannot move them while aboard.

Move the Truck, select it, and click Unload. The Soldiers should appear behind it and regain movement and harvesting. Try loading a fifth Soldier into a full Truck; it should remain outside. In the two-player test later, destroy a loaded Truck and verify its passengers are ejected under the authored death policy.

## Checkpoint

- The Factory produces the second mobile unit type.
- Capacity and accepted passenger types are authored on the Truck.
- Embark and Unload call the framework's containment operations.
- Passengers retain ownership and cannot act while contained.

Continue with [Add Fog and a Minimap](/guides/adding-fog-and-a-minimap/).
