# SeinARTSCoverExtension — Plugin Guide

Opt-in **cover system** for the SeinARTS lockstep RTS: deterministic cover providers (slots + area
volumes) on sim entities, a pluggable cover-query system, CoH-style cover-snap dispatch resolvers,
and a per-player destination-preview decal layer tinted by cover quality. Cleanly strippable —
nothing in the framework depends on it.

> **Read the project-root `CLAUDE.md` first** for the cross-cutting rules. This file covers cover
> mechanics only.

- **Depends on:** `SeinARTSFramework` (required), `SeinARTSSquadExtension` (optional — see gotcha),
  `EnhancedInput`.

---

## Three modules

| Module | Type | Role |
|---|---|---|
| **SeinARTSCover** | Runtime | The cover system: components, geometry, query system, the cover-aware default broker resolver, formation preview actor, settings, tags. |
| **SeinARTSCoverEditor** | Editor (PostEngineInit) | Cover-component Details panel (Generate Slots button) + entity-bridge cover draw callback. |
| **SeinARTSCoverSquad** | Runtime | The optional **Cover↔Squad bridge**: cover-aware *squad* dispatch resolver + the formation-preview subsystem. |

> **Optional-dependency gotcha:** Squad is optional **only in the `.uplugin`** (`Optional: true`).
> In code, `SeinARTSCoverSquad`'s `Build.cs` hard-links `SeinARTSSquad` (and `SeinARTSFramework`,
> `SeinARTSFogOfWar`) with **no `#if` guards anywhere**. So if the Squad extension is absent,
> `SeinARTSCoverSquad` simply doesn't load — it does **not** compile a degraded stub. The other two
> cover modules are unaffected and work without Squad.

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
- **`ASeinFormationPreviewActor`** (Blueprintable) — per-member decal pool with per-decal MIDs;
  `CoverQualityTints` (green/yellow/red defaults); `SetPositions` / `HideAll`.
- **`USeinARTSCoverSettings`** (DeveloperSettings) — `CoverSystemClass`,
  `FormationPreviewActorClass`, `bEnableFormationPreview`, `CoverSnapRadius` (default 500).
- Native gameplay tags: `SeinARTS.Cover.{Heavy, Light, Negative, UsesCover}`.

## Resolvers

Two resolvers share the same cover-snap idea but plug in at different layers. **Both are selected by
config**, so if this plugin is absent the settings fall back to the framework defaults and behavior
is unchanged.

- **`USeinCoverAwareDefaultBrokerResolver`** (subclasses the framework's
  `USeinDefaultCommandBrokerResolver`) — overrides `PostProcessPositions`: reads `CoverSnapRadius`,
  derives the FoW observer from member[0]'s owner, calls `FindNearbySlots`, partitions candidates by
  cursor side (`SeinCoverGeometry::PartitionSlotsByCursorSide`), then runs two-pass greedy-nearest
  allocation (preferred side, then wrong-side fallback) onto members carrying
  `SeinARTS.Cover.UsesCover`. Enabled by pointing
  `USeinARTSCoreSettings::DefaultBrokerResolverClass` at this class.
- **`USeinCoverAwareSquadDispatchResolver`** (subclasses the Squad extension's
  `USeinSquadDispatchResolver`) — overrides the squad resolver's `PostProcessPositions` hook with the
  same snap logic. Enabled per squad via `FSeinSquadComponent::DispatchResolverClass`. **The snap
  body is deliberately duplicated between the two resolvers, not shared via a common helper.**

## SeinARTSCoverSquad — the preview subsystem
`USeinFormationPreviewSubsystem` (LocalPlayerSubsystem + Tickable) drives the destination preview:
binds the PC's `OnSelectionChanged` / `OnCursorUpdated` (lazy tick-based bind), expands squad/lone
selections, calls `SeinComputeFormationPreview`, queries per-cell best cover quality (FoW-observer
gated, throttled/cached), and pushes tinted decals to `ASeinFormationPreviewActor`.

## SeinARTSCoverEditor
- **`FSeinCoverComponentDetails`** — PropertyEditor custom layout for `FSeinCoverComponent` adding a
  **Generate Slots** button; drives changes through `IPropertyHandle::SetPerObjectValues` for correct
  archetype propagation.
- **`SeinCoverEntityDraw::DrawCoverEntries`** — registered against the framework editor's draw
  registry as `RegisterComponentDataDraw(FName("SeinCoverComponent"), …)` for area + slot viz
  (unregistered on shutdown).

---

## Current state
**Substantially complete and functional**, not stubs — the query system, default impl, both
resolvers, FoW gating, slot generation (edge/area/scatter), preview pipeline with cover-quality
tinting, and editor viz / Generate button are all fully wired end-to-end. Determinism holds
(sim-affecting math is all fixed-point; the lone `FMath::FRand` is editor-time, serialized).

> **Stale docstrings:** `SeinARTSCoverModule.h` and `SeinFormationPreviewSubsystem.h` still say
> "Phase 1: neutral decals only, no cover queries/color-coding," but color-coding is fully
> implemented. `USeinCoverSystem.h` references a "per-entity cover state system" that isn't here. A
> "Cover Wrong-Side Penalty Radius" setting is mentioned in comments but no longer exists (replaced
> by the cursor-side partition; only `CoverSnapRadius` is read).
