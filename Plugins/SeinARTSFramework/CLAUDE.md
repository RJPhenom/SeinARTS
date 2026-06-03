# SeinARTSFramework — Plugin Guide

The core plugin: the deterministic sim, the entity/ability/effect systems, pluggable navigation
and fog-of-war, movement, lockstep networking, and the entire render/
editor/UI/gameplay layer.

> **Read the project-root `CLAUDE.md` first.** It owns the cross-cutting rules this plugin obeys:
> the no-worktree hard rule, sim/render separation, determinism, the designer-first / everything-
> is-an-ability / blueprint-is-the-unit principles, naming conventions, and the "code over
> comments" rule. This file does not repeat them — it covers framework **mechanics**.

The opt-in **Squad** and **Cover** features used to live here; they were extracted into separate
extension plugins. The framework retains only squad **data** structs (see "Squad data" below); it
contains **no cover code at all**.

---

## Module structure (11 modules)

```
── Sim layer (deterministic; fixed-point; no float/AActor*) ──
SeinARTSCore         Fixed-point math suite, deterministic primitives, PRNG, geometry, time.
SeinARTSCoreEntity   The sim heart: entity pool, reflection-backed component storage, the entity
                     bridge (USeinEntityComponent), the phase-based tick, abilities + latent
                     actions, effects/modifiers/attributes, command buffer + brokers, production,
                     containment, tech, resources, match-flow/voting, squad DATA, visual events,
                     ~26 BPFLs. Also hosts the render-bridge types (ASeinActor, USeinEntityComponent).
SeinARTSNavigation   Pluggable nav: abstract USeinNavigation base + polymorphic asset; ships
                     USeinNavigationAStar (single-layer 2D grid, synchronous A*, LoS smoothing) as
                     the default/reference. Owns bake, pathfinding, reachability, SeinMoveToAction.
SeinARTSMovement     Movement base + shared steering toolkit: abstract USeinMovement, the
                     USeinBasicMovement / USeinBasicUnitMovement defaults, USeinMoveToAction + the
                     "Move To" proxy, the movement BPFL, and the steering/debug show-flags. The
                     concrete modes (Infantry/Wheeled/Tracked/Hover/Flight) + per-class data moved
                     to the SeinARTSMovementPlus extension (see root CLAUDE.md).
SeinARTSFogOfWar     Pluggable vision/FoW: abstract USeinFogOfWar base + default impl; stamping,
                     baked fog grid, visibility queries, show-flag debug viz.

── Transport ──
SeinARTSNet          Lockstep networking: per-PC RPC relay, server-authoritative turn aggregation,
                     lobby, replay reader/writer, desync gossip, drop-in/out. (Real, not "Phase 0.")

── Render / editor / UI / gameplay layer (reads sim; writes sim only via command buffer) ──
SeinARTSFramework    Gameplay shell: player controller, camera pawn, HUD (marquee + drag orders),
                     selection/control groups, targeter subsystem + previews, game mode, match
                     bootstrap.
SeinARTSUIToolkit    UI runtime: read-only view-models, selection model, widget pool, UI BPFLs.
SeinARTSEditor       Content Browser factories, fixed-point pin factory, thumbnails, class picker,
                     Details customizations, and the entity-bridge visualizer with its
                     per-component DRAW-CALLBACK REGISTRY (extensions register against this).
SeinARTSGraphNodes   UncookedOnly: K2Node_SeinGetComponent / SeinSetComponent (typed BP component access).
SeinARTSFogOfWarEditor  Editor companion to FoW: volume bake button + vision-stamp draw callback.
```

(Editor companions are split into their own `PostEngineInit` modules to avoid a load-order race
against `SeinARTSEditor`'s registry.)

---

## The sim core (`SeinARTSCoreEntity`)

### Entities & storage
- **Entity pool** (`FSeinEntityPool`): generational slot pool with free-list recycling. Slot 0 is
  reserved/invalid. `FSeinEntityHandle` = index + generation; generation 0 = invalid.
- **Component storage** is reflection-backed and **generic**: all live storage is
  `FSeinGenericComponentStorage` (implements `ISeinComponentStorage`), keyed by `UScriptStruct*`,
  storing raw bytes. It handles GC ref collection, reflection-driven deterministic hashing, and
  archive serialization. There is **no runtime typed-storage registration** — `GetComponent<T>` /
  `AddComponent<T>` / `MutateComponent<T>` are templated façades over the raw-bytes interface.
- **The entity bridge** is `USeinEntityComponent` ("SeinARTS Entity Bridge"), auto-attached by
  `ASeinActor`. Authoring surface = `ComponentData: TArray<FInstancedStruct>` plus `bIsAbstract`,
  `BaseTags`, and FoW authoring fields. At spawn it injects authored components, then handles
  transform interpolation and visual-event routing for the render side.

### Spawn flow
`USeinWorldSubsystem::SpawnEntity(ActorClass, …)` acquires a pool slot, walks the CDO's entity
bridge `ComponentData`, and copies each `FInstancedStruct` into component storage. Use
`SpawnAbstractEntity` for presence-less entities (e.g. squads were once modeled this way).

### The sim tick — 4 phases
`USeinWorldSubsystem::TickSimulation → TickSystems` runs registered `ISeinSystem`s, grouped into
phases and ordered by priority within a phase:
1. **PreTick** — cooldown reduction, effect expiration, resource income, nav-blocker stamping.
2. **CommandProcessing** — dequeue the command buffer; process commands.
3. **AbilityExecution** — `USeinLatentActionManager` ticks running latent actions; the ability tick
   system ticks active primary + passive abilities (movement runs here).
4. **PostTick** — command-broker dispatch resolution, deferred destroy cleanup, state hash.

Built-in systems in CoreEntity: AbilityTick, CommandBroker, CollisionBroadphase (PreTick),
CollisionResolution (PostTick), Cooldown, EffectTick, Lifespan, Production, StateHash. Other
modules register their own systems into the loop (e.g. `SeinNavBlockerStampSystem` from Nav;
`FSeinPositionKeepSystem` from Movement; `FSeinSquadSystem` from the Squad extension).

**Collision (extent-vs-extent)** is a deterministic layer **independent of navigation** — it never
consults `bBlocksNav` / the nav grid, and nav never consults it. Authored on `FSeinExtentsComponent`'s
collision section (`bCollisionEnabled` / `Mobility` / `ObjectType` / response matrix) against a
settings-driven channel registry (`USeinARTSCoreSettings::CollisionChannels`). Broadphase =
`FSeinCollisionSpatialHash` (two-tier: cached static + per-tick dynamic, footprint cell-stamped,
rebuilt by `FSeinCollisionBroadphaseSystem`); narrowphase = MTV queries in
`SeinARTSCore/Math/CollisionQueries.h` (planar 2D disc/OBB SAT). `FSeinCollisionResolutionSystem`
Block-separates with infinite-mass statics (a unit can't be pushed through a wall) and emits
`CollisionOverlapBegin/End` visual events for Overlap responses. Editor matrix UX (Ignore/Overlap/
Block per channel) = `FSeinCollisionResponseDetails`. (This replaced the old circle-only
`PenetrationResolution` + generic `SpatialHash`, which were retired.)

> **`TimeAccumulator` is a `float` on purpose** — it's a wall-clock scheduler, not sim state. The
> delta fed into `TickSystems` is fixed-point (`FFixedPoint::One / FromInt(TickRate)`). Clients can
> drift on wall clock yet remain bit-identical at any tick N. Don't "fix" it to fixed-point.

### Component payload structs (the `ComponentData` vocabulary)
All `: FSeinComponent`, all `SeinDeterministic`:
`FSeinIdentityComponent`, `FSeinAbilityComponent`, `FSeinActiveEffectsComponent`,
`FSeinProductionComponent`, `FSeinProducibleComponent`, `FSeinConstructionComponent`,
`FSeinExtentsComponent`, `FSeinMovementComponent`, `FSeinNavigationComponent`,
`FSeinFogVisibilityComponent`, `FSeinChildTransformsComponent`, `FSeinSquadComponent`,
`FSeinSquadMemberComponent`, `FSeinCommandBrokerData`, `FSeinBrokerMembershipData`,
`FSeinContainmentData`, `FSeinContainmentMemberData`, `FSeinAttachmentSpec`, `FSeinTransportSpec`,
`FSeinGarrisonSpec`, `FSeinLifespanData`. (`FSeinCapturePointData` is a sim struct but **not** a
component.) Note: the `FSeinMovementComponent` / `FSeinNavigationComponent` payloads are *defined*
here, but their *systems* live in the Movement / Navigation modules.

> **Not components:** entity tags are **not** a component — tag state is centralized in
> `USeinWorldSubsystem::EntityTagStates`, seeded from `USeinEntityComponent::BaseTags`. FoW
> visibility is authored on the bridge (its `FSeinFogVisibilityComponent` is `SeinSubData`, hidden
> from the picker).

### Abilities & latent actions
- `USeinAbility` (Blueprintable) is the unit of behavior: cost/cooldown, dispatch policy,
  declarative targeting (`FSeinTargeterSpec`), tag-based arbitration, production/rally helpers,
  lifecycle hooks. Ability instances are stored as **int32 pool IDs** (Phase-4 indirection), not
  `TObjectPtr`.
- `USeinLatentAction` / `USeinLatentActionManager` provide cooperative multi-tick execution with no
  threads. Shipped actions: `SeinWaitAction` (here) and `SeinMoveToAction` (Movement module).

### Effects, modifiers, attributes
- **Attributes** use FProperty reflection: designers define USTRUCT attribute sets; modifiers
  target fields by `FName` + `UScriptStruct*`. Any field type is supported, not just `FFixedPoint`.
- **Modifiers** have three scopes via `ESeinModifierScope`: **Instance** (one entity, via
  `FSeinActiveEffectsComponent`), **Class** (all entities of an identity tag for one player, via
  `FSeinPlayerState::ClassEffects` + the modifier's `TargetClassTag`), **Player** (whole-player, via
  `FSeinPlayerState::PlayerEffects`). Class scope is per-player: player A's Infantry buff doesn't
  touch player B's Infantry.
- **Effects** (`USeinEffect`, Blueprintable) carry modifiers + granted tags and route by scope.
  Duration via `ESeinEffectDurationMode` (Instant/Persistent/Timed).

### Commands & brokers
- The command buffer holds `FSeinCommand`s. **Command type is a gameplay tag** (`CommandType`),
  **not** an enum, with an `FInstancedStruct Payload`. Observer (non-sim-affecting) commands exist.
- **Command brokers** resolve which entities a command fans out to (e.g. a squad-targeted order →
  its members). `USeinDefaultCommandBrokerResolver` is the base; the Squad and Cover extensions
  subclass it. Brokers are registered per entity via `World.RegisterCommandBrokerResolver`.

### Other sim systems
- **Production**: `FSeinProductionComponent` (single queue per building); `FSeinProducibleComponent`
  carries cost and `GrantedTechEffect`. Tech is emergent from build/research chains, not a graph asset.
- **Containment**: enter/exit/attach — garrison, transport, attachment (`FSeinContainmentData`,
  `FSeinGarrisonSpec`, `FSeinTransportSpec`, `FSeinAttachmentSpec`).
- **Resources, match-flow, voting, scenario, snapshot capture/restore** all have BPFLs.
- **Visual events** (`FSeinVisualEvent`): the one-way sim→render signal channel; the render layer
  drains and reacts.

### Squad data (system lives in the extension)
Only squad **data + pure-read helpers** remain in CoreEntity: `FSeinSquadComponent` (slots, leader,
reinforce queue, `GetLiveMembers`/`ComputeCentroid`, `ESeinSquadContainmentMode`),
`FSeinSquadMemberComponent`, `FSeinSquadSlot`, `FSeinSquadReinforceEntry`, plus squad visual-event
factories. Everything behavioral — `FSeinSquadSystem`, `USeinSquadSubsystem`,
`USeinSquadDispatchResolver`, the squad BPFLs, settings — is in **SeinARTSSquadExtension**.

---

## `SeinARTSCore` — fixed-point foundation
Header-only, `FORCEINLINE`-heavy leaf module (depends only on `Core`/`CoreUObject`). Full
fixed-point suite: `FFixedPoint` (32.32, platform-split 128-bit mul/div), `FFixedVector`,
`FFixedVector2D`, `FFixedQuaternion`, `FFixedRotator`, `FFixedTransform`; geometry primitives
(`FFixedBox/Sphere/Capsule/Plane/Ray/Bounds`); `FFixedRandom` (Xorshift128+ / SplitMix64 seeding);
`FFixedTime`; namespaces `SeinMath` (sqrt, LUT trig, exp/log), `SeinGeometry`, `SeinTime`. BP
make/break for fixed-point lives in `MathBPFL` (in CoreEntity). The `SEIN_SIM_SCOPE` asserts are
**not** here — they're in `SeinARTSCoreEntity/Core/SeinSimContext.h`.

## Pluggable subsystems
Two subsystems follow the same pattern: an abstract base + a polymorphic baked-data asset + a
shipped default impl, with the concrete class chosen via `USeinARTSCoreSettings`:

- **Navigation** (`USeinARTSCoreSettings::NavigationClass`): abstract `USeinNavigation` + polymorphic
  `USeinNavigationAsset`. Default `USeinNavigationAStar` (single-layer 2D grid, C-space
  footprint-aware A*, LoS smoothing, escape-nudge for stuck units). `ASeinNavVolume` and
  `USeinMoveToAction` are impl-agnostic. **Bake is synchronous** (`FScopedSlowTask`), despite the
  base-class "async" wording. No UE NavMesh.
- **Fog of War** (`USeinARTSCoreSettings::FogOfWarClass`): abstract `USeinFogOfWar` + default
  `USeinFogOfWarDefault` (single-layer grid, Bresenham LOS, per-player refcounted VisionGroups,
  delta-refcount source caching). Queries are **observer-gated** (`IsEntityVisibleToObserver`) and
  are the gate the Cover extension reuses. Editor bake is synchronous.

> Cross-module resolvers/queries **no-op until their owning module registers** (e.g. nav projection
> returns identity until Nav is present). This is the pluggability seam, not a bug.

## Movement
The framework ships the movement **base + shared steering toolkit** and the two built-in defaults:
abstract `USeinMovement` (the big static steering library — look-ahead, kinematic arrival braking,
nav collision, slope smoothing), `USeinBasicMovement` (raw seek+arrive; the null/invalid fallback)
and `USeinBasicUnitMovement` (RTS face-velocity default). `USeinMoveToAction` consumes an `FSeinPath`
and stays impl-agnostic; `USeinMoveToProxy` is the BP async "Move To" node; `USeinMovementBPFL`
exposes AnimBP-shaped movement state. Selection is per unit via `FSeinMovementComponent` — a
**`FSoftClassPath` `MovementClass`** (resolved at runtime via `TryLoadClass`) plus a polymorphic
`MovementClassData`.

**Passive re-seek (position keeping).** Idle units hold formation / cover slots without a standing
order. `USeinMoveToAction` records its destination as `FSeinMovementComponent::DesiredPosition`
("home") on its first tick; `FSeinPositionKeepSystem` (registered by `USeinMovementSubsystem`,
PostTick / priority 60) re-issues a real pathed move home for any unit that is **idle** (no active
latent action — `USeinLatentActionManager::HasActiveActionForEntity`) and has drifted past a
threshold (~150 uu) off `DesiredPosition`, e.g. after collision resolution shoves it. **Newest
move wins:** a fresh order (or another re-seek) overwrites `DesiredPosition`, and the in-flight
action self-cancels on the mismatch — so a re-seek never fights a live order. Re-seek is sim-side and
deterministic; the threshold lives in `SeinPositionKeepSystem.h`.

The **concrete modes** — Infantry, Wheeled, Tracked, Hover, Flight — and their per-class data structs
live in the opt-in **SeinARTSMovementPlus** extension, not here. They derive from the base classes
above (Infantry from `USeinBasicMovement`; the rest from `USeinMovement`) and are resolved through
the soft `MovementClass` path, so the framework has **no compile-time dependency** on them. See
`Plugins/SeinARTSMovementPlusExtension/CLAUDE.md`. (Wheeled feel = bicycle pure-pursuit + nav
corner-rounding via `GetMinTurnRadius`; NOT a Reeds-Shepp curve fit — see root doc.)

## Networking (`SeinARTSNet`)
GameInstance-subsystem-scoped (survives map travel). Real lockstep: `ASeinNetRelay` (per-PC RPC
relay) + `USeinNetSubsystem` (per-slot turn buffering, completeness gate against active slots,
deterministic command ordering before fan-out, seed distribution, replay, drop-in/out, desync
gossip). `USeinLobbySubsystem` + `ASeinLobbyState` drive the pre-match lobby; `USeinReplayWriter/
Reader` serialize turns to `.seinreplay`. Feeds the sim one-way via `SubmitLocalCommand`.

## Render / editor / UI layer
- **`SeinARTSFramework` (gameplay):** `ASeinPlayerController` (selection, smart commands, control
  groups; touches sim only via `EnqueueCommand`), `ASeinCameraPawn` (implements
  `ISeinSnapshotCameraProvider`), `ASeinHUD` (marquee, drag-order lines, command log),
  `USeinTargeterSubsystem` + `SeinTargeterPreview` actors + `USeinTargeterBPFL` (the "Targeter"
  feature — point spec complete; drag/point-facing scaffolded), `ASeinGameMode`,
  `USeinMatchBootstrapSubsystem`, `USeinInputConfig` (Enhanced Input). **No animation subsystem
  exists** — only an incidental building-hologram bind-pose use.
- **`SeinARTSUIToolkit`:** read-only `USein*ViewModel`s refreshed each sim tick, `USeinSelectionModel`,
  lobby view-model/verbs, `USeinWorldWidgetPool`, `USeinUIBPFL` (projection, minimap, formatting).
- **`SeinARTSEditor`:** factories (Ability/Actor/Effect/Widget BP, **Component-as-UDS** via
  `USeinSimComponentFactory`), fixed-point pin factory, thumbnails, class picker, Details
  customizations, the deterministic-struct validator, auto-tag generator, and the
  **entity-bridge visualizer + draw-callback registry** (see below).
- **`SeinARTSGraphNodes`:** `UK2Node_SeinGetComponent` / `SeinSetComponent` — typed BP get/set that
  expand to `USeinComponentBPFL::SeinGet/SetComponentTyped` (CustomThunk), one menu action per
  eligible `FSeinComponent` substruct.

### The editor draw-callback registry (extension point)
On `FSeinARTSEditorModule`:
```cpp
DECLARE_DELEGATE_FourParams(FSeinComponentDataDrawDelegate,
    const TArray<FInstancedStruct>& /*ComponentData*/, const FQuat&, const FVector&, FPrimitiveDrawInterface*);
void RegisterComponentDataDraw(FName Key, FSeinComponentDataDrawDelegate Draw);   // FindOrAdd
void UnregisterComponentDataDraw(FName Key);
const TMap<FName, FSeinComponentDataDrawDelegate>& GetComponentDataDraws() const;
```
`FSeinEntityComponentVisualizer::DrawVisualization` fans out to every registered delegate (each
callback filters `ComponentData` for its own struct type) plus built-in layers (Extents, Production
spawn points, Navigation footprint). Registrants own their un-registration. The FoW editor module
registers `"SeinVisionComponent"`; the Cover editor module registers `"SeinCoverComponent"`.

---

## Pitfalls worth remembering
- **CDO component iteration:** `AActor::GetComponents<T>()` on a CDO misses BP-SCS components. Use
  `AActor::GetActorClassDefaultComponents<T>(ActorClass, OutArray)` — walks native + SCS in a stable
  order. `SpawnEntity` depends on this.
- **`ComponentData` is the authoring path, not a runtime mirror.** It's walked once at spawn to
  populate storage; runtime mutations go through `World.AddComponent<T>` / `RemoveComponent<T>` /
  `MutateComponent<T>`. "Entity X has no FSeinAbilityComponent" after spawn usually means (a) the BP
  had no entry in `ComponentData`, (b) the CDO-iteration helper wasn't used, or (c) `SpawnEntity`
  got a non-Sein actor class.
- **Always re-fetch a component after `AddComponent`** — storage can reallocate; cached pointers
  dangle.
- **Ability lifecycle is explicit; arbitration is tag-based.** A BP ability is not auto-ended when a
  latent node's terminal pin fires — wire `End Ability` / `Cancel Ability` on Completed/Failed/
  Cancelled. Cross-ability interactions use three containers on `USeinAbility`:
  `ActivationBlockedTags` (entity tags that refuse this ability), `OwnedTags` (granted on activate,
  ungranted on deactivate — refcount-routed via `GrantTag`/`UngrantTag` so overlapping grants
  survive a single deactivate), and `CancelAbilitiesWithTag` (cancels active abilities whose
  `OwnedTags` intersect). Listing one of your own `OwnedTags` in `CancelAbilitiesWithTag` gives
  self-cancelling reissue (e.g. Move-reissue cancels previous Move).
- **Tech is not a primitive.** `FSeinProducibleComponent::GrantedTechEffect` (a
  `TSubclassOf<USeinEffect>`) drives research completion; the production system calls
  `World.ApplyEffect(...)`, routing modifiers + tags by the effect's scope. Player tags are
  refcounted via `GrantPlayerTag` / `UngrantPlayerTag`.
- **Trust code over docstrings.** See the root `CLAUDE.md` "Source of truth" section for the full
  list of known-stale comments (DESIGN/PLAN references, Net "Phase 0", Reeds-Shepp, etc.).

---

## Current state (per module)
- **SeinARTSCore** — complete.
- **SeinARTSCoreEntity** — mature/feature-complete: pool, generic storage, BP-CDO spawn flow,
  4-phase tick, ability + latent infra, 3-scope effects/modifiers/attributes, production,
  containment, command brokers, tech-as-effect, resources, match-flow/voting, snapshot capture/
  restore, ~26 BPFLs. WIP seams (by design): PRNG seeding not yet wired into the live session-start
  path; lockstep gate / AI-emit interceptor are delegate hooks bound by the Net module; some
  cross-module resolvers no-op until Nav/FoW register.
- **SeinARTSNavigation** — complete & hardened (lazy A* alloc, dynamic blockers, escape-nudge).
  Synchronous bake. (An empty `Public/Data/` dir exists; the vehicle curve planner is unbuilt.)
- **SeinARTSMovement** — base + shared steering toolkit + Basic/BasicUnit + MoveTo action/proxy/BPFL;
  complete. The concrete modes (Infantry/Wheeled/Tracked/Hover/Flight) were extracted to the
  **SeinARTSMovementPlus** extension on 2026-06-02 — see that plugin's CLAUDE.md for their state.
- **SeinARTSFogOfWar** (+Editor) — substantially complete: stamping, baked grid, observer-gated
  queries, dynamic blockers, sync bake, vision-stamp authoring viz.
- **SeinARTSNet** — substantially implemented, ahead of its own docstrings. Deferred: full reconnect
  snapshot + tail catch-up; adaptive input-delay (observability only); snapshot restore skips
  ability/resolver-pool reconstruction. `SeinReplayBPFL` is a header-only skeleton.
- **SeinARTSEditor / SeinARTSGraphNodes** — complete (registry, factories, visualizer, validator,
  K2 nodes).
- **SeinARTSFramework (gameplay)** — largely complete; targeter phased (point done, drag/line
  scaffolded); `SeinWorldSettings` is a forward-compat placeholder.
- **SeinARTSUIToolkit** — complete/mature (squad-aware ability aggregation, lobby flow).
