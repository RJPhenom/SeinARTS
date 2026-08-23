---
title: Units, Components, and Abilities
description: How SeinARTS turns Blueprint-authored units and data components into deterministic simulation behavior.
---

SeinARTS keeps designer-facing authoring in Blueprint while the runtime simulation operates on deterministic data.

## The Blueprint is the unit

A unit type is a Blueprint subclass of `ASeinActor`. The actor automatically owns a `USeinEntityComponent`, which is the sanctioned bridge between the Unreal object and its simulation entity.

Designers author the unit's simulation data through the bridge's `ComponentData` array. Each entry is a deterministic component payload stored in an `FInstancedStruct`.

At spawn, the world subsystem copies the authored payloads into reflection-backed simulation storage. After that boundary, simulation systems work with entity handles and component data rather than actors.

## Components are data

Components describe state. They do not own event graphs or state-mutating behavior.

Framework simulation structs use the `SeinDeterministic` metadata marker so the editor can accept them as component payloads and validate their fields. Designers can also create custom deterministic component structs through the editor workflow.

Logic belongs in:

- Abilities.
- Effects.
- AI controllers.
- Command brokers.
- Simulation systems.

## Everything is an ability

Gameplay commands share one extensible model. Moving, attacking, harvesting, building, patrolling, garrisoning, and reinforcing are ability Blueprints rather than values in a hard-coded command enum.

C++ provides deterministic primitives, command activation and cancellation, and latent execution infrastructure. Designers compose those pieces into Blueprint graphs for the needs of their game.

## Presentation reacts to state

The actor and visual layer consume render state produced by the simulation. Animation, effects, audio, and presentation telemetry remain downstream and cannot write back into authoritative state.

