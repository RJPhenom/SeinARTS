---
title: Create the Skirmish Lobby
description: Add a two-player host-and-join menu, ready state, and travel into the authored skirmish map.
---

Complete [Add Fog and a Minimap](/guides/adding-fog-and-a-minimap/) first. This chapter adds a small local-network lobby. Both players start with one Soldier and use the same mineral, building, and production rules.

## 1. Add the second start

Open `LVL_DemoSkirmish`. Add a second **Sein Player Start**, with Player Slot `2` and Spawn Entity `SU_Soldier`. On the expanded floor, position the starts at approximately X `-2000` and `2000`, both at Y `0`, with their origins on the floor surface. Keep them inside the baked playable bounds.

Set the first start's Team ID to `1` and the second's to `2`. Place mineral deposits near both starts, with comparable space for buildings.

Save the map. Directly playing this map now represents two human slots; use the lobby to close a slot for a solo test or to bind both players for a network test.

## 2. Create the menu classes

In `Content/Demo/Blueprints`, create `PC_DemoMenuController` with native parent **SeinPlayerController**, and `GMB_DemoMenuGameMode` with native parent **SeinGameMode**.

Set the menu GameMode's Player Controller Class to PC_DemoMenuController and Default Pawn Class to the camera Blueprint created earlier. Leave the gameplay controller's input and HUD graph in its own class.

Create a Basic level and save it as `LVL_DemoMenu` in `Content/Demo`. In World Settings, set GameMode Override to GMB_DemoMenuGameMode and disable **Auto-Start Sim**. This level hosts the menu; do not add gameplay units, a level volume, or player starts to it.

## 3. Register the lobby destination

Open Project Settings > Plugins > SeinARTS. Add one **Available Maps** entry:

| Field | Value |
| --- | --- |
| Map | `LVL_DemoSkirmish` |
| Display Name | Demo Skirmish |

Set **Default Gameplay Map** to LVL_DemoSkirmish and **Main Menu Map** to LVL_DemoMenu. In Project Settings > Maps & Modes, set **Game Default Map** to LVL_DemoMenu.

The entry supports two slots and two teams. Available Maps supplies the lobby's destination and slot count. Normal asset saves and Play require no compatibility-data generation step.

## 4. Create the lobby layout

Create a SeinUserWidget named `WBP_DemoLobby` in `Content/Demo/Blueprints/UI`. Use a full-screen dark Border containing a centered Vertical Box. Add:

| Widget | Name / text |
| --- | --- |
| Text Block | `StatusText` |
| Editable Text Box | `AddressInput`, default `127.0.0.1:7777` |
| Button with Text Block | Host |
| Button with Text Block | Join |
| Vertical Box | `SlotRows` |
| Button with Text Block | Ready |
| Button with Text Block | Start |
| Button with Text Block | Leave |

On PC_DemoMenuController's BeginPlay, branch on Is Local Controller. Create WBP_DemoLobby with Owning Player self, Add to Viewport, enable Show Mouse Cursor, and Set Input Mode UI Only with the widget as the focus target.

## 5. Wire host, join, ready, and start

In the lobby widget's Construct, call **Get Or Create Lobby View Model** and store its return value as `LobbyModel`.

Wire button On Clicked events:

| Button | Node |
| --- | --- |
| Host | Request Host Session |
| Join | Request Join Session, Server Address from AddressInput text converted to String |
| Ready | Request Set Ready, value = NOT LobbyModel.Is Local Ready |
| Start | Request Start Match |
| Leave | Request Leave Lobby |

For calls returning Boolean, show a short error in StatusText when false. A true Join result means the connection request was issued; the client still has to connect.

In Event Tick, while LobbyModel is valid:

- Enable Ready only when Is In Lobby Session is true.
- Enable Start only when Is Host, Can Start Match, and Get Ready Count equals Get Claimed Count, with at least one claimed slot.
- Enable Host and Join only while not in a lobby session.
- Show `Ready: {ready} / {claimed}` in StatusText while connected.

The widget's ready condition is this demo's start policy. Request Start Match does not supply that UI policy by itself.

## 6. Create the slot row

Create a SeinUserWidget named `WBP_DemoLobbySlot`. Add a Horizontal Box containing `SlotText`, a Claim button, and a Close/Open button. Add an Integer variable `SlotIndex`, Instance Editable and Expose on Spawn.

Each row gets the lobby model through Get Or Create Lobby View Model. On Tick, call **Try Get Slot** with SlotIndex. On success, display the slot index, State, Team ID, and Ready value from the returned slot data. Disable Claim unless State is Open, and enable Close/Open only for the host and a slot whose state is Open or Closed.

On Claim, call **Request Slot Claim**, Slot Index = SlotIndex, Faction ID = a **Make Sein Faction ID** with Value `0`.

On Close/Open, call **Request Set Slot State** with Open if currently Closed, or Closed if currently Open. A closed slot creates no starting player unit.

In WBP_DemoLobby, create two rows once, with SlotIndex `1` and `2`, and add them to SlotRows. These are the map's two supported slots. The model may not have replicated them on the first frame; rows should retry their read on subsequent ticks.

## 7. Assign opposing teams

After Host succeeds, the local relay and claimed slot can arrive a little later. In the lobby widget, add a Boolean `TeamsInitialized`. During Tick, when Is Host is true and both Try Get Slot reads succeed, call **Request Set Team** for slot `1` with Team ID `1` and slot `2` with Team ID `2`.

Set TeamsInitialized true only after both requests return true. This keeps the lobby's teams consistent with the demo's opposing starts. The gameplay map consumes the published lobby settings on travel.

## 8. Test one player

Start LVL_DemoMenu in Play Standalone. Click Host, wait for the host to claim a slot, close the other Open slot, then click Ready and Start.

You should travel to LVL_DemoSkirmish with one Soldier, 500 Minerals, gameplay input, and the gameplay HUD. Build, harvest, research, and produce as in the earlier checkpoints.

## 9. Test two players

Use two standalone game processes. The host opens LVL_DemoMenu and clicks Host. The second process joins `127.0.0.1:7777` on the same machine, or the host's reachable local-network address on another machine.

Check that each process claims a different slot. Both players click Ready; the host clicks Start. Each should travel to the gameplay map and control only its own starting Soldier.

Move the Soldiers into sight and issue Attack. Health should fall by `3` per hit at `10` shots per second. A Soldier at 100 health should die on the 34th hit, approximately 3.3 seconds after the first hit with uninterrupted fire, with the same deaths visible on both peers. Test harvesting the same deposit, building concurrently, training units, and loading/unloading a Truck. Check fog independently in both windows.

## Checkpoint

- The menu runs without starting a gameplay simulation.
- Host and Join use the native lobby requests.
- Slot state, teams, and ready status are visible before travel.
- Gameplay starts from the committed roster and authored player starts.

Continue with [Package and Verify the Demo](/guides/packaging-and-verifying-the-demo/).
