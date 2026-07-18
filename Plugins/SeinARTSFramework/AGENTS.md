# SeinARTSFramework — Plugin Guide

The core plugin owns the deterministic simulation, entity/ability/effect infrastructure,
level data, navigation, movement foundations, fog of war, lockstep transport, and the host
gameplay/editor/UI layers.

Read the project-root `AGENTS.md` first. It owns the cross-plugin invariants, build commands,
no-worktree rule, naming, and verification discipline. This file is intentionally narrower.
The adjacent `CLAUDE.md` is retained for Claude compatibility; do not delete it. It may lag live
code, so live behavior and this concise guide win when they conflict.

The Squad, Cover, and Movement+ behaviors are opt-in extensions. Core may expose neutral data
contracts used by them, but it must never depend on an extension module.

## Module map

| Module | Responsibility |
|---|---|
| `SeinARTSCore` | Fixed-point math, deterministic geometry, time, and PRNG. Leaf dependency. |
| `SeinARTSCoreEntity` | Entity pool/storage, actor bridge, sim tick, commands/brokers, abilities/latent actions, effects/attributes, production, containment, resources, match state, snapshots, and visual events. |
| `SeinARTSLevelData` | Unified baked substrate, canonical grid, shared traces, layer-provider registry, level volume, and baked channels. |
| `SeinARTSNavigation` | Abstract navigation contract, shipped A* implementation, path requests, reachability, direction queries, and typed path data. |
| `SeinARTSMovement` | Abstract movement/avoidance contracts, Basic defaults, persistent movement instances, Move To action/proxy, planner/mover handles, shared steering, and movement driver. |
| `SeinARTSFogOfWar` | Abstract FoW contract, shipped grid implementation, visibility state/queries, layer bake, and sim-side stamping. |
| `SeinARTSNet` | Lockstep turn aggregation, relays, lobby, replay, state-hash exchange, and session lifecycle. |
| `SeinARTSFramework` | Player controller, camera, HUD, selection, targeters/previews, game mode, and match bootstrap. |
| `SeinARTSUIToolkit` | Read-only view models, selection model, widget pooling, and UI helpers. |
| `SeinARTSEditor` | Factories, validators, Details customizations, graph pins, thumbnails, and entity-bridge visualization registry. |
| `SeinARTSGraphNodes` | Uncooked typed Blueprint component get/set nodes. |
| `SeinARTSFogOfWarEditor` | FoW authoring visualization registered with `SeinARTSEditor`. |

Editor companion modules load after `SeinARTSEditor` so registry hooks are available.

## Entity and component mechanics

- `FSeinEntityPool` is generational. Slot 0 and generation 0 are invalid. Identity comparisons
  and cache keys must include the generation, not only the slot index.
- Runtime component storage is reflection-backed `FSeinGenericComponentStorage`, keyed by
  `UScriptStruct*`. Typed accessors are facades over the raw storage.
- `ASeinActor` owns the `USeinEntityComponent` actor bridge. Its `ComponentData` is an authoring
  template copied into sim storage at spawn; it is not a live mirror of runtime components.
- Blueprint CDO component discovery must use `AActor::GetActorClassDefaultComponents`, which
  includes native and Blueprint SCS components. `GetComponents` on a CDO is insufficient.
- Storage may reallocate on `AddComponent`; never retain a component pointer across an add.
- Entity tags are centralized state on `USeinWorldSubsystem`, not a component payload.
- Component payloads are pure deterministic data. Put behavior in systems, policies, abilities,
  effects, controllers, or brokers.

## Simulation and state

`USeinWorldSubsystem` advances fixed ticks through ordered phases:

1. `PreTick`
2. `CommandProcessing`
3. `AbilityExecution`
4. `PostTick`

Registration order, phase, and priority are simulation contracts. New systems need a documented
position and a deterministic total order. Parallel work must follow parallel-read/serial-apply or
otherwise prove disjoint deterministic mutation.

`TimeAccumulator` is intentionally a render/wall-clock `float`; the delta entering simulation is
fixed-point. Do not make scheduler time part of authoritative state.

Every value that can affect a future tick must eventually participate in the canonical state
contract: hash, capture, restore, reset, replay, and reconnect. Process-local identities such as
pointer values and `FName` pool indices are never canonical serialization or hash material.

Sim-affecting settings owned by this plugin or an extension participate in
`FSeinConfigFingerprintRegistry` under frozen contributor IDs and exact reflected property names.

## Abilities, effects, and commands

- Gameplay behavior is normally a Blueprintable `USeinAbility`; latent actions provide explicit
  cooperative multi-tick state.
- Ability completion is explicit. Terminal latent pins do not automatically end or cancel the
  owning ability.
- Ability arbitration is tag-based through blocked, owned, and cancel tags.
- Commands use a gameplay tag plus `FInstancedStruct` payload. Transport/control plumbing may
  register command handlers, but ordinary gameplay remains ability-driven.
- Command brokers expand a logical recipient into deterministic performers. Resolver selection
  and provider composition require stable identities and ordering.
- Effects support instance, class-per-player, and player scopes. Identity and future state must
  remain unambiguous across every storage scope.

## Pluggable system seams

The shipped implementation is a default, not a mandatory genre rule:

- `USeinLevelData` — baked substrate and provider registry.
- `USeinNavigation` — paths and direction queries.
- `USeinMovement`, `USeinAvoidance`, and `USeinCollisionResolver` — movement policy and collision.
- `USeinFogOfWar` — authoritative visibility implementation.

Concrete classes are selected through project settings or per-entity soft class paths. Base
modules must tolerate an owning optional module being absent; an unbound cross-module resolver
uses its documented neutral fallback.

The unified level bake is synchronous and writes regenerable assets beneath the configured
LevelData folder. Navigation and FoW contribute independent layer providers and may use different
resolutions while sharing the substrate coordinate contract.

`FSeinPath` is a typed-segment seam. Navigation may emit topology kinds such as `Field` or
`AbstractEdge`; per-unit `USeinMovement::PlanPath` may shape kinematic kinds through the
`USeinPlannerHandle`/`USeinMoverHandle` seam. `Arc` and `Jump` are represented and flattened by
consumers, but the shipped A* currently emits straight segments and no shipped vehicle class
produces Reeds-Shepp/Dubins curves. Do not add runtime Reeds-Shepp/Dubins search under the guise of
movement feel; future high-fidelity curves are steering-first and, if used, approved as
offline/authored data.

## Movement ownership

- `USeinMovementSubsystem` owns persistent movement instances per entity; orders borrow them.
- `OnMoveBegin` resets per-order state. Idle ticks handle ground snap, coast-down, and shove
  settling without inventing a return-to-home destination.
- Avoidance outputs a lateral steer plus speed scale; it is not a full velocity replacement.
- Concrete Infantry/Wheeled/Tracked/Hover/Flight modes belong to Movement+, never core.
- Persistent future-affecting state may live on the movement instance and mutable component
  subdata; both sides obey the canonical state contract.

## Render/editor boundary

- Simulation emits `FSeinVisualEvent`; presentation consumes it one-way.
- Input and UI may mutate sim only through commands.
- `SeinARTSEditor` exposes a keyed component-data draw registry. Extensions own both registration
  and unregistration and must use stable unique keys.
- Editor-only random authoring is permitted only when its fixed-point result is serialized and
  runtime behavior never repeats the random operation.

## Common failure modes

- Treating `ComponentData` as runtime state.
- Holding component pointers across storage mutation.
- Keying entity state by index without generation.
- Depending on `TMap`/`TSet` iteration order in sim output.
- Calling Blueprint or delegates while iterating a container they may synchronously mutate.
- Hashing `FName` comparison indices instead of canonical names/schema IDs.
- Letting navigation silently relocate an initial destination after preview resolution.
- Adding an extension dependency to core or a Public header dependency only to `PrivateDependencyModuleNames`.
- Trusting old `DESIGN`/`PLAN` references or “Phase 0” narration over live behavior.

## Verification expectations

For deterministic framework changes, verification normally includes:

- Focused native regression tests under explicit non-shipping test organization.
- Headless subsystem/world scenarios where integration matters.
- Development build plus relevant clean/Shipping and plugin-stripping builds.
- `Sein.Sim.Parallel 0` versus `1` canonical state comparison.
- Snapshot restore followed by next-N-tick equivalence when state is touched.
- Replay/peer comparison when command timing or networking is touched.
- An independent adversarial review and RJ's PIE A/B for behavior or feel.

Do not freeze incidental implementation details in tests. Assert public contracts, invariant
properties, canonical state, and explicitly versioned compatibility outputs.
