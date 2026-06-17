# SeinARTS — Project Root Guide

This is the **project-level** guide, loaded by every session rooted at `D:/Projects/Unreal Engine/SeinARTS`.
It owns the cross-cutting rules that apply to **all four plugins**. Each plugin has its own
`CLAUDE.md` with the deep, plugin-specific detail — read the relevant one when you scope into it
(pointers below).

> Sessions used to be scoped to the `SeinARTSFramework` plugin directory only. They now run from
> this project root so a single session has native visibility across the framework **and** all
> three extension plugins. When you start work, read this file first, then the plugin-specific
> `CLAUDE.md` for whatever you're touching.

> **Active initiative — base-plugin extensibility & hardening.** If the work touches the extension
> surface (nav, fog, movement, the bake / level-volume, entities/components, or docs), read
> `Base_Plugin_Extensibility_Plan.md` (project root) first — it's the self-contained source plan
> (it supersedes the retired `planning/` BAR-program docs). Citations there are starting coordinates,
> not boundaries — read outward and re-ground against live code before asserting.

---

## HARD RULE: never use worktrees

**No exceptions.** This is a solo-dev local project — forking work into a separate worktree buys
nothing and adds synchronization overhead.

- **Never** spawn `Agent` calls with `isolation: "worktree"`. Use the default (no isolation).
- **Never** create or work inside a `.claude/worktrees/<...>/` path. If a turn starts and the
  working directory contains `.claude/worktrees/`, **stop, tell the user, and switch back to the
  main checkout at `D:/Projects/Unreal Engine/SeinARTS/` before doing anything else.**
- Parallelism is still encouraged — just spawn parallel agents against the **main checkout**.
  Feature work is naturally module-scoped, so conflicts between parallel agents are rare.

> Note: as of 2026-06-02 the project root **is** a git repository — a single project-wide monorepo
> (`main`, initial commit `ecf6068`) tracking the host project and all four plugins, with **Git LFS**
> for binary assets (`*.uasset`/`*.umap` + common media). Baked level data (`**/Content/LevelData/` + legacy patterns)
> is gitignored as a regenerable build artifact — **re-bake after a fresh clone** via the one
> "Bake Level Data" button on `ASeinLevelVolume` (unified pipeline, CP1.1). History
> starts fresh from the plugin split; the framework's pre-split history is archived at
> `https://github.com/RJPhenom/SeinARTSFramework`. No remote is configured on the monorepo yet, and
> `gh` is not installed. The no-worktree HARD RULE above still applies.

---

## Building & compiling

**Do not disc-search for the engine.** It's UE **5.7** at `C:/Program Files/Epic Games/UE_5.7`
(the `.uproject` `EngineAssociation` is `"5.7"`). Host project: `SeinARTS.uproject`. Editor target:
`SeinARTSEditor`. Use the repo build script:

```powershell
& "D:/Projects/Unreal Engine/SeinARTS/Build.ps1"                       # SeinARTSEditor Win64 Development (incremental ≈ 20s)
& "D:/Projects/Unreal Engine/SeinARTS/Build.ps1" -ExtraArgs '-Clean'   # clean rebuild
& "D:/Projects/Unreal Engine/SeinARTS/Build.ps1" -Target SeinARTS -Config Shipping
```

`Build.ps1` resolves the engine (known path → registry fallback via `EngineAssociation`), warns if
the editor is open, and returns UBT's exit code. Equivalent raw one-liner if the script is ever gone:

```powershell
& "C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" SeinARTSEditor Win64 Development -Project="D:/Projects/Unreal Engine/SeinARTS/SeinARTS.uproject" -WaitMutex
```

- **Run builds in the background** (`run_in_background`) — even incremental is tens of seconds; a
  clean build is minutes.
- **Close the editor first**, or hot-patch in-editor with **Live Coding (Ctrl+Alt+F11)**. A running
  editor locks the module DLLs, so a command-line **link** fails on `*.dll in use` — the *compile*
  still runs, so you can build-to-check-errors with the editor open, just not relink.
- Success = exit `0` / `Result: Succeeded`. UBT prints `Compile [x64] <file>.cpp` and
  `Link [x64] UnrealEditor-<Module>.dll` lines — confirm the module you changed actually rebuilt.

---

## What this is

A deterministic **lockstep RTS framework** for Unreal Engine 5, delivered as one core plugin plus
three opt-in extension plugins. The simulation layer runs entirely on fixed-point math
(`FFixedPoint`, 32.32) for cross-platform bit-determinism. Unreal is the renderer — the sim never
touches `float`, `AActor*`, or any non-deterministic UE system. Data flows one way: **sim → render**.

The genre target is the squad-tactical RTS subgenre (squad + individual units, cover, terrain
types, veterancy, tech upgrades, capture points, retreat). The framework itself stays
genre-neutral; specifics are designer-authored in Blueprint.

---

## Repository layout

```
D:/Projects/Unreal Engine/SeinARTS/
├── SeinARTS.uproject        Host UE project (used to compile/PIE the plugins during dev)
├── Source/SeinARTS/         Thin host game module — nothing of substance lives here
├── Config/ Content/         Host project config + content
└── Plugins/
    ├── SeinARTSFramework/             The core. 12 modules. → Plugins/SeinARTSFramework/CLAUDE.md
    ├── SeinARTSSquadExtension/        Opt-in squads.  1 module. → .../SeinARTSSquadExtension/CLAUDE.md
    ├── SeinARTSCoverExtension/        Opt-in cover.   3 modules. → .../SeinARTSCoverExtension/CLAUDE.md
    └── SeinARTSMovementPlusExtension/ Opt-in movement modes. 1 module ("SeinARTS Movement+"). → .../SeinARTSMovementPlusExtension/CLAUDE.md
```

## Plugin topology & dependency chain

```
SeinARTSFramework ................... base; depends on no other Sein plugin
   ├── SeinARTSSquadExtension ............. REQUIRES SeinARTSFramework
   ├── SeinARTSCoverExtension ............. REQUIRES SeinARTSFramework
   │                                        its SeinARTSCoverSquad module also REQUIRES SeinARTSSquadExtension
   │                                        (declared "Optional" in the .uplugin, but see the gotcha below)
   └── SeinARTSMovementPlusExtension ...... REQUIRES SeinARTSFramework
                                            (concrete movement modes; framework keeps Basic / Basic Unit)
```

Dependencies point **up** toward the framework, never down. The framework knows nothing about the
extensions; an extension may be stripped and the framework still builds and runs. The Cover
extension can use the Squad extension but treats it as optional at the plugin level.

> **Gotcha:** the Cover extension's optional dependency on Squad is enforced **only** in the
> `.uplugin` (`Optional: true`), not in code — the `SeinARTSCoverSquad` module's `Build.cs`
> hard-links `SeinARTSSquad` with no `#if` guards. So if Squad is absent, `SeinARTSCoverSquad`
> simply doesn't load; it does not degrade to a stub. The other two cover modules are unaffected.

## Which CLAUDE.md to read

| If you're working on… | Read |
|---|---|
| Sim core, entities, abilities, effects, nav, movement base/steering, FoW, net, editor tooling, UI, gameplay shell | `Plugins/SeinARTSFramework/CLAUDE.md` |
| Persistent squads, formation dispatch, reinforcement | `Plugins/SeinARTSSquadExtension/CLAUDE.md` |
| Cover providers/geometry, cover-aware dispatch, formation preview | `Plugins/SeinARTSCoverExtension/CLAUDE.md` |
| Infantry/Wheeled/Tracked/Hover/Flight movement modes + per-class tuning | `Plugins/SeinARTSMovementPlusExtension/CLAUDE.md` |

---

## Cross-cutting invariants (all plugins)

1. **Sim/render separation is sacred.** Sim modules never reference the visual layer. The render
   layer reads from the sim and reacts to visual events. The single sanctioned bridge is
   `USeinEntityComponent` (the **entity bridge**) on `ASeinActor`. Data flows sim → render only;
   the render/input layer feeds the sim *exclusively* through the command buffer.

2. **Determinism is non-negotiable.** Sim code uses `FFixedPoint` / `FFixedVector` /
   `FFixedTransform` / `FFixedQuaternion`, `FSeinEntityHandle` (never raw `AActor*` / `UObject*`),
   and the deterministic PRNG (`FFixedRandom`). **No** `float`, `FVector`, `FMath::`, or `rand()`
   in sim code. The boundary is asserted at runtime via `SEIN_SIM_ONLY` / `SEIN_SIM_SCOPE`
   (defined in `SeinARTSCoreEntity/Public/Core/SeinSimContext.h`). Float↔fixed conversions exist
   but are flagged non-deterministic (editor/debug only). One sanctioned exception:
   `FMath::FRand`-style calls at **editor authoring time** whose results are serialized to
   fixed-point (e.g. cover slot scatter) — deterministic at runtime because the values are baked.

3. **Designer-first.** Abilities, damage formulas, attribute sets, steering profiles, and AI are
   Blueprint-scriptable. C++ provides deterministic primitives and infrastructure; designers
   compose them in BP graphs.

4. **Everything is an ability.** Move, attack, harvest, build, patrol, garrison, reinforce are all
   `USeinAbility` Blueprints with latent-node execution graphs. There is no hardcoded command enum
   beyond the activation/cancel plumbing.

5. **The Blueprint IS the unit.** A unit type is a Blueprint subclass of `ASeinActor`. Sim
   components are authored on the actor's `USeinEntityComponent` (the entity bridge, auto-attached
   by `ASeinActor`'s constructor) via its `ComponentData` array — a `TArray<FInstancedStruct>`
   where each entry is an `FSein…Component` payload struct. At spawn, `USeinWorldSubsystem` walks
   the bridge's `ComponentData` and copies each entry into reflection-backed component storage.
   Designers can author custom components: Right-click → Component creates a `SeinDeterministic`-
   marked `UUserDefinedStruct` that the picker accepts as a valid `ComponentData` entry.

6. **Destination preview === the command's first path request. Sacred — treated as absolute.** The
   destination/formation preview (formation decals, cover-snapped slots, nearest-reachable fallbacks)
   MUST be identical to the destination(s) a movement command would submit on its **first path
   request** for the same cursor/click inputs. It is a pure dry-run of the command's destination
   computation through the **same shared resolver** the commit runs (`SeinComputeFormationPreview` →
   `ResolveFormationLayout` → `PostProcessPositions`). **A destination is an INPUT, not an opinion nav
   may relocate.** No stage silently moves a destination between the preview and that first request;
   reachability resolution (nearest-reachable projection of a genuinely-unreachable raw click;
   cover-slot authority) happens ONCE, in that shared path — never downstream in per-member pathing
   (A* partial best-H, `PushWaypointsAwayFromWalls`) where the preview can't see it. *Scope:* binds
   the **initial** submission only — once a unit is moving, interval repaths may legitimately
   re-resolve a destination the changing world made unreachable; that is not a violation. **Cover
   slots are authoritative**: a designer-authored slot overrules the coarse nav bake (a blocked/"red"
   cell under a slot is a low-resolution false-negative, not a reason to move the slot); the unit is
   delivered to the exact slot and the preview shows the exact slot.

---

## Code conventions (all plugins)

- **Prefixes:** sim USTRUCTs `FSein…`, sim UObjects `USein…`, actors `ASein…`, fixed-point types
  `FFixed…`. Component **payload** structs carry the `Component` suffix
  (`FSeinAbilityComponent`, `FSeinExtentsComponent`, …). Blueprint function libraries carry the
  `BPFL` suffix.
- **Components are pure data.** No event graphs, no state-mutating methods. Logic lives in
  abilities, effects, AI controllers, command brokers, and sim systems.
- **`SeinDeterministic` meta.** Every framework sim USTRUCT carries
  `USTRUCT(meta = (SeinDeterministic))`; this is the marker the editor uses to accept a struct as a
  valid `ComponentData` entry. (The `ComponentData` entry picker is filtered to valid Sein component
  structs via `FSeinInstancedStructFilter`; inside a UDS the field-type picker itself isn't filtered,
  but `FSeinDeterministicStructValidator` strips non-deterministic fields on save.)
- **`FInstancedStruct` ships in `CoreUObject`** (`CoreUObject/Public/StructUtils/InstancedStruct.h`).
  Do **not** add `StructUtils` as a module dependency — the standalone plugin is deprecated in UE 5.5+.
- **BP-visible naming.** Category = `SeinARTS|<Subsystem>[|<Subgroup>]` (singular nouns; `Tags` is
  the only plural exception). On UFUNCTION/UPROPERTY meta, drop the `Sein` prefix from `DisplayName`
  (`DisplayName = "Has Tag"`, not `"Sein Has Tag"`) — add an explicit `DisplayName` whenever the C++
  symbol starts with `Sein` to suppress UE auto-derivation. BPFL UCLASS `DisplayName` =
  `"SeinARTS X Library"`; ActorComponents use plain `"X Component"` + `ClassGroup = (SeinARTS)`.
  UPROPERTY field names never carry the `Sein` prefix. Write the Category + DisplayName **before**
  the body — retroactive cleanup is a whole separate session of work.

---

## Source of truth: code, not comments

The architecture has stabilized. The per-USTRUCT/UCLASS **docstrings are the primary spec** for
sim-system specifics — but **trust the code's behavior over comments**, because a number of
docstrings lag the implementation. The retired `DESIGN.md` / `PLAN.md` (gone post-Phase-5) are
still cited in some headers (e.g. "DESIGN §11") — those references are dangling.

Known stale-comment traps (verified against code during the 2026-06 doc rebuild):
- **Reeds-Shepp / Dubins vehicle curve-fitting is unbuilt.** `FitVehicleCurve` exists only in
  aspirational comments; path segments are `Straight`-only. Wheeled driving feel comes from runtime
  pure-pursuit steering + nav corner-rounding, not a curve planner.
- **Net is not "Phase 0."** Despite file docstrings saying "just logs"/"passthrough," real lockstep
  aggregation, replay, lobby, and desync handling are implemented.
- **Squads are not abstract.** A squad is a real lightweight (non-abstract) `ASeinActor` so banners
  can track its centroid — older "abstract entity" wording is obsolete.
- **`SpawnEntity`'s archetype comment is stale** — identity/cost come from injected
  `FSeinIdentityComponent` / `FSeinProducibleComponent`, not the excised `USeinArchetypeDefinition`.

When you find a docstring that contradicts the code, fix the docstring as part of your change.
