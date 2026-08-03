# SeinARTS Project State

**State date:** 2026-08-02
**Current branch:** `codex/integration-candidate`
**Stabilization commit:** `27cb490` (`Stabilize post-audit performance and determinism`)
**Cleanup boundary:** `1bebf91` (`Clean and harden the post-audit baseline`)
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
- Fixed-vector absolute-component queries now preserve 32.32 values and saturate the unrepresentable absolute minimum.
- Stateful PRNG spatial/rotator draws are explicitly ordered across compilers; `RandomRotator` converts its radian samples to the degree-based rotator contract.
- Fixed ray/box queries no longer impose an arbitrary 10,000-unit hit ceiling.

## Current verification

The latest integration-candidate source was rebuilt and re-run after closing the async navigation
continuation lifecycle and the known FoW blocker-height/stamp-scaling defects:

| Gate | Result |
|---|---:|
| `SeinARTS.Unit`, profile All | 391 passed, 0 failed |
| `SeinARTS.Unit`, profile Framework | 383 passed, 0 failed |
| `SeinARTS.Integration`, profile All | 15 passed, 0 failed |
| `SeinARTS.Integration`, profile Framework | 14 passed, 0 failed |
| `SeinARTS.Determinism`, profile All | 20 passed, 0 failed |
| `SeinARTS.Determinism`, profile Framework | 19 passed, 0 failed |
| Fresh-process collision trace | serial and parallel roots/poses identical for 120 ticks |
| `SeinARTSEditor Win64 Development` | succeeded / target current |
| `SeinARTS Win64 Shipping` | succeeded / target current |
| Staged diff validation | no whitespace errors; line-ending notices only |

Latest local evidence is under ignored `Saved/Automation/`:

- `SeinARTS.Unit-20260802-222947-1673cc5e` (All)
- `SeinARTS.Unit-20260802-223506-791aa8df` (Framework)
- `SeinARTS.Integration-20260802-223244-69f8359b` (All)
- `SeinARTS.Integration-20260802-221111-4daa9477` (Framework)
- `SeinARTS.Determinism-20260802-223309-55b25461` (All)
- `SeinARTS.Determinism-20260802-221127-c0c69aae` (Framework)
- `SeinARTS.Determinism.Process.SerialCollisionTrace-20260802-223432-00b8b11f`
- `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260802-223446-be58c9e3`

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
- Focused async lifecycle and navigation canonical-state regressions passed before the full gates.

## Evidence limits

Automation proves the tested contracts, not full production readiness. The remaining human/runtime oracles include:

- Multi-world PIE performance and interaction under the intended listen-server/client topology.
- Movement, collision settling, formation preview, cover arrival, animation recovery, fog transitions, UI, and input feel.
- Multi-process reconnect, replay seek/load, and cooked client/server behavior.
- Scale beyond the current Sandbox population, especially dense moving combat and continuous large-formation previews.

## Immediate working boundary

The old audit/remediation campaign is closed as a source of truth. Its durable results are consolidated in this directory. New work should start from live code and the ordered roadmap rather than reopening the deleted audit documents.
