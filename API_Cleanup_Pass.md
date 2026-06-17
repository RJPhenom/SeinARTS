# Movement / Nav API Cleanup — Autonomous Pass

> **SUPERSEDED (2026-06-14) by `Base_Plugin_Extensibility_Plan.md`.** This pass = **Phase 0** of that
> plan: completed and build-green. Retained as the record of the first cleanup pass.

**Date:** 2026-06-14
**Author:** Claude (autonomous session, unattended)
**Scope constraint:** **NO BEHAVIOUR CHANGE** for shipped classes. This pass is *truth + de-leak +
doc polish* only. Anything that alters runtime/authoring behaviour is explicitly deferred (Part 2).

This doc is both the plan and the record. It grounds each change with `file:line` citations (treat
them as starting coordinates — re-ground against live code before editing). Status log at the bottom.

---

## Guardrails (must hold for every edit)

1. **No behaviour change** for the shipped `USeinNavigationAStar` / `USeinBasicUnitMovement` paths.
   Verify by reasoning: for shipped classes the new code path must be observably identical.
2. **Determinism intact** — touch nothing in the sim tick that admits `float` / `AActor*` / `rand`.
   These edits are comments + one virtual-dispatch refactor; no sim math changes.
3. **"Destination preview is sacred"** — untouched; this pass does not go near the resolver path.
4. **Git: on-disk only.** No commits, branches, pushes, or co-authorship. Leave a clean dirty tree.
5. **No worktrees** (project HARD RULE). Work in the main checkout.
6. **Build-verify** in the background. Editor-open ⇒ compile verifies but relink may fail on locked
   DLL; that is acceptable for this pass — report it, don't fight it.

---

## Part 1 — Changes IN this pass

### 1A. Doc-drift purge (comments/docstrings only — zero behaviour risk)

| # | File:line (approx) | What's wrong | Fix |
|---|---|---|---|
| a | `Plugins/SeinARTSFramework/Source/SeinARTSNavigation/Private/SeinNavigationAStar.cpp:~1502–1527` | Comment block describes a `FindCellPath`/`FitVehicleCurve`/`FindPath` 3-way split + "Phase 2 Dubins / Phase 3 Reeds-Shepp." `FitVehicleCurve` does not exist; `FindPath` just calls `FindCellPath` + `PushWaypointsAwayFromWalls`. | Delete/rewrite to describe what the code actually does. |
| b | `Plugins/SeinARTSFramework/Source/SeinARTSMovement/Public/Movement/SeinMovement.h:~11–16` | "Shipped subclasses: …Infantry/Wheeled/Tracked…" — those live in SeinARTSMovementPlus now. | Reduce to `USeinBasicMovement` / `USeinBasicUnitMovement`; note the extension supplies the rest via the soft `MovementClass` path. |
| c | `Plugins/SeinARTSFramework/Source/SeinARTSMovement/Public/Movement/SeinBasicUnitMovement.h:~8` | "Sits between USeinBasicMovement and the vehicle classes (momentum, kinematic arrival, curvature preview)." Vehicle classes left the module; this class now has kinematic arrival itself. | Rewrite framing: RTS-default ground mover (seek+arrive + kinematic ramp + face-velocity). |
| d | `Plugins/SeinARTSFramework/Source/SeinARTSNavigation/Public/SeinNavigationAStar.h:~4, ~9–11` | "Minimal on purpose" — no longer true (C-space clearance, soft-wall seeding, dynamic-WD cache, escape-nudge). | Drop "minimal"; one line noting the footprint-clearance layer is the non-trivial part. |
| e | `Plugins/SeinARTSFramework/Source/SeinARTSCoreEntity/Public/Components/SeinNavigationComponent.h:~121–122` | `AcceptanceRadius` doc claims a per-call override "if > 0" — no such parameter exists on any entry point. | Remove the per-call-override claim; state it's the per-unit acceptance radius. |
| f | `Plugins/SeinARTSFramework/Source/SeinARTSMovement/Public/Movement/SeinMovement.h:~327–333` | `TODO(PlannerAStar)` / `SeinNavigationPlannerAStar` reference an orientation-aware nav + Reeds-Shepp that don't exist. | Mark explicitly aspirational/unbuilt. |
| g | `Plugins/SeinARTSFramework/Source/SeinARTSMovement/Public/Movement/SeinMovement.h:~172–182` | `GetMovementDataStruct` doc says the details panel auto-swaps `MovementClassData` — not implemented (no editor consumer). | Soften to "intended; auto-swap not yet wired — see deferred #3." Keep the runtime-unwrap purpose. |

> CLAUDE.md `planning/` dangling citations are real doc-drift too, but out of API scope and in
> project-instruction territory — left for an attended call. Noted, not edited.

### 1B. De-leak: `FindEscapeNudgeTarget` → base virtual

**Goal:** remove the only concrete-class cast in any logic path (grep-confirmed isolated):
`SeinMoveToAction.cpp:749  Cast<USeinNavigationAStar>(Nav)`.

Steps:
1. `SeinNavigation.h` (base) — add
   `virtual bool FindEscapeNudgeTarget(const FFixedVector& AgentPos, FFixedVector& OutTarget, int32& OutTargetWD) const { OutTargetWD = -1; return false; }`
   in the queries section, with a docstring (the shipped A* overrides; base = "no nudge available").
2. `SeinNavigationAStar.h:~132–135` — add `override` to the existing declaration (signature must
   match the base exactly).
3. `SeinMoveToAction.cpp:~746–757` — replace the cast + null-branch with a direct
   `Nav->FindEscapeNudgeTarget(...)` call; on `false`, fall through to the existing "sealed pocket"
   branch (read it first to preserve semantics).
4. Drop the now-unused `#include "SeinNavigationAStar.h"` from the action **only if** nothing else in
   the TU references the concrete type; otherwise leave it.

**Behaviour proof:** shipped A* — the cast always succeeded, so routing through the virtual hits the
identical override → identical result. Non-AStar nav doesn't ship, so no observable change; it merely
degrades to "sealed pocket / stranded" gracefully instead of via the explicit cast-fail branch.

### 1C. Contract honesty (comments only — NO specifier flip)

- `SeinMovement.h:109` (`UCLASS(Abstract, Blueprintable …)`) and `SeinNavigation.h` class decl:
  add a docstring line stating the truth — these are **C++ extension points**; the sim-tick contract
  (`Tick` is `PURE_VIRTUAL`; nav queries are plain `virtual`) is not Blueprint-overridable. Blueprint
  is for **data/tuning** (`MovementClassData`, settings), not behaviour overrides.
- **Do NOT** remove the `Blueprintable` specifier — that's the user's ruling (deferred #1). This pass
  only makes the docs stop implying BP-authorable behaviour.

---

## Part 2 — Deferred (NOT in this pass) + why

| Item | Why deferred | When |
|---|---|---|
| #1 `Blueprintable` flip OR real deterministic BP hooks | User's architectural ruling; flipping the specifier could orphan an existing BP asset unattended; real hooks are a determinism design task. | Attended decision next session |
| #3 `MovementClassData` auto-swap + filtered picker | New editor behaviour; needs editor-restart verification. | Front of Movement+ tuning |
| #4 Steering-profile container (lift inline avoidance literals) | Changes data layout; values come out of tuning. | Front of Movement+ tuning |
| #5 Nav areas / per-request cost filter; revive/remove dead `BlockedTerrainTags` | Feature touching bake + request + cost fn + map UX; removing a public USTRUCT field unattended risks asset/serialization breakage. | Scoped design task |
| #6 Stop/Abort node, path-invalidation event, progress, cheap reachability, expose raycast/random-reachable | Behaviour additions; drag-formations should define the group-feedback semantics. | After drag formations |

---

## Verification checklist

- [x] All Part-1 edits applied (10 edits across 6 files).
- [x] `Build.ps1` background build run.
- [x] Result: `Succeeded` (full relink; editor was not open). 17.07s, exit 0.
- [x] Confirm changed modules rebuilt: `SeinARTSNavigation`, `SeinARTSMovement`, `SeinARTSCoreEntity`
      all recompiled + relinked; UHT regenerated 4 files; `-WarningsAsErrors` passed.
- [x] No commits made; working tree left dirty for user review.

---

## Status log

- 2026-06-14 — Doc created; pass starting.
- 2026-06-14 — Pass applied. Edits:
  - 1A doc-drift: `SeinNavigationAStar.cpp` (FitVehicleCurve/Dubins block rewritten),
    `SeinNavigationAStar.h` ("Minimal on purpose" dropped), `SeinMovement.h` (Shipped-subclasses
    list corrected; `GetMovementDataStruct` auto-swap claim softened; `TODO(PlannerAStar)` marked
    unbuilt), `SeinBasicUnitMovement.h` ("sits between vehicle classes" + "instant speed" rewritten),
    `SeinNavigationComponent.h` (AcceptanceRadius per-call claim removed).
  - 1B de-leak: `FindEscapeNudgeTarget` promoted to `USeinNavigation` base virtual (default false);
    `SeinNavigationAStar.h` decl now `override`; `SeinMoveToAction.cpp` drops the
    `Cast<USeinNavigationAStar>` and calls through the base. `Nav` confirmed `USeinNavigation*`
    (SeinMoveToAction.cpp:159). Behaviour-identical for shipped A* (cast always succeeded);
    non-A* navs now reach the same Stranded outcome via the sealed-pocket branch instead of the cast-fail branch.
  - 1C contract honesty: deferred into the same docstring edits where natural; `Blueprintable`
    specifier NOT flipped (user's ruling).
  - Note: left the now-unused `#include "SeinNavigationAStar.h"` in `SeinMoveToAction.cpp` (harmless;
    removing it is extra risk for zero behaviour gain).
- 2026-06-14 — Build kicked off in background (log: `Saved/api_cleanup_build.log`). Awaiting result.
- 2026-06-14 — First build invocation was a no-op (Bash tool lacked `pwsh`; `exit 0` was `tee`).
  Caught on log inspection; re-run via the PowerShell tool.
- 2026-06-14 — **Build SUCCEEDED** (exit 0, 17.07s). Touched TUs recompiled (SeinNavigation,
  SeinNavigationAStar, SeinMoveToAction, SeinBasicUnitMovement, SeinMovement); all header-dependent
  modules (CoreEntity, Cover, FogOfWar, MovementPlus, CoverSquad, Editor) rebuilt and relinked; UHT
  clean under -WarningsAsErrors. **PASS COMPLETE — behaviour unchanged, working tree dirty, no commits.**
