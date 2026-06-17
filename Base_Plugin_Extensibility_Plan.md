# SeinARTS Base Plugin — Extensibility & Hardening (Source Plan)

**Date:** 2026-06-14 · **Status:** active · **Supersedes:** `API_Cleanup_Pass.md` (= Phase 0, done).

## 0. How to use this document
This is the **self-contained source of truth** for the base-plugin extensibility pass. A fresh
session can execute from *this file + the code* without reading the originating conversation. Line
citations are **starting coordinates** — re-ground against live code before editing. Decisions are
recorded as locked unless marked otherwise.

## 1. Goal
A cleanly-architected, **opinionated** RTS base: default Nav / Fog / Movement that **swap cleanly**,
fast jump-in prototyping, and deep customization **without forking framework source**. Docstrings are
the spec and **must be accurate** — they feed a from-scratch docs site (next initiative).

## 2. The extension model (the mental map)

Two authoring layers. Build each piece where it lives.

| You want to… | Build/extend by | Mechanism | Language |
|---|---|---|---|
| New unit type | subclass `ASeinActor`, author `ComponentData` | author | Editor / BP |
| New entity component | `FSeinComponent` struct **or** UDS (right-click) | author | C++ or UDS |
| Gameplay logic on a component (capture pts, smoke) | passive **Ability** / **Effect** that reads the component | author | Blueprint |
| Per-unit movement | `USeinMovement` subclass + `FInstancedStruct` data | per-unit class | C++ class + BP data |
| Swap Nav / Fog / Level-bake / Net | subclass the abstract base, pick in Project Settings | swap | C++ + Settings |
| Custom bake layer (threat, sound, vision) | `ISeinLevelLayerProvider` + register | register | C++ |
| Cross-entity infra system | `ISeinSystem` + `RegisterSystem` | register | C++ |
| UI | `SeinUserWidget` + view-models | author | Blueprint |

**Data flow:** Sim (deterministic, fixed-point) → Entity Bridge (`USeinEntityComponent`) →
Render/Input/UI. One-way: render reads sim; input feeds sim only via the command buffer.

**The fork test (priority lens):** the *best* extension path is subclass / configure / register;
the *worst* is one that forces editing shipped framework files — because that forks the customer off
the update train. **Eliminating fork-forcing gaps is the priority.**

## 3. Guardrails (hold for every change)
1. **Determinism** — no `float`/`FVector`/`FMath`/`rand` in sim paths; fixed-point only; respect `SEIN_SIM_ONLY`.
2. **"Destination preview is sacred"** — movement first-submission binding untouched.
3. **No behaviour regression** on shipped defaults; where feel/bake could shift, keep values identical and **PIE-diff**.
4. **Git on-disk only** (RJ-controlled): no commits/branches/pushes.
5. **No worktrees** (project HARD RULE).
6. **Build via the PowerShell tool**, then **read the log** (Bash lacks `pwsh` → false green). Editor-open ⇒ compile ok, relink may block (note it).

## 4. Decisions locked
- **CORE = C++, swap via Settings.** Nav, Fog, Level-bake substrate, Net, Broker resolver. Opinionated; customers follow the shipped interface.
- **Per-entity gameplay logic = Blueprint** via abilities/effects. This is the designer's "system." **Verified fully capable today** (Section 7). We do **not** build a parallel "BP systems" framework.
- **Cross-entity infra systems = C++** (`ISeinSystem`), hardened (Section 8.4).
- **#4 (UI ↔ `ASeinPlayerController` coupling) is NOT a roadblock.** RTS input↔HUD↔selection coupling is inherent (drag-select forces PC↔HUD); the PC is a subclassable base. **Cut by default.** Reinstate only the *tiny* local-player/selection seam **if** observer / replay / mobile front-ends land on the roadmap (open question to RJ).

## 5. Phase 0 — DONE (build-green 2026-06-14)
De-leaked `FindEscapeNudgeTarget` to a `USeinNavigation` base virtual (removed the only concrete-class
cast in a logic path); purged the first batch of doc-drift; honest C++/BP docstrings. Full record in
`API_Cleanup_Pass.md`.

## 6. Phase 1 — Safe sweep (autonomous, behaviour-free / editor-only)
Compile/link verified; no runtime behaviour change. Design-independent — runs first; makes docstrings
accurate for the docs site.

**6.1 Doc-drift purge**
| Stale doc | Where (approx) | Fix |
|---|---|---|
| `PerceptionLayers`/`EmissionLayers` name-API (doesn't exist) | `SeinVisionLayerDefinition.h:53`, `PluginSettings.h:691` | Document the real model: per-stamp `LayerMask` bits ∩ target `FogVisibilityLayerMask`. |
| "AC synthesis pipeline" | `SeinSimComponentFactory.h:9-11` | Remove — spawn walks `ComponentData` directly. |
| `SpawnEntity` archetype / `USeinActorComponent` wording | `SeinWorldSubsystem.h:553-558`, `ComponentStorage.h:71-73`, `SeinWorldSubsystem.h:632-636` | Rewrite to the bridge-`ComponentData` flow. |
| `ComponentData` "on `FSeinAbilityComponent`" | `SeinComponent.h:11-13` | It's on `USeinEntityComponent`. |
| "UDS picker = designer discipline" | root `CLAUDE.md` | Now actively filtered (`FSeinInstancedStructFilter`). |
| Dangling `planning/…` citations | root + framework `CLAUDE.md` | Remove (dir retired). |

**6.2 Safe code fixes** (editor/settings-only)
- A* settings gating: replace class-path **string match** with `IsChildOf` — `PluginSettings.h:454-458`.
- Align the two component-eligibility predicates — `SeinComponentNodeMenuCache.cpp:~40` vs `SeinSimComponentFactory.cpp:~71`.
- UI `GetBaseAttribute`: read int/bool/enum/FName, not only `FFixedPoint` — `SeinEntityViewModel.cpp:~216`.
- Make the shared bake trace channel configurable (today hardcoded `ECC_Visibility`) — `SeinLevelDataDefault.cpp:~352`.

**6.3 Movement data picker** (editor-only, no-clobber)
Wire `GetMovementDataStruct()` into a `FSeinMovementComponentDetails`: auto-fill + restrict
`MovementClassData` to the selected class's struct; fill only when empty/mismatched. `SeinMovement.h:172`,
`SeinMovementComponent.h:100`. *Verify:* compile green; **editor-restart PIE check by RJ.**

## 7. Phase 2 — RESOLVED: the two-layer model (verified)
CapturePoint is **fully BP-authorable today** — every piece exists:

| Need | Exists | Cite |
|---|---|---|
| Author component in-editor | UDS `SeinDeterministic` → generic storage | §audit |
| Continuous logic | passive ability (`bIsPassive`) | `SeinAbility.h:142` |
| Read/write component per tick | K2 Get/Set Component nodes | `SeinARTSGraphNodes` |
| Enemies in radius | `SeinGetEntitiesInRange(Origin, Radius, Tags)` | `SeinEntityQueryBPFL.h:36-46` |
| Nearest / distance / direction | `SeinGetNearestEntity` / `…DistanceBetween` / `…DirectionTo` | `SeinEntityQueryBPFL.h` |
| Tag/count/named lookups | `SeinEntityLookupBPFL` | `SeinEntityLookupBPFL.h` |
| Flip ownership / apply effect | `SeinGrantAbility`, apply-effect, set-component | `SeinAbilityBPFL.h` |

⇒ The work is **documentation + optional ergonomics**, not new machinery (Sections 8.4, 10).

## 8. Phase 3 — De-fork + feature work (ordered by the fork test)
Autonomous build; items touching feel/bake are **PIE-PENDING** (RJ verifies).

**8.1 Custom bake-layer config registry** *(de-fork #2)* — `ASeinLevelVolume` hardcodes Nav/FoW config
(`SeinLevelVolume.h:43-82`); add a per-layer config registry mirroring the existing
`RegisterDebugComponentClass` (`SeinLevelVolume.h:167`) + a layer-host base so a custom provider
(`RegisterLayerProvider`, `SeinLevelData.h:125`) plugs in **without editing framework source**.

**8.2 Steering-profile seam** *(de-fork #3)* — the avoidance system is `final` with inline literal
constants (`SeinAvoidanceSystem.h:63`, `:72-78`); lift them into a tunable profile/data so teams change
movement feel without rewriting the whole `Tick`. Values copied verbatim. **PIE-PENDING (feel diff).**

**8.3 Fog per-layer static occlusion bake** *(feature)* — bake forces `0xFE` for static blockers
(`SeinFogOfWarDefault.cpp:~208`, TODO `:224`); bake real per-layer masks so baked geometry can block
e.g. Normal-not-Thermal (already works for dynamic blockers). **PIE-PENDING (re-bake + visual).**

**8.4 C++ infra-system hardening + BP recipe doc** *(Phase-2 idiom)*
- C++: GC-safe base host for `ISeinSystem` + a public reference system + named phase-priority constants
  (today `RegisterSystem` is raw-ptr/undocumented — `SeinWorldSubsystem.h:~1040`, `SeinTickPhase.h:~30`;
  examples `SeinSquadSubsystem.cpp:11-37`, `SeinNavigationSubsystem.cpp:257-264`).
- Docs: write the **component-reactive recipe** ("Component + passive Ability + `GetEntitiesInRange`"),
  CapturePoint worked end-to-end. Highest-value for the docs site.

**8.5 Movement request-surface fills** *(feature)* — add a first-class **Stop/Abort** BP node and a
**path-invalidation event** to the move proxy (today: cancel only via ability lifecycle;
`OnNavigationMutated` is C++-only). **PIE-PENDING.**

## 9. Terrain types → nav cost + cover *(worst offender #1)* — DESIGN SETTLED 2026-06-15
**Authoring model decided (RJ):** author a neutral per-cell **TERRAIN TYPE** once into the shared
substrate; each system **interprets** it — nav→cost (BASE), cover→quality (EXTENSION), FoW→vision (later).
Sources: **terrain volumes (primary) + physical-material mapping (secondary). NO paint tool.**

**Why type, not cost (rationale):** A* already consumes `CellCost` as a runtime multiplier
(`SeinNavigationAStar.cpp:1367`); the bake just writes 1/0 today (`:67`) — so cost is a bake/authoring
problem, not an A* one. `SeinARTS.Cover.Negative` already exists as a first-class quality tag. Authoring
TYPE (not raw cost) lets one road region drive BOTH faster movement and negative cover without
double-authoring, and keeps the base ignorant of cover (cover just reads the shared channel).

**Grounding facts:** the one shared down-trace discards `FHitResult.PhysMaterial` today
(`SeinLevelDataDefault.cpp:354-363`, only Height+NormalZ kept) — the phys-material source must capture it.
Per-volume override precedent = `bOverrideX` + `GetResolvedX()` (`SeinLevelVolume.cpp:79-87`), consumed
**first-match-wins by array order** (`SeinNavigationAStar.cpp:119`) — cost/type regions need an explicit
**Priority** field, not iteration order (a small mud patch nested in the play-area volume must win). TYPE
is a SHARED input (nav AND cover read it) → it belongs in the substrate's shared cell data (alongside
Height/NormalZ/bInBounds), NOT a per-system opaque channel.

**Phase 1 — BASE (movement cost), self-contained:**
- `USeinARTSCoreSettings::TerrainTypes : TArray<FSeinTerrainTypeDef{ FGameplayTag Tag; uint8 NavCost=1; FColor DebugColor; }>`
  — canonical ordered registry; array index = the per-cell type id. Index 0 = Default (cost 1). (NavCost
  lives here because nav IS base; cover's mapping lives in the extension, keyed by the same Tag.)
- Substrate: add a shared per-cell `uint8 TerrainTypeIndex` (on `FSeinLevelCellSurface` + serialized arrays
  + a `GetCellTerrainType(i)` accessor). Resolved in `DoSyncBake`'s per-cell loop: start Default → phys-
  material map (read `TopHit.PhysMaterial` via a settings material→type table) → highest-Priority terrain
  volume override (volume beats material — explicit designer intent wins).
- New `ASeinTerrainVolume` (AVolume; `FGameplayTag TerrainType` + `int32 Priority`), gathered at bake like
  `ASeinLevelVolume`.
- Nav: `BakeLayer` writes `Cost[i] = passable ? clamp(TerrainTypes[type].NavCost,1,254) : 0`; load a runtime
  `CellTerrainType` in `LoadFromSubstrate`. Wire `BlockedTerrainTags`: precompute the blocked type-set per
  request, gate those cells impassable (finally uses the declared-but-unused field). Runtime A* loop
  unchanged.
- Default type → cost 1 → **behaviour-preserving until authored**. **RE-BAKE required** (data is
  regenerable/gitignored).
- Interaction note: per-agent terrain blocking makes E's single global reachability component an
  *approximation* for agents WITH blocked tags (still exact for the common no-blocked-tags agent;
  per-tag component fill is a later refinement, log if it bites).

**Phase 2 — COVER EXTENSION (negative cover), entirely in the extension** *(DONE + build-green 2026-06-15):*
- **Seam (already shipped by Phase 1):** the substrate bakes a per-cell terrain type readable at runtime via
  `USeinLevelData::GetCellSurface(idx).TerrainTypeIndex`; the index↔tag map is the public
  `USeinARTSCoreSettings::TerrainTypes`. Cover already depends on the framework, so it reads both — **no base
  change needed for Phase 2** (optional 1-line `GetTerrainTag(idx)` base helper to hide the reserved-0/+1
  encoding; cover can read the array directly to keep zero base touch).
- Cover settings (cover extension's OWN settings page): `TMap<FGameplayTag,FGameplayTag> TerrainCoverQuality`
  (e.g. Road→Negative). Empty = terrain confers no cover (pure opt-in).
- `USeinCoverDefault::QueryCoverAt` / `QueryBestCoverQualityAt`: after the entity-provider pass, sample the
  substrate's type at the query point (via `USeinLevelDataSubsystem`), map type-tag → cover-quality, and
  contribute a synthesized context. Extends cover from entity-only to terrain-driven; base stays cover-free.
- **Decisions:** terrain cover is OMNIDIRECTIONAL (`bIsDirectional=false` — a road exposes from all sides)
  and NOT FoW-gated (terrain isn't hidden info), unlike entity providers. **No new bake layer** — it's a pure
  function of the already-baked type, resolved at query time (O(1) cell lookup). Priority: the existing
  best-quality order (Heavy>Light>designer>Negative) means a sandbag on a road gives the wall's cover, not the
  road's penalty — if Negative should instead STACK as additive exposure, that's a combat-formula tunable, not
  a query change. `Negative` tag + its damage handling already exist. Deterministic (baked type + static settings).

**Phase 3:** debug viz (tint nav cells by type) — **DONE** · FoW reads type (VisionMultiplier dial, reduced
own-sight) — **DONE** · paint tool — deferred indefinitely · FoW LOS-occlusion-through (forests hide what's
behind them) — possible future, distinct from the VisionMultiplier dial.

**Determinism:** type baked to uint8, phys-material read at bake→serialized, integer cost multiplier, tag
gate over sim state — all deterministic. (Sibling, still deferred: `ESeinPathSegmentType` for
jumps/links/arcs.)

## 10. Deferred / optional (build ONLY on request)
Spatial-index-backed entity query (perf; `SeinGetEntitiesInRange` is O(N), fine at RTS scale) ·
BP-exposed entity/component lifecycle events · command-handler registry (today hardcoded if/else;
"everything is an ability" covers most) · generic latent-action BP node + base · custom-visor render
path · per-entity broker resolver for non-squad selections · the #4 UI selection seam (only if
observer/replay/mobile).

## 11. Verification
Each phase ends build-green (PowerShell build + read log). **PIE-PENDING** items (8.2, 8.3, 8.5, 6.3
picker, plus any nav-cost work): built + flagged, verified by RJ in a PIE/editor pass — not claimed done.

## 12. Status log
- 2026-06-14 — Consolidated source plan. Phase 0 done. Phase 2 resolved. #4 cut (conditional).
  Phase 1 cleared to execute. Nav cost regions = scoped design. Awaiting: go on Phase 1, and the
  observer/replay roadmap answer (gates #4).
- 2026-06-14 — Phase 1 executing. DONE + build-green: doc-drift purge (7 docstrings + 3 CLAUDE.md);
  UI `GetBaseAttribute` extended to numeric/bool/enum. Re-scoped after inspection: A* gating LEFT
  (deliberate module-dependency-avoidance design, not a bug); component-eligibility align DEFERRED
  (regression risk — removes K2 menu entries); bake trace channel → folded into Phase 3.1 (bake-coupled).
  Remaining Phase 1 item: movement-data picker (editor customization; PIE-pending RJ check).
- 2026-06-14 — Movement-data picker first cut landed + COMPILE-GREEN. Approach: `GetMovementDataStruct`
  made a `UFUNCTION`; `FSeinInstancedStructDetails` gains a sibling-class mode (meta
  `SeinDataStructFromClass="MovementClass"`) that restricts + auto-swaps `MovementClassData` via
  reflection — editor module stays decoupled (no Movement/Nav dep). **Item 1 implementation COMPLETE.**
  Picker is **PIE-PENDING** (RJ: verify auto-swap + restrict fire through the deeply-nested property
  handle — the one likely-fragile spot). A* gating left (deliberate); eligibility align deferred;
  trace channel → Phase 3.1. Working tree dirty, no commits.
- 2026-06-14 — Picker **PIE-VERIFIED** by RJ: MovementClass=Wheeled → MovementClassData auto-fills
  `FSeinWheeledMovementData` + picker restricted to it. **ITEM 1 COMPLETE + verified.** Finding:
  `USeinInfantryMovement` doesn't override `GetMovementDataStruct` (returns null) although
  `FSeinInfantryMovementData` exists — a Movement+ tuning-completeness gap (one-line override), NOT a
  base-plugin issue; belongs to the Movement+ tuning pass.
- 2026-06-14 — Starting item 2: BP component-reactive recipe doc (seeds docs site) →
  `docs/recipe-custom-component-system.md`.
- 2026-06-14 — **Item 2 DONE**: `docs/recipe-custom-component-system.md` (BP CapturePoint pattern, end-to-end).
- 2026-06-14 — **Item 6 (docs half) DONE**: `docs/recipe-cpp-system.md` — ISeinSystem pattern +
  the *accurate* phase/priority table pulled from live code (PreTick: EffectTick 0 / Broadphase 5 /
  Avoidance 6 / NavStamp 7 / Cooldown 10 · AbilityExec: AbilityTick 0 / MovementDriver 10 /
  Production 50 · PostTick: Lifespan -10 / CollisionResolution 10 / NavContainment 11 / Squad 30 /
  CommandBroker 40 / StateHash 100) + determinism rules + the lifetime footgun. This documents the
  audit's #1 gap (C++-only, undocumented component→system seam).
  **Item 6 CODE half remaining**: GC-safe base host subsystem (kills the manual new/delete footgun)
  + optional named-priority constants header. Both docs are markdown — no build needed.
- 2026-06-14 — **DOCS DE-SCOPED (RJ).** `/docs` removed; this is a CODE-cleanup pass, not doc
  authoring. The two "recipe DONE" entries above are VOID (files deleted). Item 2 (BP recipe) is OUT
  of scope. Item 6 is CODE-ONLY going forward. (The recipes' accurate phase/priority table now lives
  in code as `SeinSystemPriority.h`.)
- 2026-06-14 — **System-host base + named priorities DONE** (the code-cleanup-bucket #1 / item-6 code half):
  added `USeinSystemHostSubsystem` (CoreEntity — managed lifetime, kills the new/delete footgun; subclass
  + override `CreateSystems`) and `SeinSystemPriority.h` (named tick-priority slots). Migrated
  `USeinSquadSubsystem` onto it as the reference (mechanical equivalence — behaviour-preserving;
  Nav/Movement left as-is, they're multi-responsibility). Build verifying. Squad PIE smoke-test
  advisable (squads still form) but it's a drop-in equivalence.
- 2026-06-14 — **Cleanup #2/#3/#4 DONE + build-green.**
  • #2 steering seam: lifted the 7 hardcoded avoidance model constants into `USeinARTSCoreSettings`
    → "Movement|Avoidance" (defaults = former literals; `FSeinAvoidanceSystem` reads them once/tick).
    Per-unit dials (Strength/Weight/SameWeights) already on the component. Behaviour-preserving until
    tuned (PIE feel-diff advisable). Also deleted a stale Reeds-Shepp/`FitVehicleCurve` settings comment.
  • #3 eligibility align: `SeinComponentNodeMenuCache` now excludes `SeinSubData` structs from the
    typed Get/Set Component node menu — they're nested in FInstancedStruct fields, never entity
    components, so such a node could never resolve in storage. Matches the bridge picker.
  • #4 bake trace channel: added `USeinARTSCoreSettings::BakeTraceChannel` (Level Data, default
    ECC_Visibility); `USeinLevelDataDefault`'s shared down-trace reads it. Re-bake to apply a change.
  Working tree dirty, no commits.
- 2026-06-15 — **§8.5 movement request-surface DONE + build-green.** `Stop Movement` BPFL now cancels
  MOVE actions ONLY (`CancelActionsForEntityOfClass(handle, USeinMoveToAction)`) — a node named for
  movement must not reach beyond it; broad/selective cancels live in separate explicit nodes
  (`SeinCancelAllActions`, `SeinCancelActionsOfClass` on a new `SeinLatentActionBPFL`). Added an
  `OnPathRecomputed` event to the Move-To proxy/action (mirrors `OnPartialPath`, fired at both repath
  sites). **PIE-PENDING** (RJ: Stop halts only the move; OnPathRecomputed fires on interval repath).
- 2026-06-15 — **§8.1 bake-layer config registry DONE + build-green.** `ASeinLevelVolume` gains
  `LayerConfigs: TArray<USeinLayerConfig*>` (EditAnywhere/Instanced, "SeinARTS|Layer Config") + a
  static `Register/UnregisterLayerConfigClass` registry + `GetLayerConfig(class)` + WITH_EDITOR
  `ReconcileLayerConfigs` (additive, from PostRegisterAllComponents) — mirrors the existing
  `RegisterDebugComponentClass` pattern. New header-only abstract base `USeinLayerConfig`
  (EditInlineNew/Blueprintable). A custom `ISeinLevelLayerProvider` can now surface per-layer authoring
  config on the volume **without editing framework source** (closes de-fork #2). **PIE-PENDING** (RJ:
  config sections appear on the volume; default re-bake byte-identical).
- 2026-06-15 — **Optional nav-API cluster (B/C/D/E) DONE + build-green** — four reference-nav
  completeness features, each built + compiled one-at-a-time (deterministic A* internals; a slip = desync):
  • **D — per-request search cap:** `FSeinPathRequest::AgentMaxSearchNodes` (0 = project default) threads
    into `FindCellPath`'s `MaxIters`, letting a caller bound an expensive long-range pathfind (A* returns
    its best-effort partial on cap). Authored per-unit via a new `FSeinNavigationComponent::MaxSearchNodes`
    field (Bitmask-style ELI5 doc, added to the determinism `GetTypeHash`) and plumbed into the request in
    `SeinMovement.cpp` beside `NavLayerMask`/`WallPadding` — so it's a real per-unit knob on the shipped
    move path, not a dangling C++-only field. Default 0 → project default → behaviour unchanged. (Left OFF
    the `SeinFindPath` BPFL by design — that helper exposes none of the per-unit Agent* params.)
  • **B — nav raycast:** base virtual `NavRaycast(From,To,&Hit)` + AStar override (Bresenham supercover
    with the SAME static passability + connectivity + diagonal anti-squeeze gates as `HasLineOfSight`;
    reports the first blocked cell center, else clear) + `SeinNavRaycast` BPFL. Cheap straight-line LoS
    on the static bake — no pathfind, no detour.
  • **E — O(1) reachability:** load-time flood-fill (`RebuildConnectivityComponents`, run beside
    `RebuildWallDistanceField`) labels each cell's static connectivity component (`CellComponent`); AStar
    overrides `IsReachable` to compare component ids (project both endpoints first). Edge relation =
    set connection bit + passable neighbor (the bake-time island-prune relation + the runtime
    stale-bit passability guard). **Verdict change (improvement, not a regression):** the base fell
    back to `FindPath`, which returned true for ANY partial polyline — so it effectively never rejected
    an in-bounds target; the component check returns the CORRECT verdict (true iff goal is in the start's
    region), which is the documented intent and exactly what the sole caller wants
    (`PathableTargetResolver` gating `bRequiresPathableTarget` — now actually functional). Precision
    boundary documented on `CellComponent` (diagonal-squeeze corners / oversized agents may over-report
    → graceful partial in FindPath, never a hard fail; dynamic blockers ignored by design).
  • **C — random reachable point:** base virtual `GetRandomReachablePoint(Origin,Radius,&Rng,&Out)` +
    AStar override (disc rejection-sampling via `FFixedRandom::PointInCircle`, accept first passable +
    same-component cell within true radius; 32-attempt best-effort cap) + seed-based
    `SeinGetRandomReachablePoint` BPFL (deterministic: identical Seed → identical point). Built on E's
    component field.
  All four are additive API surface; only E changes an existing verdict (for the better). Working tree
  dirty, no commits.
- 2026-06-15 — **§9 Phase 1 DEBUG + two-dial speed (RJ found terrain cost not visibly applying).** Two issues:
  • **Bake bug (fixed):** the terrain-volume `EncompassesPoint` probe used the cell's surface Z, which a
    ground-placed brush often doesn't vertically contain → 0 cells classified → cost stayed 1. Now probes at
    the volume's own mid-height (a terrain region is a 2D XY concept). Added a bake diagnostic: "N volume(s)
    gathered, M phys-mat mapping(s) -> K/total cells classified non-Default."
  • **Conceptual gap (I mis-stated it earlier):** NavCost is an A* ROUTING weight (avoid terrain), NOT a
    traversal-speed multiplier. RJ wanted the CoH speed feel → decided **both dials, independent** (route
    around AND/or slow down separately: "you don't walk slower on roads but might avoid them for negative
    cover"). Implemented the **SPEED dial**: `FSeinTerrainTypeDefinition::SpeedMultiplier` (FFixedPoint,
    floored 0.05) + `USeinARTSCoreSettings::GetTerrainSpeedMultiplier` + neutral `USeinNavigation::
    GetTerrainTypeAt(WorldPos)` (AStar override reads runtime `CellTerrainType`) + `FSeinMovementContext::
    TerrainSpeedMultiplier` (sampled per tick in `USeinMoveToAction` from the unit's cell) + shared
    `USeinMovement::EffectiveTopSpeed(Ctx)` = TopSpeed × mult. Adopted in Basic, BasicUnit, and the
    Movement+ ground modes (Infantry/Wheeled/Tracked cruise targets); air modes (Hover/Flight) intentionally
    excluded (ground terrain doesn't slow flyers); vehicle REVERSE keeps ReverseTopSpeed (edge case). All
    deterministic, default 1 = behaviour-preserving until authored. Build-green. Memory: [[terrain-cost-routing-not-speed]].
- 2026-06-15 — **§9 Phase 1 PIE-VERIFIED by RJ** ("it works"): both dials confirmed in editor — terrain
  volume classifies cells after the probe-Z fix; SpeedMultiplier slows/speeds traversal; NavCost routes
  around. §9 Phase 1 (BASE terrain types: routing cost + speed) is **DONE**. Phase 2 (cover binding) remains
  deferred (designed; cached in §9 + [[terrain-cover-binding-plan]]) — pick up on RJ's go. Phase 3 (debug-viz
  tint, FoW-reads-type) still optional.
- 2026-06-15 — **§9 Phase 2 (COVER binding) + Phase 3 (debug-viz + FoW vision) DONE + build-green** (RJ: "do
  that ... add debug viz and FoW reading"):
  • **Cover binding (Phase 2, cover extension only):** `USeinARTSCoverSettings::TerrainCoverQuality`
    (TMap<TerrainTag, CoverQualityTag>, e.g. Road→Negative). `USeinCoverDefault::QueryCoverAt` samples the
    baked terrain type under the query point (via `USeinNavigation::GetTerrainTypeAt` → `GetTerrainTag` →
    the map) and appends an OMNIDIRECTIONAL, non-fog-gated context; `QueryBestCoverQualityAt` gets it free
    (calls QueryCoverAt). Dropped the empty-provider early-out so terrain cover works with zero placed
    providers. Cover Build.cs gained `SeinARTSNavigation`. **Zero base changes** beyond the neutral
    `GetTerrainTag` settings helper. Negative tag + combat formula already existed.
  • **Debug viz (Phase 3):** `USeinNavigationAStar::CollectDebugCellQuads` tints walkable cells by their
    terrain type's `DebugColor` (Default stays green, blocked red) — authored terrain shows in the nav
    overlay (`Sein.Nav.Show`).
  • **FoW vision (Phase 3) — the THIRD dial:** `FSeinTerrainTypeDefinition::VisionMultiplier` +
    `USeinARTSCoreSettings::GetTerrainVisionMultiplier`. `USeinFogOfWarDefault::TickStamps` scales each
    source's stamp radii (Radius/HalfExtentX/Y on a transient copy) by the terrain under it; the stamp
    cache hashes the scaled stamps and source MOVEMENT (the only way terrain-under-source changes — bake
    is static) already invalidates via WorldPos, so stationary sources keep the fast path. FoW Build.cs
    gained `SeinARTSNavigation`. **Semantic = reduced own-sight on the terrain** (forest = see less far),
    NOT LOS-occlusion-through (height-based occluders have a unit-inside-goes-blind problem — deliberately
    not done; flagged on the field docstring as a separate future feature). Air doesn't apply (FoW reads
    terrain under the source; flyers' GetTerrainTypeAt is the ground type — acceptable; revisit if flyers
    shouldn't be forest-blinded).
  Terrain types now drive THREE independent dials: NavCost (routing) · SpeedMultiplier (speed) · VisionMultiplier
  (sight), plus the cover-extension binding. All deterministic, default 1/empty = behaviour-preserving.
  **PIE-PENDING** (RJ: author a Road→Negative cover map + a forest VisionMultiplier; confirm cover quality on
  terrain, nav overlay tints, and reduced sight in forest). §9 Phases 1–3 complete.
- 2026-06-15 — **Debug-viz tint actually fixed in the scene proxy** (RJ: terrain cells painted red despite
  being walkable). Root cause was NOT the bake — `CollectDebugCellQuads` emitted the right DebugColor, but
  `FSeinNavDebugProxy` re-bucketed every cell by a crude `C.R > C.G` test into TWO fixed materials
  (green/red), so any terrain tint with R>G (e.g. brown) drew with the blocked-red material. Fix: the proxy
  now buckets static cells by EXACT color (one material per color, same mechanism the dynamic-blocker overlay
  already used) and `CollectAssetPreviewQuads` emits per-cell colors (terrain-tinted) too — so the authored
  DebugColor renders faithfully in both live + editor-idle previews. (False trail first: I wrongly suspected
  the terrain volume's brush was being traced and blocking the cells, added bake-ignore edits to
  SeinLevelDataDefault/SeinNavigationAStar, then REVERTED them once RJ confirmed behaviour was correct — the
  cells were always walkable. Lesson: "red in the nav overlay" did NOT mean blocked, because the proxy
  snapped colors.) Build-green. **PIE-VERIFIED by RJ ("worked").**
- 2026-06-15 — **Terrain-types initiative wrap.** Verified by RJ: routing + speed dials, and the debug-viz
  tint. Built + build-green, PIE-pending RJ's own authoring pass: the Cover binding (author a
  TerrainCoverQuality entry → confirm terrain cover quality) and the FoW VisionMultiplier dial (author a
  forest type's VisionMultiplier <1 → confirm reduced sight). §9 Phases 1–3 done; nothing else outstanding
  on this initiative. Paint tool + FoW LOS-occlusion-through remain explicitly deferred. Working tree dirty,
  no commits (git is RJ's).
- 2026-06-15 — **§9 terrain-cost DESIGN SETTLED** (RJ answered the two forks): author neutral terrain
  TYPE once into the shared substrate (not raw cost); sources = terrain volumes (primary) + phys-material
  mapping (secondary), no paint tool. Concrete 3-phase build spec written into §9 (Phase 1 BASE = type
  registry + substrate type field + ASeinTerrainVolume + nav cost-from-type + BlockedTerrainTags wiring;
  Phase 2 EXTENSION = cover reads the type channel → Negative on roads; Phase 3 optional). Grounded in
  the bake/cover code dig (A* already multiplies CellCost; phys-material currently discarded by the shared
  trace; first-match volume resolution needs a Priority field). NOT yet started — awaiting go on Phase 1.
- 2026-06-15 — **§9 Phase 1 (BASE terrain cost) DONE + build-green.** Built in 3 chunks, compiled after each:
  • Chunk A — `FSeinTerrainTypeDefinition` (Data/, sibling of the nav-layer def: Tag + NavCost[1..254] +
    PhysicalMaterials + DebugColor) + `USeinARTSCoreSettings::TerrainTypes` (Category "Terrain") +
    `GetTerrainNavCost(idx)` / `GetTerrainTypeIndex(tag)` helpers (centralize the reserved-0/+1 mapping).
    Phys mats stored as `FSoftObjectPath` + AllowedClasses (NOT TSoftObjectPtr<UPhysicalMaterial>) to keep
    a PhysicsCore link OUT of the sim module — first build caught that as an LNK2019.
  • Chunk B — substrate per-cell type: `FSeinLevelCellSurface::TerrainTypeIndex` + asset `CellTerrainType`
    + runtime `CellTerrainType` (lenient load: old assets → all-Default, no forced re-bake) + `ASeinTerrainVolume`
    (AVolume, Tag + Priority; SeinARTSLevelData gained PhysicsCore dep). Bake resolves per cell: Default →
    phys-material path map (trace now sets bReturnPhysicalMaterial) → highest-Priority terrain volume override.
  • Chunk C — nav consumption: `BakeLayer` writes `Cost = GetTerrainNavCost(type)` (Default→1, so an
    un-authored level bakes byte-identically); runtime loads `CellTerrainType`; `BlockedTerrainTags` wired
    as a per-request 256-entry gate in `IsCellPassableForPath` (guarded — no-blocked-terrain agents pay
    nothing; honored by A* AND the LoS smoother).
  Behaviour-preserving until a designer authors a type + RE-BAKES. PIE-PENDING (RJ: author a Road/Mud type,
  drop a terrain volume or paint a phys-mat'd landscape layer, re-bake, confirm cost shifts + an amphibious
  BlockedTerrainTags unit routes around its barred type). Incidental: fixed a latent `/permissive-` lambda
  capture in the editor movement-data picker (`SeinInstancedStructDetails.cpp`) that only surfaced on the
  CoreEntity-triggered non-unity recompile. Phase 2 (cover) NOT started — proposed below, deferred to RJ.
