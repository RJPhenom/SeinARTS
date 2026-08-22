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
| `SeinARTSCombat` | Genre-neutral vitals, damage/healing, weapon cycling, on-demand target acquisition, and instant/projectile delivery. |
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

Economy behavior is composed through abilities rather than a hardcoded worker type. Node stock and
worker cargo live in deterministic components accessed through typed component nodes; dropoff calls
**Grant Income** from an authorized simulation callback. Income validates the whole resource map
atomically and saturates valid uncapped overflow. Worker construction calls **Add Construction
Progress** against `FSeinConstructionComponent`; only positive, non-overflowing increments mutate,
completion removes the component and releases only the framework-owned
`State.UnderConstruction` grant. Designer-authored ownership of that tag remains intact.

Combat participation is opt-in through `FSeinVitalsComponent` and `FSeinWeaponComponent`.
`USeinDamageFormula` and `USeinTargetScorer` are stateless Blueprint policy CDOs; empty classes use
neutral built-ins. Target acquisition is an on-demand query, not an always-on engagement loop.
Abilities own engagement cadence and use the restricted fire/damage/heal mutations. Projectile
delivery spawns ordinary pooled entities, so projectile state follows the normal canonical
snapshot/replay/reconnect lifecycle.

## Player-pair capabilities

- Ordered player-pair capability grants are the authoritative relationship substrate. Direction
  matters: `A -> B` may differ from `B -> A`; for ShareVision, `A -> B` means B may consume A's
  vision. Self is implicit and self-pairs are never stored.
- Every grant carries a capability tag, source-kind tag, stable positive source-instance ID, and
  refcount. Overlapping sources compose; revocation removes only the exact matching source.
- Source records are canonical. The effective pair refcount map is a derived query cache that must
  validate, rebuild, hash, snapshot, restore, replay, and reconnect consistently with those records.
- Team ID is bootstrap seeding only. Team ID 0 preserves free-for-all defaults; after bootstrap the
  ledger is authoritative. Friendly/Enemy/Neutral remain UI disposition projections, not Core
  diplomacy state. Treaty/posture policy and capability consumers belong in optional layers.
- Canonical ordering and hashing use exact gameplay-tag names, never process-local `FName` or tag
  indices. Mutations occur only through authorized deterministic command timing.

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
`USeinPlannerHandle`/`USeinMoverHandle` seam. The shipped A* emits straight segments. Movement+
Wheeled and Tracked can prepend a bounded, clearance-probed Reeds-Shepp-style start maneuver as typed
`Arc`/`Straight` segments before runtime steering follows the coarse tail. That is a curated candidate
set, not a general Reeds-Shepp/Dubins route search; do not broaden it or replace it without the
Movement+ Vehicle Gym and product-feel decision.

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
