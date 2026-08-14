# SeinARTS Project State

**State date:** 2026-08-14
**Baseline branch:** `main`
**Stabilization commit:** `27cb490` (`Stabilize post-audit performance and determinism`)
**Cleanup boundary:** `1bebf91` (`Clean and harden the post-audit baseline`)
**Content/consumer boundary:** `1438051` (`Add clean downstream consumer verification`)
**Reporter-bootstrap fix:** `89f1b5d` (`Fix remote determinism reporter bootstrap`)
**Remote posture:** this qualified state is integrated on `main` and synchronized with `origin/main`.

## Stabilized capability boundary

The post-audit performance/remediation work is now committed and fast-forwarded onto `main`. It includes:

- Sparse authoritative component traversal in recurring systems.
- Exact settled-work exits and reduced narrow-phase work in the collision resolvers.
- Gauss-Seidel as the project default collision policy; deterministic Jacobi remains selectable and covered.
- Incremental BLAKE3-128 routine world roots, sealed only at protocol checkpoint boundaries, with forced-rebuild verification.
- Correct peer-report liveness based on the frozen epoch participant manifest, including
  authenticated authority-to-peer manifest delivery and digest validation during bootstrap.
- Frame-coalesced presentation updates while network/replay observers remain tick-exact.
- Event/cadence-driven fog presentation and cached navigation debug geometry.
- Cached selection ability aggregation and bulk/linear-time minimap fog generation.
- Native skeletal-mesh and hardware-ray-tracing policies appropriate for RTS crowds, with per-actor opt-outs.
- The Widget Blueprint source-asset class moved to the UncookedOnly authoring module, fixing uncooked standalone loading.
- Streaming replay v9 journals and authenticated checkpoint-plus-command-tail resync from the prior remediation commits.
- Strict PIE manifest freshness made opt-in; runtime, cook, replay, snapshot, and peer compatibility protections remain.
- Balance Data now has a qualified entity/ability Gather, Push, and Check Sync round trip with
  fail-closed row/source/schema binding, collision-resistant duplicate names, sparse-column
  accounting, stale UDS repair, canonical output-path validation, and saved/reinstanced Blueprint
  ability persistence.

## Baseline cleanup

- Historical audit/report artifacts were removed. Current release-documentation inputs are staged
  under `.agents/Docs/`; `Docs/` is reserved for the future customer-facing documentation set.
- Durable current state was consolidated under `.agents/`; generated PDF output now belongs outside the repository.
- Empty scratch directories, duplicate/template config entries, stale comments, and dead source surfaces were removed.
- Removed reflected/source surfaces had no source, config, or binary-asset consumers: `ESeinElevationMode`, `FFixedBounds`, `SeinTime`, `FSeinCapturePointData`, `FSeinFootprintData`, and `USeinLevelLoS`.
- Designer-facing reflected APIs were not removed merely because native code does not include them. `UMathBPFL`, `FSeinBasicMatchSettings`, and `FSeinGarrisonSpec` remain intentional public authoring surfaces.
- Example maps, gameplay Blueprints, and mannequin content now belong to the host project under `/Game/SeinARTSExamples`; the base plugin retains only its runtime-owned UI content. Framework packages no longer reference host `/Game/SeinARTS` content or opt-in extension packages.
- The unreachable simulation-content manifest commandlet was removed. Clean consumers invoke the registered editor console command through Python instead of relying on a commandlet class hidden in a `PostEngineInit` module.
- Cover and Squad are now physically independent production plugins. Their shared
  `SeinARTSCoverSquad` runtime module lives in the separate opt-in
  `SeinARTSCoverSquadExtension`; its reflected script path, simulation-content contributor ID,
  pool-codec ID, and schema/revision identities did not change.
- Fixed-vector absolute-component queries now preserve 32.32 values and saturate the unrepresentable absolute minimum.
- Stateful PRNG spatial/rotator draws are explicitly ordered across compilers; `RandomRotator` converts its radian samples to the degree-based rotator contract.
- Fixed ray/box queries no longer impose an arbitrary 10,000-unit hit ceiling.

## Current verification

The latest integration-candidate source was rebuilt and re-run after closing async navigation,
FoW state, per-unit navigation policy, ability/passive ownership, checkpoint-authoring admission,
formation/resolver state coverage, provider teardown, and downstream content ownership:

| Gate | Result |
|---|---:|
| `SeinARTS.Unit`, profile All | 458 passed, 0 failed |
| `SeinARTS.Unit`, profile Framework | 434 passed, 0 failed |
| `SeinARTS.Integration`, profile All | 26 passed, 0 failed |
| `SeinARTS.Integration`, profile Framework | 20 passed, 0 failed |
| `SeinARTS.Determinism`, profile All | 48 passed, 0 failed |
| `SeinARTS.Determinism`, profile Framework | 33 passed, 0 failed |
| `SeinARTS.Editor`, profile All | 45 passed, 0 failed |
| `SeinARTS.Editor`, profile Framework | 43 passed, 0 failed |
| `SeinARTS.Sim`, profile All | 50 passed, 0 failed |
| `SeinARTS.Sim`, profile Framework | 47 passed, 0 failed |
| `SeinARTS.Perf`, profile All | 6 passed, 0 failed; cover 128x128 averaged 11.998 ms; public 128-member preview measured 1.255/1.337 ms median/p95 coverless and 3.181/3.324 ms dense; collision full-tick medians 1.257/3.114/7.214 ms at 64/128/256 movers |
| `SeinARTS.Perf`, profile Framework | 4 passed, 0 failed; latest replay operational soak wrote 135,244,673 bytes over 449 turns with 135.552/203.523/211.276 ms checkpoint p50/p95/max; containment at 1,000 occupants measured 2.825 ms canonical root, 4.328 ms invalidated checkpoint, and 0.926 ms warm checkpoint; moving-entity checkpoint medians remain 2.037/7.815/15.288 ms at 100/500/1,000 entities; collision full-tick medians 1.401/3.035/7.494 ms at 64/128/256 movers |
| Replay Memory Insights (qualified) | Clean commit `8178dec` has a same-attempt build and production `Qualified` receipt; the warmed 56-checkpoint interval retained zero production replay allocations/bytes against the fixed 4 KiB ceiling, with complete callstacks and separately validated allocator sentinels |
| Fresh-process collision trace | 2026-08-14 serial and parallel roots/poses identical for all 120 ticks under `SeinARTS.Replay.6`; final root `389FF04BD23FD4D3F5C659C9384FD2EC`, pose `0xF9AA3969BB04EAD0` |
| `SeinARTSEditor Win64 Development` | succeeded / target current |
| `SeinARTS Win64 Shipping` | succeeded / target current |
| Focused Core boundary/epoch | 2026-08-13 exact epoch test passed with `SeinARTS.Replay.6`; prior `SeinARTS.Unit.Core` Framework profile passed 131 tests including opposite-endpoint saturated distance and maximum-diagonal normalization |
| Focused Net protocol | 2026-08-13 `SeinARTS.Unit.Network.Protocol` Framework profile: 40 passed through production listen-authority commit and disconnect pipeline-backfill entry points |
| Focused Movement+ | 2026-08-13 unit 5/5, determinism 9/9, movement snapshot 8/8, and continuation 1/1 passed |
| Focused Move To repath | 2026-08-14 interval/off-path behavior 13/13 and real-boundary fresh-world continuation 1/1 passed; covers same-tick commit, throttle cadence, forced attempts, unavailable navigation/subsystem gates, failure limits, partial callback order/state, implicit-origin drift, and canonical continuation |
| Focused default avoidance | 2026-08-13 Unit 6/6 passed after private-kernel decomposition; covers crossing serial/parallel agreement, opt-out/idle release, idle dodge, gap resolution, and broker cohesion |
| Focused containment integrity | 2026-08-13 Unit 4/4, Determinism 3/3, and Perf 1/1 passed; covers cycle/overflow admission, bootstrap/root/checkpoint refusal, failure-atomic malformed restore, rotated local deploy offsets, ability commands, pre/post checkpoint roots, per-tick replay roots, and 100/500/1,000-occupant invalidated/warm checkpoint curves |
| Focused Balance Data | 2026-08-14 Editor 6/6 passed; covers entity and ability round trips, exact sparse accounting, stale same-name UDS replacement, exact source-class rebinding, colliding Blueprint paths, invalid output rejection, Blueprint reinstance, package save/unload/reload, and persisted value recovery |
| Clean consumer: Framework | fresh 2026-08-12 Editor + Shipping build, exact map load, cook/package, real Shipping startup passed |
| Clean consumer: Framework + Cover only | fresh 2026-08-12 Editor + Shipping build, exact map load, cook/package, real Shipping startup passed; Squad/bridge absent |
| Clean consumer: Framework + Squad only | fresh 2026-08-12 Editor + Shipping build, exact map load, cook/package, real Shipping startup passed; Cover/bridge absent |
| Clean consumer: Framework + Movement+ | 2026-08-13 Editor + Shipping build, exact map load, cook/package, real Shipping startup, two-peer lockstep movement, resync/reconnect continuation, and checkpoint-seek replay passed |
| Clean consumer: all five production plugins | fresh 2026-08-12 Editor + Shipping build, exact map load, cook/package, real Shipping startup passed; bridge mounted/started/shut down |
| Packaged Framework multiplayer/replay | 2026-08-12 Shipping listen server + client completed a 2-of-2 equal world-root checkpoint at turn 5, then passed lobby travel, lockstep and pair-capability commands, forced resync, physical reconnect, pair-capability persistence, replay witness/seek, and exact terminal-root agreement at tick 239 (`753757B5C66CAB58D25E1075AF4E41A6`) |
| Packaged Movement+ multiplayer/replay | 2026-08-13 run `818e493845364517a476b594ccf25809` regenerated a Shipping listen server + client and passed two-peer root gossip, a real 50,000/5,000-unit wheeled Move command, exact instantiated-class/target/telemetry witnesses, forced resync, physical reconnect, exact movement/capability persistence, grant/revoke, and checkpoint-seek replay at tick 251 (`639951F0AE0379BF84FD98044686F64D`); executable SHA-256 `215F1AE76D1075FE51E3A1A7966A2F48C6D95458F0DB3782DF2C44089D2FB016` |
| Generated simulation-content manifest | All profile: 10 contributors, 93 records, digest `ECADF1E2FA666B99B967ECDBD5BD5E57`; Framework profile: 6 contributors, 93 records, digest `49DEFB62286E02460B89441C0D418543` |
| Staged diff validation | no whitespace errors; line-ending notices only |

Latest local evidence is under ignored `Saved/Automation/`:

- `SeinARTS.Editor.BalanceData-20260814-011402-9dac922b` (Framework, 6 passed;
  entity/ability Gather-Push-Check Sync, stale schema/source rejection, Blueprint reinstance and
  persisted reload)
- `SeinARTS.Editor.BalanceData-20260814-012021-b329bd83` (Framework, 6 passed after
  Blueprint-lifetime hardening), `SeinARTS.Editor-20260814-012418-1225e017` (Framework, 43 passed),
  and `SeinARTS.Editor-20260814-012504-bf491e5a` (All, 45 passed; both full suites enforced the
  floors established against implementation commit `3abf652`)
- `SeinARTS.Sim.Movement.Repath-20260814-002006-b4d103b6` (Framework, 13 passed)
- `SeinARTS.Editor.Snapshot.Movement.MoveToContinuationCrossesRealRepathBoundaryExactly-20260814-000233-9fa959a7`
  (Framework, 1 passed; fresh-world continuation crosses a real interval repath boundary)
- `SeinARTS.Unit-20260814-000329-a73f1ba3` (All, 458 passed),
  `SeinARTS.Sim-20260814-002107-fde045b2` (All, 50 passed),
  `SeinARTS.Sim-20260814-002148-e5a7f301` (Framework, 47 passed),
  `SeinARTS.Integration-20260814-000505-99e7fcb5` (All, 26 passed),
  `SeinARTS.Editor-20260814-000556-84346e58` (All, 39 passed), and
  `SeinARTS.Determinism-20260814-000618-8c0a8874` (All, 48 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260814-000658-33a2826f` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260814-000715-14ec43e5`
  (fresh processes; all 120 canonical roots and raw poses matched after Move To repath extraction)
- `Scripts/Build.ps1` and `Scripts/Build.ps1 -Target SeinARTS -Config Shipping`
  (2026-08-14; Development current and 176-action Shipping build linked successfully)
- `SeinARTS.Unit.Movement.Avoidance-20260813-145658-05842d87` (Framework, 6 passed)
- `SeinARTS.Unit-20260813-145809-0315289c` (Framework, 434 passed) and
  `SeinARTS.Unit-20260813-150458-6a24a13d` (All, 452 passed)
- `SeinARTS.Sim-20260813-145922-506a1810` (Framework, 34 passed) and
  `SeinARTS.Determinism-20260813-145947-f17a9a28` (Framework, 33 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-150037-4a221f6d` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-150059-c5f1cb4b`
  (fresh processes; all 120 canonical roots and raw poses matched exactly after the default
  avoidance-kernel decomposition)
- `SeinARTS.Unit.Navigation-20260813-140554-b71dccb6` and
  `SeinARTS.Unit.Navigation-20260813-141012-e758b08b` (Framework, 34 passed before and after
  extracting the non-shipping A* path reporters)
- `SeinARTS.Unit-20260813-141200-c31acc38` (Framework, 428 passed) and
  `SeinARTS.Determinism-20260813-141249-1eed39dc` (Framework, 33 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-141326-d7d20de1` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-141343-d2646e6e`
  (fresh processes; all 120 canonical roots and raw poses matched exactly after the A* diagnostic
  extraction)
- `SeinARTS.Sim.Broker.IdleReseek-20260813-140051-0ebee27d` (Framework,
  7 passed; exact nonzero jitter, watch/release cadence and episode-cap boundaries, aligned and
  claimed/free-slot pairing, moving-traffic clearance, and multi-candidate loose returns)
- `SeinARTS.Sim-20260813-140128-4432a579` (Framework, 34 passed) and
  `SeinARTS.Determinism-20260813-140151-361b76f8` (Framework, 33 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-135404-64435211` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-135421-0aa49a32`
  (fresh processes; all 120 canonical roots and raw poses matched exactly after the broker re-seek
  extraction)
- `SeinARTS.Perf.Replay.OperationalSoak-20260813-103957-dfe48bc9` (Framework,
  clean pre-fix full-memory trace; 47,573,706 retained bytes overall and 8,388,688 under replay tags)
- `Saved/Profiling/Insights/ReplayOperationalMemoryConsume-20260813-104559.utrace`
  (development rerun; 22,443,426 retained bytes overall and 80 under replay tags)
- `Scripts/Qualification/Invoke-ReplayMemoryInsightsSelfTest.ps1` (Windows PowerShell 5.1 valid,
  over-retention, and tampered-trace fixtures passed); the exporter independently rejected the clean
  pre-fix trace without publishing a qualification directory
- `SeinARTS.Perf.Replay.OperationalSoak-20260813-100340-b1db339d` (Framework,
  1 passed; 449 turns, 64 natural periodic checkpoints, eight proven GC/exclusion overlaps,
  uncheckpointed final grant replay, sampled exact seeks, and full canonical-root playback)
- `SeinARTS.Integration.Network.Replay.Capacity-20260813-094103-b4d4bfab` (Framework,
  1 passed; configured 64 MiB policy exhaustion preserved an exactly replayable durable partial)
- `SeinARTS.Integration-20260813-095954-a5eac1ab` (All, 26 passed) and
  `SeinARTS.Integration-20260813-095858-2b1c72b7` (Framework, 20 passed)
- `SeinARTS.Perf-20260813-100529-fc6b3112` (All, 6 passed) and
  `SeinARTS.Perf-20260813-100437-70690a7c` (Framework, 4 passed)
- `SeinARTS.Unit-20260813-100626-7d1b4eca` (All, 446 passed)
- `SeinARTS.Determinism-20260813-100723-2c586db7` (All, 43 passed)
- `Scripts/Build.ps1` and `Scripts/Build.ps1 -Target SeinARTS -Config Shipping`
  (2026-08-13; Development current, Net recompiled in Shipping, and Shipping executable linked)
- `SeinARTS.Unit-20260813-085708-60185381` (All, 446 passed with exact floor)
- `SeinARTS.Unit-20260813-085611-d820118f` (Framework, 428 passed with exact floor)
- `SeinARTS.Integration-20260813-083308-613d2a27` (All, 25 passed)
- `SeinARTS.Integration-20260813-082942-5be7722d` (Framework, 19 passed)
- `SeinARTS.Determinism-20260813-085838-340f6f0b` (All, 43 passed with exact floor)
- `SeinARTS.Determinism-20260813-085805-c728eb8a` (Framework, 33 passed with exact floor)
- `SeinARTS.Sim-20260813-084439-750e0f11` (All, 30 passed)
- `SeinARTS.Sim-20260813-083034-3a5ac5dc` (Framework, 27 passed)
- `SeinARTS.Perf-20260813-085943-4887bf37` (All, 5 passed with exact floor)
- `SeinARTS.Perf-20260813-085918-29f85569` (Framework, 3 passed with exact floor)
- `SeinARTS.Determinism.CoreEntity.Containment-20260813-083917-86078194`
  (3 passed, including checkpoint transfer and per-tick replay-prefix roots)
- `SeinARTS.Perf.Containment-20260813-085323-24d4a6e3`
  (1 passed, including invalidated and warm checkpoint curves)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-084522-3cbeb406`
- `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-084543-f955cbb9`
- `SeinARTS.Unit-20260803-092025-a47e1818` (All)
- `SeinARTS.Unit-20260803-092204-6014d34e` (Framework)
- `SeinARTS.Integration-20260803-092115-78560738` (All)
- `SeinARTS.Integration-20260803-094952-e1952b89` (Framework)
- `SeinARTS.Determinism-20260803-092131-e9853584` (All)
- `SeinARTS.Determinism-20260803-095010-0388823a` (Framework)
- `SeinARTS.Editor-20260803-095812-6c947010` (All; checked-in floor, after redirect cleanup)
- `SeinARTS.Editor-20260803-095029-89627fda` (Framework; checked-in floor)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260803-092256-63018e6b`
- `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260803-092310-7a4edeb3`
- `SeinARTS.Unit-20260803-111739-444c4c59` (Framework, post-runtime fixes)
- `SeinARTS.Integration-20260803-111857-4fb48cf2` (Framework, post-runtime fixes)
- `SeinARTS.Determinism-20260803-111857-fe97367f` (Framework, post-runtime fixes)
- `SeinARTS.Integration.MovementPlus.VehicleGym-20260803-121632-b7f862f0` (5 passed)
- `SeinARTS.Determinism.MovementPlus.VehicleGym-20260803-122625-0265a447` (5 passed,
  including forced serial/parallel recovery roots)
- `SeinARTS.Integration-20260803-122715-95e9ce08` (All, 20 passed)
- `SeinARTS.Determinism-20260803-122841-6c52d746` (All, 25 passed)
- `SeinARTS.Unit-20260803-122946-4c898aa0` (All, 411 passed)
- `SeinARTS.Unit-20260803-125931-a30e5277` (All, 412 passed after authenticated
  participant-manifest regression coverage)
- `SeinARTS.Unit-20260803-131608-f3fcc8a9` (Framework, 404 passed with the
  checked-in regression floor applied)
- `SeinARTS.Unit.Network.Protocol-20260803-131114-4cd02bbc` (38 passed)
- `SeinARTS.Unit-20260811-130258-0a726948` (All, 418 passed)
- `SeinARTS.Unit-20260811-124827-ea9357f1` (Framework, 408 passed)
- `SeinARTS.Integration-20260811-130440-067618b4` (All, 20 passed)
- `SeinARTS.Determinism-20260811-130346-42ea6c90` (All, 38 passed)
- `SeinARTS.Determinism-20260811-131017-b315afa0` (Framework, 29 passed)
- `SeinARTS.Editor-20260811-130509-9e408b46` (All, 36 passed)
- `SeinARTS.Determinism.MovementPlus-20260811-130229-79933669` (9 passed,
  including replay-file and bounded reconnect continuation)
- `SeinARTS.Unit.Core-20260813-005443-4b9170b5` (Framework, 130 passed,
  including exact saturated long-range fixed-vector magnitude)
- `SeinARTS.Unit.Network.Protocol-20260813-013344-fb6e6463` (Framework,
  40 passed, including zero-author suppressed-slot backfill and listen-authority turn delivery)
- `SeinARTS.Unit.MovementPlus-20260813-013531-ecfa8b25` (All, 5 passed)
- `SeinARTS.Determinism.MovementPlus-20260813-013610-4b4fbc01` (All, 9 passed)
- `SeinARTS.Editor.Snapshot.Movement-20260813-013650-4dd4d203` (All, 8 passed)
- `SeinARTS.Unit.Movement.Continuation-20260813-013713-82f0ff5c` (All, 1 passed)
- `SeinARTS.Unit.Movement.LongRange-20260813-052542-8f679b7b` (Framework,
  6 passed, including exact sloped endpoint clamping and legacy raw-unit squared-radius semantics)
- `SeinARTS.Unit-20260813-053858-20c9324c` (All, 442 passed)
- `SeinARTS.Determinism-20260813-054003-4ba20115` (All, 40 passed)
- `SeinARTS.Integration-20260813-054035-efcf90b3` (All, 25 passed)
- `SeinARTS.Sim-20260813-054117-61733d2f` (All, 30 passed)
- `SeinARTS.Editor-20260813-054135-0802c166` (All, 38 passed)
- `SeinARTS.Editor.Snapshot.Movement-20260813-054152-b4a63a2e` (All, 8 passed)
- `SeinARTS.Unit-20260813-054208-aee2ef60` (Framework, 424 passed)
- `SeinARTS.Determinism-20260813-054256-921d00b5` (Framework, 30 passed)
- `SeinARTS.Integration-20260813-054320-b1924431` (Framework, 19 passed)
- `SeinARTS.Sim-20260813-054402-2499ed98` (Framework, 27 passed)
- `SeinARTS.Editor-20260813-054420-a0129ca4` (Framework, 36 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-054436-298a6a1a`
  and `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-054453-6a2f1605`
  (fresh processes; all 120 canonical roots and raw poses matched exactly)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260811-130538-ba506961`
- `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260811-130553-1a34b7ac`
- `SeinARTS.Unit.Network-20260811-200749-dfabc4c1` (78 passed, including
  execution-gate-only latency attribution)
- `SeinARTS.Unit.Cover.Assignment-20260811-203439-82b6a2ff` (4 passed)
- `SeinARTS.Perf.Cover.Assignment-20260811-203549-04c4546b` (1 passed;
  dense 128x128 average 12.890 ms)
- `SeinARTS.Unit-20260811-203654-ff2a8dcd` (All, 422 passed)
- `SeinARTS.Unit-20260811-203809-9fd2ba88` (Framework, 408 passed)
- `SeinARTS.Integration-20260811-203925-698fb831` (All, 20 passed)
- `SeinARTS.Determinism-20260811-203959-a3b3cc2b` (All, 38 passed)
- `SeinARTS.Unit.Squad.Reinforcement-20260811-211537-002d13d1` (2 passed)
- `SeinARTS.Sim.Squad.Reinforcement-20260811-211558-24408e72` (1 passed)
- `SeinARTS.Determinism.Squad.Reinforcement-20260811-211717-8f6c19ee`
  (1 passed; fresh-world snapshot continuation and structural rejection)
- `SeinARTS.Unit-20260811-212028-4b41a09b` (All, 424 passed)
- `SeinARTS.Determinism-20260811-212116-9211faa0` (All, 39 passed)
- `SeinARTS.Integration-20260811-212155-61c13959` (All, 20 passed)
- `SeinARTS.Unit-20260811-212233-f9b0c1e4` (Framework, 408 passed)
- `SeinARTS.Determinism-20260811-212339-4197849c` (Framework, 29 passed)
- `SeinARTS.Integration-20260811-212411-2d3a122e` (Framework, 14 passed)
- `SeinARTS.Unit.Squad.Reinforcement-20260811-212922-4020a75a` (2 passed after
  atomic overflow/invalid-authoring hardening)
- `SeinARTS.Unit-20260811-213142-82fda742` (All, 424 passed after hardening)
- `SeinARTS.Determinism-20260811-213229-695c8059` (All, 39 passed after hardening)
- `SeinARTS.Integration-20260811-213307-92d572d8` (All, 20 passed after hardening)
- `SeinARTS.Determinism.Squad.Reinforcement-20260811-213849-24426f2d`
  (1 passed through the bounded v15 reconnect envelope into a fresh world)
- `SeinARTS.Editor-20260811-213923-ceff715e` (All, 36 passed)
- `SeinARTS.Unit.Squad.Reinforcement-20260811-215107-c46d8e8b` (3 passed,
  including malformed/overflowing charge rejection)
- `SeinARTS.Sim.Squad.Reinforcement-20260811-215138-314a9cdc` (1 passed,
  including unsafe broker self-cull normalization)
- `SeinARTS.Determinism.Squad.Reinforcement-20260811-215159-1bc04693`
  (1 passed; lexical tag identity, v15 reconnect transfer, continuation, and restore rejection)
- `SeinARTS.Unit-20260811-215232-3ee52949` (All, 425 passed)
- `SeinARTS.Determinism-20260811-215322-50225798` (All, 39 passed)
- `SeinARTS.Integration-20260811-215357-ae427557` (All, 20 passed)
- `SeinARTS.Unit-20260811-215713-303e17f6` (Framework, 408 passed with
  extensions physically disabled and the refreshed exact manifest profile)
- `SeinARTS.Determinism-20260811-215801-1038a89a` (Framework, 29 passed)
- `SeinARTS.Integration-20260811-215830-3f71fd56` (Framework, 14 passed)
- `SeinARTS.Editor-20260811-215856-23e0f0ed` (All, 36 passed)
- `Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1 -Profile All -SkipClientServer`
  (2026-08-12; Framework, Cover-only, Squad-only, Movement+-only, and Full all passed fresh
  Editor/Shipping build, consumer-manifest/map load, cook/package, and packaged startup; Framework
  also passed two-peer root gossip, forced resync, physical reconnect, and replay seek at tick 236,
  root `FBBE86ECC20E2E12B174B2C71778DABF`)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260812-000139-7f134305`
  and `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260812-000256-3e36379b`
  (fresh processes; 120/120 roots and pose hashes identical, final root
  `E8177AC3EB66C19E6C05B25A55A2099A`, pose `0x8B576390ECC600E1`)
- `SeinARTS.Sim.Squad.Reinforcement-20260812-000714-f1c79df1` (2 passed,
  including invalid completion clamp, exact-refund retry, and safe cull)
- `SeinARTS.Sim.Broker.CallbackSafety-20260812-001051-7e392ab5` (7 passed after
  moving resolver scenario state inside the authorized tick-zero transaction)
- `SeinARTS.Sim-20260812-001121-59c50ba8` (All, 30 passed in one process;
  confirms no cross-suite bootstrap-state leakage)
- `SeinARTS.Sim-20260812-001545-7556930b` (Framework, 27 passed with all
  production extensions physically disabled)
- `SeinARTS.Perf-20260812-001152-e3491fc4` (All, 1 passed; dense 128x128 cover
  assignment averaged 11.873 ms on UE 5.8)
- `SeinARTS.Perf.Collision.Scale-20260812-003430-42f0bba0` (Framework, 1 passed;
  real fixed-tick medians 1.662/3.446/7.590 ms at 64/128/256 packed movers)
- `SeinARTS.Perf-20260812-003536-84a668ef` (Framework, 1 passed under the
  checked-in floor; medians 1.580/3.409/7.280 ms at 64/128/256 packed movers)
- `SeinARTS.Perf-20260812-003609-5a2b04f5` (All, 2 passed under the checked-in
  floor; collision medians 1.565/3.413/7.394 ms and dense cover assignment green)
- `SeinARTS.Integration.Network.Replay-20260812-001946-e698f371` (Framework,
  13 passed; optional checkpoint full-flush is background-ordered and commits
  durability state only after worker completion; >64 MiB streaming remains green)
- `SeinARTS.Integration.Network.Replay.ReplayScheduledCheckpointFinalizationDrainsBackgroundDurability-20260812-002800-ca7cce23`
  (1 passed; production cadence queues the periodic checkpoint and finalization
  drains it before publishing; direct checkpoint API remains synchronously durable)
- `SeinARTS.Integration.Network.Replay.ReplayScheduledCheckpointFinalizationDrainsBackgroundDurability-20260812-003938-6242ee02`
  (1 passed after adding named CPU trace scopes around snapshot capture and
  checkpoint-envelope encoding)
- `SeinARTS.Integration-20260812-002207-f95c3873` (All, 21 passed)
- `SeinARTS.Integration-20260812-002231-537e081c` (Framework, 15 passed)
- `SeinARTS.Unit-20260812-025448-95da5c71` (All, 428 passed after relationship
  cache and Movement+ telemetry adversarial fixes)
- `SeinARTS.Determinism-20260812-025546-4bda5386` (All, 40 passed)
- `SeinARTS.Editor-20260812-025710-44b271d0` (All, 37 passed; Movement+
  presentation getters are rejected in deterministic movement Blueprints)
- `SeinARTS.Unit-20260812-025734-79f0e84e` (Framework, 410 passed with every
  production extension physically disabled)
- `SeinARTS.Determinism-20260812-025825-f1da8e91` (Framework, 30 passed)
- `SeinARTS.Integration-20260812-025855-65a4e432` (Framework, 15 passed)
- `SeinARTS.Sim-20260812-025918-98949e0d` (Framework, 27 passed)
- `SeinARTS.Editor-20260812-025940-28d50972` (Framework, 36 passed)
- `SeinARTS.Editor.MovementPlus.Telemetry-20260812-031845-970dadaa` (All,
  2 passed; movement and Ability graphs both reject presentation-only telemetry)
- `SeinARTS.Unit.MovementPlus.Telemetry-20260812-031948-68816d9d` (All,
  3 passed; exact long-run wheel phase and repeated rollover included)
- `SeinARTS.Editor-20260812-032010-37667699` (All, 38 passed)
- `SeinARTS.Unit-20260812-032031-07ac28fa` (All, 428 passed)
- `SeinARTS.Editor-20260812-032127-798cf7c8` (Framework, 36 passed)
- `SeinARTS.Unit-20260812-032158-993b7d71` (Framework, 410 passed)
- `SeinARTS.Integration.Network.Replay.ReplayPeriodicCheckpointSessionStaysBoundedAndEveryCheckpointSeeksExact-20260812-205135-f0f4323a`
  (1 passed; 25 periodic checkpoints, every exact seek/root, full command replay, and stable cache
  payload/allocation across cold/hot restore)
- `SeinARTS.Integration.Network.Replay-20260812-205238-97d76772` (Framework,
  16 passed after repeated-checkpoint lifecycle qualification)
- `SeinARTS.Integration-20260812-205322-54064b7e` (Framework, 18 passed)
- `SeinARTS.Integration-20260812-205410-9b12aff9` (All, 24 passed)
- `SeinARTS.Integration.Network.Replay.ReplayCheckpointWorkersOverlapTicksAndCatchUpWithoutManualDrains-20260812-215135-b0b0321f`
  (1 passed; eight controlled 128-entity cycles advanced fixed ticks across internal encode and
  file-append midpoints with exact resident-byte accounting and callback-only catch-up)
- `SeinARTS.Integration.Network.Replay-20260812-215224-211024c6` (Framework,
  17 passed after controlled replay worker-overlap qualification)
- `SeinARTS.Integration-20260812-215854-4c2bb8a8` (Framework, 19 passed with
  the checked-in floor bound to implementation commit `f85bef0`)
- `SeinARTS.Integration-20260812-220022-750991d8` (All, 25 passed with the
  checked-in floor bound to implementation commit `f85bef0`)
- `SeinARTS.Determinism-20260812-032246-84a12b7c` (All, 40 passed)
- `SeinARTS.Determinism-20260812-032321-853573ea` (Framework, 30 passed)
- `SeinARTS.Integration-20260812-032358-75874dd5` (All, 21 passed)
- `SeinARTS.Integration-20260812-032418-f24f1163` (Framework, 15 passed)
- `SeinARTS.Sim-20260812-032437-ab6ff89c` (All, 30 passed)
- `SeinARTS.Sim-20260812-032456-cabb28eb` (Framework, 27 passed)
- `SeinARTS.Perf-20260812-032714-c7d2a57a` (All, 2 passed; collision medians
  1.299/3.230/7.251 ms and dense Cover assignment 12.097 ms)
- `SeinARTS.Perf-20260812-032731-e1a258c9` (Framework, 1 passed; collision
  medians 1.401/3.035/7.494 ms)
- `SeinARTS.Integration.Network.Replay-20260812-050833-74f234d4` (Framework,
  14 passed; synthetic background-append failure and real read-only storage denial both stop
  recording without publishing unpersisted progress or removing the partial journal)
- `SeinARTS.Integration-20260812-050911-7f799ba4` (Framework, 16 passed)
- `SeinARTS.Integration-20260812-050933-e6fc1329` (All, 22 passed)
- `SeinARTS.Unit.Formation-20260812-081600-82576472` (Framework, 5 passed;
  formation separation and execution contracts remain green after trace instrumentation)
- `SeinARTS.Unit.Cover-20260812-081627-02d6127e` (All, 10 passed; Cover assignment,
  canonical-state, and restore contracts remain green)
- `SeinARTS.Perf-20260812-081656-9a968ce2` (All, 3 passed; collision medians
  1.257/3.114/7.214 ms, worst-case 128x128 Cover assignment 11.998 ms, and the
  public 128-member formation layout 1.255/1.337 ms median/p95 coverless versus
  3.181/3.324 ms with 16 providers, 128 accepted slots, and every member snapped)
- `Scripts/Build.ps1 -Target SeinARTS -Config Shipping` (2026-08-12; Net rebuilt and Shipping linked
  successfully with the development-only replay fault seam compiled out)
- `Scripts/PackagePlugins.ps1 -PackageOnly` (2026-08-12; all five UE 5.8 Marketplace-style Editor,
  Development Game, and Shipping Game builds passed; exact `0.0.120` archive preflight passed)
- `Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1 -Profile All -ArtifactDirectory .dist -SkipClientServer`
  (run `5136cb01289342429a61f491bc78e5d0`; all five fresh exact-ZIP profiles passed Editor,
  Shipping, release-mode installation diagnostics, consumer-owned manifest/map load,
  cook/package, and packaged startup; Framework also passed the multiplayer/reconnect/capability/
  replay leg at tick 239, root `1CC3EC718160314CD4433EB0DCCB1C10`)
- `SeinARTS.Integration.Network.Replay.ReplaySlowCheckpointStoragePreservesBoundedOrderedDurability-20260812-184415-e3dcf255`
  (1 passed; a held periodic checkpoint accumulated eligible turns to the exact resident bound,
  published no false bytes/counters, forced the production wait, then drained and loaded in order)
- `SeinARTS.Integration.Network.Replay-20260812-183830-21bc9da2` (Framework, 15 passed)
- `SeinARTS.Integration-20260812-183947-0e277b66` (Framework, 17 passed)
- `SeinARTS.Determinism-20260812-184013-e47d2779` (Framework, 30 passed)
- `SeinARTS.Integration-20260812-184042-76386474` (All, 23 passed)
- `Scripts/Build.ps1 -Target SeinARTS -Config Shipping` (2026-08-12; Net rebuilt and
  `SeinARTS-Win64-Shipping.exe` linked with the slow-storage test gate compiled out)
- `SeinARTS.Unit.Entity-20260812-194058-7dbdb9aa` (Framework, 13 passed; includes forced
  component-storage mutation/topology revision-wrap coverage)
- `SeinARTS.Determinism.Snapshot.Bootstrap-20260812-194924-deb10fd8` (Framework, 1 passed;
  post-restore storage cache is cold once, then fully reusable)
- `SeinARTS.Perf.Replay.Checkpoint-20260812-194951-a42b1703` (Framework, 1 passed;
  snapshot-capture medians 2.037/7.815/15.288 ms at 100/500/1,000 moving entities)
- `SeinARTS.Unit-20260812-195052-29dc4644` (All, 432 passed)
- `SeinARTS.Unit-20260812-200210-1f225e9f` (Framework, 414 passed)
- `SeinARTS.Integration-20260812-195154-136a8c4b` (All, 23 passed)
- `SeinARTS.Determinism-20260812-195225-03dccd18` (All, 40 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260812-195507-650042ae` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260812-195525-422a8b1a`
  (fresh processes; canonical roots and raw poses matched for all 120 ticks)
- `Scripts/Build.ps1 -Target SeinARTS -Config Shipping` (2026-08-12 after checkpoint-cache
  hardening; all touched production modules rebuilt and `SeinARTS-Win64-Shipping.exe` linked)
- `Scripts/Build.ps1 -Target SeinARTS -Config Shipping` (2026-08-12 after controlled replay
  worker-overlap qualification; Net rebuilt and linked with midpoint hooks compiled out)
- `SeinARTS.Unit-20260813-070821-4393ec61` (Framework, 427 passed) and
  `SeinARTS.Unit-20260813-071047-b4979d0f` (All, 445 passed)
- `SeinARTS.Determinism-20260813-070909-4eda967e` (Framework, 31 passed) and
  `SeinARTS.Determinism-20260813-071154-8242e50a` (All, 41 passed)
- `SeinARTS.Integration-20260813-070938-96575b09` (Framework, 19 passed) and
  `SeinARTS.Integration-20260813-071231-a73e1ffd` (All, 25 passed)
- `SeinARTS.Sim-20260813-071025-b907e33c` (Framework, 27 passed) and
  `SeinARTS.Sim-20260813-071317-747a5987` (All, 30 passed)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260813-071345-8b6b8314` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260813-071402-23fe6f4b`
  (fresh processes; canonical roots and raw poses matched for all 120 ticks)
- `Scripts/Build.ps1 -Target SeinARTS -Config Shipping` (2026-08-13 after containment
  integrity hardening; 152 production actions compiled and `SeinARTS-Win64-Shipping.exe` linked)

## Integration-candidate progress

- Async path requests above the per-tick budget now stay queued in canonical handle order instead
  of being discarded.
- Ready async paths now survive unrelated drains until consumed, superseded, or explicitly
  cancelled.
- `USeinMoveToAction` cancels its continuation before Blueprint-capable terminal hooks, preventing
  both leaks and re-entrant cancellation of a newly issued order.
- Dynamic FoW blockers retain exact per-layer top heights on mixed-layer overlaps without paying
  for seven additional dense height grids; only exceptional cells allocate sparse layer tops.
- Extents-authored FoW sources now include their local Z offset in the world-space blocker top,
  and terrain vision multipliers scale the active range parameter for radial, rectangle, and cone
  shapes.
- `FSeinNavAgentProfile` now centralizes an entity's nav-layer mask, forbidden terrain, wall
  padding, whole compound-collider radius, and classification tags. Initial paths, repaths,
  escape/floor probes, collision containment, formation projection, Movement+ maneuver probes,
  and Blueprint requests with a requester use that same policy.
- Forbidden terrain is hard route topology and footprint clearance. An authoritative destination
  may override only the coarse static bake; it cannot bypass forbidden terrain or a dynamic
  blocker. Dynamic blockers can change the route while remaining transient for fundamental
  command-admission reachability.
- A* caches static connectivity by exact agent profile in a bounded, configurable cache and warms
  profiles for path-requiring entities. Capacity affects rebuild frequency, not simulation results.
- Legacy `AgentTags` remain a classification seam, not terrain topology in the shipped A*. Cover's
  final destination post-processing is not yet requester/context-rich; that remains part of the
  shared tactical allocation work rather than being hidden inside navigation.
- Focused async lifecycle, per-agent path/clearance/projection, and navigation canonical-state
  regressions passed before the full gates.
- Ability activation now publishes its exact pool identity before `OnActivate`; end/cancel removes
  it before refund, latent teardown, or `OnEnd`. Direct, command, passive-grant, and re-entrant
  callback paths therefore share one lifecycle owner.
- The singular primary slot now fails closed when another live primary is still active instead of
  silently orphaning the older ability from ticking, cancellation, and snapshots. Existing broker
  and cancellation-tag arbitration remains the explicit replacement policy.
- Snapshot admission enforces the bidirectional activity contract: every active ability must be
  indexed in the correct primary/passive role and every indexed ability must be active. Passive
  execution state survives exact restore through the registered pool codec.
- Per-activation cooldown-start state resets correctly, so `OnEnd`-timed cooldowns start on every
  activation rather than only the first.
- Checkpoint-capable Blueprint ability continuations are compile/save admitted through explicit
  supported node/codec contracts; unsupported continuation shapes fail before a live reconnect.
- Mutable Level Data coverage and formation/resolver statefulness are explicit admission contracts.
  The built-in formation and collision implementations are stateless by contract, while custom
  stateful implementations must declare and register their state ownership.
- Movement+ coverage-provider withdrawal is atomic. Live unload removes the provider's complete
  claim set before one registry refresh, so no transient partial ownership can be observed.
- `Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1` creates disposable external consumers from
  selected source plugins, generates a consumer-owned manifest and map, rejects host-content
  dependencies, and exercises uncooked load plus packaged Shipping startup.
- The consumer matrix has direct Framework-only, Cover-only, Squad-only, Movement+-only-extension,
  and Full profiles. This proves both parents survive physical bridge stripping and the bridge
  remains loadable when its required parents are present.
- The Framework profile now owns a packaged runtime qualification subsystem only in its disposable
  generated consumer. It proves a real listen-server/client match, requires a successful two-peer
  world-root comparison before topology changes, then exercises forced checkpoint resync,
  disconnect/reconnect with authorship withheld until exact catch-up, streaming replay publication,
  checkpoint seek, and terminal canonical-root agreement.
- Pure lobby maps now materialize relays from the lobby's final authoritative controller/slot map
  before protocol preparation. Reconnect reuses a dropped slot's retained relay by transferring
  ownership to the returning controller; a genuinely Connected duplicate still fails closed.
- Restore generation/lease checks execute in Shipping instead of hiding required work inside
  compiled-out `check`/`checkf` expressions. This closes the packaged Fog restore crash found by
  the clean-consumer harness and preserves fail-closed behavior if a verified provider vanishes.
- The non-shipping extension suite now owns a deterministic Movement+ Vehicle Gym. Its real
  production planners/drivers cover representative wheeled/tracked archetypes, U-turns, reverse,
  K-turns, corridor escape, S-bends, repath and order replacement, formation-facing settle,
  recovery, and mixed infantry/vehicle collision. Active arc, reverse, recovery, and congestion
  checkpoints continue with exact roots after restore. Human feel, scale, and combined
  real multi-client PIE remain explicit gates in `.agents/VEHICLE_GYM.md`.

## Current development state

The default avoidance implementation now lives in a private kernel split into explicit gather,
cohesion, idle-dodge, neighbor-response, gap-resolution, per-mover, output, and publication stages.
The public policy UObject remains a one-call adapter, and the ordered neighbor gate pipeline stays
intact rather than being fragmented into stateful abstractions. Independent adversarial comparison
found the original and extracted bodies token-equivalent after the required policy qualification;
fixed-point evaluation order, candidate and neighbor ordering, diagnostics, deferred writes, and
canonical serial publication are unchanged. Focused avoidance passed 6/6, Unit passed 434/434
Framework and 452/452 All, Sim passed 34/34, Determinism passed 33/33, fresh-process serial/parallel
roots and poses agree for all 120 ticks, and Development plus Shipping builds succeeded. No public
API, canonical state, tuning, schema, behavior revision, or movement behavior changed.

A* partial-path, unreachable-segment, and clearance reporters now live in one adjacent private
implementation include instead of interrupting the search and path-pipeline bodies. They remain in
the same translation unit behind the same non-shipping declarations, calls, and compile guard.
Independent token-level review found their branches, log strings, levels, gates, iteration order,
and helper visibility unchanged. The focused Navigation Unit suite passed 34/34 both before and
after extraction, Framework Unit passed 428/428, Framework Determinism passed 33/33, fresh-process
serial/parallel roots and poses agree for all 120 ticks, and Development plus Shipping builds
succeeded. No public API, path output, canonical state, tuning, schema, or behavior revision changed.

Command-broker idle re-seek is now isolated from the broker tick in one private deterministic
kernel. The extraction stages loose-candidate collection, broker pairing/traffic/release work, and
post-iteration broker creation while leaving every persistent field in the existing canonical
components. It changes no public API, state codec, behavior revision, command timing, or tuning.
Independent adversarial comparison against the prior inline body found no semantic defect; its
coverage findings were closed with exact tick-boundary assertions, a required free-slot rematch
around an in-flight claimed slot, and multi-candidate storage-growth coverage. Focused re-seek is
green at 7/7, Framework Sim at 34/34, Framework Determinism at 33/33, fresh-process serial/parallel
roots and poses agree for all 120 ticks, and Development plus Shipping builds succeeded.

The completed pair-capability and Movement+ telemetry milestone adds a directional,
source-attributed capability ledger for ordered player pairs. Team IDs seed the existing friendly
default, while exact grants become authoritative after bootstrap. The ledger participates in
initial compatibility, canonical roots, snapshot/reset/restore, replay command execution, and
read-only command-authority queries. Existing UI relation values remain a compatibility projection;
no treaty or authoritative posture model was added. The world snapshot and canonical envelope
semantics are now v15, so older peers fail compatibility instead of accepting the new ledger and
Squad reinforcement schemas.
Initial-state, exact-root, and maintained-root schemas were advanced with the new bytes; roots fail
closed if the authoritative source records and derived effective cache ever disagree. Grant/revoke
now repair a drifted derived cache before mutating, then reject if authoritative records themselves
remain invalid; focused coverage exercises missing, undercounted, overcounted, and phantom entries.

Movement+ now publishes typed, render-only steering angle, yaw rate, normalized throttle/brake,
wrapped wheel rotation, and left/right track velocity through `USeinMovementPlusBPFL`. Settled
post-collision transforms drive wheel/track motion while movement-driver velocity drives
throttle/brake, so correction displacement cannot masquerade as input. Missing movement instances,
class swaps, and snapshot restore clear stale telemetry. Raw render state is not Blueprint-visible,
and validation blocks presentation-only getters from deterministic movement and Ability graphs.
Focused relationship Unit/Determinism tests, telemetry tests, replay-file continuation, bounded
reconnect transfer, and both broad test profiles are green. A generated downstream Shipping
Movement+ consumer now also executes a real long-range wheeled command through two peers, resync,
physical reconnect, exact terminal movement state, and checkpoint-seek replay. That qualification
exposed and closed fixed-point squared-distance wrap beyond the 32.32 squared range, an unopened
zero-author reconnect gate, and listen-authority dependence on local Client RPC loopback.
The human PIE animation/performance matrix remains open; the supplied two-player PIE run found no
framework regression.

Containment now rejects cyclic admission, signed-load overflow, non-reciprocal occupants, stale
visual/attachment slots, and malformed ancestry. One linear validator owns both destination
preflight and whole-world checks at bootstrap, routine/full canonical roots, snapshot capture, and
staged restore before authoritative mutation. Hierarchy queries terminate defensively on malformed
graphs, and exit/death load cleanup avoids signed arithmetic overflow. Focused fresh-world coverage
restores nested attachment/visual state, performs matching post-restore containment mutations,
continues both worlds to exact roots, and proves a corrupt restore leaves root, tick, load, slot,
attachment, and future continuation unchanged. This closes structural-state integrity, not the
remaining representative garrison/transport command/replay, large-fan-out, multi-client PIE, or
shared observer/team presentation work.

Replay v9 automatic periodic checkpoints encode and durably append through the existing ordered
background pipeline. Frame digest/sequence and checkpoint persistence diagnostics commit on the game
thread only after worker success. Periodic snapshot capture remains synchronous, but unchanged
component-storage blobs are reused from a process-local cache only when topology and mutation
revisions match and no mutable payload pointer has ever escaped. Every snapshot owns its byte copy,
retention is capped at 64 MiB per world, revision wrap disables reuse, and restore clears the prior
timeline cache. The moving-storage capture curve improved from 4.033/14.245/27.831 ms to
2.037/7.815/15.288 ms at 100/500/1,000 entities without changing snapshot or canonical schemas.
Mandatory initial/direct writes, final publication, and pressure-forced drains remain synchronous.
File-backed integration now holds the periodic checkpoint after the append file is open and positioned
but before byte writes, advances the real fixed-tick world while eligible turns accumulate
to the exact resident bound, proves no false bytes/counters or frame overtaking, then forces the
production wait and validates the published journal after release. The >64 MiB bounded-memory proof,
asynchronous failure, and real write denial remain green. New Insights scopes separate checkpoint
capture cache hits/live serialization/encode, background/synchronous durable append, and game-thread
append waits. Accelerated real-file automation now covers 449 turns (448 across the periodic cycles
plus one journal catch-up turn) and 64 natural periodic
checkpoints over 128 entities with eight full-GC boundaries, sampled exact seek/root/capability
checks, full playback, and process working-set/private-commit/late-growth sentinels. Clean commit
`8178dec` passed attempt `SeinARTS.Perf.Replay.OperationalSoak-20260813-133014-e47a257d`, writing
135,244,673 bytes with 135.552 ms p50, 203.523 ms p95, and 211.276 ms maximum checkpoint latency
across eight guaranteed GC/exclusion overlaps. Final and observed-peak working-set growth were
12.24 MiB, private-commit growth was 9.06 MiB, and late growth was 8.56 MiB. A configured
64 MiB file-policy exhaustion test stops recording without deleting the
partial, then replays that partial to the exact last durable tick, capability state, and canonical
root. Full `default,memory,metadata` tracing over the operational-soak bookmarks attributed
8,388,688 retained replay bytes in the clean pre-fix run, including one 8,388,608-byte checkpoint
envelope buffer copied out of its completed future. Consuming completed futures, making checkpoint
buffer ownership explicit, and draining only matching worker operations removed the retained
production replay allocations. The production `Qualified` receipt for that clean commit measures
the 56 periodic checkpoints after an eight-checkpoint warmup: 1,295 retained allocations and
48,393,922 bytes overall, all with callstacks, but zero production `SeinARTS/Replay/*` allocations
or bytes against the fixed 4 KiB ceiling. Two allocator-attribution sentinel rows totaling
35,651,648 bytes are validated separately and excluded from the production budget. The 21 Insights
heap-reconstruction warnings occurred only during startup, before the measurement interval. The
receipt-bound exporter validates every attempt/build/trace/analyzer input and publishes
CSV/log/receipt artifacts only on success. This closes the local warmed allocator-retention gate.
Multi-hour real-device latency/hitch and allocator-high-water distributions, platform storage
matrices, and true OS disk-full behavior remain open.

A compressed 128-entity repeated-lifecycle fixture now captures 25 periodic checkpoints plus the
required initial checkpoint through the real ordered background encode/append bodies. Alternating
authoritative pair-capability commands leave a non-empty terminal state; every checkpoint exact-seeks
to its source capability state and canonical root, and full tick-zero playback observes every command
transition before reaching the same terminal root. Cache payload bytes, `TArray` allocation, entry
count, and cold-miss/hot-hit behavior remain exact across each cycle and fresh-world restore. The
fixture intentionally forces and waits for maintenance each cycle. A separate eight-cycle,
128-entity fixture pauses checkpoint encode after real snapshot payload serialization and pauses
checkpoint append after the real file is open and positioned at its exact expected offset. Fixed
ticks and alternating authoritative mutations continue for two turns in each held stage; exact
resident bytes, file/durability non-advancement, production-callback catch-up, checkpoint index,
seek/root/capability state, and full playback remain exact. These controlled internal-midpoint tests
are complemented by the accelerated operational soak above; they still do not replace multi-hour
real-device timing and allocator-high-water, platform storage matrices, or true OS disk-full evidence.

The supplied 2026-08-11 two-player PIE log contains two healthy sessions: lockstep configuration
and participant roots agree throughout, with no gate stall, persistent incomplete turn, retransmit,
resync, desync, ensure, crash, or runtime error. The observed command latency matches the configured
30 Hz simulation, 10 turns/second, and `InputDelayTurns=2` (roughly 200-300 ms including turn-boundary
alignment), not a hidden PIE-only stall. Straggler telemetry now records a participant only when an
incomplete turn actually reaches the execution gate; ordinary local RPC ordering no longer produces
the misleading 100% "last to submit" warning. The checked-in debug fixed session seed is also reset
to zero so packaged matches do not reuse seed 12345.

Public adoption now has a read-only installation diagnostic with stable result codes and JSON
output. It validates UE 5.8 identity, exact production-plugin dependency closure, recursively
discovered duplicate installs, release/source cohort consistency, test-plugin stripping, canonical
project-owned manifest containment, strict SemVer, and clean Git source identity where verifiable.
The adversarial Windows PowerShell 5.1 fixture rejects nested duplicates, unexpected dependencies,
leading-zero prerelease SemVer, and `/Game/` traversal. Framework source consumer run
`a86ac77e16904f1da92928f56ffe754d` passed Editor/Shipping builds, manifest/map checks, and its
schema-5 diagnostic receipt (`A86CC655C05EE1B3350BEE87E072493A39961E17145E918F312C7C78CBADBE89`).
Exact ZIP preflight also passes with each snapshot held read-locked through validation, extraction,
and hashing. Release evidence now carries each profile's bound installation receipt, and the release
gate runs fast Windows PowerShell 5.1 pass/adversarial fixtures before host qualification.
Public setup documentation now includes a first-skirmish path grounded in the actual project
settings, entity bridge component template, level-volume bake, player-slot bootstrap, manifest, and
two-player verification contracts; host example content is explicitly non-shipping reference data.

Cover remediation now uses one Cover-owned exact assignment planner from both ordinary and Squad
resolver adapters. It removes the duplicated greedy allocation and closes local starvation while
preserving the fixed-point objective exactly; behavior revisions were advanced so mixed builds and
old snapshots cannot accept the changed destination behavior. Focused Cover Unit tests pass 10/10,
the all-extension Unit suite passes 428/428, and the latest dense 128x128 solver-only stress test
passes at 11.998 ms average. The real public layout path is now a separate 64/128-member performance
sentinel with exact repeated output and unchanged canonical roots; its dense 128-member workload
snaps every member at 3.181 ms median / 3.324 ms p95. Selection, quality, renderer, navigation, and
complete-frame costs remain an Insights gate. Cross-broker slot contention, canonical reservations,
and exact preview-artifact command admission remain open and require the explicit conflict policy
recorded in the roadmap.

The UE 5.8 full-game formation-preview gate is now closed for the current one-world Sandbox workload
at 100 owned movers. Formation projection ignores only the ordered group's exact generational dynamic
blocker owners, and navigation maintains a derived sparse cell index for unrelated runtime blockers
instead of rerasterizing their shapes on every candidate query. Repeated matched captures reduced the
stable refresh scope from 11.490 to 4.518 ms and measured a 1.60-2.86 ms complete-frame preview delta.
The exhaustive index-equivalence fixture covers shapes, layer masks, owner generations, reload, and
group exclusion; Framework Unit 413/413, Integration 16/16, Determinism 30/30, and independent
120-frame serial/parallel process traces pass with zero canonical-root or pose-digest differences.

Squad reinforcement now treats the slot declaration index as exact runtime identity and tags as
canonical query metadata. Requests carry monotonic IDs plus snapshotted payer/cost, enqueue is
atomic, cancellation exactly reverses the committed deduction, and completion maintains slot,
member, broker, leader, cache, and cooldown invariants. Snapshot v15 preflight rejects allocator,
queue, slot, member, or broker drift before commit. Canonical tag selection uses exact lexical tag
names in runtime and restore validation, existing squad brokers cannot self-cull ahead of paid
queues, and charge/refund arithmetic rejects malformed or overflowing fixed-point operations
atomically. Focused accounting/membership tests pass 3/3,
system completion/failure-retry passes 2/2, fresh-world snapshot continuation passes 1/1, and the
broad All and Framework profiles are green at Unit 428/410, Determinism 40/30, Integration 21/15,
plus the cross-system All simulation aggregate at 30/30. Broker callback fixtures now author their
resolver scenario state before sealing tick zero, so the aggregate also proves bootstrap isolation.
Explicit
squad destruction refunds, queue replacement, wipe/recreation, and retreat remain product-policy
work rather than implicit cleanup behavior.

Move To's interval/off-path repath stage now lives in one private member instead of interrupting
the main latent-action tick. Canonical action fields, planner classification, force consumption,
timer/reset ordering, callback order, and pre-movement mutation timing are unchanged. Independent
adversarial review found no production defect; its coverage findings were closed with 13 focused
behavior tests plus a fresh-world snapshot continuation that crosses a real repath boundary. The
all-extension Unit/Sim/Integration/Editor/Determinism profiles pass at 458/50/26/39/48, fresh-process
serial/parallel roots and poses match for all 120 ticks, and Development plus Shipping builds
succeeded. No public gameplay API, reflected state, codec schema, tuning, behavior revision, or
movement behavior changed.

## Evidence limits

Automation proves the tested contracts, not full production readiness. The remaining human/runtime oracles include:

- Multi-world PIE performance and interaction under the intended listen-server/client topology.
- Movement, collision settling, formation preview, cover arrival, animation recovery, fog transitions, UI, and input feel.
- Development Client and Dedicated Server target compilation: the installed Epic launcher engine
  rejects Client targets before project compilation (`Client targets are not currently supported
  from this engine distribution`). These gates require a source/installed engine with target support
  and belong in CI; they are not represented as green locally.
- Scale beyond the current Sandbox population, especially dense moving combat and continuous large-formation previews.

## Immediate working boundary

The old audit/remediation campaign is closed as a source of truth. Its durable results are
consolidated in this directory. The Cover/Squad topology decision is closed through the opt-in
bridge plugin, and the supported local packaged multiplayer/replay consumer gate is green. Release
tooling now binds skipped test runs to source fingerprints and exact test-DLL hashes, validates
packaged ZIP structure and hashes, and can compile every shipped public header independently. The
clean committed `0.0.178` cohort from source commit `9d7efc3708cc07dfbb76c3f6f8c528b1290ed264`
passed the complete exact-ZIP five-profile consumer matrix on UE 5.8.1: fresh Editor and Shipping,
325/340/333/337/362 independent public headers, release diagnostics, uncooked load, cook/package,
real Shipping startup, and the Framework plus Movement+ packaged multiplayer/replay legs. The
remaining Integration Candidate release evidence is the Development Client/Dedicated Server gate
on a capable engine distribution plus the explicit PIE oracles. Local implementation can proceed
to the Vehicle Gym and squad-tactical gameplay-backbone qualification without pretending those external
gates are green.
