---
title: Create the Demo Project
description: Start a blank Unreal Engine project, install SeinARTS, and configure the project before authoring the demo.
---

This series builds a small RTS demo from an empty project. You will create its maps, player controller, camera Blueprint, input assets, HUD, units, abilities, and game rules yourself.

The starting project has the framework installed and no demo content. The shipped demo is the completed result of these guides, available for readers who want to inspect or run the result. Here, you author that result yourself using the framework's native classes and Blueprint nodes. The first chapter installs the framework and prepares the project; its checkpoint is a configured editor, not a playable scene.

## 1. Prepare the tools and framework source

Use **Unreal Engine 5.8** for the current framework source. Install it through the Epic Games Launcher before creating the project.

This walkthrough uses a C++ project to compile the source plugin. The generated game module can remain unchanged while you author the demo's gameplay in Blueprint. Install Visual Studio with the **Game development with C++** workload and the compiler and Windows SDK required by your Unreal Engine installation.

Get the source from the [SeinARTS repository](https://github.com/RJPhenom/SeinARTS). Its binary assets use Git LFS. With Git and Git LFS installed, run these commands in a directory where you keep source downloads:

```powershell
git lfs install
git clone https://github.com/RJPhenom/SeinARTS.git SeinARTS-source
git -C SeinARTS-source lfs pull
```

Keep this source checkout separate from the new demo project. It supplies the plugin, not a project template to duplicate.

If you already have a compiled framework distribution matching your exact engine build and platform, install that complete plugin folder instead. Keep the distribution's binaries and supporting files together; the source-build instructions below apply to the repository checkout.

## 2. Create an empty project

1. Launch Unreal Engine 5.8.
2. In the project browser, choose **Games > Blank**.
3. Choose **C++**, target **Desktop**, and name the project `SeinDemo`.
4. Create the project and let the initial game module finish building.
5. Close the editor before installing the source plugin.

The Blank template's generated C++ files provide the build target. The series authors the game-specific behavior in Blueprint.

## 3. Install and enable the framework

Create a `Plugins` directory beside `SeinDemo.uproject`.

From the downloaded repository, copy `Plugins/SeinARTSFramework` into that directory. For a source installation, copy the descriptor and the source/support directories that exist: `Source`, `Content`, `Config`, `Resources`, and `Shaders`. Do not reuse the source checkout's `Binaries`, `Intermediate`, or `Saved` directories.

The resulting layout must include:

```text
SeinDemo/
  SeinDemo.uproject
  Source/
  Config/
  Content/
  Plugins/
    SeinARTSFramework/
      SeinARTSFramework.uplugin
      Source/
      Content/
      Config/
      Resources/
```

Check that the descriptor is directly inside `Plugins/SeinARTSFramework`, with no extra nested `SeinARTSFramework` directory.

1. Reopen `SeinDemo.uproject`. If Unreal asks to rebuild missing modules, allow it to build the source plugin.
2. Open **Edit > Plugins**, search for **SeinARTS Framework**, and enable it.
3. Confirm its declared dependencies, **Enhanced Input** and **Gameplay Tags Editor**, are enabled.
4. Restart the editor when prompted.

If the source build fails, stop and resolve its reported compiler or SDK error. With the editor closed, you can regenerate Visual Studio project files from the `.uproject`, open the solution, and build the `SeinDemo` project in **Development Editor / Win64** before reopening Unreal.

At this stage you need the core framework. Its built-in movement, navigation, and fog implementations are available without enabling the optional SeinARTS extensions.

All gameplay assets authored by this series belong to the project's own Content folder.

## 4. Configure Unreal's project settings

Open **Edit > Project Settings**. Configure the following values where they differ from your project's settings. Use the settings search to find the named property.

| Property | Value | Purpose |
| --- | --- | --- |
| World Settings Class | `SeinWorldSettings` | Gives newly created levels the framework's bootstrap controls. Restart the editor after changing this setting, before creating the gameplay map. |
| Default GameMode | `SeinGameMode` | Establishes the native gameplay base for the project. The next authoring step will create your own subclass. |
| Default Player Input Class | `EnhancedPlayerInput` | Enables the player input implementation used by Enhanced Input actions. |
| Default Input Component Class | `EnhancedInputComponent` | Enables Enhanced Input action bindings on the controller. |
| Import Tags From Config | Enabled | Loads the gameplay-tag sources written by the authoring tools. |

**Default GameMode** is on **Maps & Modes**. The two input defaults are on **Engine > Input**, under **Default Classes**. Search for **World Settings Class** directly; this is an engine setting and is different from a level's Game Mode Override.

For reference, the world-settings assignment is stored in the project's `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.Engine]
WorldSettingsClassName=/Script/SeinARTSFramework.SeinWorldSettings
```

The input assignments are stored in `Config/DefaultInput.ini`:

```ini
[/Script/Engine.InputSettings]
DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput
DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent
```

Changing the default world-settings class does not establish a player slot, create a faction, or wire input events. Those are assets and level configuration that you will author in the following steps.

## Checkpoint

Before creating the first Blueprint, verify:

- `SeinDemo` opens as a Blank C++ project without build or module errors.
- **SeinARTS Framework**, **Enhanced Input**, and **Gameplay Tags Editor** are enabled.
- **World Settings Class** is `SeinWorldSettings`, and you have restarted the editor after changing it.
- **Default GameMode** is `SeinGameMode`.
- **Default Player Input Class** is `EnhancedPlayerInput`, and **Default Input Component Class** is `EnhancedInputComponent`.
- **Import Tags From Config** is enabled, as set in step 4.

There is no Soldier, movement ability, custom controller, or gameplay map yet. Creating those assets is the next part of building the demo.

Continue with [Create the Gameplay Classes and Input](/guides/creating-gameplay-classes-and-input/).
