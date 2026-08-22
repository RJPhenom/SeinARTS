# SeinARTS — Project Root Guide

This is the **project-level** guide, loaded by every session rooted at `D:/Projects/Unreal Engine/SeinARTS`.
It owns the cross-cutting rules that apply to **all six production plugins** and the two test suites. Each plugin has its own
`AGENTS.md` with the deep, plugin-specific detail — read the relevant one when you scope into it
(pointers below).

> Sessions used to be scoped to the `SeinARTSFramework` plugin directory only. They now run from
> this project root so a single session has native visibility across the six production plugins
> and both disabled test plugins. When you start work, read this file first, then the plugin-specific
> `AGENTS.md` for whatever you're touching.

> Read `.agents/WORKFLOW.md` and `.agents/STYLE_GUIDE.md` before changing code or documentation.
> `.agents/` is dot-prefixed but mandatory agent context; this guide owns technical boundaries and
> implementation rules.

> **Active initiative — movement & navigation depth.** The movement/avoidance/nav seams are clean and
> pluggable (`USeinAvoidance` / `USeinCollisionResolver` / `USeinNavigation` abstract-base + settings
> picker; the `FSeinPath` typed-segment seam); current work is deflating localized bloat in a few
> function bodies (A* diagnostics, the `TickAction` re-seek tangle, the avoidance kernel) without
> redesigning the seams, plus qualification of Movement+'s shipped steering-first, curated
> Reeds-Shepp-style start-maneuver planner. It is not a general Reeds-Shepp/Dubins route solver. The
> nav↔movement seam is still evolving — re-ground against live code before asserting.

---

## HARD RULE: never use worktrees

**No exceptions.** Git worktrees are banned across all branches.

- **Never** create, enter, or delegate work through a Git worktree.
- If a session starts outside `D:/Projects/Unreal Engine/SeinARTS`, stop and return to the primary
  checkout before changing files.
- One author writes to the checkout at a time. Preserve work and complete the handoff review before
  taking over.

> Note: as of 2026-06-02 the project root **is** a git repository — a single project-wide monorepo
> (`main`, initial commit `ecf6068`) tracking the host, six production plugins, and two disabled
> test plugins, with **Git LFS**
> for binary assets (`*.uasset`/`*.umap` + common media). Baked level data (`**/Content/LevelData/` + legacy patterns)
> is gitignored as a regenerable build artifact — **re-bake after a fresh clone** via the one
> "Bake Level Data" button on `ASeinLevelVolume` (unified pipeline, CP1.1). History
> starts fresh from the plugin split; the framework's pre-split history is archived at
> `https://github.com/RJPhenom/SeinARTSFramework`. The monorepo's `origin` remote is
> `https://github.com/RJPhenom/SeinARTS.git`, and `gh` (v2.94.0) is installed — but may be
> unauthenticated; run `gh auth login` if PR/remote tooling reports expired auth. The no-worktree
> HARD RULE above still applies.

---

## Building & compiling

**Do not disc-search for the engine.** It's UE **5.8** at `C:/Program Files/Epic Games/UE_5.8`
(the `.uproject` `EngineAssociation` is `"5.8"`). Host project: `SeinARTS.uproject`. Editor target:
`SeinARTSEditor`. Use the repo build script:

```powershell
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/Build.ps1"                       # SeinARTSEditor Win64 Development (incremental ≈ 20s)
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/Build.ps1" -ExtraArgs '-Clean'   # clean rebuild
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/Build.ps1" -Target SeinARTS -Config Shipping
```

`Scripts/Build.ps1` resolves the engine (known path → registry fallback via `EngineAssociation`), warns if
the editor is open, and returns UBT's exit code. Equivalent raw one-liner if the script is ever gone:

```powershell
& "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" SeinARTSEditor Win64 Development -Project="D:/Projects/Unreal Engine/SeinARTS/SeinARTS.uproject" -WaitMutex
```

- **Run builds in the background** (`run_in_background`) — even incremental is tens of seconds; a
  clean build is minutes.
- **Close the editor first**, or hot-patch in-editor with **Live Coding (Ctrl+Alt+F11)**. A running
  editor locks the module DLLs, so a command-line **link** fails on `*.dll in use` — the *compile*
  still runs, so you can build-to-check-errors with the editor open, just not relink.
- Success = exit `0` / `Result: Succeeded`. UBT prints `Compile [x64] <file>.cpp` and
  `Link [x64] UnrealEditor-<Module>.dll` lines — confirm the module you changed actually rebuilt.

### Automated tests

Automated tests live in the disabled, non-shipping `Plugins/SeinARTSTestSuite` plugin; read its
`AGENTS.md` before adding or running tests. Production modules never depend on that plugin or
`CQTest`. Enable it explicitly through its `RunTests.ps1`; ordinary and Shipping builds leave it out.

---

## Working style: making changes that stick

This is a **lockstep-deterministic** sim, so a subtle mistake is a *silent desync discovered late* — the
failure mode that ends in rollback (this project's owner has lived many). The discipline that avoids it,
learned from the sessions that *worked*:

- **Match verification to blast radius.** A change touching the sim spine, determinism, or multiple
  modules earns the full loop: investigate against **live code** → design and present the real fork(s)
  for RJ to decide → implement → **adversarially red-team the change** (independent agents whose job is
  to *refute* it) → build green (read the log, not just the exit code) → hand RJ the A/B. Trivial
  mechanical edits skip the ceremony. Turn **ultracode ON** for the former (it defaults you to workflow
  orchestration + adversarial verification); leave it off for the latter — the verification is
  token-expensive and only earns its cost when a missed bug means a rollback.
- **Determinism is invisible to code-reading — verify, don't trust confidence.** "This is bit-identical
  / deterministic" is a **hypothesis** until an independent adversarial pass has tried and failed to
  refute it AND the `Sein.Sim.Parallel 0`-vs-`1` StateHash agrees (plus peer/replay for anything that
  shifts *which tick* something happens). An assistant's certainty about determinism is **not evidence**
  here — confidently-wrong determinism claims have been caught by the red-team, never by re-reading. The
  A/B StateHash is the definition of "done" for a sim change, not a nice-to-have; RJ's PIE is the final
  oracle and everything before it is reasoning.
- **RJ owns the forks; the assistant owns the mechanism.** Feel, product, sequencing, and ship-posture
  are RJ's calls — investigate, present options **with a recommendation**, then gate implementation on
  his pick. Don't guess at taste, and don't reorder his "this then that."
- **Ground every load-bearing claim in the live code before acting** — including re-verifying a
  subagent's or workflow's own synthesis (they err too, and the workflows themselves sometimes fail or
  return stubs; read their output critically). See "Source of truth" below on stale docstrings.
- **Defer explicitly, don't gold-plate.** Record deferred items where a future dev will find them.

### Engineering-document artifacts

- Root `Docs/` is reserved for RJ's GitHub Pages documentation website. Agents must leave it
  empty and must not author or restore content there unless RJ explicitly opens a website task.
  Consolidate durable engineering contracts into the existing `.agents/` records instead of
  creating a mirrored documentation tree.
- Durable agent engineering context belongs in `.agents/`; short-lived exploration stays untracked.
- Do not create a repository `Output/` tree. Put requested PDF exports in the user's Downloads directory.

---

## What this is

A deterministic **lockstep RTS framework** for Unreal Engine 5, delivered as one core plugin plus
five opt-in extension plugins. The simulation layer runs entirely on fixed-point math
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
    ├── SeinARTSFramework/             The core. 13 modules. → Plugins/SeinARTSFramework/AGENTS.md
    ├── SeinARTSSquadExtension/        Opt-in squads.  1 module. → .../SeinARTSSquadExtension/AGENTS.md
    ├── SeinARTSCoverExtension/        Opt-in cover.   2 modules. → .../SeinARTSCoverExtension/AGENTS.md
    ├── SeinARTSCoverSquadExtension/   Opt-in Cover+Squad bridge. 1 module. → .../SeinARTSCoverSquadExtension/AGENTS.md
    ├── SeinARTSMovementPlusExtension/ Opt-in movement modes. 1 module ("SeinARTS Movement+"). → .../SeinARTSMovementPlusExtension/AGENTS.md
    ├── SeinARTSOnlineServicesExtension/ Backend-neutral online product services. 1 module. → .../SeinARTSOnlineServicesExtension/AGENTS.md
    ├── SeinARTSTestSuite/              Disabled framework/editor tests. 3 modules. → .../SeinARTSTestSuite/AGENTS.md
    └── SeinARTSExtensionTestSuite/     Disabled all-extension tests. 2 modules. → .../SeinARTSExtensionTestSuite/AGENTS.md
```

## Plugin topology & dependency chain

```
SeinARTSFramework ................... base; depends on no other Sein plugin
   ├── SeinARTSSquadExtension ............. REQUIRES SeinARTSFramework
   ├── SeinARTSCoverExtension ............. REQUIRES SeinARTSFramework
   ├── SeinARTSMovementPlusExtension ...... REQUIRES SeinARTSFramework
   │                                        (concrete movement modes; framework keeps Basic / Basic Unit)
   ├── SeinARTSOnlineServicesExtension .... REQUIRES SeinARTSFramework
   │                                        (provider-neutral contracts + local loopback provider)
   └── SeinARTSCoverSquadExtension ........ REQUIRES Framework + Cover + Squad
                                            (optional integration bridge only)

SeinARTSTestSuite ................... disabled development-only Framework consumer
SeinARTSExtensionTestSuite ......... disabled consumer of the base test suite + all extensions;
                                     no production plugin may depend on either test plugin
```

Dependencies point **up** toward the framework, never down. The framework knows nothing about the
extensions; an extension may be stripped and the framework still builds and runs. Cover and Squad
are physically independent plugins. Their only cross-extension integration lives in the separate
`SeinARTSCoverSquadExtension`, so games enable that bridge only when they use both parent features.

## Which AGENTS.md to read

| If you're working on… | Read |
|---|---|
| Sim core, entities, abilities, effects, nav, movement base/steering, FoW, net, editor tooling, UI, gameplay shell | `Plugins/SeinARTSFramework/AGENTS.md` |
| Persistent squads, formation dispatch, reinforcement | `Plugins/SeinARTSSquadExtension/AGENTS.md` |
| Cover providers/geometry, cover-aware dispatch, formation preview | `Plugins/SeinARTSCoverExtension/AGENTS.md` |
| Cover-aware Squad dispatch integration | `Plugins/SeinARTSCoverSquadExtension/AGENTS.md` |
| Infantry/Wheeled/Tracked/Hover/Flight movement modes + per-class tuning | `Plugins/SeinARTSMovementPlusExtension/AGENTS.md` |
| Account, party, matchmaking, results, saves, replay evidence, telemetry, and provider adapters | `Plugins/SeinARTSOnlineServicesExtension/AGENTS.md` |
| Automated tests, fixtures, scripted maps, and test runners | `Plugins/SeinARTSTestSuite/AGENTS.md` |
| Tests intentionally linking every opt-in extension | `Plugins/SeinARTSExtensionTestSuite/AGENTS.md` |

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

7. **Lockstep configuration is state.** Every plugin that owns sim-affecting project settings must
   register them with `FSeinConfigFingerprintRegistry` under a frozen stable contributor ID. Reflected
   property names must match exactly, ordering must be canonical, and contributors unregister on
   module shutdown. A missing extension or mismatched setting must fail compatibility at join instead
   of becoming a silent desync.

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

Known stale-comment traps (re-grounded against live code):
- **Vehicle curves are bounded start maneuvers, not a full route solver.** The shipped A*/default
  planner emits straight segments. Movement+ Wheeled and Tracked may post-process the route head with
  a deterministic curated Reeds-Shepp-style candidate set (departure arc, straight reverse, K-turn,
  reverse-out), emit typed `Arc`/`Straight` segments, then follow the coarse tail with runtime steering.
  Older claims that no shipped vehicle emits arcs are false; claims that this is a general
  Reeds-Shepp/Dubins family search are also false.
- **Net is not "Phase 0."** Despite file docstrings saying "just logs"/"passthrough," real lockstep
  aggregation, replay, lobby, and desync handling are implemented.
- **Squads are not abstract.** A squad is a real lightweight (non-abstract) `ASeinActor` so banners
  can track its centroid — older "abstract entity" wording is obsolete.
- **`SpawnEntity`'s archetype comment is stale** — identity/cost come from injected
  `FSeinIdentityComponent` / `FSeinProducibleComponent`, not the excised `USeinArchetypeDefinition`.

When you find a docstring that contradicts the code, fix the docstring as part of your change.
