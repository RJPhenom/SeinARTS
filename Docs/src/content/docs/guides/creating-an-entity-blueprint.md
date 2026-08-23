---
title: Creating an Entity Blueprint
description: Create a SeinARTS entity Blueprint and separate its authored simulation data from its Unreal presentation.
---

In SeinARTS, an entity Blueprint is the authored definition and Unreal-side presentation for a unit, building, projectile, or other simulation-backed object. This guide creates a simple entity Blueprint, checks its identity, and explains where visual and simulation data belong.

## What you will make

- A Blueprint based on `ASeinActor`.
- One automatically attached **SeinARTS Entity Bridge**.
- An identity component generated from the asset name when auto-tag generation is enabled.
- A visual component, such as a Static Mesh or Skeletal Mesh, kept separate from authoritative simulation data.

## Before you begin

- Open an Unreal Engine project with the SeinARTS Framework and SeinARTS editor tooling enabled.
- Choose a Content Browser folder for your entity assets. For example, `Content/Entities/Units`.
- Decide what kind of entity you are creating. This walkthrough uses a visible unit, not an abstract simulation-only entity.

## 1. Create the entity Blueprint

1. Open the **Content Drawer** or **Content Browser**.
2. Right-click an empty area and choose **SeinARTS Entity Blueprint** from the **SeinARTS** asset category.
3. In **Pick Parent Class for Entity**, choose **Generic Entity**. This creates a bare Blueprint subclass of `ASeinActor`.
4. Name the asset `SU_Infantry_Officer`.
5. The asset opens in the Blueprint Editor.

### Why the name matters

With the default auto-tag settings, SeinARTS reads the asset prefix and underscores to create an identity tag. In this example, `SU_Infantry_Officer` becomes `SeinARTS.Unit.Infantry.Officer`.

The standard entity prefixes are:

- `SU_` for a unit.
- `SBP_` for a generic entity.
- `SR_` for research.

If the generated tag would duplicate a tag already used by another SeinARTS asset, the editor refuses the creation and asks you to choose a different name. Identity tags identify entities and allow them to be looked up reliably in the simulation.

## 2. Inspect the SeinARTS Entity Bridge

1. In the Blueprint Editor, find **SeinARTS Entity Bridge** in the Components panel.
2. Select it, then find the **SeinARTS** section in the Details panel.
3. Leave **Is Abstract** disabled for this guide. A visible unit needs an actor and a rendered presence.
4. Expand **Component Data**. This array is the authoring surface for deterministic simulation component data.

### The three authoring areas to know

- **Component Data** stores the simulation-data templates that are copied into deterministic storage when the entity spawns.
- **Base Tags** stores tags that every instance of this entity begins with. Do not add a separate tags component.
- **Is Abstract** removes actor and visual presence entirely. Use it only for entities that should exist purely in the simulation.

## 3. Confirm the identity component

1. Look for an **Identity Component** entry in **Component Data**.
2. Expand the entry and verify that **Identity Tag** is `SeinARTS.Unit.Infantry.Officer`.
3. Optionally fill in **Display Name**, **Description**, **Icon**, **Portrait**, or **Minimap Icon**. These fields can describe the entity to the player-facing UI.

If no Identity Component was created, auto-tag generation may be disabled or the asset name may not use a recognized prefix. Check the auto-tag configuration, or add an Identity Component manually and assign it a unique Identity Tag.

## 4. Add the visual representation

1. In the Components panel, add a Static Mesh or Skeletal Mesh component.
2. Attach it to the Blueprint's scene root.
3. Assign the mesh and any materials, animation Blueprint, particles, audio, or other presentation components the entity needs.

These components form the **Unreal Render Layer**, or simply the **Render Layer**. They can show the simulation, but their floating-point transforms, collision, or animation state must never become authoritative simulation state. The SeinARTS Framework provides Blueprint and Animation Graph APIs for reading and reacting to simulation-state changes in the Render Layer.

## 5. Add simulation data only when the entity needs it

1. Select **SeinARTS Entity Bridge**.
2. Under **Component Data**, add an array entry and choose the required component type from the filtered picker.
3. Configure the authored fields for that component.
4. Add no more than one entry of each component struct type.

Common examples include:

- **Extents Component** for the entity's simulation-side body or footprint.
- **Movement Component** for movement configuration and state.
- **Abilities Component** for activating ability scripts and defining actions the entity can take.

A newly created entity does not need every component. Component structs hold data only; behavior belongs in abilities, effects, AI, command brokers, and simulation systems.

## 6. Compile and save

1. Click **Compile** and resolve any Blueprint errors.
2. Click **Save**.
3. Check that the Blueprint contains exactly one SeinARTS Entity Bridge and no duplicate Component Data types.

## Optional: place the entity in a level

1. Drag the Blueprint into the level.
2. Select the placed instance and set **SeinARTS → Ownership → Player Slot**. Use `0` for neutral, or a one-based player slot for an owned entity.
3. Position and rotate the actor, then save the level. SeinARTS bakes the placed actor's deterministic transform during editor moves.

Registering and controlling the entity in play also requires the level's SeinARTS match setup. That setup is outside this guide.

## What happens when the entity spawns

- SeinARTS acquires a deterministic entity handle and starting transform.
- The Entity Bridge copies each authored Component Data entry into live simulation storage.
- The identity tag and Base Tags seed the entity's runtime tag state.
- The actor and its visual components present the simulation state to the player.

Component Data is an authoring template, not a live mirror of the running entity. Runtime gameplay code should read and change simulation storage through the framework's supported systems and Blueprint libraries.

## Common mistakes

- Creating an ordinary Actor Blueprint instead of a SeinARTS Entity Blueprint.
- Adding a second Entity Bridge. The `ASeinActor` parent already provides one.
- Adding the same component struct type twice. Duplicate types are an authoring error.
- Enabling **Is Abstract** on an entity that needs a mesh, actor, or other visual presence.
- Putting behavior inside a component struct. SeinARTS components contain data only; abilities and systems perform the work.
- Treating the actor's transform, collision, or animation state as authoritative gameplay state.
- Editing Component Data during play and expecting the already-spawned simulation component to change.

## Finished checklist

- The asset was created with **SeinARTS Entity Blueprint**.
- Its parent class is `ASeinActor`.
- It contains exactly one **SeinARTS Entity Bridge**.
- Its identity tag is present and unique.
- Its visual components are separate from Component Data.
- Component Data contains no duplicate struct types.
- The Blueprint compiles and saves without errors.

You now have a clean SeinARTS entity Blueprint that can be extended with simulation components, abilities, and game-specific presentation.
