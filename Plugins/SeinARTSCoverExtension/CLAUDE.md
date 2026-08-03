# SeinARTSCoverExtension — Plugin Guide

Opt-in **cover system** for the SeinARTS lockstep RTS: deterministic cover providers (slots + area
volumes) on sim entities, a pluggable cover-query system, automatic cover-snap dispatch resolvers,
and a per-player destination-preview decal layer tinted by cover quality. Cleanly strippable —
nothing in the framework depends on it.

> **Read the project-root `CLAUDE.md` first** for the cross-cutting rules. This file covers cover
> mechanics only.

- **Depends on:** `SeinARTSFramework` and `EnhancedInput`.
- **Does not depend on Squad.** Cover-aware Squad dispatch lives in the separate
  `SeinARTSCoverSquadExtension` bridge.

---

## Two modules

| Module | Type | Role |
|---|---|---|
| **SeinARTSCover** | Runtime | The cover system: components, geometry, query system, the cover-aware default broker resolver, preview-quality provider, settings, tags. |
| **SeinARTSCoverEditor** | Editor (PostEngineInit) | Cover-component Details panel (Generate Slots button) + entity-bridge cover draw callback. |

Cover and Squad are physically independent. The optional bridge is its own production plugin and
declares Framework, Cover, and Squad as required dependencies. Neither parent plugin depends back
on it.

---

## SeinARTSCover (runtime)

- **`FSeinCoverComponent`** (BP, `SeinDeterministic`) — the sim cover provider authored in an
  entity's `ComponentData`: `QualityTag`, `bIsDirectional`, `Slots` (`TArray<FFixedVector>` — pure
  positions), `Area`, plus edit-time Generate params. `GenerateSlots()` is deterministic
  (`FFixedPoint` math). The only non-deterministic call is `FMath::FRand` for **editor-time** scatter
  authoring, whose output is serialized to fixed-point — deterministic at runtime (documented safe).
- **`FSeinCoverArea`** (Box/Sphere/None), **`FSeinCoverContext`**, **`FSeinCoverSlotCandidate`** —
  all BP types.
- **`USeinCoverSystem`** (abstract BP UObject) — the pluggable query surface:
  `Register/UnregisterProvider`, `QueryCoverAt`, `QueryBestCoverQualityAt`, `FindNearbySlots`. Every
  query takes a fog-of-war **`Observer`** and gates on `USeinFogOfWar::IsEntityVisibleToObserver` —
  you can't snap to cover you can't see.
- **`USeinCoverDefault`** — the shipped impl: flat provider list, per-provider cached "reach"
  distance gate, priority Heavy > Light > Negative > other.
- **`USeinCoverSubsystem`** (WorldSubsystem) — owns the active system (class from settings),
  auto-registers/unregisters providers via `USeinWorldSubsystem::OnEntitySpawned/Destroyed`.
- **`USeinCoverBPFL`** — `SeinQueryCoverAt`, `SeinQueryBestCoverQualityAt`, `SeinGetCoverDirection`.
- **Destination preview** — now a BASE framework feature: `ASeinFormationPreviewActor` + its
  swappable render backends (default = ghost-free mesh quads; Decal / ISM subclasses) live in
  SeinARTSFramework. Cover only feeds it per-cell quality tags via
  `USeinWorldSubsystem::PreviewQualityProvider`; the `CoverQualityTints` (green/yellow/red) are
  authored on the preview BP.
- **`USeinARTSCoverSettings`** (DeveloperSettings) — `CoverSystemClass`, `CoverSnapRadius`
  (default 500). (`FormationPreviewActorClass` / `bEnableFormationPreview` moved to
  `USeinARTSCoreSettings`.)
- Native gameplay tags: `SeinARTS.Cover.{Heavy, Light, Negative, UsesCover}`.

## Resolvers

The Cover plugin owns the loose-unit/default-broker resolver. The separate Cover+Squad bridge owns
the Squad-derived resolver. Both are opt-in through class settings, so absent plugins fall back to
framework/Squad defaults.

- **`USeinCoverAwareDefaultBrokerResolver`** (subclasses the framework's
  `USeinDefaultCommandBrokerResolver`) — overrides `PostProcessPositions`: reads `CoverSnapRadius`,
  derives the FoW observer from member[0]'s owner, calls `FindNearbySlots`, partitions candidates by
  cursor side (`SeinCoverGeometry::PartitionSlotsByCursorSide`), then runs two-pass greedy-nearest
  allocation (preferred side, then wrong-side fallback) onto members carrying
  `SeinARTS.Cover.UsesCover`. Enabled by pointing
  `USeinARTSCoreSettings::DefaultBrokerResolverClass` at this class.
- **`USeinCoverAwareSquadDispatchResolver`** lives in `SeinARTSCoverSquadExtension`. It subclasses
  `USeinSquadDispatchResolver` and overrides the same `PostProcessPositions` hook. Enable it per
  squad via `FSeinSquadComponent::DispatchResolverClass` or as the Squad default resolver.

## SeinARTSCoverEditor
- **`FSeinCoverComponentDetails`** — PropertyEditor custom layout for `FSeinCoverComponent` adding a
  **Generate Slots** button; drives changes through `IPropertyHandle::SetPerObjectValues` for correct
  archetype propagation.
- **`SeinCoverEntityDraw::DrawCoverEntries`** — registered against the framework editor's draw
  registry as `RegisterComponentDataDraw(FName("SeinCoverComponent"), …)` for area + slot viz
  (unregistered on shutdown).

---

## Current state
**Substantially complete and functional**, not stubs — the query system, default implementation,
loose-unit resolver, FoW gating, slot generation (edge/area/scatter), preview pipeline with cover-quality
tinting, and editor viz / Generate button are all fully wired end-to-end. Determinism holds
(sim-affecting math is all fixed-point; the lone `FMath::FRand` is editor-time, serialized).
