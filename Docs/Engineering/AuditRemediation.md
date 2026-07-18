# SeinARTS Audit Remediation Ledger

This ledger is the single coverage map for the 2026-07 Codex audit. It prevents findings from
silently disappearing while implementation is split into reviewable phases.

## Baseline

- Branch point: `d22ba7101ea74a88d88dbf99294ab391384535d5`
- Remediation branch: `codex/audit-remediation`
- Baseline editor build: succeeded on 2026-07-18 (`SeinARTSEditor Win64 Development`, 25.77 s)
- Production source baseline (`.h`, `.cpp`, `.cs`):

| Area | Files | Lines | Blank | Comment-like |
|---|---:|---:|---:|---:|
| Host `Source` | 5 | 65 | 16 | 10 |
| Framework | 508 | 96,707 | 11,943 | 29,192 |
| Squad extension | 18 | 2,018 | 228 | 527 |
| Cover extension | 33 | 4,083 | 431 | 1,386 |
| Movement+ extension | 18 | 2,361 | 283 | 828 |

These are review metrics, not reduction quotas. Production, tests, and comments are tracked
separately.

### Phase 0 verification

- Test infrastructure: disabled base `Plugins/SeinARTSTestSuite` plus disabled
  `Plugins/SeinARTSExtensionTestSuite`; three base modules and two extension modules, all denied in
  Shipping. The extension companion has separate runtime and Editor modules. The split permits
  a true framework-only build/run profile without mounting extension assets.
- Production modules depend on neither the test plugin nor `CQTest`.
- Test-enabled editor build: succeeded on 2026-07-18.
- Headless Automation smoke: `SeinARTS.Unit` discovered and passed; the runner parsed the exported
  `index.json` rather than trusting process exit alone.
- Both `All` and extension-stripped `Framework` profiles build and execute; the framework receipt
  marks Squad, Cover, Movement+, and the extension test suite disabled.
- The strict runner correctly rejects seven pre-Automation asset-load errors that Unreal's test
  result otherwise reports as green. `-AllowKnownStartupErrors` accepts only the exact sorted
  checked-in signature multiset while `CONTENT-01` remains explicitly tracked.
- Test-profile builds immediately restore the ordinary editor receipt before Automation launches;
  a completed test run does not enable test plugins or suppress production extensions on normal
  Editor startup.
- Ordinary editor build after test scaffolding: succeeded and its target receipt contains no test
  plugin/module entries.
- Baseline Shipping build: test modules were excluded, but existing production FoW render code
  failed on editor-only `UTexture2D::MipGenSettings`; tracked below as `BUILD-01`.

## Status vocabulary

- **Queued** — audit finding awaiting phase-local revalidation.
- **Confirmed** — reproduced or re-grounded against the branch-point code.
- **In progress** — implementation or verification is active.
- **Fixed** — implementation and focused regression coverage are complete.
- **Verified** — required build/determinism/integration gates are also green.
- **Disproved** — live behavior or a test refuted the audit claim; evidence is recorded.
- **Gate** — product/API decision belongs to RJ before implementation.
- **Deferred** — explicitly accepted by RJ with rationale; never an implicit backlog.

## Approved decisions

- Effects use one simulation-global deterministic identity namespace.
- Authored destinations may overrule coarse static nav false negatives and their owning provider's
  obstruction, but not unrelated live blockers, occupants, reservations, or hazards.
- Tactical cover matching/reservations are implementation scope, not an idea backlog.
- FoW legacy cell output is not a compatibility requirement. A faster deterministic default wins
  when it meets or improves truthfulness, resolution, responsiveness, shapes, layers, and visual
  quality.
- Crisp contracts are automated; movement/tactical/presentation feel remains an RJ PIE decision.
- Tests live in clearly separated non-shipping organization; production modules do not acquire
  CQTest dependencies merely to host a suite.
- Touched production code should become smaller and clearer where that follows naturally from the
  fix. Raw LOC reduction never outranks correctness.
- Plugin-local `AGENTS.md` guides are canonical for Codex. All `CLAUDE.md` files remain present for
  possible future Claude use.
- Work remains in the main checkout; worktrees are forbidden.

## Correctness, determinism, and lifecycle

| ID | Finding | Phase | Status |
|---|---|---:|---|
| COR-01 | Multiplayer command ownership, sender-role, match-control, payload, and turn-window validation are incomplete. | 2 | Gate |
| COR-02 | Effect IDs collide across scope/storage; removal identity is ambiguous. | 1 | Confirmed |
| STATE-01 | Snapshot capture/restore is not exact continuation state across all future-affecting systems. | 3 | Confirmed |
| COR-03 | Latent-action iteration can be invalidated by synchronous Blueprint callbacks. | 1 | Confirmed |
| STATE-02 | StateHash coverage/canonicalization is incomplete and includes process-local `FName` identity. | 3 | Confirmed |
| NAV-01 | Initial destinations can be silently moved after preview by partial A*, wall push, authority recognition, or endpoint restoration. | 5 | Confirmed |
| NAV-02 | Budgeted asynchronous repath results can be overwritten/lost when keyed only by requester. | 5 | Confirmed |
| CACHE-01 | Structured-XOR/fingerprint keys can collide; FoW source/blocker rotation/invalidation is incomplete. | 1/6 | Confirmed |
| NET-01 | Failed local submission, map restart, retained hash/turn sets, and pruning lifecycle are incomplete. | 1/4 | Confirmed |
| COR-04 | Free-rotation placement validates an incorrect/default yaw. | 1 | Confirmed |
| COR-05 | Collision overlap pair identity omits entity generation. | 1 | Confirmed |
| NAV-03 | Same-cell vehicle paths can contain no drivable segment and fail incorrectly. | 5 | Queued |
| MOVE-01 | Fixed-wing idle/coast behavior can violate continuous-flight expectations. | 7 | Gate |
| COR-06 | Generic component initialization can run twice. | 1 | Confirmed |
| EDIT-01 | Editor-owned rooted resources are not reliably released. | 1 | Confirmed |
| COR-07 | Player-effect callbacks may depend on unordered storage iteration. | 1 | Confirmed |
| FOW-01 | No-bake/runtime fallback crosses nondeterministic float/editor behavior into authoritative visibility. | 6 | Queued |
| MATH-01 | Extreme fixed-point construction/arithmetic contains signed-overflow/undefined-behavior edges. | 1 | Confirmed |
| CFG-01 | Cover's sim-affecting settings are not yet a stable config-fingerprint contributor. | 1 | Confirmed |
| BUILD-01 | Shipping compile references editor-only `UTexture2D::MipGenSettings` in FoW rendering. | 1 | Confirmed |
| CONTENT-01 | Existing Blueprint assets emit tagged-property deserialization errors during headless startup. | 1/7 | Confirmed |

## Performance and memory

| ID | Finding/opportunity | Owning phase | Status |
|---|---|---:|---|
| PERF-01 | Redundant full StateHash walks occur at incompatible cadences. | 3/8 | Confirmed |
| PERF-02 | A* scratch allocation churn is high; retained worker contexts need an explicit memory cap. | 5/8 | Confirmed |
| PERF-03 | FoW changed-source footprint generation is roughly cubic in radius. | 6 | Confirmed |
| PERF-04 | Any dynamic FoW blocker change invalidates all sources rather than spatially affected sources. | 6 | Confirmed |
| PERF-05 | Minimap performs dense point queries, buffer churn, and full texture recreation/upload. | 6 | Confirmed |
| PERF-06 | Cover provider/slot work has quadratic paths and duplicated allocation bodies. | 5/8 | Confirmed |
| PERF-07 | Squad, avoidance, and collision scan broad entity sets and allocate avoidable per-tick containers. | 7/8 | Confirmed |
| PERF-08 | Replay and completed/hash/turn histories can grow without practical bounds. | 4/8 | Confirmed |

## API, modularity, and extensibility

| ID | Finding | Phase | Status |
|---|---|---:|---|
| API-01 | Command dispatch/validation is centralized as hardcoded branching rather than stable registered handlers/policies. | 2 | Gate |
| API-02 | Some public headers rely on dependencies declared private in Build.cs. | 1/7 | Queued |
| API-03 | Cover's optional Squad dependency is metadata-only; the bridge module hard-links Squad. | 7 | Confirmed |
| API-04 | Terrain query ownership leaks through Navigation rather than a neutral terrain contract. | 7 | Gate |
| API-05 | Single-cast delegates prevent deterministic composition of multiple providers. | 5/7 | Confirmed |
| API-06 | LevelData channel/schema identity and compatibility versioning are weak. | 3/7 | Queued |
| API-07 | Typed path kinds lack an extensible custom payload/validation story. | 5/7 | Gate |
| API-08 | Per-unit terrain restrictions are not wired through the full request/planner path. | 5 | Queued |
| API-09 | FoW lifecycle hooks and implementation contracts are advertised more broadly than wired. | 6/7 | Queued |
| API-10 | Targeter gestures and attribute modifier types are narrower than their Blueprint-facing promise. | 7 | Gate |
| API-11 | Cover authority is a single-cast Boolean with no requester, source, stable slot, or override policy context. | 5 | Confirmed |

## Approved feature/completeness scope

| ID | Work | Phase | Status |
|---|---|---:|---|
| FEAT-01 | Checkpoint plus command-tail reconnect and exact catch-up. | 4 | Queued |
| FEAT-02 | Replay journaling, checkpoints, seeking, validation, and bounded storage. | 4 | Queued |
| FEAT-03 | Tactical cover matching, stable slot identities, reservations, lifecycle, and shared preview/commit planning. | 5 | Approved |
| FEAT-04 | Faster height-aware FoW default plus spatial invalidation and performance/quality A/B. | 6 | Approved |
| FEAT-05 | Rich targeter/gesture registry and public policy composition. | 7 | Gate |
| FEAT-06 | Team/shared FoW policy. | 7 | Gate |
| FEAT-07 | Production/voting/reinforcement completeness and stable identities. | 7 | Gate |
| FEAT-08 | Vehicle typed-path producer/validation and flight/3D behavior, consistent with steering-first and offline-authored curves. | 7 | Gate |
| FEAT-09 | Adaptive input delay after observability and policy review. | 7/8 | Gate |

## Design gates

| Gate | Decision |
|---|---|
| A | Entity control/delegation, host/match-control authority, and native versus Blueprint custom network commands. |
| B | Exact persistence contract for active Blueprint latent execution and opaque continuation fallback. |
| C | Contextual authoritative-destination provider registry and allowed bypass flags. |
| D | Cover scoring, contention, reservation lifecycle, queued orders, and moving providers. |
| E | Height-aware FoW quality policy and evidence required to replace the default. |
| F | Public API clusters: targeters, modifiers, terrain, production, team vision, and movement feel. |

## Definition of complete

The campaign is complete only when every ledger row is Fixed/Verified, Disproved with evidence, or
explicitly Deferred by RJ, and the final matrix includes:

- Development, clean, and Shipping builds.
- Framework-only and supported extension combinations.
- Downstream public-consumer compilation.
- Canonical serial/parallel and fresh-process digest agreement.
- Snapshot restore plus next-N-tick equivalence.
- Replay/peer/reconnect equivalence where applicable.
- Hostile-network and malformed-payload scenarios.
- Long-match memory/performance soak.
- Blueprint compile/content validation.
- Scripted PIE tactics-gym review and RJ acceptance for behavior-sensitive defaults.
