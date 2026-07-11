# SeinARTSMovementPlusExtension — Plugin Guide

Opt-in extension holding the **concrete movement modes** — Infantry, Wheeled, Tracked, Hover, and
Flight — plus their per-class tuning data. Displays as **"SeinARTS Movement+"** in the editor (the folder is
`SeinARTSMovementPlusExtension`; the `.uplugin` `FriendlyName` is what shows). Strippable: the
framework ships the movement base + `Basic` / `Basic Unit` defaults and runs fine without this.

> **Read the project-root `CLAUDE.md` first** for cross-cutting rules. This file covers the
> movement-*modes* extension only. The movement **infrastructure** — abstract `USeinMovement`, the
> shared steering toolkit, `USeinMoveToAction` / `USeinMoveToProxy`, `USeinMovementBPFL`, the debug
> show-flags — lives in the framework's `SeinARTSMovement` module
> (`Plugins/SeinARTSFramework/CLAUDE.md`).

- **Module:** one runtime module, `SeinARTSMovementPlus`.
- **Depends on:** `SeinARTSFramework` (required). Build deps: Core/CoreUObject/Engine, SeinARTSCore,
  SeinARTSCoreEntity, SeinARTSNavigation, **SeinARTSMovement** (base class + steering toolkit +
  exported debug show-flag helpers), GameplayTags; UnrealEd editor-only.

---

## Why the split is clean (the seam)

The framework was already decoupled from the concrete modes, so extracting them added no coupling:
- `FSeinMovementComponent::MovementClass` (in `SeinARTSCoreEntity`) is a **`FSoftClassPath`**, not a
  `TSubclassOf` — resolved to a `UClass*` at action-init via `TryLoadClass`. Core never hard-links a
  concrete class; null/invalid falls back to `USeinBasicMovement`.
- The editor picker filters by the **base** (`MetaClass = "/Script/SeinARTSMovement.SeinMovement"`),
  which stays in the framework — so every subclass here shows up regardless of module.
- Per-class tuning rides in `MovementClassData` as a generic `FInstancedStruct`. No hard type dep.

Net: the dependency points **up** (extension → framework). Remove this plugin and any unit whose
`MovementClass` points here simply falls back to `Basic`.

## Contents

| Mode | Class | Base | Per-class data (in `MovementClassData`) |
|---|---|---|---|
| Infantry | `USeinInfantryMovement` | `USeinBasicMovement` | `FSeinInfantryMovementData` |
| Wheeled | `USeinWheeledVehicleMovement` | `USeinMovement` | `FSeinWheeledMovementData` |
| Tracked | `USeinTrackedVehicleMovement` | `USeinMovement` | `FSeinTrackedMovementData` |
| Hover | `USeinHoverMovement` | `USeinMovement` | `FSeinHoverMovementData` |
| Flight | `USeinFlightMovement` | `USeinMovement` | `FSeinFlyingMovementData` |

- **Name skew:** the class is `Flight`, its data struct is `Flying` (`FSeinFlyingMovementData`).
  Preserved as-is from before the split — don't "fix" one without a redirect.
- `USeinInfantryMovement` inherits `USeinBasicMovement` (which stays in the framework) — a deliberate
  cross-plugin inheritance; Infantry overrides `ComputeMotion` (a Tier-1 policy over the base
  `USeinMovement::Tick` harness) and uses Basic only for hierarchy.
- All data structs are `SeinSubData`-tagged `FSeinComponent`s — they surface only inside the
  `MovementClassData` polymorphic picker, never the top-level ComponentData picker.
- The classes reach back into the framework's `SeinARTSMovement` for the shared steering helpers
  (`USeinMovement::ResolveLookAheadPoint`, `StepSpeedToward`, nav collision, …) and the debug gate
  (`UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld`) — all `SEINARTSMOVEMENT_API`-exported, so
  the cross-module link is fine.

## Core redirects (do not remove)

`Config/DefaultEngine.ini` ships 5 `ClassRedirects` + 5 `StructRedirects` mapping the old
`/Script/SeinARTSMovement.*` paths to `/Script/SeinARTSMovementPlus.*`. Moving a UClass/UStruct
between modules changes its path; without these, a `MovementClass` soft path or `MovementClassData`
payload saved before the 2026-06-02 split would fail to resolve (silent fallback to `Basic` / lost
sub-data). If you ever rename or re-home one of these types again, add the matching redirect pair.

## Current state

Extracted from the framework's `SeinARTSMovement` module on **2026-06-02**; behavior unchanged by the
move. All five modes are BUILT and behavior-correct — this is refactor/extend territory, not a
skeleton. The steering seam is a landed TWO-TIER contract: **Infantry** is the Tier-1 exemplar
(overrides `ComputeMotion` only — accel/decel + alignment-scaled turn + kinematic arrival brake, all
riding the base `USeinMovement::Tick` harness), and the four vehicles (Wheeled/Tracked/Hover/Flight)
override `Tick` wholesale (Tier-2). Per-class tuning now rides entirely on the sub-data structs
(`FSeinInfantryMovementData` etc.); the earlier half-finished migration is done — each mode class
holds only runtime state (e.g. `CurrentSteer`, `bIsReversing`). Wheeled/Tracked are the most-iterated.

Known gaps for the mode-depth work (2026-07-06): aircraft bank is computed then discarded (Flight
writes yaw-only); Hover turns-to-face rather than strafing; Flight loiter/idle is punted to the AI
controller; air avoidance is planar (no vertical channel) and Flight doesn't consume it;
`GetMinTurnRadius` is a producer with no consumer. The aspirational vehicle curve-fitting planner
(Reeds-Shepp/Dubins) referenced in stale comments is **unbuilt** — see root `CLAUDE.md`.
