# SeinARTSSquadExtension — Plugin Guide

This opt-in runtime extension adds persistent heterogeneous-slot squads, formation dispatch,
reinforcement, and squad lifecycle behavior. Read the project-root `AGENTS.md` first. The adjacent
`CLAUDE.md` remains for Claude compatibility and must not be deleted. It may lag live code, so live
behavior and this concise guide win when they conflict.

## Boundary and dependency

- Runtime module: `SeinARTSSquad`.
- Required dependency: `SeinARTSFramework` and its core modules.
- The framework owns neutral squad data; this extension owns all squad behavior.
- The framework must continue to build and run when this extension is absent.

Core-defined data includes `FSeinSquadPayload`, `FSeinSquadMemberPayload`, slots,
reinforcement entries, containment mode, and squad visual-event factories. This plugin adds the
system, subsystem, formation, dispatch resolver, Blueprint libraries, settings, and starter
reinforcement ability. Do not duplicate the payload structs in the extension.

A squad is a lightweight non-abstract `ASeinActor`; presentation such as a banner can follow the
squad entity's centroid.

## Runtime flow

`USeinSquadSubsystem` registers `FSeinSquadSystem` with the world simulation and owns its lifetime.
The system performs deterministic lifecycle work including:

- Lazy initialization and member spawning.
- Member back-references, leader promotion, and dead-member removal.
- Persistent command-broker creation and membership synchronization.
- Squad centroid/actor transform and formation-radius maintenance.
- Slot cooldowns, reinforcement progress, and member spawn.
- Empty-squad cleanup.

The system runs in a stable PostTick position before command-broker dispatch. Reverify the live
priority before changing its registration or moving work between phases.

The module registers the frozen `SquadExtension` config-fingerprint contributor for
`bPaceSquadsTogether` and `DefaultSquadDispatchResolverClass`. The contributor ID and property names
are lockstep compatibility data: do not rename them without an explicit protocol/config migration.

## Dispatch and formation

`USeinSquadDispatchResolver` derives from the framework default broker resolver:

- Predetermined abilities use the broker capability map plus the ability's dispatch policy.
- Smart movement treats the squad as one outer formation element, then creates the squad's compact
  inner layout through `USeinFormation::MakeInnerLayoutTarget`.
- Preview and commit must use the same inner-layout computation.
- `USeinSlotFormation` uses authored slot transforms. A squad may override its formation class;
  non-slot formations must not accidentally consume authored slot offsets.
- Cover integration belongs in `SeinARTSCoverSquadExtension` through `PostProcessPositions`; Squad
  must not depend on Cover or on the bridge.
- `bAvoidAsBlob` and the broker's multi-squad pacing fields are the intended squad-to-base
  avoidance data seam; the framework does not load Squad settings directly.

Resolver selection is per-squad class, then squad settings default, then the Squad extension's
`USeinSquadDispatchResolver` fallback. Soft
class references and neutral fallbacks preserve extension stripping.

`bPaceSquadsTogether` is a sim-affecting outer-cohesion policy stamped onto squad broker data; it
must stay synchronized with the config fingerprint and must not make the framework read extension
settings directly.

## Mutation invariants

Squad Blueprint mutation helpers are sim-only operations. Any mutation must preserve all of:

- Member-to-squad back-reference.
- Slot occupancy and exact declaration-index identity; tags are query metadata.
- Broker member list.
- Leader validity.
- Deterministic declaration/handle ordering.
- Reinforcement and cooldown state.
- Squad brokers must keep `bSelfCullOnEmpty=false`; Squad owns lifetime and snapshot preflight
  rejects an unsafe broker before state commit.

Re-fetch components after storage additions. Never retain raw component pointers across an add or
spawn. Slot declaration indices are runtime identity: do not reorder the slot array after
initialization. Legacy whole-struct setters reject live topology; use exact index mutations.

## Reinforcement

The starter reinforce ability selects an eligible empty slot in deterministic declaration order,
charges the slot-authored cost atomically, and adds a monotonic request carrying exact slot index,
canonical tag metadata, snapshotted payer/cost, and build time. Exact cancellation reverses the
deduction before removing the request. Projects may replace this ability; reinforcement policy
must not become a mandatory core rule.

Canonical slot-tag metadata is the valid tag whose exact tag-name string sorts first. Runtime,
state hashing, and restore validation must use `FSeinSquadSlot::GetCanonicalSlotTag`; do not use
`FName::Compare` or process-local name/tag indices.

The request allocator, queue, slots, member back-references, and broker membership are canonical
snapshot state. Restore preflight rejects non-monotonic/duplicate requests, invalid or duplicate
slot occupancy, stale metadata, missing payers, and non-reciprocal membership before state commit.
Advance snapshot/envelope semantics and the Squad behavior/content revisions when these contracts
change.

Squad destruction settles queued reinforcement charges through the per-squad authored policy:
Refund, Forfeit, or PartialRefund with a fixed-point fraction. The deterministic teardown sweep
reads the core-owned squad component and applies the selected policy; do not add a second extension-
local settlement path.

## Verification

Squad changes need focused coverage for:

- Lazy initialization and destruction.
- Leader death/promotion and member back-references.
- Reinforcement enqueue, cooldown, completion, cancellation, and snapshot state.
- Mixed loose-unit/multi-squad dispatch.
- Preview versus first path destinations.
- Formation translation/rotation and deterministic member ordering.
- Framework-without-Squad and Squad-without-Cover builds.
- Serial/parallel state agreement and a PIE tactics-gym review for formation feel.

Prefer semantic assertions—membership, destination, slot identity, arrival, and state digest—over
brittle snapshots of every intermediate formation coordinate.
