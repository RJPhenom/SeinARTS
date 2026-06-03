# Movement Debugging — Session Handoff

**Created:** 2026-06-02 · **Build baseline:** SeinARTSEditor (UE 5.7) compiles clean (exit 0).
**Purpose:** Bootstrap a dedicated session to iron out **movement bugs across all four plugins** —
squad formation dispatch, cover snapping, the Movement+ modes, path planning, and steering.

> Movement is the most cross-cutting subsystem in SeinARTS. A single symptom ("unit walks to the
> wrong spot", "vehicle spins out", "squad clumps", "unit won't path") can originate in any of
> ~6 stages spread across 3 plugins. This doc maps that pipeline so you can localize fast.

---

## Read first (load context)

1. Project-root `CLAUDE.md` — determinism + sim/render rules, plugin topology.
2. `Plugins/SeinARTSFramework/CLAUDE.md` — **Movement**, **Pluggable subsystems (Navigation)**, and
   **Sim tick** sections.
3. `Plugins/SeinARTSMovementPlusExtension/CLAUDE.md` — the concrete modes.
4. `Plugins/SeinARTSSquadExtension/CLAUDE.md` — dispatch resolver + `PostProcessPositions` hook.
5. `Plugins/SeinARTSCoverExtension/CLAUDE.md` — cover-aware resolvers + formation preview.

**Determinism is the backdrop for every fix.** Movement runs inside the sim tick (AbilityExecution
phase) on fixed-point math. Do **not** introduce `float` / `FVector` / `FMath::` / `rand()` into
sim paths. `TimeAccumulator` being a `float` is a wall-clock scheduler, not a bug. Movement bugs
that differ between clients are determinism bugs — see State Hash + Replay below.

---

## The movement pipeline (end-to-end, cross-plugin)

Trace a move order through these stages. Each names the **module** and the **key types/files**.

```
[1] ORDER ISSUED            render → sim, one-way via command buffer
       └─ ASeinPlayerController::IssueSmartCommand(Ex)  (Framework gameplay)
          → USeinTargeterSubsystem (point/drag targeting; drag + point-facing are WIP)
          → EnqueueCommand → FSeinCommand (tag-typed; CommandType is a GameplayTag, payload = FInstancedStruct)

[2] COMMAND PROCESSED + DISPATCH RESOLVED   "who moves, and to what positions"
       └─ Command buffer drained in the sim tick; the command broker resolves fan-out:
          USeinDefaultCommandBrokerResolver            (CoreEntity — base)
            └ USeinSquadDispatchResolver               (Squad ext)
                 ResolveDispatch / ResolvePositions / ResolveFormationLayout
                 → PostProcessPositions  ← extension hook
                     └ USeinCoverAwareSquadDispatchResolver  (Cover ext: snap members to cover)
          USeinCoverAwareDefaultBrokerResolver         (Cover ext: snap for non-squad selections)
       Resolver chosen by config: per-squad FSeinSquadComponent.DispatchResolverClass →
       USeinARTSCoreSettings.DefaultBrokerResolverClass → framework default.

[3] PER-ENTITY MOVE STARTS
       └─ USeinMoveToAction (latent; Framework movement). Instantiates the USeinMovement subclass
          from FSeinMovementComponent.MovementClass (soft path; null → USeinBasicMovement).

[4] PATH PLANNED
       └─ USeinMovement::PlanPath → USeinNavigation(AStar)::FindPath  (Framework nav)
          = FindCellPath (grid A*) + LoS smoothing (BuildSmoothedPath) + corner rounding via
            GetMinTurnRadius. NOTE: BuildSmoothedPath SKIPS CellPath[0] (avoids a start "hook"),
            so Path.Waypoints[0] is NOT the agent's start position — see PathOriginAgentPos.

[5] PER-TICK ADVANCEMENT (steering)
       └─ USeinMoveToAction::TickAction → USeinMovement subclass Tick:
            Basic / BasicUnit                         (Framework movement)
            Infantry / Wheeled / Tracked / Hover / Flight  (Movement+ ext)
          Shared steering helpers on the USeinMovement base (Framework):
            ResolveLookAheadPoint (carrot, with cluster-skip thinning),
            AdvanceWaypointAlongPath (dot-product crossover advance),
            ComputeAdaptiveLookAhead, StepSpeedToward, KinematicArrivalSpeedCap,
            ResolveNavCollision / IsFootprintPassable, slope pitch/roll smoothing,
            ShouldAutoReverse / IsOvershootArrival.
          Repath while moving: RepathInterval / RepathFailureLimit (see gotcha below).

[6] ARRIVAL / FAILURE
       └─ Arrival zeroes Velocity; cancellation/preemption leaves Velocity for momentum carry-over.
          Failure → ESeinMoveFailureReason: PathNotFound, EntityDestroyed, NoMovementComponent,
            NoNavigation, Cancelled, Stranded (escape-nudge gave up).
          Escape-nudge: when A* can't expand from the chassis cell, override Path with a single
            waypoint up the WallDistance gradient; success → force repath, failure → Stranded.
          USeinMoveToProxy fires Completed / Failed / Waypoint / Cancelled / PartialPath to BP.
```

---

## Debug tooling (turn these on FIRST)

Console commands (all `[0|1|on|off]`, no-arg toggles):

| Command | Shows |
|---|---|
| `Sein.Nav.Show` | Nav grid cell viz **+ the active-move path overlay** (yellow remaining-path cells, blue destination cell, entity→target→waypoint lines). The single most useful movement command. |
| `Sein.Nav.Show.Layer <0-7>` | Filter nav viz to one nav layer. |
| `Sein.Show.Steering` | Per-unit steering viz: footprint ring + velocity arrow (every unit with a movement component, idle included) + per-tick carrot/path-tangent inside active moves. |
| `Sein.Show.Extents` | Entity collision shapes (box = yellow, capsule = cyan) — the footprint that drives nav collision + clearance. |
| `Sein.Commands.ShowLog` / `.Observer` / `Sein.Commands.ClearLog` | On-screen log of issued commands — confirm the move order actually reached the buffer and what it targeted. |
| `Sein.Sim.StateHash` / `Sein.Sim.StateHash.Log` | Per-tick sim state hash — divergence pinpoints the first desyncing tick (determinism bugs). |
| `Sein.FogOfWar.Show` (+ `.Player`, `.Layer`) | FoW viz. Relevant because **cover snapping is FoW-observer-gated** — a unit won't snap to cover it can't see, so FoW state changes cover dispatch. |

Repro / isolation:
- `Sein.Net.DumpSnapshot` / `Sein.Net.LoadSnapshot <file>` — capture/restore sim state. **Caveat:**
  ability/resolver-pool reconstruction is deferred, so **active MoveTo actions reset on restore**
  (a move in flight won't resume). Good for static-position repro, not mid-move.
- `Sein.Net.SaveReplay` / `Sein.Net.LoadReplay <file>` (Saved/Replays/) — deterministic replay;
  the gold-standard way to reproduce a movement bug bit-identically.

Code-side readouts:
- `USeinMovementBPFL::SeinGetMovementState` → Velocity / Speed / GroundSpeed / Direction /
  bIsMoving / bIsReversing / bIsAirborne / bArrivalImminent / bHasMovementInput. Drop on a HUD/AnimBP
  to watch a unit's live movement state.
- Per-mode `DEFINE_LOG_CATEGORY_STATIC` logs (Verbose): `LogSeinMove` (action), `LogSeinWheeled`,
  `LogSeinTracked`, `LogSeinHover`, `LogSeinFlight`, `LogSeinBasicUnit`, `LogSeinMovement` (infantry).

> **Viz gotcha:** the debug draw is camera-culled + per-frame entity-capped via
> `USeinARTSCoreSettings` "Debug Visualization" (`DebugDrawMaxDistance`, `DebugDrawMaxEntities`,
> `bDebugDrawFrustumCullEnabled`). If a far-off unit's viz is missing, it may be culled, not absent.

---

## Known WIP / bug-traps / red herrings

- **`ESeinRepathMode::OffPathOnly` is UNIMPLEMENTED** — it "currently behaves as no-repath until the
  off-path detector is wired up." A unit authored with OffPathOnly **will not repath at all**. If a
  unit ignores newly-placed obstacles / never recovers from drift, check its `RepathMode` first.
- **Reeds-Shepp / Dubins vehicle curve planner is UNBUILT** — `FitVehicleCurve` exists only in stale
  comments; path segments are `Straight`-only. Don't chase it. Vehicle turning = runtime bicycle
  pure-pursuit + nav corner-rounding via `GetMinTurnRadius`. (See root CLAUDE.md stale-comment list.)
- **Hover / Flight have a half-finished tuning migration** — some tunables still live as class-level
  UPROPERTYs while others moved to the sub-data struct. Watch for a value being read from the wrong
  place. Wheeled/Tracked are the most-iterated and cleanest.
- **Conservative bounding-circle footprint** — Box extents collapse to `max(HalfExtentX,Y)`, so long
  tanks refuse corridors narrower than their bounding circle even when they'd fit oriented. This is
  the `// TODO(PlannerAStar)` orientation-aware-pathing gap, not a bug.
- **Waypoint crossover** — `AdvanceWaypointAlongPath` uses a dot-product crossover test (not just
  close-radius) specifically to stop fast vehicles from overshooting a waypoint, generating a
  backward carrot, and spinning off-path. Regressions here look like vehicles pirouetting.
- **`Path.Waypoints[0]` ≠ start position** — `BuildSmoothedPath` skips `CellPath[0]`. Off-path drift
  math uses `PathOriginAgentPos` to compensate. Don't assume the polyline begins at the agent.
- **Footprint cascade is shared** — `Extents.Shapes (max BoundingRadius) → NavComp.FallbackFootprintRadius
  → 0`, used by BOTH planning clearance and runtime collision. If they disagree you get "planned a
  path the body can't follow" stuckness; the cascade exists to keep them identical
  (`USeinMovement::ResolveCollisionRadius`).
- **`WallPadding`** (FSeinNavigationComponent, cells) can make narrow corridors UNREACHABLE — a unit
  routing "the long way around" may be padding, not a planner bug.
- **Cover snap** uses `CoverSnapRadius` (default 500) + a cursor-side partition
  (`SeinCoverGeometry::PartitionSlotsByCursorSide`) + two-pass greedy-nearest onto members tagged
  `SeinARTS.Cover.UsesCover`; FoW-observer-gated. A vestigial "wrong-side penalty radius" appears in
  comments but no longer exists.
- **Squad formation** — `ResolveFormationLayout` computes facing (`ComputeFormationFacing`), applies
  a backward-walk slot mirror (`bInvertSlotOrderWhenMovingBackward`), then calls `PostProcessPositions`
  (where Cover snaps). If member offsets are all-identity/unresolved it falls back to a parent grid.
- **Targeter drag / point-facing are scaffolded** (point-target spec is complete). If drag-issued or
  facing-issued move orders misbehave, suspect the targeter, not the movement code.
- **Nav bake is synchronous**; dynamic blockers are stamped PreTick (`SeinNavBlockerStampSystem`).
  Stale-bake or unstamped-blocker symptoms point here, not at steering.

---

## Suggested triage workflow

1. Reproduce with `Sein.Nav.Show` **and** `Sein.Show.Steering` on. Watch the path overlay + carrot.
2. Localize to a stage:
   - **Wrong destination/positions?** → Stage 2 (dispatch/formation/cover resolver). Single-select a
     lone unit vs a squad to isolate squad-formation vs base dispatch; toggle cover relevance.
   - **Path looks wrong (routes oddly / not found)?** → Stage 4 (nav): check footprint, `WallPadding`,
     `NavLayerMask`, bake freshness, blockers.
   - **Path is right but the unit steers badly (spins, overshoots, won't arrive)?** → Stage 5 (the
     mode's `Tick` + base steering helpers). Check the specific mode; watch the carrot.
   - **Unit gives up / freezes?** → Stage 6: check `ESeinMoveFailureReason` (Stranded vs PathNotFound),
     escape-nudge, and `RepathMode` (OffPathOnly trap!).
3. Isolate scope: lone unit → squad → squad+cover. The bug usually appears at the layer that adds it.
4. Determinism check: does it repro identically every run? If it diverges, capture a replay and use
   `Sein.Sim.StateHash.Log` to find the first diverging tick.

---

## Key files by stage

| Stage | File(s) | Module |
|---|---|---|
| Order / targeting | `SeinPlayerController.cpp`, `SeinTargeterSubsystem.cpp` | Framework (gameplay) |
| Command + broker base | `Input/SeinCommand.h`, `Brokers/SeinDefaultCommandBrokerResolver.cpp` | SeinARTSCoreEntity |
| Squad dispatch/formation | `SeinSquadDispatchResolver.cpp`, `SeinSquadSystem.h` | SeinARTSSquad (ext) |
| Cover snap | `SeinCoverAwareDefaultBrokerResolver.cpp`, `SeinCoverAwareSquadDispatchResolver.cpp`, `Lib/SeinCoverGeometry.h`, `System/SeinCoverDefault.cpp` | SeinARTSCover/CoverSquad (ext) |
| Move action / repath / escape | `Actions/SeinMoveToAction.cpp` (+`.h`) | SeinARTSMovement |
| Path planning | `Movement/SeinMovement.cpp` (`PlanPath`), `SeinNavigationAStar.cpp` (`FindPath`/`FindCellPath`/`BuildSmoothedPath`) | SeinARTSMovement / SeinARTSNavigation |
| Steering base + defaults | `Movement/SeinMovement.cpp`, `SeinBasicMovement.cpp`, `SeinBasicUnitMovement.cpp` | SeinARTSMovement |
| Steering modes | `Movement/Sein{Infantry,WheeledVehicle,TrackedVehicle,Hover,Flight}Movement.cpp` | SeinARTSMovementPlus (ext) |
| Authoring knobs | `Components/SeinMovementComponent.h`, `Components/SeinNavigationComponent.h` | SeinARTSCoreEntity |
| Debug viz | `SeinARTSMovementModule.cpp` (`DrawActiveMoveDebug`, `DrawSteeringVectorsViz`), `Debug/SeinDebugDrawCull.cpp` | SeinARTSMovement |

---

## How to use this session

Absorb the pipeline above, then have the user describe the specific movement symptom(s). For each:
localize to a stage, reproduce with the relevant show flags, and fix at the layer that introduces the
behavior — respecting determinism (fixed-point, sim-tick-ordered). When a fix touches a docstring that
contradicts the code, update the docstring too (house rule). This doc is a starting map, not a
contract — delete or update it as the work proceeds.
