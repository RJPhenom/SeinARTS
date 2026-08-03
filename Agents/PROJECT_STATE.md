# SeinARTS Project State

**State date:** 2026-08-03
**Current branch:** `codex/integration-candidate`
**Stabilization commit:** `27cb490` (`Stabilize post-audit performance and determinism`)
**Cleanup boundary:** `1bebf91` (`Clean and harden the post-audit baseline`)
**Content/consumer boundary:** `1438051` (`Add clean downstream consumer verification`)
**Remote posture:** local `main` and the integration-candidate branch have not been pushed in this pass.

## Stabilized capability boundary

The post-audit performance/remediation work is now committed and fast-forwarded onto `main`. It includes:

- Sparse authoritative component traversal in recurring systems.
- Exact settled-work exits and reduced narrow-phase work in the collision resolvers.
- Gauss-Seidel as the project default collision policy; deterministic Jacobi remains selectable and covered.
- Incremental BLAKE3-128 routine world roots, sealed only at protocol checkpoint boundaries, with forced-rebuild verification.
- Correct peer-report liveness based on the frozen epoch participant manifest.
- Frame-coalesced presentation updates while network/replay observers remain tick-exact.
- Event/cadence-driven fog presentation and cached navigation debug geometry.
- Cached selection ability aggregation and bulk/linear-time minimap fog generation.
- Native skeletal-mesh and hardware-ray-tracing policies appropriate for RTS crowds, with per-actor opt-outs.
- The Widget Blueprint source-asset class moved to the UncookedOnly authoring module, fixing uncooked standalone loading.
- Streaming replay v9 journals and authenticated checkpoint-plus-command-tail resync from the prior remediation commits.
- Strict PIE manifest freshness made opt-in; runtime, cook, replay, snapshot, and peer compatibility protections remain.

## Baseline cleanup

- Historical audit/report artifacts were removed; `Docs/` is physically empty and reserved for deliberate product documentation.
- Durable current state was consolidated under `Agents/`; generated PDF output now belongs outside the repository.
- Empty scratch directories, duplicate/template config entries, stale comments, and dead source surfaces were removed.
- Removed reflected/source surfaces had no source, config, or binary-asset consumers: `ESeinElevationMode`, `FFixedBounds`, `SeinTime`, `FSeinCapturePointData`, `FSeinFootprintData`, and `USeinLevelLoS`.
- Designer-facing reflected APIs were not removed merely because native code does not include them. `UMathBPFL`, `FSeinBasicMatchSettings`, and `FSeinGarrisonSpec` remain intentional public authoring surfaces.
- Example maps, gameplay Blueprints, and mannequin content now belong to the host project under `/Game/SeinARTSExamples`; the base plugin retains only its runtime-owned UI content. Framework packages no longer reference host `/Game/SeinARTS` content or opt-in extension packages.
- The unreachable simulation-content manifest commandlet was removed. Clean consumers invoke the registered editor console command through Python instead of relying on a commandlet class hidden in a `PostEngineInit` module.
- Fixed-vector absolute-component queries now preserve 32.32 values and saturate the unrepresentable absolute minimum.
- Stateful PRNG spatial/rotator draws are explicitly ordered across compilers; `RandomRotator` converts its radian samples to the degree-based rotator contract.
- Fixed ray/box queries no longer impose an arbitrary 10,000-unit hit ceiling.

## Current verification

The latest integration-candidate source was rebuilt and re-run after closing async navigation,
FoW state, per-unit navigation policy, ability/passive ownership, checkpoint-authoring admission,
formation/resolver state coverage, provider teardown, and downstream content ownership:

| Gate | Result |
|---|---:|
| `SeinARTS.Unit`, profile All | 411 passed, 0 failed |
| `SeinARTS.Unit`, profile Framework | 403 passed, 0 failed |
| `SeinARTS.Integration`, profile All | 15 passed, 0 failed |
| `SeinARTS.Integration`, profile Framework | 14 passed, 0 failed |
| `SeinARTS.Determinism`, profile All | 20 passed, 0 failed |
| `SeinARTS.Determinism`, profile Framework | 19 passed, 0 failed |
| `SeinARTS.Editor`, profile All | 36 passed, 0 failed |
| `SeinARTS.Editor`, profile Framework | 36 passed, 0 failed |
| Fresh-process collision trace | serial and parallel roots/poses identical for 120 ticks |
| `SeinARTSEditor Win64 Development` | succeeded / target current |
| `SeinARTS Win64 Shipping` | succeeded / target current |
| Clean consumer: Framework | fresh Editor + Shipping build, exact map load, cook/package, real Shipping startup passed |
| Clean consumer: Framework + Movement+ | fresh Editor + Shipping build, exact map load, cook/package, real Shipping startup passed |
| Clean consumer: all production plugins | fresh Editor + Shipping build, exact map load, cook/package, real Shipping startup passed |
| Generated simulation-content manifest | 10 contributors, 93 records, digest `0E018D9C38BD9389BF25B7648F54A87B` |
| Staged diff validation | no whitespace errors; line-ending notices only |

Latest local evidence is under ignored `Saved/Automation/`:

- `SeinARTS.Unit-20260803-024142-7baac61d` (All)
- `SeinARTS.Unit-20260803-024239-0dc98cd8` (Framework)
- `SeinARTS.Integration-20260803-024324-b7757f8c` (All)
- `SeinARTS.Integration-20260803-024344-95b4351a` (Framework)
- `SeinARTS.Determinism-20260803-024407-b7abece3` (All)
- `SeinARTS.Determinism-20260803-024431-0c094bbf` (Framework)
- `SeinARTS.Editor-20260803-025624-690e4c29` (All; checked-in floor)
- `SeinARTS.Editor-20260803-025638-e2fb00c2` (Framework; checked-in floor)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260803-024504-090e4e16`
- `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260803-024518-e35d5550`

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
- `Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1` creates disposable external consumers from
  selected source plugins, generates a consumer-owned manifest and map, rejects host-content
  dependencies, and exercises uncooked load plus packaged Shipping startup.

## Evidence limits

Automation proves the tested contracts, not full production readiness. The remaining human/runtime oracles include:

- Multi-world PIE performance and interaction under the intended listen-server/client topology.
- Movement, collision settling, formation preview, cover arrival, animation recovery, fog transitions, UI, and input feel.
- Multi-process reconnect, replay seek/load, and cooked client/server behavior.
- Development Client and Dedicated Server target compilation: the installed Epic launcher engine
  rejects Client targets before project compilation (`Client targets are not currently supported
  from this engine distribution`). These gates require a source/installed engine with target support
  and belong in CI; they are not represented as green locally.
- Scale beyond the current Sandbox population, especially dense moving combat and continuous large-formation previews.

## Immediate working boundary

The old audit/remediation campaign is closed as a source of truth. Its durable results are consolidated in this directory. The next non-PIE source decision is whether Cover's Squad bridge becomes a separate opt-in plugin or Squad becomes a required Cover dependency; the current descriptor is not physically strip-safe. All other work should follow the ordered roadmap from live code.
