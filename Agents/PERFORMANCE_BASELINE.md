# SeinARTS Performance Baseline

**Baseline date:** 2026-08-02
**Code boundary:** stabilization commit `27cb490`

**UE 5.8 automated preview qualification:** 2026-08-12, All-profile run
`SeinARTS.Perf-20260812-081656-9a968ce2`. The exact worst-case cover assignment
`128x128` averaged 11.998 ms. The public `SeinComputeFormationPreview` layout path measured:

- 64 members: 0.259 ms median / 0.284 ms p95 coverless; 0.467 / 0.486 ms with
  16 providers, 128 accepted slots, and all 64 members snapped.
- 128 members: 1.255 ms median / 1.337 ms p95 coverless; 3.181 / 3.324 ms with
  16 providers, 128 accepted slots, and all 128 members snapped.

The fixture uses one real canonical transient world, the shipped Cover-aware resolver, a pinned
500-unit snap radius, exact Blob separation, repeated byte-identical layouts, and an unchanged
canonical root. It excludes selection expansion, per-position quality queries, actor/renderer
updates, navigation-backed projection, and the rest of a game frame, so it is a core-layout
regression sentinel rather than an end-to-end preview frame budget.

**UE 5.8 collision scale microbenchmark:** 2026-08-12, real canonical bootstrap and complete
fixed ticks with reset packed contacts: 64 movers 1.257 ms median, 128 movers 3.114 ms, and
256 movers 7.214 ms (`SeinARTS.Perf-20260812-081656-9a968ce2`). The enforced
256-mover ceiling is a 25 ms local regression sentinel, not a portable hardware budget. The test
isolates one world's exact production pump and proves every mover is present in the rebuilt
broadphase; it is not a replacement for a moving-combat Insights capture with presentation.

**UE 5.8 full-game continuous-preview qualification:** 2026-08-12, one standalone Sandbox world,
100 owned movers, 1280x720, debug overlays off, settled 15-30 second windows. A repeated matched
preview-on/off capture measured 8.982 / 7.378 ms average `FEngineLoop::Tick`, a 1.604 ms frame delta.
An earlier optimized pair measured a 2.855 ms delta, so use 1.60-2.86 ms as the observed run range.
The stable preview scopes were 4.518 ms refresh, 1.303 ms nav projection, 0.911 ms relocation,
1.854 ms separation, and 0.694 ms reassignment. The derived sparse dynamic-blocker cell index reduced
refresh from 11.490 to 4.518 ms (60.7%), projection from 8.171 to 1.303 ms (84.1%), and relocation
from 7.422 to 0.911 ms (87.7%) without changing the relocation workload. Its rebuild ran only twice
during startup at 0.015 ms average and did not run in the settled window. Source traces are under
`Saved/Profiling/Insights/PreviewSparseIndex-20260812-092226` and
`Saved/Profiling/Insights/PreviewSparseIndexRepeat-20260812-092517`; timer exports are under
`D:/SeinTrace/sparseindex-20260812-092226-*.csv` and
`D:/SeinTrace/sparseindex-repeat-20260812-092517-*.csv`.

## Comparable benchmark contract

A result is comparable only when these are fixed or recorded:

- UE 5.8 Development Editor executable in standalone `-game` mode. The historical table below is
  UE 5.7 evidence; the 2026-08-12 preview capture above starts the current UE 5.8 series.
- `/Game/SeinARTSExamples/Levels/Sandbox`.
- One complete game world at 1280x720 with audio disabled.
- A settled 15-second window containing 452 sampled engine ticks.
- `Sein.Nav.Show 0` and `Sein.FogOfWar.Show 0`, unless measuring a debug overlay explicitly.
- The real selection, command, formation, navigation, latent action, movement, avoidance, collision, animation, UI, and presentation paths remain enabled.
- Record mover count, total actor/mesh count, world count, active preview state, collision resolver, and skeletal diagnostic mode.

Development-only helpers include `Sein.Perf.MoveOwned`, `Sein.Perf.SkeletalMode`, and `Sein.Perf.FixedCursor`. They do not create a Shipping gameplay path.

## Measured one-world baseline

| Workload | Engine avg | Median | Sim systems | Collision | Latent actions | Native UI |
|---|---:|---:|---:|---:|---:|---:|
| Idle | 7.80 ms | 7.70 ms | 1.43 ms | 0.60 ms | negligible | 0.28 ms |
| 60 movers | 9.68 ms | 8.22 ms | 2.96 ms | 1.20 ms | 0.65 ms | 0.47 ms |
| 100 movers | 11.59 ms | 10.17 ms | 3.99 ms | 1.72 ms | 1.06 ms | 0.53 ms |
| 100 movers, animation frozen | 9.24 ms | 8.15 ms | 4.01 ms | 1.84 ms | 1.03 ms | 0.51 ms |
| 148 movers | 12.81 ms | 12.10 ms | 5.41 ms | 1.41 ms | 1.83 ms | 0.60 ms |
| 100 movers + continuous preview | 14.40 ms | 13.00 ms | 3.99 ms | 1.80 ms | 1.05 ms | 0.53 ms |

At 100 movers, normal skeletal animation/presentation adds about 2.35 ms over the frozen-animation A/B. It is material but does not explain the original 50-190 ms failures by itself.

## Major remediations already present

- UI ability aggregation: about 2.43 ms to 0.53 ms at 100 movers.
- Earlier controlled full game-thread trace: 33.76 ms to 9.33 ms.
- Earlier simulation pump: 9.25 ms to 2.16 ms.
- Earlier collision block: 3.63 ms to 0.73 ms in the settled controlled run.
- Minimap fog: bulk grid sampling, reused buffers, and linear-time separable blur.
- Fog/debug/presentation work: dirty/cadence/event driven rather than repeated per sim tick.
- Routine root maintenance: incremental and invoked only for due protocol checkpoints; no fixed three-second all-state freeze.
- RTS crowd meshes: no ordinary skinned BLAS allocation and no unnecessary animation-to-Chaos bone copy.

The final GPU/resource capture did not reproduce the prior ray-tracing geometry or VRAM exhaustion warnings. It recorded about 28.8 MiB of ray-tracing BLAS and a CPU-limited frame. Do not suppress those warnings if they return; treat them as a policy or content regression.

## Known scale risks

1. The fresh UE 5.8 full-game A/B bounds continuous 100-unit preview at a measured 1.60-2.86 ms
   frame delta after sparse dynamic-blocker indexing. This closes the comparable one-world Sandbox
   capture, but does not prove larger selections, multi-world PIE, or game-content render costs.
2. The current map does not prove 300/500/1,000-unit dense moving-combat budgets.
3. Active collision does not usually settle early. The automated packed-contact curve reaches 256
   movers; 300/500/1,000 moving-combat populations still require game-world Insights captures.
4. Complex game AnimBPs, Control Rig, cloth, physics, and unique meshes can exceed the mannequin baseline.
5. `Sein.Nav.Show 1` is a correctness visualization, not a performance-safe overlay. A measured 100-mover run rose from 18.62 ms to 64.62 ms before the latest debug caching work; always record the flag.
6. Multi-client PIE intentionally hosts multiple complete simulations/presentations in one process. Record world count and do not present it as one shipped client's cost.

## Required profiling discipline

Do not optimize from `stat unit` alone. Capture settled Unreal Insights and use the named `Sein_*`
scopes to attribute simulation systems, latent actions, formation resolution, actor bridge, UI, fog,
root maintenance, collision passes, and replay boundaries. Formation preview now exposes scratch
capture/materialization/revalidation, provider gathering, slot resolution/deduplication/emission,
eligible-edge construction, cost-matrix construction, and Hungarian solve scopes. Replay checkpoint
qualification should include `Sein_Replay_CaptureCheckpoint`,
`Sein_Replay_Checkpoint_CaptureSnapshot`, `Sein_World_CaptureSnapshot`, and
`Sein_Replay_Checkpoint_EncodeEnvelope`. Keep looking after the first expensive scope: the original
regression had independent sim, UI, animation, debug-render, and GPU-memory causes.

Every simulation optimization that changes traversal or mutation timing must rerun Unit, Integration, Determinism, fresh-process serial/parallel traces, and a PIE behavior A/B. Presentation-only changes still need visual/editor validation.
