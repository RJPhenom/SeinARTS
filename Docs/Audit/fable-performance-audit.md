# SeinARTS Performance Audit — fable-performance-audit

**Date:** 2026-08-02 · **Branch:** `fable-performance-audit` (from `219f586`) · **Auditor:** Fable
**Symptom:** low single-digit FPS in 2-player listen-server PIE, ~100 mannequin units, near-empty
level, on a Ryzen 9800X3D + Radeon 9070 XT + 64 GB.
**Method:** five parallel adversarial code-audit agents (sim spine / movement–avoidance–collision–nav /
FoW–LevelData–UI / render bridge + GPU config / net–replay–gossip), all findings grounded in live
code with file:line. **These are mechanism-derived estimates, not profiler measurements** — the
first PIE session should confirm with `stat unit` + Unreal Insights (see §7).

---

## 1. Executive summary

> **MEASURED (Session A baseline, 2026-08-02): Frame=185.9 ms, Game=177.7 ms, GPU=23.2 ms,
> Draw≈0, RHIT≈0.** The frame is **~95% game-thread bound**. This DEMOTES claim B (GPU config)
> from "sufficient by itself" to a secondary ~23 ms cost — the CPU findings (C, D, E + the
> amplifier A) own the frame. Session B (GPU config) is now optional; the code fixes are the
> campaign. Also observed at baseline: a turn-50 session kill from the nav capture validator's
> false terminal-waypoint invariant — fixed on this branch (see §9).

There is **no single villain**. The collapse is **four stacked "huge" costs plus one amplifier that
locks them in**:

| | Finding | Class |
|---|---|---|
| A | **The catch-up amplifier**: `MaxTicksPerFrame=5` × 2 worlds → every rendered frame runs **10 full sim ticks**, forever, once frame time exceeds 33 ms. Every per-tick cost below is billed ~10× per frame. Self-sustaining floor, not a spiral — it never recovers. | Amplifier |
| B | **GPU config**: hardware ray tracing (`r.RayTracing=True`) with **200 animated skeletal meshes** (each needs its own BLAS rebuilt every frame → the logged 3.245 GiB vs 400 MiB budget), + VSM invalidated by 200 animated casters, + Lumen, + Maximum scalability, + VRAM 884 MB overcommitted (every pass slows 5–20×). **Sufficient for single-digit FPS by itself.** | HUGE (GPU) |
| C | **FoW dev-build trap**: the debug-viz component rebuilds a **full-grid scene proxy (250k–1M cells) on every fog mutation, even with the showflag OFF**, in both worlds, ~10 Hz. Est. 150 ms–1.5 s CPU per wall-second. Invisible in Shipping. **Also sufficient by itself in Dev PIE.** | HUGE (CPU, dev-only) |
| D | **FoW compute + render**: O(R³) vision stamps for every moving unit at a **100 cm cell default that is 4× finer than the class's 400 cm design point** (→ 64× the LOS work), plus a full-res CPU fog texture + default-on CPU blur rebuilt on every mutation (ignores its own `FogRenderTickRate` setting). | HUGE |
| E | **Canonical-root gossip**: a full reflected BLAKE3 digest of the entire world — which re-plans reflection per struct *instance*, does a **complete second fog-of-war solve** as a verify step, and calls `TryLoadClass` per entity — synchronously on the game thread, 1×/sec **per world**. Est. 40–250 ms *each*. A 1 Hz sawtooth eating up to ~8–50% of wall-clock. | HUGE (hitch→duty) |

Below those: A\* repath pressure + per-batch scratch churn (PERF-02 confirmed, ~30 MB alloc/batch),
per-tick frozen-contract watchdogs rebuilding multi-KB reflection strings **every tick**, UI/minimap
riding every sim tick (×10 under the amplifier), and a tail of small wins.

**Cleared of suspicion** (audited, healthy): collision broadphase/narrowphase structure (spatial-hash
correct, no O(N²)), avoidance neighbor gathering (hash-scoped), visual-event drain (single queue,
once per render frame), HUD/marquee, LevelData grid queries (flat-array O(1)), per-turn net
aggregation/fan-out (µs-level), legacy StateHash (CVar-gated, off by default — **but verify
`Sein.Sim.StateHash.Log` is 0**; left on from an A/B it alone produces this symptom).

**Realistic expectation:** Phase 0 (config only, ~15 min) should already move PIE from single digits
into the teens-to-twenties. Phase 1 (risk-free code wave) should put this scene comfortably past 30
and restore headroom. Phase 2 (sim-visible wave, full A/B ceremony) buys the scaling for real unit
counts on real maps.

---

## 2. The amplifier (fix first — everything else becomes measurable)

`SeinWorldSubsystem.cpp:2289–2437`: the accumulator pump runs up to `MaxTicksPerFrame` (default 5,
`PluginSettings.h:126`) sim ticks per rendered frame per world. Once sustained frame time exceeds
33 ms the accumulator refills faster than it drains (clamped to one tick's backlog at :2436, refilled
past 5×FixedDelta by the next 100–300 ms frame delta) → **stable state = 10 sim ticks per rendered
frame across both worlds**. The `Simulation falling behind … (continuous)` warning at :2430 is the
tell — grep the PIE log.

Consequences: every per-tick cost in this report is charged ~10× per frame; everything hung off
`OnSimTickCompleted` (bridge snapshots, all viewmodels, minimap, gossip checks) also runs 10×; the
frame can never get cheaper than 10× the per-tick cost.

**Fixes (zero determinism risk — `TimeAccumulator` is a wall-clock scheduler by design, pacing never
changes state at tick N; the lockstep turn gate already stalls the pump safely):**
1. **Time-budget the pump**: break the while-loop when wall-clock spent this frame exceeds ~15–20 ms
   regardless of `TicksProcessed` (dropped ticks are already legal — the clamp does exactly that).
2. `MaxTicksPerFrame=2` as an immediate PIE mitigation (`DefaultGame.ini`).
3. Optional: in single-process multi-world PIE, alternate which world catches up per frame.

---

## 3. Phase 0 — config/content changes, no code (do these first, ~15 minutes)

| # | Change | Where | Expected effect |
|---|---|---|---|
| 0.1 | `r.RayTracing=False` (restart editor) — Lumen falls back to software (`r.GenerateMeshDistanceFields=True` already on). Alternative if RT is wanted later: `r.RayTracing.Geometry.SkeletalMeshes=0` + `r.Lumen.HardwareRayTracing=0`. For a top-down RTS also consider GI Method = None + reflections ≠ Lumen. | `Config/DefaultEngine.ini:19,21,13,15` | **Biggest single win.** Kills the 3.2 GiB BLAS pool + VRAM overcommit. |
| 0.2 | `r.Shadow.Virtual.Enable=0` (CSM fallback) and/or turn off `CastShadow` on the unit mesh, use blob shadows. | `DefaultEngine.ini:26` + unit BP | HUGE (GPU). 200 animated casters invalidate VSM pages every frame. |
| 0.3 | Unit skelmesh: `bEnableUpdateRateOptimizations=true`, `VisibilityBasedAnimTickOption=OnlyTickPoseWhenRendered`; consider the AnimationBudgetAllocator plugin. | unit BP (content) | BIG (5–15 ms of anim workers). |
| 0.4 | `VisionCellSize` 100 → **400** (the class design point). | Project Settings → SeinARTS (`PluginSettings.cpp:85`) | Cuts FoW LOS work **64×** (R³) and every fog grid consumer (debug proxy, overlay texture, minimap sampling) **16×**. Synchronized config — fingerprint-covered, determinism-safe. |
| 0.5 | `MaxTicksPerFrame` 5 → **2**. | `Config/DefaultGame.ini:12` | Halves the amplifier while the real fix lands. |
| 0.6 | `DeterminismCheckIntervalTurns` 10 → **30**. | settings (`PluginSettings.cpp:101`) | Gossip capture 1×/s → 1×/3s per world; detection latency 3 s, zero sim impact. |
| 0.7 | Scalability Medium while profiling; shrink/hide the second PIE window; or untick **Run Under One Process** (closer to real multiplayer, halves the process load). | Editor prefs / PIE settings | BIG. |
| 0.8 | Verify `Sein.Sim.StateHash.Log` is **0** in the repro session. | console | If it was on: mystery solved twice over. |

---

## 4. Phase 1 — code wave with ZERO sim/determinism exposure (render/dev/infra only)

Ranked by expected win. None of these change sim state or its timing.

1. **FoW debug proxy showflag gate** — `SeinFogOfWarDebugComponent.cpp:519–575, 385–494`: early-out
   `CreateSceneProxy` (return nullptr) when the FogOfWar showflag is off (helper exists at :66–75);
   recreate on toggle via cvar sink. Kills finding C outright. *(HUGE, dev builds)*
2. **Fog overlay rebuild discipline** — `SeinFogOfWarRender.cpp:391–394, 251–375`: coalesce
   mutations into a dirty flag consumed at `FogRenderTickRate` (the setting exists and is ignored);
   cap texture dimension (~512) and sample down; drop the CPU blur (bilinear + material smoothstep).
   *(HUGE→BIG)*
3. **Frozen-contract watchdogs: validate on change signals, not per tick** —
   `SeinWorldSubsystem.cpp:2294–2295, 2442, 2461`; `PluginSettings.cpp:318–387`;
   `SeinCanonicalStateRegistry.cpp:837–902`. Cache the fingerprint int + binding frames; invalidate
   from settings-change events / provider registration / generation counters (nav already has
   `FrozenStaticEnvironmentGeneration`). Worst case: breach detection latency grows one turn.
   *(BIG under amplifier: 1–4 ms/frame + allocator pressure)*
4. **Decouple UI from sim ticks** — `SeinUISubsystem.cpp:170–209`: refresh viewmodels/minimap once
   per **rendered frame** (GFrameCounter guard) or 10 Hz wall-clock, not per sim tick. Rendering
   can't display more than one state per frame; under catch-up this work runs 10×. *(BIG)*
5. **Canonical-root capture cost** *(evidence-path only, digest bytes unchanged)* —
   (a) cache a per-`UScriptStruct` **digest plan** (sorted props + pre-encoded name/path bytes) in
   `FSeinCanonicalReflectedStateDigest` (`SeinCanonicalReflectedStateDigest.cpp:1117–1186`) — today
   it re-gathers + re-sorts (comparator allocates strings!) per struct *instance*: 5–10× win;
   (b) build failure-context strings lazily (`SeinWorldStateRoot.cpp:655–661`);
   (c) **gate the FoW rebuild-and-verify** (`SeinFogOfWarDefaultStateCodec.cpp:365–801` — a full
   second vision solve + per-cell compare per capture!) to checkpoint captures / dev only — it
   verifies reconstructibility, it doesn't feed the digest;
   (d) cache the movement contributor's `TryLoadClass` per instance
   (`SeinMovementCanonicalStateProvider.cpp:440`). *(HUGE for the 1 Hz sawtooth)*
6. **A\* worker-scratch pool (PERF-02)** — `SeinNavigationAStar.cpp:2852–2856`: persistent
   per-worker scratch on the nav object (gen-tag pattern already supports reuse — `MainScratch`
   proves it); replace `bOverlayReuseValid` with a monotonic blocker-epoch counter compared per
   scratch (red-team the epoch: a stale overlay would be a real desync). ~30 MB alloc/batch → ~0.
   *(BIG)*
7. **Movement instance map-first lookup** — `SeinMovementSubsystem.cpp:294–312`: check
   `MovementInstanceMap` before `TryLoadClass`; 6,000 soft-path resolves/sec → ~0. Ten lines. *(MED)*
8. **Bridge caching** — `SeinActorBridgeSubsystem.cpp:188–210, 247`: store the
   `USeinEntityComponent*` in `EntityActorMap` (kills 1,000+ `FindComponentByClass` walks/frame);
   shift transform snapshots once per rendered frame (latest completed tick), not per sim tick.
   Optional structural: disable `PrimaryActorTick` on `ASeinActor` (`SeinActor.cpp:20` — native tick
   is empty) and drive interpolation from one tight subsystem loop; watch BP EventTick users. *(MED)*
9. **Minimap fog sampling** — `SeinMinimapViewModel.cpp:205–307`: hoist observer group + grid
   geometry once, step cell indices incrementally (affine map — zero per-texel fixed divides),
   persistent pixel buffer, `UpdateTextureRegions` instead of full `UpdateResource`; split blips
   (30 Hz → 10 Hz) from the fog interval. ~10× cheaper. *(BIG under amplifier)*
10. **Formation preview cursor gate** — `SeinPlayerController.cpp:135–140`: broadcast on cursor
    moved ≥ ~5 cm or selection/gesture change (memoized inputs, same resolver — preview===commit
    preserved exactly; A/B it per house discipline). Confirmed NOT running A\* (ring-scan projection
    only) — spikes to several ms on unreachable clicks. *(SMALL steady / BIG spikes)*
11. **Replay IO** — `SeinReplayFileIO.cpp:179–237`: persistent `IFileHandle` (no reopen/verify per
    append); move append+flush to a sequenced background thread; `bFullFlush` only for
    checkpoints/finalize. 2 Hz game-thread fsync hitches → 0. *(hitch-class)*
12. **Per-tick allocation tail** — persistent scratch in `SeinCommandBrokerSystem.h:547–549`,
    `SeinLatentActionManager.cpp:69`, `SeinEffectTickSystem.h:45,62`, avoidance kernel locals
    (`SeinAvoidanceDefault.cpp:328,592` → `TInlineAllocator`/task contexts), AI-controller sort
    (`SeinWorldSubsystem.cpp:12309`), broadphase inner arrays + hoisted storage
    (`SeinCollisionBroadphaseSystem.h:81,133`), FoW gen-scratch reuse. *(SMALL each, real in sum)*

---

## 5. Phase 2 — sim-visible optimizations (full A/B ceremony: `Sein.Sim.Parallel` 0-vs-1 StateHash, peer/replay, red-team)

1. **A\* repath pressure** (`SeinNavigationComponent.h:122,130`; `SeinNavigationAStar.h:232`):
   (a) split the repath iteration cap from the initial-path cap (~1–2k nodes; keep old path on
   partial — interval self-heals); (b) `OffPathOnly` as shipped default or event-driven repath off
   `OnNavigationMutated` region overlap; (c) **island-ID reachability precheck** at bake (O(1)
   unreachable reject — kills the 10k-node partial-search class); (d) later: `GroupId` shared
   route per ordered group. *(HUGE at scale; the crowd-arrival worst case is routine RTS load)*
2. **FoW stamp algorithm** (`SeinFogOfWarDefault.cpp:552–940`): recursive shadowcasting (O(R²),
   already on the header's roadmap) + recompute-on-cell-cross pose quantization. Changes *which
   cells are visible when* → sim-visible via the LOS gate; lockstep-safe if identical everywhere,
   but full ceremony. *(HUGE at 100 cm; still BIG at 400 cm)*
3. **FoW blocker invalidation scoping (PERF-04)** (`SeinFogOfWarDefault.cpp:593–612`): invalidate
   only sources whose stamp bounding circle intersects dirty cells (deterministic superset test in
   fixed point). One moving vision-blocker currently makes *every* source pay O(R³) at 10 Hz. *(BIG)*
4. **Collision resolution trims** (`SeinCollisionResolverDefault.cpp:38–46,122`): early-exit
   remaining Gauss-Seidel passes on a zero-push pass (pure function of state — bit-exact); rebuild
   self shapes only when moved; fuse overlap-detect with pass 4. *(SMALL-MED)*
5. **Idle settle early-out** (`SeinMovement.cpp:838–1043`): skip snap/slope/facing for converged
   idle units (velocity zero, no dodge, pose unchanged, smoothing ramps inside convergence band —
   the code's own comment flags this); cache "no BP override of `BP_TickIdle`" and call
   `_Implementation` directly. *(MED: ~5–10 ms/frame under amplifier)*
6. **Canonical-root architecture** (after Phase 1 §5): capture raw leaves synchronously, hash
   asynchronously, report a turn later (protocol tolerates late reports — 256-turn window); later,
   per-storage Merkle/dirty-tracking so unchanged storages reuse digests. *(removes the sawtooth
   entirely)*
7. **Component storage dense-index façade** (`SeinWorldSubsystem.h:2308–2334`): replace per-call
   TMap finds with per-type dense indices (`TArray<ISeinComponentStorage*>`); tactically, systems
   hoist the storage pointer per tick and iterate the *rarest* storage instead of all entities
   (broker idle path does 5 lookups/idle entity/tick today). 20–60k TMap finds/frame → ~0. *(MED)*

---

## 6. Landmines & follow-ups surfaced in passing

- **Synthetic nav-blocker fallback** (`SeinNavBlockerStampSystem.h:118–158`): an entity with **no**
  `FSeinExtentsComponent` but `FallbackFootprintRadius > 0` becomes a nav blocker **unconditionally**
  (no `bBlocksNav` opt-in on that path). If any mannequin lacks Extents: blocker list churns every
  tick → overlay re-stamp per path request + units block each other's nav. **Verify the unit BP; then
  gate the fallback on explicit opt-in.**
- **Broadphase dev-warning `static TSet`** (`SeinCollisionBroadphaseSystem.h:83–137`) is shared
  across PIE worlds and never cleared — suppresses warnings cross-world (cosmetic).
- **World-widget pool contract** (`SeinWorldWidgetPool.h:24–28`) documents BP-driven
  `ReleaseAll()`+`Acquire()` per tick per visible entity — if the banner overlay does this with 100
  units in both windows, that's a Slate invalidation storm. Content-side; check the widget BP.
- `USeinFogOfWarVisibilitySubsystem` polls every frame (default `PollInterval` 0) with a volumetric
  query per entity — fine at ~0.2 ms, but a 0.1 s default is free savings.
- Fork-join density: ~5 parallel dispatches/tick/world × amplifier on ~100-element batches; consider
  raising `Sein.Sim.ParallelMinBatch` for the small passes after the big wins land (A/B first).
- CLAUDE.md stale docs: built-in system list still names StateHash (no such registered system);
  "movement.trace" is an observation-only, log-gated jam classifier, not a tracer.

## 7. Diagnostic protocol (RJ) — three sessions, ordered, with report template

### Session A — baseline (change nothing)
1. Launch the usual 2-player listen-server PIE, get into the match, issue a group move order so
   units are moving (the normal repro).
2. Click into the HOST window, open the console (`~`), type: `stat unit`
3. Wait ~10 s, then write down these five numbers from the overlay: **Frame, Game, Draw, GPU, RHIT**.
4. In the same console, type: `Sein.Sim.StateHash.Log` and press Enter — note the value it prints.
5. Play ~60 more seconds, then stop PIE.
6. In the editor Output Log, search for the phrase `falling behind` — note whether any line contains
   `(continuous)`.

**Report for A:** the five stat numbers, the cvar value, and yes/no on `(continuous)`.

### Session B — GPU config only (isolates the GPU stack)
1. Edit `Config/DefaultEngine.ini`:
   - line 19: `r.RayTracing=True` → `r.RayTracing=False`
   - line 26: `r.Shadow.Virtual.Enable=1` → `r.Shadow.Virtual.Enable=0`
2. Restart the editor (required for the RT flag).
3. Editor toolbar → Settings → Engine Scalability Settings → **Medium**.
4. Repeat Session A steps 1–3 exactly.

**Report for B:** the five stat numbers.

### Session C — sim/FoW config on top (isolates the CPU stack)
1. Project Settings → Plugins → SeinARTS:
   - `Vision Cell Size`: 100 → **400**
   - `Max Ticks Per Frame`: 5 → **2**
   - `Determinism Check Interval Turns`: 10 → **30**
2. Open the Sandbox map, select the SeinLevelVolume, click **Bake Level Data** (the fog grid
   resolution changed), save the level.
3. Repeat Session A steps 1–3 and 5–6.

**Report for C:** the five stat numbers + yes/no on `(continuous)`.

### Report template (fill in nine numbers + three flags)
```
A: Frame=__ms Game=__ms Draw=__ms GPU=__ms RHIT=__ms | StateHash.Log=__ | continuous: y/n
B: Frame=__ms Game=__ms Draw=__ms GPU=__ms RHIT=__ms
C: Frame=__ms Game=__ms Draw=__ms GPU=__ms RHIT=__ms | continuous: y/n
```

How the numbers convict: A's Game-vs-GPU split says which side dominates today. B−A is the GPU
stack's share (claims B in §1). C−B is the FoW/amplifier/gossip share (claims A, D, partial E). If C
still shows Game > 33 ms, the remaining cost is the Phase 1 code targets — and an Unreal Insights
capture (~10 s of session C) becomes the next step; I'll read the trace.

Keep the A/B StateHash gate green for anything in Phase 2 (§5).

## 8. Verification obligations (house rules)

Phase 0 and Phase 1 items are config/render/evidence-path only — no sim-state exposure — but items
touching evidence encoding (digest plan cache, FoW capture gating) must produce **byte-identical
digest streams** (assert with existing canonical-state suites + a before/after root compare on a
fixture world). Phase 2 items are sim-visible: each needs the full loop — red-team, `Sein.Sim.Parallel`
0-vs-1 StateHash A/B, peer + replay agreement, and RJ's PIE as the final oracle.

## 9. Blocker fixed en route: nav capture validator killed healthy sessions (2026-08-02)

RJ's baseline diagnostic session died at turn 50 (first gossip checkpoint): "canonical root
unavailable … Navigation complete ready path for entity Handle(106:1) does not terminate at its
requested destination" → DETERMINISM SESSION FAILED on both peers identically (a fail-closed
capture refusal, not a real divergence). Root cause: the canonical nav provider asserted that a
complete ready path's last waypoint equals `Request.End` exactly — but the live pipeline
deliberately violates this (`PushWaypointsAwayFromWalls` nudges a terminal waypoint clicked near a
wall edge inward, SeinNavigationAStar.cpp:2623-2646; only authoritative cover-slot destinations
restore the exact End; the same-cell partial→complete upgrade at :2587 quantizes terminals to
cells). Both exact-terminal checks (complete≠End rejected, partial==End rejected) removed from
SeinNavigationCanonicalStateProvider.cpp — the terminal position is pipeline-internal, not a
canonical invariant. The rejects-wrong-terminal unit test inverted into
NavigationCaptureAcceptsCompletePathWithNudgedTerminal (the regression test for this kill).
Same lesson class as fable-findings §9/§10: fail-closed validators must encode invariants the
LIVE pipeline actually guarantees, and the fixture corpus must include the ordinary-gameplay case
(a click near a wall) — every fixture seeded exact-terminal paths, so 371 green tests missed it.

## 10. Session A re-run (post-§9-fix, 2026-08-01) — session survives; amplifier tell did NOT fire

Frame=195.2 ms, Game=190.6 ms, GPU=24.7 ms, Draw≈0, RHIT=0.06. StateHash.Log=0 confirmed. The
turn-50 kill is gone. **No `falling behind` log lines appeared** — the catch-up amplifier's
logging tell did not fire, an honest strike against claim A as stated (either the pump is not
saturating, or the :2430 warning has stricter conditions than the audit assumed). Implication:
the ~190 ms game thread may be dominated by per-RENDER-frame costs (FoW debug proxy rebuild at
stamp cadence is the prime suspect) rather than sim-tick multiplication. Session C discriminates:
VisionCellSize 400 shrinks fog proxy/stamp/overlay costs 16–64× while MaxTicksPerFrame=2 caps the
pump — whichever change moves Game tells us which mechanism owned the frame. Also hit en route:
re-baking level data staled the Simulation Content Manifest and PIE preflight fail-closed until
Project Settings → SeinARTS → "Generate / Regenerate Manifest" is clicked. Correct behavior,
rough workflow — polish item: auto-regenerate (or offer to) on bake completion instead of
blocking the next PIE.
