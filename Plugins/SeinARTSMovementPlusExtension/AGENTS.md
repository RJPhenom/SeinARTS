# SeinARTSMovementPlusExtension — Plugin Guide

This opt-in runtime extension supplies concrete Infantry, Wheeled, Tracked, Hover, and Flight
movement modes plus their per-class tuning data. Read the project-root guide and
`Agents/WORKFLOW.md` first, then the Framework guide for movement/navigation infrastructure.

The framework ships abstract movement plus Basic/Basic Unit defaults and must remain fully usable
without Movement+.

## Dependency seam

- Runtime module: `SeinARTSMovementPlus`.
- Dependency direction is extension to framework only.
- `FSeinMovementComponent::MovementClass` is a soft class path resolved when
  `USeinMovementSubsystem` creates the persistent per-entity movement instance.
- Invalid or missing extension classes fall back to the framework Basic implementation.
- `MovementClassData` is an `FInstancedStruct`; per-mode data does not create a core dependency.
- The editor picker filters on the framework base class, so extension subclasses remain discoverable.

## Modes

| Mode | Class | Data |
|---|---|---|
| Infantry | `USeinInfantryMovement` | `FSeinInfantryMovementData` |
| Wheeled | `USeinWheeledVehicleMovement` | `FSeinWheeledMovementData` |
| Tracked | `USeinTrackedVehicleMovement` | `FSeinTrackedMovementData` |
| Hover | `USeinHoverMovement` | `FSeinHoverMovementData` |
| Flight | `USeinFlightMovement` | `FSeinFlyingMovementData` |

The Flight/Flying name skew is serialized API. Do not rename it without redirects and migration.
Mode data structs are `SeinSubData` and belong only in the polymorphic movement-data picker.

Infantry is the lightweight policy exemplar over the shared movement harness. Vehicle modes may
own a fuller tick policy but should reuse shared fixed-point steering and collision helpers rather
than copy them.

## Redirects

`Config/DefaultEngine.ini` contains class and struct redirects from the pre-split
`/Script/SeinARTSMovement.*` paths. They preserve Blueprint soft paths and instanced data. Do not
remove or rewrite them casually; every future re-home/rename needs matching redirects and an asset
load validation.

## Path planning contract

Navigation produces topology/reachability. Per-unit kinematic shaping belongs to
`USeinMovement::PlanPath` through the planner/mover handles and typed `FSeinPath` segments.

- `Straight`, `Field`, `AbstractEdge`, `Arc`, and `Jump` are typed contracts.
- The shipped A* emits a straight coarse route. Wheeled/Tracked may prepend a bounded,
  clearance-probed Reeds-Shepp-style start maneuver (departure arc, reverse, K-turn/reverse-out)
  through typed `Arc`/`Straight` segments, then use runtime steering for the remaining route.
- The start-maneuver toolkit is a curated deterministic candidate set, not a general
  Reeds-Shepp/Dubins family solver or full-route curvature smoother.
- Do not broaden or replace that planner without the Vehicle Gym evidence and RJ's feel decision.
- A path producer must validate segment continuity, endpoints, direction, and drivable expansion.

## Known mode-depth areas

Re-ground against live code before changing feel. Current areas requiring deliberate work include:

- Wheeled/tracked steering, reversal, and corner behavior.
- Hover strafe semantics.
- Flight idle/loiter, bank application, and three-dimensional avoidance.
- Consumption/validation of typed non-straight segments.

These are product-feel decisions as well as correctness work. Present a concrete A/B and obtain RJ's
direction before changing the shipped default behavior.

## Determinism and state

- Movement instances persist across orders; `OnMoveBegin` is the per-order reset boundary.
- All authoritative kinematics and tuning use fixed-point types.
- Runtime state that can affect future motion participates in canonical hash/capture/restore/reset.
- Never key state by entity index without generation.
- Parallel computation may read immutable snapshots but commits in a canonical deterministic order.
- Contained entities are posed by containment and must not be independently moved.

## Verification

Movement+ changes require:

- Mode-specific acceleration, braking, turn-rate, reversal, and arrival bounds.
- No blocked-cell penetration or invalid typed-segment transitions.
- Same-cell and failed-path behavior.
- Serial/parallel and replay state agreement.
- Asset redirect/soft-path loading with Movement+ present and clean fallback when absent.
- Long-run idle and order-transition state tests.
- Scripted PIE tactics-gym A/Bs for wheeled cornering, tracked pivoting, hover strafe, flight
  banking/loiter, crowd behavior, and animation readability.

Automation proves safety, bounds, lifecycle, and determinism. RJ's PIE review remains the oracle
for motion feel.
