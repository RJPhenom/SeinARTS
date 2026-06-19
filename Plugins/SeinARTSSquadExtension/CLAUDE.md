# SeinARTSSquadExtension — Plugin Guide

Opt-in extension adding **persistent, heterogeneous-slot squads** (CoH-style) on top of
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

- **`SeinARTSSquadModule.{h,cpp}`** — `IModuleInterface` with empty startup/shutdown. All work is
  subsystem-driven; there is **no module-startup registry hook**.
- **`SeinSquadSubsystem.{h,cpp}`** — `USeinSquadSubsystem` (UWorldSubsystem). On
  `OnWorldBeginPlay` it `new`s `FSeinSquadSystem` and calls `Sim->RegisterSystem(...)`; deletes/
  unregisters on `Deinitialize`. This is the **only** integration mechanism with the framework.
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
- **`SeinSquadMutationBPFL.{h,cpp}`** — `USeinSquadMutationBPFL`, BlueprintCallable mutations,
  `meta=(RestrictedToClasses="SeinAbility,SeinEffect")`, each asserting `SEIN_CHECK_SIM()`: Set
  Squad/Member Data, Set Leader, Add/Remove Member, Fill/Empty Slot, Set Slot Offset. Each keeps the
  broker member-list, member back-refs, and leader consistent.
- **`SeinAbility_SquadReinforce.{h,cpp}`** — `USeinAbility_SquadReinforce`, a starter ability. Fills
  the first empty + off-cooldown slot in declaration order; charges the slot's `ReinforceCost` at
  enqueue, then enqueues a `FSeinSquadReinforceEntry` the system builds over `ReinforceBuildTime`.
  Per-slot cost (clears the ability-level cost); `CooldownScope::Squad`.
- **`SeinARTSSquadSettings.{h,cpp}`** — `USeinARTSSquadSettings` (UDeveloperSettings). One field:
  `DefaultSquadDispatchResolverClass` (`TSoftClassPtr`).

---

## Dispatch (and the Cover extension point)

`USeinSquadDispatchResolver` (subclass of the default resolver) overrides **`ResolveDispatch`** and
adds a **constructor** that selects its formation:

- **`ResolveDispatch`** — for predetermined-ability orders, dispatches via the broker capability map
  filtered by the ability's own dispatch policy (`ApplyAbilityDispatchPolicy`) — CoH "leader throws
  the smoke." Smart right-click orders route each member to its slot's world position, and DROP the
  order's gesture guide/formation tag so squads stay slot-driven (ignore drag-formations).
- **Slot layout** — the constructor sets `DefaultFormationClass = USeinSlotFormation`, which reads
  each member's slot `OffsetTransform` (by `SlotIndex`, tag fallback) rotated by anchor facing and
  nav-projected; unauthored squads / unresolved members fall back to a blob at the anchor. This
  replaced the old `ResolvePositions` override (squads now go through the same formation pipeline as
  loose units).
- **Cover extension point** — the inherited `ResolveFormationLayout` calls `PostProcessPositions`
  (empty base impl) after layout; the Cover extension's `USeinCoverAwareSquadDispatchResolver`
  overrides exactly that to snap final member positions to cover.

**Resolver selection order:** per-squad `FSeinSquadComponent::DispatchResolverClass` → settings
`DefaultSquadDispatchResolverClass` (soft path; null if Cover absent) → framework default. A squad
registers its chosen resolver instance into its broker via `World.RegisterCommandBrokerResolver` at
lazy-init.

---

## Current state
**Complete and functional** — system, resolver, both BPFLs, the reinforce ability, and settings are
all fully implemented with defensive re-fetch-after-`AddComponent` discipline and diagnostic
logging (recently demoted from Warning to Log/Verbose after a "culled-at-PIE-start" regression was
fixed — i.e. this module saw recent active iteration). Only the module startup/shutdown bodies are
empty, by design.
