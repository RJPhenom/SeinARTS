---
title: Getting Started
description: Understand the SeinARTS architecture and choose the plugins your project needs.
---

SeinARTS is a deterministic lockstep RTS framework for Unreal Engine 5. Its C++ layer provides deterministic primitives and infrastructure; designers assemble game-specific behavior in Blueprint.

:::caution[Pre-release documentation]
SeinARTS is undergoing production-readiness qualification. Versioned installation, compatibility, and migration instructions will be published with the first public package. This guide currently covers the stable architecture and authoring model without promising an unreleased distribution workflow.
:::

## 1. Start with the framework

Every SeinARTS project uses **SeinARTSFramework**. It owns the simulation core, entities, abilities, effects, navigation, base movement and steering, fog of war, networking, editor tooling, UI, and gameplay shell.

The framework does not depend on any SeinARTS extension. Removing every optional extension leaves a valid core plugin.

## 2. Select optional capabilities

Enable only the extensions your game needs:

- **Squad** for persistent squads, formation dispatch, and reinforcement.
- **Cover** for cover providers, cover geometry, cover-aware dispatch, and formation preview.
- **Cover + Squad** only when both parent extensions are present and need to integrate.
- **Movement+** for infantry, wheeled, tracked, hover, and flight movement modes.
- **Online Services** for provider-neutral account, party, matchmaking, results, saves, replay evidence, and telemetry contracts.

See [Plugin Ecosystem](/ecosystem/) for the dependency map.

## 3. Author units as Blueprints

A unit type is a Blueprint subclass of `ASeinActor`. Its `USeinEntityComponent` bridge exposes a `ComponentData` array containing deterministic component payloads. At spawn, the framework copies those payloads into simulation storage.

Components remain pure data. Behavior belongs in abilities, effects, AI controllers, command brokers, and simulation systems.

## 4. Compose commands as abilities

Move, attack, harvest, build, patrol, garrison, and reinforce all use the same ability model. C++ supplies deterministic operations and lifecycle plumbing; Blueprint graphs compose the game-specific command flow.

Continue with [Units, Components, and Abilities](/core-concepts/units-components-abilities/).

## 5. Preserve the simulation boundary

Authoritative simulation code uses fixed-point types, entity handles, and the deterministic random generator. Unreal actors, floating-point vectors, and presentation-only systems stay outside simulation state.

Read [Deterministic Simulation](/core-concepts/deterministic-simulation/) before extending the simulation layer.

