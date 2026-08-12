# SeinARTSSquadExtension — Plugin Guide

Opt-in extension adding **persistent, heterogeneous-slot squads** on top of
`SeinARTSFramework`: per-tick squad lifecycle, formation dispatch, and reinforcement. Strippable —
when this plugin is absent, the framework carries no squad *behavior* (only the data structs).

> **Read the project-root `CLAUDE.md` first** for the cross-cutting rules (determinism, sim/render
> separation, naming, no-worktrees, code-over-comments). This file covers squad mechanics only.

- **Module:** one runtime module, `SeinARTSSquad` (Default loading phase).
- **Depends on:** `SeinARTSFramework` (required). Build deps: Core, CoreUObject, Engine,
  DeveloperSettings, GameplayTags, `SeinARTSCore`, `SeinARTSCoreEntity`.

---

## The data/behavior boundary

This is the single most important thing to understand here: **the data lives in the framework
core; the behavior lives in this extension.**

Defined in **`SeinARTSFramework/SeinARTSCoreEntity`** (NOT here):
`FSeinSquadComponent` (slots, leader, reinforce queue; fields incl. `DispatchResolverClass`,
`bReassignSlotsLateral`/`bReassignSlotsDepth`; helpers `GetLiveMembers`, `ComputeCentroid`,
`IndexOfSlotByTag/ByMember`), `FSeinSquadMemberComponent`, `FSeinSquadSlot`,
`FSeinSquadReinforceEntry`, `ESeinSquadContainmentMode`, and the squad visual-event factories.

Defined **here**: the systems, the dispatch resolver, the BPFLs, the reinforce ability, settings.
This extension declares **no new USTRUCTs** — it operates entirely on the core's squad data.

> A squad is a **real lightweight (non-abstract) `ASeinActor`**, not an abstract presence-less
> entity — so a banner/health widget can follow the squad's centroid. (Older framework docs called
> squads "abstract"; that's obsolete.)

---

## Files & key types

- **`SeinARTSSquadModule.{h,cpp}`** — registers the frozen config-fingerprint contributor,
  simulation-content contributor, and reflected pool codecs for the reinforcement ability and
  dispatch resolver. Pre-unload terminates affected worlds and releases hosted systems before
  withdrawing codecs.
- **`SeinSquadSubsystem.{h,cpp}`** — `USeinSquadSubsystem` (`USeinSystemHostSubsystem`) creates and
  owns `FSeinSquadSystem` through the framework's hosted-system lifecycle.
- **`SeinSquadSystem.h`** — `FSeinSquadSystem` (`ISeinSystem`, **PostTick**, priority ~30, runs
  before the CommandBroker system). Owns the whole squad lifecycle:
  - lazy init (detected by broker absence → spawns slot members, wires member back-refs, builds the
    persistent command broker),
  - dead-member strip + `SquadMemberDied` events, leader promotion,
  - centroid → squad-actor transform + broker sync, `FormationWidth` calc,
  - slot cooldown decay, reinforce-queue progression + member spawn,
  - empty-squad cull.
- **`SeinSquadDispatchResolver.{h,cpp}`** — `USeinSquadDispatchResolver` (subclasses
  `USeinDefaultCommandBrokerResolver`). See "Dispatch" below.
- **`SeinSquadBPFL.{h,cpp}`** — `USeinSquadBPFL`, BlueprintPure queries: Get Squad/Member Data
  (+ batch), Get Squad Members/Leader/Size, Get Entity Squad, Is Squad Member.
- **`SeinSquadMutationBPFL.{h,cpp}`** — exact-index BlueprintCallable member/slot mutations plus
  reinforcement enqueue/cancel. They preserve broker membership, member back-references, leader,
  layout/reseek caches, queue accounting, and cooldown state. Legacy whole-struct setters reject
  live topology.
- **`SeinAbility_SquadReinforce.{h,cpp}`** — starter ability using the shared reinforcement service.
  It selects the first eligible exact slot in declaration order and atomically stores a monotonic
  request ID, canonical tag metadata, payer/cost snapshot, and build time. Exact cancellation
  reverses the committed deduction before removing the request.
- **`SeinARTSSquadSettings.{h,cpp}`** — `USeinARTSSquadSettings` (UDeveloperSettings). One field:
  `DefaultSquadDispatchResolverClass` (`TSoftClassPtr`).

---

## Dispatch (and the Cover extension point)

`USeinSquadDispatchResolver` (subclass of the default resolver) overrides **`ResolveDispatch`** and
adds a **constructor** that selects its formation:

- **`ResolveDispatch`** — for predetermined-ability orders, dispatches via the broker capability map
  filtered by the ability's own dispatch policy (`ApplyAbilityDispatchPolicy`) — leader-performs
  semantics (the squad leader performs the ability). Smart right-click orders route each member to its slot's world position, and DROP the
  order's gesture guide/formation tag so each squad keeps its own COMPACT shape at the anchor the parent formation gave it. Squads now
  participate in the multi-unit formation as ELEMENTS (sized by `FSeinCommandBrokerData::FormationRadius`
  = the squad's own footprint); the parent gesture spaces the squad ANCHORS, never each squad's
  internals. The gesture-free squad-internal target is built via `USeinFormation::MakeInnerLayoutTarget`,
  used by this resolver AND the preview (`SeinComputeFormationPreview`) so the two can't drift and the
  drag can never re-expand a squad's own formation.
- **Slot layout** — the constructor sets `DefaultFormationClass = USeinSlotFormation` as the DEFAULT, overridable
  per-squad via `FSeinSquadComponent::FormationClass` (Grid/Wedge/Ring/custom; the framework editor
  hides the per-slot `OffsetTransform` authoring for non-slot picks via `FSeinSquadSlotDetails` +
  `USeinFormation::UsesAuthoredSlotOffsets`). The slot formation reads
  each member's slot `OffsetTransform` by exact `SlotIndex`, rotated by anchor facing and
  nav-projected; unauthored squads / unresolved members fall back to a blob at the anchor. This
  replaced the old `ResolvePositions` override (squads now go through the same formation pipeline as
  loose units).
- **Cover extension point** — the inherited `ResolveFormationLayout` calls `PostProcessPositions`
  (empty base impl) after layout; the separate Cover+Squad bridge's
  `USeinCoverAwareSquadDispatchResolver` overrides exactly that to snap final member positions to
  cover.

**Resolver selection order:** per-squad `FSeinSquadComponent::DispatchResolverClass` → settings
`DefaultSquadDispatchResolverClass` (soft path; null if the bridge is absent) → framework default. A squad
registers its chosen resolver instance into its broker via `World.RegisterCommandBrokerResolver` at
lazy-init.

---

## Current state
**Implemented and under qualification** — exact reinforcement identity/accounting, completion,
membership reciprocity, snapshot v15 continuation, and structural restore admission are automated.
Product policy remains open for explicit squad destruction refunds, queue replacement,
wipe/recreation, and retreat; do not invent those semantics during unrelated maintenance.
