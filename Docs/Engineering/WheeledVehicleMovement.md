# Wheeled Vehicle Movement — Maneuver Planning + Segment Driving (Movement+)

**Status:** implemented 2026-07-24, build-green pending red-team + RJ PIE A/B.
**Scope:** `SeinARTSMovementPlusExtension` only — zero base-plugin changes (base seams verified
complete: `PlanPath`/`USeinPlannerHandle` typed-segment emit, `FSeinPathSegment::bReverse`,
`FlattenToWaypoints`, `GetMinTurnRadius`).
**Benchmark:** CoH/CoH2/CoH3 M8 Greyhound feel — full-speed U-turn arcs in open ground, braked
tight arcs in confined ground, multi-point turns against walls, reverse for close behind-goals,
smooth from-rest acceleration and arrival braking, no orbiting, no wall-stuck.

---

## What was built

Two halves, both inside `USeinWheeledVehicleMovement`:

1. **Maneuver planner** (`PlanPath` override + `SeinWheeledManeuver.h/.cpp` pure-function toolkit).
   Runs the base coarse A* (`Super::PlanPath` — budgeted, async-aware, BP-overridable) and then
   post-processes the START of the route into a Reeds-Shepp-style maneuver when the chassis is badly
   misaligned with the path (heading error above ~100°). Emits typed segments (Arc / Straight, each
   with `bReverse`) via the already-bound `CachedPlannerHandle` (`ClearPath` → `AddArcSegment`/
   `AddStraightSegment` → `FinalizeTypedPath`). The destination is never relocated (invariant #6);
   only the head of the route is reshaped.

2. **Segment-aware driver** (`Tick` rework). When the path has typed segments the driver follows the
   segment chain with its own geometric segment cursor (never the flattened waypoint backbone —
   which has no waypoint↔segment mapping and whose carrot-thinning corner-cuts flattened arcs):
   - **Arc segments:** curvature feed-forward steer (`δ = atan(±L/R)`) + heading/cross-track
     correction; per-arc speed law `v ≤ min(EffectiveTopSpeed, TurnRate·R)` — this is what makes
     open-ground arcs full-speed and tight arcs braked, resolving the `GetMinTurnRadius`-vs-
     `TurnRate` coupling (the plan only emits `R ≥ R_min`, the driver only drives `v ≤ TurnRate·R`).
   - **Reverse segments:** reverse pure-pursuit per segment (steer inverted, negative speed capped
     at `ReverseTopSpeed`), heading = travel direction not facing.
   - **Cusps** (adjacent segments with different `bReverse`): kinematic brake to ~0 at the cusp
     point, direction latch flips only below a small speed epsilon; during the brake-out the wheels
     PRE-STEER toward the next leg's lock (kept through the flip), so K-turn legs depart at lock
     instead of drifting wide while the steer slews from center.
   - **Anticipatory braking:** kinematic cap into the next segment's entry speed (0 at a cusp,
     `TurnRate·R` at an arc) — smooth approach braking everywhere, not just at the goal.
   - Once the cursor passes the last maneuver segment (all-forward-straight tail), the driver hands
     back to the existing pure-pursuit carrot loop — corner anticipation on the open route is
     unchanged.

Contract-compliance fixes rolled in (apply in both modes, small and unconditionally correct):
- Arrival now routes through `DispatchArrivalMotion` (was a hand-written `Velocity = 0`), and
  `ComputeArrivalMotion` is overridden to roll residual velocity through the arrival ring — the
  idle coast-down then finishes the stop through the same decel ramp (smooth arrival, no snap).
- The avoidance **SpeedScale yield** is now consumed on forward cruise (was silently ignored);
  the lateral steer bend stays forward-cruise-only (a maneuver leg yields by braking, not bending).
- `ResolveNavCollision` now receives the authoritative-destination exemption (cover-slot delivery
  parity with the harness).
- `IsOvershootArrival` input is now the **travel heading** (facing × drive sign), so a reversing
  approach no longer misreads as "heading away" and completes early.
- `Super::OnMoveBegin` is now called (BP_OnMoveBegin fires for BP subclasses; per-order tuning
  hydration runs for vehicles).

### Stuck / orbit protection
- **Stuck:** the driver measures ENTRY-to-ENTRY displacement across ticks (so PostTick collision
  pushes are included — entity/crowd pins register, not only nav-floor wall pins) vs commanded
  speed. Sustained ~0.5 s of commanded-but-not-moving enters a probe-gated reverse-nudge recovery,
  then the normal repath replans from the freed pose. Suppressed near the goal (the action's
  crowd-stall settle owns the endgame) and gated on `bManeuverPlanning` so the OFF setting stays a
  faithful legacy A/B control. (Vehicles are outside the action's hold-escape ladder by the
  commanded-velocity exemption — kept deliberately; the ladder's straight escape legs are
  undriveable for a min-turn-radius chassis.)
- **Traffic stall:** a maneuver leg held near zero speed ~1.2 s (avoidance yield against parked
  neighbours — invisible to the commanded-speed stuck gate) abandons the head into carrot pursuit,
  whose avoidance steer-bend routes around traffic naturally. The abandon threshold carries a
  per-entity deterministic jitter (handle % 8 × 0.1 s) so two mutually-yielding vehicles never
  abandon-and-replan on the same tick (mirror-lock symmetry break).
- **Orbit:** the planner is the orbit killer — a goal inside the effective turn circle produces a
  cusp plan instead of a circling pursuit, re-evaluated from the live pose at every repath
  (default Interval 0.25 s). A yaw-accumulator backstop (>2.5π swept without progress) also feeds
  the stuck recovery.
- **Replan hysteresis:** once a maneuver head is being driven, replans keep engaging down to a 45°
  heading error (cold engage ~100°), so an in-progress U-turn/K-turn re-plans its continuation
  instead of truncating into sharp-turn-braked pursuit mid-swoop; 45° hands off below the
  sharp-turn-brake band (60°) so the pursuit finish is smooth. Driver state resets in `OnMoveEnd`
  so a new order's initial plan (which runs before `OnMoveBegin`) never sees the stale flag.

---

## The candidate ladder (planner)

Engaged only when heading error to the path direction exceeds `PlanEngageAngle` (~100°); below
that, pure pursuit already arcs naturally without excessive braking. Candidates are validated by
footprint probes (`IsWorldPositionClear` static+dynamic, center + 4 ring samples at the shared
collision-cascade radius, fixed compile-time sample spacing) and scored by drive length (reverse
legs weighted by TopSpeed/ReverseTopSpeed); **forward-only wins ties by `PathLengthBias`** (forward
preferred unless more than ~35% longer — the pic-4 "too-long forward-first loses to 3-point" rule).

| # | Candidate | When | Emit |
|---|---|---|---|
| A | **U-turn arc** (full speed preferred) | Space fits an arc at `R_cruise = max(R_min, TopSpeed/TurnRate)`; shrinks stepwise to `R_min` (driver brakes to match) | `Arc` + tangent `Straight` + tail |
| B | **Straight reverse** | Goal behind + within `ReverseEngageDistanceThreshold` + near-straight path + can-reverse | All-reverse straights |
| C | **3-point turn** | U-turn blocked at all radii; forward and reverse arc legs probe-limited sweeps at `R_min` | `Arc`(fwd) + `Arc`(rev) + join + tail |
| D | **Reverse-out-then-forward** | Corridor too tight to turn at all (pic 1/4) | Reverse straights along the path to the first probed turnaround point, cusp, U-turn arc, tail |
| — | **Legacy fallback** | Nothing feasible / can-reverse off | Coarse path unchanged (helping-hand pivot survives as the escape hatch) |

Replan stability: the planner reads the current drive direction from hashed state
(`Velocity·Forward`) and prefers continuing an in-progress reverse leg, so 0.25 s interval replans
mid-maneuver produce the plan's suffix instead of thrashing. `RepathMode = OffPathOnly` is the
recommended authored setting for vehicles (drift-gated; a well-tracking chassis stays ~5 cm off the
flattened backbone) but Interval-default is fully supported.

---

## Decisions (RJ-visible forks — pick-up points if the posture should change)

1. **Analytic RS word subset at plan time, not a baked table.** The prior direction said
   "Reeds-Shepp baked offline, never runtime" — that ban targeted an iterative full-RS-family
   solver in fixed point (division-heavy). What shipped is a curated 4-word subset solved
   **closed-form** (one `Asin`/`Atan2` tangent solve per candidate, plan-time only, bounded probe
   counts) — cost is noise next to the A* call it post-processes. If full RS optimality is ever
   wanted, the upgrade path is a committed generated C++ LUT keyed on pose normalized to
   `R_min = 1` (SIN_TABLE precedent; no editor module needed).
2. **Reverse defaults ON for wheeled** via `FSeinWheeledMovementData::bCanReverse = true`
   (effective gate = sub-data flag OR the unit-level `FSeinMovementComponent::bCanReverse`). The
   unit-level flag stays authoritative for the base helpers and explicit reverse abilities; the
   sub-data default delivers "wheeled vehicles reverse out of the box" without touching the base
   component default. Untick the sub-data field to opt a unit out.
3. **`bManeuverPlanning` master toggle (default ON)** on the sub-data — OFF reproduces the legacy
   pure-pursuit planner/driver (modulo the contract fixes above), giving a clean in-PIE A/B.
4. **Position-only planning (no goal heading).** The order pipeline carries no facing; arrival
   facing remains the idle settle-facing pivot. Pose-to-pose RS + a "reverse into the slot facing"
   polish is deferred (needs sanctioned goal-heading plumbing — a base fork RJ owns).
5. **Commanded-velocity persistence kept** (status quo) — keeps vehicles out of the hold-escape
   ladder; stuck handling is the mode's own (see above).

## Deferred (recorded, not gold-plated)

- Interior-corner arc fitting (mid-path corners still pure-pursuit; feel is acceptable, revisit
  with the curve-planner tier).
- Pose-to-pose RS / arrival-facing maneuvers (needs goal-heading plumbing — base fork).
- Orientation-aware (facing-per-node) corridor A* for long chassis — future nav subclass, not here.
- Baked full-RS LUT upgrade (see decision 1).
- `SettlesToSlotFacing` posture for wheeled (parked car pivoting in place while idle — feel call).
- Tracked-mode contract fixes (hand-rolled waypoint advance, ignored SpeedScale, arrival bypass) —
  same fixes as rolled into wheeled here; separate task.

## Red-team outcomes (2026-07-24, five adversarial refuters)

All 20 findings adjudicated; the fixes folded in: probe sampling honesty (96-sample cap +
subdivided K-turn swing slices), arc completion by angular progress only (the 50 cm proximity
shortcut could chain-skip a min swing leg on tight chassis), the reverse-out start stub probed
before any pocket is accepted, chain-exact micro-leg merging (terminal waypoint never dropped),
wrap-safe distance math (32.32 squares wrap past ~463 m — far prechecks on arrival/brake/planner
distances + a 100 m cruise-radius cap), entry-to-entry stuck detection, near-goal recovery
suppression, engage hysteresis, recovery-vs-planned-reverse disambiguation, legacy steer-smoothing
order restored, and BP-authored typed paths never clobbered.

**Two pre-existing BASE issues surfaced (not fixed here — base is out of this work's scope):**
1. **Interval/OffPathOnly repaths are starved under async pathfinding in busy scenes** — a repath's
   `Throttled` waits a full interval before re-asking, but unconsumed async results are dropped at
   the NEXT tick's drain, so the queued result is never collected while any other unit requests
   paths (initial plans are immune — they retry every tick). Consequence: repath-dependent behavior
   (this feature's live-pose replans, world-change truncation) degrades to
   "drive the committed plan" in busy scenes. Suggested base fix: retry a Throttled REPATH every
   tick (mirroring the initial-plan cadence), or keep async results until consumed/superseded.
2. **The base arrival/vicinity tests square unbounded distances** (`SizeSquared` vs acceptance in
   the Tier-1 harness and `IsOvershootArrival`) — wraps negative past ~463 m and can false-arrive
   long orders. The wheeled driver now prechecks; the harness has the same latent pattern.

## Determinism posture

All new tunables live on the hashed `FSeinWheeledMovementData` (GetTypeHash updated); no settings
were added, so no config-fingerprint work. All planner/driver constants (probe spacing, engage
angle, cusp epsilon, sweep steps) are compile-time. Plan-time math is fixed-point LUT trig only;
probes read nav state that is stable within the tick (PreTick-stamped dynamic blockers). Instance
driver state (segment cursor, drive latch, stuck accumulators) is unhashed-but-derivable, same
class as the pre-existing `CurrentSteer` (known snapshot-restore caveat, unchanged). A/B gates:
`Sein.Sim.Parallel` 0-vs-1 StateHash, plus peer/replay for tick-shift sensitivity.
