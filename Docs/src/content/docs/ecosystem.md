---
title: Plugin Ecosystem
description: The production plugins, extension boundaries, and dependency rules that make up SeinARTS.
---

SeinARTS ships as one core plugin and five opt-in production extensions. Dependencies point toward the framework, never back from the framework into an extension.

## Production plugins

| Plugin | Adds | Requires |
| --- | --- | --- |
| **SeinARTSFramework** | Deterministic simulation, entities, abilities, effects, navigation, base movement, fog of war, networking, editor tooling, UI, and gameplay shell | Nothing else in SeinARTS |
| **SeinARTSSquadExtension** | Persistent squads, formation dispatch, and reinforcement | Framework |
| **SeinARTSCoverExtension** | Cover providers and geometry, cover-aware dispatch, and formation preview | Framework |
| **SeinARTSCoverSquadExtension** | The optional integration bridge between cover and squad dispatch | Framework + Cover + Squad |
| **SeinARTSMovementPlusExtension** | Infantry, wheeled, tracked, hover, and flight movement modes with per-class tuning | Framework |
| **SeinARTSOnlineServicesExtension** | Provider-neutral account, party, matchmaking, results, saves, replay evidence, telemetry, and provider adapters | Framework |

## Dependency shape

```text
SeinARTSFramework
├── SeinARTSSquadExtension
├── SeinARTSCoverExtension
├── SeinARTSMovementPlusExtension
├── SeinARTSOnlineServicesExtension
└── SeinARTSCoverSquadExtension
    ├── SeinARTSSquadExtension
    └── SeinARTSCoverExtension
```

The Cover and Squad extensions are intentionally independent. Their only cross-extension integration is the separate Cover + Squad bridge, which a game enables only when it uses both parent capabilities.

## Development-only test plugins

Two disabled test plugins validate the ecosystem without becoming shipping dependencies:

- **SeinARTSTestSuite** exercises the framework and editor surfaces.
- **SeinARTSExtensionTestSuite** consumes the base test suite and every opt-in extension for integration coverage.

No production plugin may depend on either test plugin.

## Shared contracts

Every production plugin follows the same boundaries:

- Simulation state flows to rendering; rendering does not write authoritative state.
- Simulation uses deterministic fixed-point math and stable entity handles.
- Designer-authored Blueprint content composes deterministic C++ primitives.
- Sim-affecting settings contribute to the lockstep configuration fingerprint.
- Optional features remain removable without breaking the framework.

