---
title: Create the Gameplay Classes and Input
description: Author the demo game mode, camera, controller, HUD, and basic Enhanced Input bindings from native framework classes.
---

Start after [Create the Demo Project](/guides/creating-the-demo-project/). Your project has the framework enabled and its project settings configured.

You will create the classes that run the scene and wire keyboard camera movement, mouse-wheel zoom, selection, commands, and selection modifiers. All the Blueprints and Input assets below are new assets you create in your project.

## 1. Create four Blueprint classes

In the Content Browser, right-click the project's **Content** root and choose **New Folder**. Name it `Demo`, then create a `Blueprints` folder inside it for the gameplay classes. `Content/Demo` corresponds to `/Game/Demo` in asset paths.

In `Content/Demo/Blueprints`, right-click and choose **Blueprint Class**. Expand **All Classes**, search for the native parent, select it, and create the Blueprint. Repeat for each row:

| New Blueprint | Native parent | Responsibility |
| --- | --- | --- |
| `BP_DemoSkirmishCameraPawn` | `SeinCameraPawn` | Camera pivot, spring arm, zoom, pan, and ground following. |
| `PC_DemoSkirmishController` | `SeinPlayerController` | Player input, selection, and issuing commands. |
| `HUD_DemoSkirmishHUD` | `SeinHUD` | Selection marquee, command feedback, and debug overlays. |
| `GMB_DemoSkirmishGameMode` | `SeinGameMode` | Selects the classes used when the level starts. |

Compile and save each Blueprint. These are ordinary Blueprint subclasses of the named native gameplay classes, not unit/entity assets.

The camera parent already creates its pivot, spring arm, and camera components.

The inherited HUD draws the selection marquee without a Widget Blueprint. You will create the resource and action panels when their gameplay data exists.

## 2. Assign the classes to the game mode

Open `GMB_DemoSkirmishGameMode`, select **Class Defaults**, and assign:

| Setting | Blueprint created above |
| --- | --- |
| Default Pawn Class | `BP_DemoSkirmishCameraPawn` |
| Player Controller Class | `PC_DemoSkirmishController` |
| HUD Class | `HUD_DemoSkirmishHUD` |

Compile and save. In **Project Settings > Maps & Modes**, change **Default GameMode** from the native base to this new Blueprint.

Do not place a second camera pawn in the level. The game mode will spawn the configured pawn for the player.

## 3. Create the Input Actions

Create an `Input` folder inside `Content/Demo/Blueprints` for the input assets. Open it, right-click and choose **Input > Input Action**. Create each action and set its **Value Type**:

| Action | Value Type |
| --- | --- |
| `IA_KeyPan` | Axis2D |
| `IA_KeyRotate` | Axis2D |
| `IA_KeyZoom` | Axis1D |
| `IA_MouseZoom` | Axis1D |
| `IA_Select` | Digital (bool) |
| `IA_Command` | Digital (bool) |
| `IA_KeyModifier_Shift` | Digital (bool) |
| `IA_KeyModifier_Ctrl` | Digital (bool) |
| `IA_KeyModifier_Alt` | Digital (bool) |

Save the assets.

## 4. Create the Input Mapping Context

In the same folder, choose **Input > Input Mapping Context** and name it `IMC_SeinARTSDefaultMappingContext`.

Add the following action/key mappings. Where modifiers are listed, add them to that individual key mapping in the listed order, rather than to the whole Input Action.

| Action | Key | Mapping modifiers | Result |
| --- | --- | --- | --- |
| `IA_KeyPan` | D | None | Positive X: pan right. |
| `IA_KeyPan` | A | Negate | Negative X: pan left. |
| `IA_KeyPan` | W | Swizzle Input Axis Values, order YXZ | Positive Y: pan forward. |
| `IA_KeyPan` | S | Negate, then Swizzle Input Axis Values (YXZ) | Negative Y: pan backward. |
| `IA_KeyRotate` | E | None | Positive X: rotate. |
| `IA_KeyRotate` | Q | Negate | Negative X: rotate the other way. |
| `IA_KeyZoom` | X | None | Positive zoom input. |
| `IA_KeyZoom` | Z | Negate | Negative zoom input. |
| `IA_MouseZoom` | Mouse Wheel Axis | None | Signed wheel zoom input. |
| `IA_Select` | Left Mouse Button | None | Select press/release. |
| `IA_Command` | Right Mouse Button | None | Command press/release. |
| `IA_KeyModifier_Shift` | Left Shift and Right Shift, as separate mappings | None | Shift modifier. |
| `IA_KeyModifier_Ctrl` | Left Control and Right Control, as separate mappings | None | Control modifier. |
| `IA_KeyModifier_Alt` | Left Alt and Right Alt, as separate mappings | None | Alt modifier. |

A keyboard key starts as a positive one-dimensional value. **Negate** changes its sign. **Swizzle Input Axis Values (YXZ)** moves that value onto Y, so W/S and A/D can drive different axes of the same action.

Save the mapping context.

## 5. Add the mapping context for the local player

Open the Event Graph of `PC_DemoSkirmishController`.

Build this execution chain:

```text
Event BeginPlay
  → Branch: Is Local Controller (self)
    True → get the Enhanced Input Local Player Subsystem for self
         → Is Valid (the subsystem)
           Valid → Add Mapping Context
                     Mapping Context = IMC_SeinARTSDefaultMappingContext
                     Priority = 0
```

Obtain the subsystem from this controller's local player. Do not use another player's controller or add the mapping context in a unit Blueprint.

The native controller already configures game-and-UI input mode and shows the cursor for the local player. This graph supplies the mapping context; it does not replace the native selection or command implementations.

## 6. Wire the axis actions

Right-click the controller's Event Graph and add the Enhanced Input event for each action below. Use the event's **Triggered** execution pin. Convert or extract its **Action Value** as the specified type and connect it to the handler's value input. The target is `self`.

| Action event | Value | Call |
| --- | --- | --- |
| `IA_KeyPan` | Vector2D | **Handle Key Pan** |
| `IA_KeyRotate` | Vector2D | **Handle Key Rotate** |
| `IA_KeyZoom` | Float | **Handle Key Zoom** |
| `IA_MouseZoom` | Float | **Handle Mouse Zoom** |

For example, `IA_KeyPan` feeds its Axis2D Action Value into **Handle Key Pan → Axis Value** on every Triggered event.

Use these handler nodes from inside the controller subclass. They forward the input to its camera pawn with the controller's configured speed modifiers. The camera consumes pan input each frame; no custom camera Tick graph is needed.

## 7. Wire selection and commands

Add the `IA_Select` and `IA_Command` Enhanced Input events. Expand their event pins if needed and wire:

| Action | Started | Completed | Canceled |
| --- | --- | --- | --- |
| `IA_Select` | **Handle Select Pressed** | **Handle Select Released** | **Handle Select Canceled** |
| `IA_Command` | **Handle Command Pressed** | **Handle Command Released** | **Handle Command Canceled** |

Press begins tracking the click or drag. Release completes it. Cancellation discards it without issuing a command or committing a selection.

Leave **Triggered** unconnected for these two actions. Calling the press handler repeatedly while the button is held would repeatedly restart the gesture.

## 8. Wire the modifier actions

For each modifier action, call its setter with `true` on **Started**, and with `false` on both **Completed** and **Canceled**:

| Action | Setter |
| --- | --- |
| `IA_KeyModifier_Shift` | **Set Shift Held** |
| `IA_KeyModifier_Ctrl` | **Set Ctrl Held** |
| `IA_KeyModifier_Alt` | **Set Alt Held** |

Each setter's target is `self`. Wire explicit true and false values; do not leave the release path latched true.

Compile and save the controller, game mode, camera, HUD, actions, and mapping context.

## Checkpoint

Before creating the level, inspect your assets:

- Each gameplay Blueprint has the native parent shown in step 1.
- The game mode points to your new controller, camera, and HUD.
- The mapping context references only the actions you created.
- BeginPlay adds that context to the local player's Enhanced Input subsystem.
- Axis events pass their Action Value to the matching camera handler.
- Selection and command events have distinct press, release, and cancellation connections.
- Modifier release and cancellation clear their held state.
- All assets compile and save.

These graphs establish the controls. Camera movement and the selection marquee can be checked once the next guide supplies the ground and player start. Selecting a Soldier and issuing a move command will become testable after you author that unit and its movement ability.

Continue with [Create the First Level](/guides/creating-the-first-level/).
