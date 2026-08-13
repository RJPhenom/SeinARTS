# Movement+ Vehicle Gym

**Evidence date:** 2026-08-13

**Scope:** deterministic wheeled/tracked movement qualification; automated source evidence plus a human PIE acceptance matrix.

## What ships today

Movement+ does not run a general Reeds-Shepp or Dubins search over the whole route. The navigation policy first produces a coarse, footprint-aware route. Wheeled and tracked `PlanPath` overrides may then prepend a bounded, deterministic start maneuver selected from a curated closed-form word set:

- a clearance-probed forward departure/U-turn arc;
- a short straight reverse;
- an alternating forward/reverse K-turn; or
- a reverse-out word that reaches a viable turning pocket.

The result is an `FSeinPath` containing typed `Straight` and `Arc` segments with per-segment reverse flags. The real vehicle driver follows that typed head, brakes to zero at direction-changing cusps, and then hands off to the ordinary coarse-route tail. Wheeled motion uses bicycle kinematics with an authored wheelbase and steering limit. Tracked motion can pivot at rest, use momentum arcs while moving, or opt into the full non-pivoting ladder by authoring a non-zero minimum turn radius.

This split is intentional. Navigation owns topology and unit-specific clearance; Movement+ owns chassis kinematics. The authoritative destination is never relocated. `bManeuverPlanning` remains the explicit legacy A/B switch.

## Automated evidence

The Vehicle Gym lives in the disabled, non-shipping `SeinARTSExtensionTestSuite`. Its navigation double is stateless from the simulation's perspective: a scenario installs an immutable recipe before the transient world starts, the complete recipe enters the static-environment digest, and diagnostic query counters never affect an answer. The tests run the production Movement+ planners, drivers, move action, collision/extents data, snapshots, and canonical-root machinery.

| Contract | Automated scenario |
|---|---|
| Representative archetypes | Wheeled scout, logistics truck, pivot-capable MBT, and authored-radius IFV/APC exercise their distinct planner contracts. |
| Open and confined turning | Open U-turn, short reverse-behind goal, exact K-turn, and narrow-corridor reverse-out require valid typed paths and the unmodified destination. |
| Bounded planning | Maneuver probes have an explicit query ceiling; flattened corridor paths must remain passable. |
| Driver execution | U-turn, reverse, S-bend, and tracked pivot/momentum cases complete within a fixed 45-second simulation bound; per-tick displacement stays bounded. |
| Order lifecycle | A real `USeinMoveToAction` survives interval repath, then cancellation and reissue without stale route or vehicle state. |
| Formation arrival | A real broker-owned move settles to the exact formation-slot facing. |
| Recovery | A blocked wheeled vehicle enters the production reverse-recovery state deterministically without tunneling through the navigation floor. |
| Serial/parallel equivalence | Separate transient serial and forced-parallel (minimum batch 1) recovery timelines produce the same canonical root at every tick. |
| Checkpoint continuation | Snapshots taken during a live arc, reverse leg, and recovery restore into a second world; canonical roots and latent-action lifecycles remain equal on every following tick. |
| Replay and reconnect | A live wheeled move continues from a real replay-file checkpoint and from the bounded reconnect-transfer envelope in fresh worlds; terminal root, transform, velocity, steering state, and latent actions match exactly. |
| Mixed traffic | Two vehicles and two infantry units with different dimensions, masses, speeds, and opposing destinations meet under collision/avoidance; a close-contact checkpoint restores and both worlds remain root-identical until all orders clear. |
| Presentation telemetry | Typed steering angle, yaw rate, normalized throttle/brake, wheel rotation, and left/right track velocity are bounded/reset correctly and do not change the canonical root. |

Run the focused gates from the project root:

```powershell
& "Plugins/SeinARTSTestSuite/RunTests.ps1" -Suite "SeinARTS.Integration.MovementPlus.VehicleGym" -Profile All
& "Plugins/SeinARTSTestSuite/RunTests.ps1" -Suite "SeinARTS.Determinism.MovementPlus.VehicleGym" -Profile All
& "Plugins/SeinARTSTestSuite/RunTests.ps1" -Suite "SeinARTS.Determinism.MovementPlus" -Profile All
```

## What automation does not prove

This suite proves exact state and bounded completion for its scenarios. The generated downstream
consumer separately proves a real packaged Shipping listen server/client movement, resync,
reconnect, and replay path. Neither proves designer-perceived motion quality, arbitrary-map
clearance, large-scale performance, dedicated-server transport, or adversarial network conditions.

- Snapshot continuation, replay-file checkpoint continuation, bounded reconnect transfer, and the
  packaged two-process listen-server path are covered directly for Movement+. Real multi-client PIE
  remains the feel, presentation, and configured-game oracle.
- Presentation exposes truthful settled ground speed/reverse state plus typed render-only steering
  angle, yaw rate, normalized throttle/brake, wheel rotation, and left/right track velocity through
  `USeinMovementPlusBPFL`. Animation Blueprint readability remains a PIE oracle.
- The mixed-traffic fixture is a correctness stress, not a scale benchmark.
- The planner reshapes the route head only. Whether ordinary downstream A* corners also need curvature shaping is a feel/performance decision that must be based on PIE evidence, not inferred from these tests.

## Required PIE acceptance matrix

Use the same authored vehicle Blueprint with `bManeuverPlanning` on and off. Restart the editor after reflected tuning changes; Live Coding is not sufficient for property-layout changes.

1. **Open ground:** issue 90-degree, 135-degree, and behind-goal orders at rest and at cruise speed. No orbiting, teleport, or destination drift.
2. **Reverse choice:** issue near-behind and far-behind orders. The near case should reverse decisively; the far case should choose a readable forward maneuver unless constrained.
3. **Confined space:** reverse out of a narrow gate and turn in a bounded pocket. No wall penetration, indefinite forward/reverse chatter, or repeated repath loop.
4. **Route tail:** drive an S-route and a route with several coarse A* corners. Record corner cutting, braking, overshoot, and total travel time with the A/B switch.
5. **Order changes:** cancel mid-arc, reissue to a different destination, spam stop/move, and destroy the destination/provider. The next order must start cold.
6. **Formation:** mix vehicle and infantry footprints, then test slot arrival/facing and a formation repath around a blocker.
7. **Congestion:** run representative counts through opposing and crossing flows. Capture `stat unit`, Insights/GameThread samples, simulation-behind warnings, and movement/collision counters.
8. **Snapshot/network/replay:** checkpoint during an arc, cusp, and recovery in the intended multiplayer topology; reconnect and replay through each point, comparing canonical roots.
9. **Presentation:** verify animation reads settled post-collision motion, reverse, steering/yaw,
   throttle/brake, wheel rotation, and differential track velocity correctly.

Do not broaden the production planner until this matrix identifies a concrete failure. If tail corners are the failing case, compare a bounded path-tail curvature stage against the existing head-only planner behind the current A/B seam. If the head maneuvers already feel wrong, tune or replace the curated word selection first; those are different problems.
