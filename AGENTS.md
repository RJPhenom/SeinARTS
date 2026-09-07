# SeinARTS — Project Root Guide

SeinARTS is a UE 5.8 deterministic lockstep RTS framework: one core plugin, five opt-in
extensions, and two disabled test plugins. Unreal renders the simulation; designers compose
abilities and gameplay in Blueprint. Framework primitives remain genre-neutral.

## Start here

- Work only in `D:/Projects/Unreal Engine/SeinARTS`. **Git worktrees are banned, without exception.**
  Never create, enter, or delegate through one. Only one author writes to this checkout at a time.
- Inspect current Git state and the relevant live implementation; preserve unrelated WIP.
  Do not create or switch branches without an explicit user request.
- Read `.agents/WORKFLOW.md` before implementation and the affected plugin's `AGENTS.md` below.
  Read `.agents/STYLE_GUIDE.md` when writing code, comments, or designer-facing text.
  Do not reload guidance already present in the task unless it changed.
- Use `.agents/README.md` to locate relevant context. `PROJECT_STATE.md` is a short orientation;
  historical evidence is under `.agents/history/` and is read only for a specific question.
  Agents maintain internal records autonomously; bookkeeping must not create chores for RJ.
- User decisions own product behavior, feel, compatibility policy, sequencing, and release posture.
  Proceed with routine mechanisms inside the authorized scope; present real forks before implementing them.
- Verification policy has one owner: `.agents/WORKFLOW.md` §3.3. Use scripts to execute and summarize
  checks. A successful build alone never proves runtime correctness or determinism.

## Repository and plugin boundaries

The project-wide Git repository tracks `Source/`, `Config/`, `Content/`, and all plugins.
`Source/SeinARTS` is the thin host module. Origin is `https://github.com/RJPhenom/SeinARTS.git`.
Binary assets use Git LFS. Baked `Content/SeinARTS/LevelData/` is ignored and must be regenerated
with the level volume's **Bake Level Data** button after a fresh clone. Merging does not delete refs.

Read the guide under `Plugins/<plugin>/AGENTS.md` for the affected scope:

| Plugin | Ownership / dependencies |
|---|---|
| `SeinARTSFramework` | Core simulation, entities, abilities/effects, navigation, base movement, FoW, networking, editor, UI, gameplay shell. Depends on no extension. |
| `SeinARTSSquadExtension` | Persistent squads, formations, reinforcement. Requires Framework. |
| `SeinARTSCoverExtension` | Cover geometry/providers, dispatch, preview. Requires Framework. |
| `SeinARTSCoverSquadExtension` | Optional bridge requiring Framework, Cover, and Squad. |
| `SeinARTSMovementPlusExtension` | Infantry/Wheeled/Tracked/Hover/Flight modes. Requires Framework. |
| `SeinARTSOnlineServicesExtension` | Provider-neutral accounts, parties, matchmaking, results, saves, replay evidence, telemetry. Requires Framework. |
| `SeinARTSTestSuite` | Disabled framework/editor tests and runners. Requires Framework; never a production dependency. |
| `SeinARTSExtensionTestSuite` | Disabled all-extension tests. Requires the base test suite and extensions. |

Dependencies point toward the framework. Cover and Squad remain independent; their integration
belongs only in the bridge plugin. Removing an extension must leave the framework usable.
Production modules never depend on either test plugin or `CQTest`.

## Build and validation entrypoints

UE **5.8** is at `C:/Program Files/Epic Games/UE_5.8`; do not search the disk for it.
Host: `SeinARTS.uproject`; editor target: `SeinARTSEditor`.

```powershell
& ./Scripts/Build.ps1 -Quiet
& ./Scripts/Build.ps1 -Target SeinARTS -Config Shipping -Quiet
& ./Scripts/Validate.ps1 -Preset Focused -Profile Framework -Suite SeinARTS.Unit.Core
```

`Build.ps1` resolves the engine and returns UBT's exit code. `-Quiet` retains the full log and
writes a receipt with compilation/link actions under `Saved/Build`; inspect this summary first.
`-ExtraArgs '-Clean'` cleans outputs only; a subsequent ordinary build performs the rebuild.
Run long commands asynchronously and wait for completion; do not repeatedly poll unchanged logs.
Close Unreal before external linking, or use Live Coding (Ctrl+Alt+F11) when appropriate.
Success requires exit 0 and the intended target/module build evidence; incremental up-to-date
results are valid only for the same source/build inputs.

Read the test-suite guide before adding/running tests. `RunTests.ps1` explicitly enables test
plugins, restores the ordinary editor receipt, and rejects stale `-SkipBuild` provenance.
`Scripts/Validate.ps1` sequences development checks; it does not publish or certify a release.
`Scripts/Release/Invoke-ReleaseGate.ps1` owns full release qualification and publication.

## Cross-cutting invariants

1. **Sim/render separation.** Sim modules never reference visual-layer systems. The sanctioned
   bridge is `USeinEntityBridgeComponent` on `ASeinActor`; data flows sim → render. Input feeds
   simulation exclusively through the command buffer. Never put renderer state in canonical state.

2. **Determinism.** Sim code uses `FFixedPoint` (32.32), `FFixedVector`, `FFixedTransform`,
   `FFixedQuaternion`, `FSeinEntityHandle`, and `FFixedRandom`. No `float`, `FVector`, `FMath::`,
   `rand()`, raw actor pointers, or non-deterministic UE calls in sim work. Respect
   `SEIN_SIM_ONLY` / `SEIN_SIM_SCOPE`. Float conversions are editor/debug boundaries only.
   Editor-time randomness may author values serialized to fixed-point before simulation.
   Relevant canonical serial/parallel traces and lifecycle tests are evidence; confidence is not.

3. **Designer-first abilities.** Move, attack, harvest, build, patrol, garrison, and reinforce are
   `USeinAbility` Blueprints with latent execution graphs. C++ supplies deterministic primitives;
   there is no hardcoded gameplay command enum beyond activation/cancellation plumbing.

4. **Blueprint authoring and payloads.** A unit type is an `ASeinActor` Blueprint. Data-only
   `USeinEntityComponent` authoring components generate `FSein...Payload` values into the entity
   bridge's backend `TArray<FInstancedStruct> ComponentData`; spawn injects those payloads into
   reflection-backed sim storage. Preserve this authoring/backend split and serialized compatibility.
   Payloads are pure deterministic data; runtime gameplay logic belongs in systems, abilities,
   effects, AI controllers, and brokers. Do not regress the component/payload refactor.

5. **Initial destination preview equals command submission.** Preview and commit use the same
   shared resolver (`SeinComputeFormationPreview` → `ResolveFormationLayout` → `PostProcessPositions`).
   Resolve nearest-reachable destinations and cover authority once there. No downstream A* partial
   result or wall adjustment silently relocates the initial destination. Cover slots are authoritative
   even over coarse blocked cells: preview and initial submission use the exact slot. This binds the
   first path request; later interval repaths may re-resolve a destination the world made unreachable.

6. **Configuration is state.** Every owner of sim-affecting settings registers them with
   `FSeinConfigFingerprintRegistry` under a frozen stable contributor ID. Use exact reflected names,
   canonical ordering, and shutdown unregistration. Missing extensions or mismatched settings must
   fail compatibility admission rather than silently desynchronize.

## Shared code conventions

- Types: `FSein...`, `USein...`, `ASein...`, fixed types `FFixed...`. Backend structs use `Payload`;
  authoring ActorComponents use `Component`; Blueprint libraries use `BPFL`. Native data components
  derive their menu labels from class names without redundant `DisplayName` overrides.
- Every sim USTRUCT uses `USTRUCT(meta = (SeinDeterministic))`; retain deterministic authoring validation.
- `FInstancedStruct` is in `CoreUObject` (`StructUtils/InstancedStruct.h`). Do not add the deprecated
  standalone `StructUtils` module dependency.
- `.agents/STYLE_GUIDE.md` owns Blueprint categories, tooltips, property metadata, and presentation style.
  Preserve reflected names when metadata can implement a requested label change.

## Records and source of truth

- `Docs/` is the public website. Edit it when public documentation is in the authorized task scope;
  otherwise record affected pages in `.agents/PUBLIC_DOCS_BACKLOG.md` and report the impact.
  Preserve existing website content. Agent reports and engineering notes do not belong there.
- Durable engineering contracts belong in existing `.agents/` records; keep exploration untracked.
  Do not create a mirrored documentation tree or repository `Output/` directory. Requested PDFs go
  to the user's Downloads directory. Generated tests, builds, and logs remain ignored under `Saved/`.
- Live code wins over stale comments or historical evidence. Fix contradictory comments in touched
  code. Do not cite retired `DESIGN.md` / `PLAN.md` as authority.
- Known stale claims: networking is implemented, not “Phase 0”; squads are real non-abstract actors;
  spawn identity/cost come from payloads, not the removed archetype definition. Movement+ vehicles
  emit bounded curated start-maneuver arcs/straights, not a general Reeds-Shepp/Dubins route solver.
- `.agents/READINESS_ROADMAP.md` and `.agents/OPEN_RISKS.md` retain unresolved product forks and
  human/runtime gates. Re-ground the relevant boundary in live code; do not silently choose a fork.
