# SeinARTS Performance Baseline

**Baseline date:** 2026-08-02
**Code boundary:** stabilization commit `27cb490`

## Comparable benchmark contract

A result is comparable only when these are fixed or recorded:

- UE 5.7 Development Editor executable in standalone `-game` mode.
- `/SeinARTSFramework/Levels/Sandbox`.
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

1. Continuous 100-unit formation preview still costs about 3.12 ms; separation is the largest sub-block.
2. The current map does not prove 300/500/1,000-unit dense moving-combat budgets.
3. Active collision does not usually settle early; larger dense populations need their own scale curve.
4. Complex game AnimBPs, Control Rig, cloth, physics, and unique meshes can exceed the mannequin baseline.
5. `Sein.Nav.Show 1` is a correctness visualization, not a performance-safe overlay. A measured 100-mover run rose from 18.62 ms to 64.62 ms before the latest debug caching work; always record the flag.
6. Multi-client PIE intentionally hosts multiple complete simulations/presentations in one process. Record world count and do not present it as one shipped client's cost.

## Required profiling discipline

Do not optimize from `stat unit` alone. Capture settled Unreal Insights and use the named `Sein_*` scopes to attribute simulation systems, latent actions, formation resolution, actor bridge, UI, fog, root maintenance, collision passes, and replay boundaries. Keep looking after the first expensive scope: the original regression had independent sim, UI, animation, debug-render, and GPU-memory causes.

Every simulation optimization that changes traversal or mutation timing must rerun Unit, Integration, Determinism, fresh-process serial/parallel traces, and a PIE behavior A/B. Presentation-only changes still need visual/editor validation.
