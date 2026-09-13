---
title: Package and Verify the Demo
description: Cook the example, check its complete gameplay loop, and prepare a reproducible project for distribution.
---

Complete [Create the Skirmish Lobby](/guides/creating-the-skirmish-lobby/) first. You now have a small skirmish sandbox: two unit types, two production buildings, one research unlock, mineral income, transport, and a local-network lobby.

## 1. Save and compile the project

Compile every authored Blueprint and save all assets and both maps. Resolve Blueprint compiler errors before packaging.

Check the final grants:

| Entity | Abilities |
| --- | --- |
| Soldier | Move, Attack, Harvest, Place Barracks, Place Factory, Embark |
| Barracks | Complete Construction, Produce Soldier, Produce Research Factory, Set Rally Point |
| Factory | Complete Construction, Produce Truck, Set Rally Point |
| Truck | Move, Disembark |

The Soldier remains each player's Spawn Entity. The starting balance is enough to place the first Barracks; harvesting supports the rest of the economy.

## 2. Include the maps in the cook

Open Project Settings > Packaging. Add `/Game/Demo/LVL_DemoMenu` and `/Game/Demo/LVL_DemoSkirmish` to **List of maps to include in a packaged build**.

Use **Development** for the first package. The compiled game module must include the transport authoring classes, and the saved Blueprints must have their baked component data. Unreal's cook follows the map, ability, effect, widget, mesh, and material references you authored.

The framework builds compatibility evidence during a normal by-the-book cook. There is no manual manifest creation or regeneration step. Treat a compatibility build error as a cook failure and fix the specific missing or invalid input reported in the log.

## 3. Package Windows

With the Windows build tools installed, use Unreal's Platforms menu to package the project for Windows into a directory outside the project source tree.

Read the final packaging result and check for errors, rather than relying on the presence of an executable. Launch the packaged executable. It should open the menu with the lobby UI.

## 4. Run the complete solo loop

Host a session, close the other slot, ready, and start. Check:

| Action | Expected result |
| --- | --- |
| Start | One owned Soldier; 500 Minerals |
| Move and queue moves | Unit travels, animates, stops, and visits queued points |
| Harvest a 25-stock deposit | Exactly 25 income; deposit disappears |
| Place Barracks | One 100-Mineral charge; ten-second construction |
| Train Soldier | One 50-Mineral charge; five-second queue entry; spawned unit follows rally |
| Research Factory Access | One 100-Mineral charge; unlock after ten seconds |
| Place Factory | Requires unlock; one 200-Mineral charge |
| Build Truck | One 150-Mineral charge; eight-second queue entry |
| Embark and unload | Capacity four; passengers hide, travel, and regain control after exit |
| Minimap and fog | Camera clicks align; visible and explored areas follow the local player's units |

Also try unaffordable production, a full queue, invalid placement, an exhausted deposit, a full Truck, and cancelling a move or harvest order. Failure should leave resources and entities in a consistent state.

## 5. Verify two packaged peers

Run two copies of the same Development package. Host on one and join from the other. Ready both slots and start.

Play the full loop from both players. In particular, attack the same unit with multiple Soldiers, harvest the same deposit concurrently, and destroy a loaded Truck. Verify resource totals, ownership, deaths, passengers, and production agree on both peers. Each player's fog should show only their own visibility.

Repeat a match after leaving and rehosting. Do not count a successful connection as proof of a complete match.

## 6. Build Shipping

After the Development checks pass, package **Shipping** and repeat the menu-to-match smoke test and core gameplay loop. Use the same Shipping package on both peers.

Keep the Development package and its logs available while reviewing failures; Shipping removes many diagnostics. A successful cook or native compile does not replace the interactive checks above.

## 7. Prepare the example project

Distribute the source project with its `.uproject`, Source, Config, required Plugins, and authored Content. Include the plugin and imported content notices required for the assets you distribute.

Leave Intermediate, Saved, DerivedDataCache, and temporary packaging directories out of the source archive. When the source distribution omits regenerated level-bake assets, include a short setup instruction to select the Sein Level Volume in LVL_DemoSkirmish, press **Bake Level Data**, and save before playing or packaging.

Write a short project README with:

- The Unreal Engine version and required plugin.
- How to build the C++ project and open LVL_DemoMenu.
- Camera controls and right-click orders from the input guide.
- The Host, Join, Ready, and Start sequence.
- The demo loop: harvest, build, train, research, transport, and fight.

## Final checkpoint

The example can be built from its source, opened through its menu, played locally or with a second peer, and packaged without manual compatibility-data generation. Keep the per-chapter checkpoints beside your review notes as you refine the scene and add its images.
