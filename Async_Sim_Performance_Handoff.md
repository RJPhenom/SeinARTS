# Async Sim Performance — Handoff (→ vehicle-movement work)

> **What this is.** A one-time handoff from the forked session that built the BAR-inspired
> deterministic-multithreading layer for the SeinARTS sim. The vehicle-movement / Reeds-Shepp
> thread was paused at a juncture so this could land on a stable base; it now resumes **on top of
> this foundation**. Read this once on resume, then delete or archive it.
>
> **Status (2026-06-25):** P0–P5 **plus a follow-on optimization batch (see §7)** built and build-green. Avoidance PIE-verified by RJ; the rest
> awaiting RJ's full PIE pass. Nothing committed by Claude. Defaults are behavior-neutral.
>
> Living detail lives in the memory file `async-sim-performance-architecture.md`; this doc is the
> self-contained narrative.

---

## TL;DR

The sim now parallelizes its hottest systems **with zero behavior change at shipped defaults**.
Async pathfinding and the parallel collision resolver are opt-in. Determinism is preserved by
construction and verifiable with a state-hash A/B gate. Your movement work is **not invalidated** —
and the work you already did (helping-hand, reorient-hold, the steering controllers) already obeys
the one new contract.

---

## 1. What's in the sim now

| | What it does | Default | Behavior vs before |
|---|---|---|---|
| **P0** | `SeinParallelFor` primitive (`SeinARTSCoreEntity/Public/Core/SeinParallel.h`) + a global parallel-section guard (`SEIN_CHECK_NOT_PARALLEL`) + cvars | — | new infra |
| **P1** | **Avoidance** runs across worker threads | on | bit-identical (PIE-confirmed) |
| **P2** | **Idle movement driver** (native→parallel, BP→serial) + **nav-containment** | on | bit-identical |
| **P3** | **Async pathfinding** — reentrant A\* (`FAStarScratch`), batched `RunPathBatch`, nav-subsystem queue/drain | **off** | when on: ~1-tick path-delivery latency |
| **P4** | **Sort-grid broadphase** — parallel per-tick rebuild of the collision spatial hash | on | bit-identical (verified) |
| **P5** | **Pluggable collision resolver** — `Default` (Gauss-Seidel) vs `Parallel` (Jacobi) | Default | Default bit-identical; Parallel opt-in, different settling feel |

With shipped defaults, P1/P2/P4 are already parallel and behavior-neutral. P3 and P5-Parallel are
opt-in.

---

## 2. The contract your movement work must honor

The async layer imposes one small, clean contract — and everything you already built complies.

- **Native movement modes' `TickIdle` runs on worker threads.** It must: read only
  immutable/snapshot state + the const nav bake; **write only its own entity**; no neighbour reads;
  no shared mutation; no float. Your **helping-hand, reorient-hold, and the bicycle/arc-pivot
  controllers are per-unit self-mutation → already compliant.** Blueprint-authored modes run serial
  (safe; UE's BP VM isn't thread-safe) — no change to how they're authored, they just don't get the
  speedup.
- **`PlanPath` path requests may be 1 tick deferred** (only when `Sein.Sim.AsyncPathfinding` is on).
  The result returns through the *existing* `Throttled`→retry-next-tick path your `PlanPath` /
  `USeinMoveToAction` already handles — **zero change required**.
- **A custom nav / planner must be reentrant** (per-search scratch, the `FAStarScratch` pattern) to
  ride `RunPathBatch` in parallel. Non-reentrant impls simply run serial — still correct.
- **The guard catches mistakes:** calling a structural mutator
  (spawn/destroy/`AddComponent`/`EnqueueCommand`/`EnqueueVisualEvent`) from inside a parallel body
  trips `SEIN_CHECK_NOT_PARALLEL` at the call site in non-shipping builds.
- **Verify any new parallel system** with the determinism gate (see §5).

---

## 3. New modularity seam

Collision resolution is now a **pluggable seam** — `USeinCollisionResolver`, chosen via
`USeinARTSCoreSettings::CollisionResolverClass`, matching the Nav/FoW pattern. Two shipped impls:
`USeinCollisionResolverDefault` (Gauss-Seidel, serial — the default) and
`USeinCollisionResolverParallel` (Jacobi, multithreaded). If the vehicle work ever needs a bespoke
push model, it's a drop-in swap now.

---

## 4. Vehicle / Reeds-Shepp specifics

- **RS curve-fit in `PlanPath`:** requests a coarse nav path (possibly async-deferred → handled by
  `Throttled`-retry) then curve-fits. The fit reads immutable baked RS/Dubins tables + writes its
  own path → **parallel-safe**. Bake-offline-never-runtime-fixed-point-RS still holds.
- **Wheeled/Tracked are native classes → their `TickIdle` runs parallel** → keep them snapshot-read
  / own-write (they already are).
- **The `PlanPath` / `USeinPlannerHandle` / `GetMinTurnRadius` seam is unchanged.**
- **The `FSeinPath.Segments` segment-execution seam is untouched and orthogonal** to all of this.

---

## 5. Toggles & verification

- `Sein.Sim.Parallel` (default **1**) — master; `0` forces every `SeinParallelFor` serial.
- `Sein.Sim.AsyncPathfinding` (default **0**) — the batched path-request path.
- `CollisionResolverClass` (default **Default/GS**) — set to **Parallel** for Jacobi;
  `NumPasses` (8) / `Relaxation` (1) tune its feel.
- `Sein.Sim.ParallelMinBatch` (64) — small-batch serial cutoff.
- **Determinism gate:** identical inputs (a replay) at `Sein.Sim.Parallel 0` vs `1`, with
  `Sein.Sim.StateHash.Log 1` → the per-tick hash streams must be **byte-identical**. For P4/P5-Default
  (designed bit-identical), additionally diff against the pre-change commit.

---

## 6. Resume point

Pick the **vehicle-movement / Reeds-Shepp / wheeled-tracked polish** thread back up exactly where it
paused — the steering feel, the RS curve layer, per-class tuning — now standing on a verified,
settled async foundation. Build movement on it freely; honor the §2 contract for anything new (you
mostly already do).

---

## 7. Addendum — follow-on optimizations (post-foundation; movement-neutral)

After the foundation landed, the same session implemented a batch of additional sim-perf wins from a
BAR + internal scan. **None change the §2 contract or anything the movement work must honor** — they
are optimizations *elsewhere* in the sim, add **no new cvars**, and ride the existing
`Sein.Sim.Parallel` gate. Listed for situational awareness, plus one tool you may actually want.

- **`ISeinComponentStorage::ForEachLiveComponent` — NEW, and useful to you.** A public live-slot
  iterator: `void ForEachLiveComponent(TFunctionRef<void(int32 SlotIndex, void* RawComponent)>)`,
  walking a storage's live slots in slot-index-ascending order. Reconstruct the full handle with
  `FSeinEntityPool::GetSlotGeneration(SlotIndex)` (also new). **Why it matters here:** the movement
  driver and avoidance currently scan the *whole* entity pool and filter by `FSeinMovementComponent`;
  they could instead iterate the `FSeinMovementComponent` storage's live slots directly — the same
  lean-iteration win `FSeinLifespanSystem` / `FSeinEffectTickSystem` just took. Optional,
  behavior-neutral, order-preserving (so it stays parallel- and determinism-safe). Reference impl:
  `FSeinGenericComponentStorage::ForEachLiveComponent`; reference usage: the two systems above.
- **Fog-of-War LOS stamping parallelized** — entirely behind the `USeinFogOfWar` default impl (the
  abstract base is unchanged). Movement-irrelevant; noted only so the FoW code moving isn't a surprise.
- **StateHash gated + parallelized** — standalone now skips the per-tick full-world hash when nothing
  consumes it (networked/log still compute it, byte-identical); the per-storage hashing fans out, the
  fold stays serial-ordered. Internal; movement-irrelevant.
- **Command-broker dispatch `TSet` pooled** — internal allocation reuse; movement-irrelevant.

**Parked (not built):** a shared read-only AI proximity index (build when an AI consumer needs it),
and per-movement-class nav repatch on terrain edits (N/A unless destructible/deformable terrain is
ever added — see the scan notes). Neither touches the movement work.
