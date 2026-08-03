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

**Wheeled maneuver planning landed 2026-07-24** (see `Agents/FRAMEWORK_MAP.md` and live source):
`USeinWheeledVehicleMovement` overrides
`PlanPath` to post-process the coarse A* polyline into a Reeds-Shepp-style start maneuver
(U-turn arc at the largest feasible radius / straight reverse / 3-point turn / reverse-out of
corridors) emitted as typed Arc/Straight segments with per-segment `bReverse`, and its `Tick`
drives typed paths with a geometric segment cursor (curvature feed-forward on arcs with the
`v <= TurnRate·R` speed law, cusp brake-to-zero gates, anticipatory braking, stuck/orbit
recovery nudges), falling back to the classic carrot pursuit for the all-forward tail and for
`bManeuverPlanning = false` (the in-PIE A/B switch). The closed-form maneuver solver lives in
`Private/Movement/SeinWheeledManeuver.h/.cpp` (pure functions, plan-time only). Wheeled reverse
now defaults ON via `FSeinWheeledMovementData::bCanReverse` (OR-combined with the unit-level
flag). `GetMinTurnRadius` finally has its consumer — the wheeled planner itself.

**Tracked maneuver rework landed 2026-07-25** (build-green, red-teamed, PIE-pending):
`USeinTrackedVehicleMovement` got the same contract fixes as wheeled (harness
`AdvanceWaypointAlongPath`, `DispatchArrivalMotion` + roll-through arrival, avoidance SpeedScale
yield, authoritative-dest exemption, reverse-aware overshoot, far prechecks on the >463 m
fixed-point square wrap, `Super::OnMoveBegin`/`OnMoveEnd`) plus a tracked-flavored maneuver tier
via `PlanPath`: a segment-native straight-reverse word (replaces the whole-order auto-reverse
latch) and a momentum U-turn arc word (forward, at speed, R = speed/TurnRate clamped, driven at
7/8·TurnRate·R with a radial correction); an authored non-zero `MinTurnRadius` switches to the
FULL shared word ladder (the chassis declared itself non-pivoting). Cusps resolve through the
existing ARC/PIVOT mode split. Tracked reverse defaults ON via
`FSeinTrackedMovementData::bCanReverse` (OR-combined with the unit flag). The shared plan-time
toolkit still lives under `Private/Movement/SeinWheeledManeuver.h/.cpp` — rename to
`SeinVehicleManeuver` + driver-logic unification (tracked duplicates the segment-cursor helpers)
are deferred until the wheeled PIE pass lands. Move actions now finalize movement exactly once on
completion, cancellation, or failure. Both vehicle modes also destination-gate plan-time
hysteresis as defensive protection against stale state crossing an order boundary.

Known gaps for the mode-depth work (2026-07-06, minus the vehicle items closed above): aircraft
bank is computed then discarded (Flight writes yaw-only); Hover turns-to-face rather than
strafing; Flight loiter/idle is punted to the AI controller; air avoidance is planar (no vertical
channel) and Flight doesn't consume it.
