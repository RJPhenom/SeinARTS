---
title: Deterministic Simulation
description: The boundaries that keep SeinARTS lockstep state reproducible across peers and platforms.
---

Determinism in SeinARTS is an architectural boundary. The authoritative simulation is designed to produce the same state from the same initial state and ordered command stream on every peer.

## Fixed-point state

Simulation math uses 32.32 fixed-point types:

- `FFixedPoint` for scalar values.
- `FFixedVector` for positions and directions.
- `FFixedTransform` and `FFixedQuaternion` for transforms and rotation.
- `FFixedRandom` for deterministic pseudo-random values.

Simulation code uses `FSeinEntityHandle` instead of raw Unreal object or actor pointers. Floating-point math, `FVector`, Unreal random helpers, and renderer-owned objects stay outside authoritative state.

## Simulation and presentation

Data crosses the boundary in one direction:

```text
Input → command buffer → deterministic simulation → render state → presentation
```

The input and presentation layers submit commands through the command buffer. They do not mutate simulation storage directly. Render telemetry can improve visuals or diagnostics, but it cannot become a simulation input.

## Canonical state

Lockstep systems need more than deterministic arithmetic. State ownership, iteration order, serialization, restore behavior, and derived caches must also remain canonical.

Changes to authoritative state therefore need lifecycle coverage across:

- Initial construction and reset.
- Snapshot and restore.
- Replay and reconnection.
- Schema and cache consistency.
- Fresh-process and peer comparison.

## Configuration is state

Project settings that can alter simulation results participate in a configuration fingerprint. Each owning plugin registers its contribution under a stable identifier with canonical property ordering.

A missing extension or mismatched sim-affecting setting must fail compatibility at join instead of becoming a delayed desynchronization.

## Proving determinism

A successful compile cannot prove deterministic behavior. Simulation changes are qualified with serial-versus-parallel state-hash comparisons and, where relevant, peer, replay, reconnection, and snapshot evidence.

