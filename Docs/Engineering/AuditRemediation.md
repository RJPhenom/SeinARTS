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
  Shipping. The extension companion has separate DeveloperTool and Editor modules. The split permits
  a true framework-only build/run profile without mounting extension assets.
- Production modules depend on neither the test plugin nor `CQTest`.
- Test-enabled editor build: succeeded on 2026-07-18.
- Headless Automation smoke: `SeinARTS.Unit` discovered and passed; the runner parsed the exported
  `index.json` rather than trusting process exit alone.
- Both `All` and extension-disabled `Framework` profiles build and execute; the framework receipt
  marks Squad, Cover, Movement+, and the extension test suite disabled.
- The strict runner correctly rejects seven pre-Automation asset-load errors that Unreal's test
  result otherwise reports as green. `-AllowKnownStartupErrors` accepts only the exact sorted
  checked-in signature multiset while `CONTENT-01` remains explicitly tracked.
- Test-profile builds immediately restore the ordinary editor receipt before Automation launches;
  a completed test run does not enable test plugins or suppress production extensions on normal
  Editor startup.
- Ordinary editor build after test scaffolding: succeeded and its target receipt contains no Sein
  test plugin/module entries.
- Baseline Shipping build: test modules were excluded, but existing production FoW render code
  failed on editor-only `UTexture2D::MipGenSettings`; tracked below as `BUILD-01`.

### Phase 1 content migration evidence

An all-content `LoadPackage -all -projectonly` scan proved that exactly seven packages retained the
pre-native `FFixedPoint` tagged layout. A temporary read-only compatibility path recovered one raw
32.32 value from each package; packages were then resaved individually and the compatibility code
was removed. A native full-content reload produced zero tagged-property mismatches and reproduced
the exact raw-value manifest:

| Package | Raw `int64` |
|---|---:|
| `SA_Build` | 1,288,490,188,800 |
| `SA_Place_Barracks` | 214,748,364,800 |
| `SA_Place_VehicleDepot` | 214,748,364,800 |
| `SA_Produce_CombatCar` | 64,424,509,440 |
| `SA_Produce_Research_VehicleDepot` | 107,374,182,400 |
| `SA_ThrowSmoke` | 2,147,483,648,000 |
| `SE_UnlockVehicleDepot` | -4,294,967,296 |

The same scan exposed four unrelated obsolete root assets with broken redirected imports
(`BP_Unit`, `SU_Basic`, `SU_Wheeled`, and `LVL_Lobby`); those are isolated as `CONTENT-02` rather
than being mixed into the serializer migration.

### Phase 1 verification

- The full `All`-profile `SeinARTS` Automation run passed 74/74 tests. Focused evidence includes
  Effects 30/30, Snapshot 4/4, all determinism tests 3/3, placement/cover integration 2/2, and
  editor-style lifecycle 1/1. The framework-only unit profile passed 68/68 before the final
  all-category run.
- The effect suite exercises globally unique IDs, exact source-aware ability ownership, callback
  removal/destruction, stack/tag saturation, transactional rollback, and deterministic iteration.
  An adversarial claim that nested away/back transfer lost one of multiple detached ability classes
  was disproved by an exact two-class lifecycle test: the ownership multiset balances after removal.
- Snapshot v4 rejects dirty free slots, abstract/deprecated/newer classes, malformed or trailing
  tagged bytes, cross-wired ability owners, and contradictory primary/passive roles before mutating
  live state. A custom implementation using `Within=SeinWorldSubsystem` also round-trips, preserving
  the framework's pluggable class seam.
- A same-process serial/parallel collision workload passed. Two fresh editor processes each ran the
  same 100-collider workload for 120 production ticks: all 120 raw fixed-point pose digests matched,
  while all 120 existing `StateHash` values differed. This isolates `STATE-02` canonicalization from
  the now-tested parallel collision result; fresh-process StateHash agreement is **not** claimed.
- Clean MSVC Development and Shipping builds succeeded. The Shipping receipt contains neither Sein
  test plugin nor Sein test build product; the ordinary Editor receipt contains no enabled Sein test
  plugin.
- Clang compiled and linked `SeinARTSCore` and `SeinARTSCoreEntity` after fixed-point division was
  moved off compiler-emitted 128-bit runtime helpers. A full Clang Editor executable cannot be linked
  against Epic's launcher-engine MSVC binaries because of unrelated Windows ABI imports, so Clang
  execution is not claimed.
- `CompileAllBlueprints` reported zero Blueprint compile errors. It retains seven known warnings:
  two Python-name collisions (`ExtentsShape`, `StampShape`), two references to the missing BrandKit
  `SeinAssetIcon92.png`, and three invalid gameplay tags (`SeinARTS.Ability.Garrison`,
  `SeinARTS.Unit.Vehicle`, `SeinARTS.Tech.VehicleDepot`). The native all-content load has zero
  serializer mismatches and only the four separately tracked `CONTENT-02` load failures.

### Phase 1 production-size checkpoint

Tests are excluded, matching the baseline method. Correctness and explicit network/snapshot
contracts increased production source by 2,534 lines overall; this is evidence, not a target. The
largest increase is concentrated in validation and lifecycle state, while several effect, nav, and
FoW paths became smaller. Later readability work should compare against this checkpoint without
removing validated behavior merely to reduce LOC.

| Area | Files | Lines | Delta | Blank | Comment-like |
|---|---:|---:|---:|---:|---:|
| Host `Source` | 5 | 65 | 0 | 16 | 10 |
| Framework | 508 | 99,222 | +2,515 | 12,150 | 29,255 |
| Squad extension | 18 | 2,026 | +8 | 229 | 526 |
| Cover extension | 33 | 4,094 | +11 | 432 | 1,377 |
| Movement+ extension | 18 | 2,361 | 0 | 283 | 828 |

### Phase 2 working checkpoint

- The full test-enabled `All` profile builds, restores the ordinary editor receipt, and passes
  172/172 `SeinARTS.Unit` tests. The exported report is
  `Saved/Automation/SeinARTS.Unit-20260722-122403/index.json` (153 clean passes, 19 expected-warning
  passes, zero failures or skipped tests).
- Raw `FName` command payloads now use a frozen, manifest-bound catalog and bounded `uint32` wire
  indices; hostile bytes cannot intern names. Standalone, network, and replay paths normalize to the
  same catalog representative. The current coordinated compatibility boundary is command wire v3 /
  command protocol domain 6 / replay file v6 / header metadata v5.
- Executable replay and header-metadata serialization use the active world's immutable catalog.
  Literal-null metadata calls retain an explicit worldless-tooling fallback to current project
  defaults; a supplied context without a frozen simulation world fails closed.
- Adversarial review exposed a separate tick-zero bootstrap contract/barrier problem before replay
  playback was widened. It is tracked as `STATE-03` and remains a design gate rather than being
  hidden behind the green unit suite.

### Phase 3/4 tick-zero, authority, and lifecycle checkpoint

- Gate G is approved and implemented as a world-lifetime inert facade plus one transient bootstrap
  transaction: freeze contract -> materialize -> seal/preflight -> compare exact local receipts ->
  authorize every simulation participant -> release the transaction -> launch the already-reserved
  dormant tick-zero scheduler. Failure is terminal for that world; reconnect/checkpoint adoption does
  not reopen bootstrap.
- `ASeinGameMode` remains Unreal's match authority/controller shell, but no longer owns parallel
  deterministic defaults. The legacy default-faction and GameMode starting-resource paths were
  removed. Active Human/AI slots require explicit valid factions; framework starting resources are a
  canonical match extension. The default materializer requires exactly one `ASeinPlayerStart` per
  active Human/AI slot, while custom materializers remain pluggable.
- The canonical receipt binds the frozen contract, authorization context, materialization plan, and
  initial state. Native contributors use registration handles with teardown; Blueprint/extensions can
  contribute immediate deterministic `FInstancedStruct` values. Seed installation and guarded
  framework Applying-phase mutators require the exact lexical materializer capability; legacy mutable
  storage accessors remain tracked as `API-14`.
- Bootstrap consensus is transport-neutral. The shipped Unreal relay supports standalone, dedicated,
  and listen-server topologies without encoding those shapes into the authority policy. Pending travel
  state is source-world/destination-bound, same-map travel is explicit, failure delegates are removed
  on deinitialize, and the irreversible launch fanout occurs only after unanimous authorized-ready.
- Command ingress is closed until `Consumed + running`. Local drafts, authenticated transport,
  replay, deterministic-system, AI, and observer routes now have distinct capabilities and lifecycle
  gates. AI emission is exact-controller and lexical; replay primes commands at a private exact-tick
  boundary; public completion/bootstrap callbacks enter the guarded read-only observer scope.
- State hardening now includes live-only guarded entity/component reads, exact deferred-destroy
  inspection, generation-preserving component iteration, generation-exhaustion retirement, staged
  snapshot-v7 restore, snapshot reentrancy/GC protection, and explicit ownership-transition
  quiescence. Full continuation snapshots and canonical cross-process state hashing remain Phase 5.
- Independent adversarial review found no remaining source-level Phase-4 blocker. The All profile
  passes Unit 210/210, Sim 27/27, Determinism 5/5, Integration 10/10, and Editor 1/1. The
  extension-disabled Framework profile passes Unit 210/210, Sim 26/26, Determinism 5/5, and
  Integration 9/9. Representative reports are under `Saved/Automation/` at
  `SeinARTS.Unit-20260723-000315`, `SeinARTS.Sim-20260723-000947`,
  `SeinARTS.Determinism-20260723-001010`, `SeinARTS.Integration-20260723-001409`, and
  `SeinARTS.Unit-20260723-001507` (Framework).
- Ordinary `SeinARTSEditor Win64 Development` and `SeinARTS Win64 Shipping` builds succeed. Neither
  receipt enables a Sein test plugin or contains a Sein test build product, and production source has
  no `CQTest`/test-suite dependency. A real multi-peer travel/launch `SeinARTS.Network.PIE` scenario
  is not yet present, so `STATE-03` is Fixed rather than promoted to Verified.

### Phase 4 production-size checkpoint

The Phase-4 correctness work did **not** shrink production source. Relative to the Phase-1 checkpoint,
Framework production grew by 23,631 lines and 40 files. Most of the increase is executable protocol,
validation, serialization, and lifecycle machinery rather than comments, but the concentration is not
an acceptable final readability posture: `SeinWorldSubsystem.cpp` is 9,615 lines and
`SeinNetSubsystem.cpp` is 6,401. `API-13` tracks internal decomposition after the exact-state contracts
are frozen; reduction must preserve the tested seams and behavior.

| Area | Files | Lines | Delta from baseline | Delta from Phase 1 | Blank | Comment-like |
|---|---:|---:|---:|---:|---:|---:|
| Host `Source` | 5 | 65 | 0 | 0 | 16 | 10 |
| Framework | 548 | 122,853 | +26,146 | +23,631 | 13,737 | 30,219 |
| Squad extension | 18 | 2,056 | +38 | +30 | 231 | 531 |
| Cover extension | 33 | 4,093 | +10 | -1 | 432 | 1,376 |
| Movement+ extension | 18 | 2,361 | 0 | 0 | 283 | 828 |

### Phase 5 live checkpoint and second adversarial pass

The 2026-07-29 Fable re-review inspected the working tree that became `aea047a`, ran an
incremental editor build, and re-grounded the current implementation against this ledger. It
confirmed every sampled Fixed/Verified row, but also confirmed that implementation had advanced
well beyond the Phase-4 narrative here:

- Canonical state now has a BLAKE3-128 world root, authoritative/continuation/derived-cache roles,
  a frozen provider registry and restore DAG, snapshot v13, Wait and MoveTo latent codecs, and
  Movement, Navigation, and FoW providers wired into snapshot, replay, and peer comparison.
- The last broad evidence before the checkpoint was Unit 292/292 and Determinism 15/15. The
  focused `SeinARTS.Editor.Snapshot.Movement` suite is currently red in four Blueprint MoveTo
  continuation-capture cases, so Gate B and `STATE-01` remain open.
- Snapshot restore validates pending command shape and enum values but currently re-adopts stored
  `PlayerID`/`IssuerKind`/payer/tick claims without the trusted live-ingress stamping path. This is
  tracked as `COR-08`.
- The legacy 32-bit `ComputeStateHash` path still hashes `FName` identities in component-property
  and effect/grant class keys. The canonical root is not disproved by this residual, but
  `STATE-02` cannot close while an advertised determinism path remains process-local.
- Movement, Navigation, and FoW are the only separately registered subsystem canonical providers.
  That count alone does not prove missing coverage: component-backed extension state may already
  be captured by Core, while broadphase/cache state may be deterministically derived. `STATE-01`
  now explicitly requires a system-by-system coverage inventory before closure.
- `RunTests.ps1` rejects an invoked run with no `index.json` or zero matched tests. Historical
  evidence tallies that merely enumerate surviving report directories can still omit aborted
  attempts; `TEST-01` tracks durable attempted-run accounting and expected-count evidence.

The full second-pass delta and the still-open first-pass corrections are preserved in
`Docs/Audit/fable-findings.md`.

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
- Command authority is topology-neutral. Transport-authenticated participants, gameplay player
  slots, simulation/hash peers, coordinator capability, and match-administration capability are
  distinct identities/roles. Coordinating a turn never grants gameplay or administration rights.
- The default command-authority policy is owner control plus explicit deterministic scoped grants.
  Broker orders preserve their existing mixed-recipient behavior by filtering unauthorized/stale
  members; single-entity commands reject unauthorized control. Delegated actions spend the entity
  owner's resources by default, while custom policies may implement shared-resource games.
- Commands use an exact registered tag + schema-version contract with bounded deterministic payloads.
  Native built-ins and Blueprint/extension handlers share the same frozen compatibility manifest;
  missing or conflicting handlers fail before tick zero rather than being silently skipped.
- The framework will ship the existing Unreal dedicated/listen relay as its first transport adapter,
  while authority and turn aggregation remain compatible with future elected-P2P and all-peer
  adapters. Authenticated fail-stop peers are the P2P baseline; Byzantine consensus and anti-DoS are
  outside the framework guarantee.

## Correctness, determinism, and lifecycle

| ID | Finding | Phase | Status |
|---|---|---:|---|
| COR-01 | Multiplayer command ownership, sender-role, payload, and turn-window validation were incomplete. Match administration applies to `EndMatch`; pause and concede intentionally remain authenticated Self-scope commands. | 2/4 | Verified |
| COR-02 | Effect IDs collide across scope/storage; removal identity is ambiguous. | 1 | Verified |
| STATE-01 | Snapshot capture/restore is not yet exact continuation state across every future-affecting system. Canonical state, provider roles, snapshot v13, and Wait/MoveTo codecs are implemented, but Blueprint MoveTo continuation is red and the complete system/provider coverage inventory is not closed. | 5 | In progress |
| COR-03 | Latent-action iteration can be invalidated by synchronous Blueprint callbacks. | 1 | Verified |
| STATE-02 | Canonical BLAKE3-128 world roots and peer comparison are implemented, but legacy `ComputeStateHash` coverage still includes process-local `FName` identity and final fresh-process/full-coverage proof is incomplete. | 5 | In progress |
| STATE-03 | Tick-zero bootstrap lacked a canonical completion barrier and replay-compatible shared materialization contract; server/client derived GameMode defaults and failure paths differently. | 3/4 | Fixed |
| COR-08 | Snapshot restore re-trusts serialized pending-command authority claims instead of re-deriving `PlayerID`, `IssuerKind`, payer, and tick through the trusted ingress authority context. | 5 | Confirmed |
| NAV-01 | Initial destinations can be silently moved after preview by partial A*, wall push, authority recognition, or endpoint restoration. | 5 | Confirmed |
| NAV-02 | Request-identity protection is implemented, but `DrainAsyncPathQueue` still resets all unconsumed results before interval repaths poll; busy scenes can repeatedly compute then discard interval results. | 5 | In progress |
| CACHE-01 | Structured-XOR/fingerprint keys can collide; FoW source/blocker rotation/invalidation is incomplete. Exact nav/FoW cache identities and equal-cell reset are fixed; broader invalidation remains. | 1/6 | In progress |
| NET-01 | Failed local submission, map restart, retained hash/turn sets, and pruning lifecycle were incomplete. Readiness, ownership, exact travel binding, failure cleanup, retry, bounds, slot/turn, and reset contracts are fixed; checkpoint catch-up and long-session retention remain. | 1/4/5/8 | In progress |
| COR-04 | Free-rotation placement validates an incorrect/default yaw. | 1 | Verified |
| COR-05 | Collision overlap pair identity omits entity generation. | 1 | Verified |
| NAV-03 | Same-cell routes can contain one waypoint and no typed segment. Shipped movement follows waypoints, so the earlier vehicle failure consequence is unsupported; revalidate the path-data/extension contract before changing behavior. | 5 | Queued |
| MOVE-01 | Fixed-wing idle/coast behavior can violate continuous-flight expectations. | 7 | Gate |
| COR-06 | Generic component initialization can run twice. | 1 | Verified |
| EDIT-01 | Editor-owned rooted resources are not reliably released. | 1 | Verified |
| COR-07 | Player-effect callbacks may depend on unordered storage iteration. | 1 | Verified |
| FOW-01 | No-bake/runtime fallback crosses nondeterministic float/editor behavior into authoritative visibility. | 6 | Queued |
| FOW-02 | FoW stores one blocker height beside an OR'd layer mask, so overlapping layers incorrectly inherit the tallest blocker. | 6 | Confirmed |
| FOW-03 | Dynamic FoW blocker height ignores the authored extents `LocalOffset.Z`. | 6 | Confirmed |
| FOW-04 | Terrain vision scaling adjusts radial/rect shapes but omits cone length. | 6 | Confirmed |
| MATH-01 | Extreme fixed-point construction/arithmetic contained signed-overflow/undefined-behavior edges. Verified fixes intentionally change overflow-wrapped quaternion normalization visible through the Blueprint `NormalizeQuaternion` node; include that surface in the final PIE A/B. | 1 | Verified |
| CFG-01 | Cover's sim-affecting settings are not yet a stable config-fingerprint contributor. | 1 | Verified |
| BUILD-01 | Shipping compile references editor-only `UTexture2D::MipGenSettings` in FoW rendering. | 1 | Verified |
| BUILD-02 | Uncooked headless `-game` loads `SW_UnitBanner`, whose generated class depends on editor-only `USeinWidgetBlueprint`; the asset class needs an UncookedOnly/runtime-safe ownership seam. | 7 | Confirmed |
| CONTENT-01 | Existing Blueprint assets emit tagged-property deserialization errors during headless startup. | 1/7 | Verified |
| CONTENT-02 | Four obsolete root-level assets have broken redirected imports during an all-content load. | 7 | Confirmed |
| CONTENT-03 | `main` still contains seven pre-resave `FFixedPoint` assets that load silently zeroed under the native serializer; a mixed stale/resaved fleet can silently desync. | Immediate | Confirmed |
| SER-01 | No focused regression test pins `FFixedPoint`'s native eight-byte serializer, exact raw-bit round trip, and `WithSerializer` trait. | Immediate | Confirmed |
| TEST-01 | Retrospective Automation evidence can omit aborted/no-report attempts and has no durable expected-count floor bound to suite/profile/commit. | 5/8 | Confirmed |

## Performance and memory

| ID | Finding/opportunity | Owning phase | Status |
|---|---|---:|---|
| PERF-01 | Redundant full StateHash walks occur at incompatible cadences. | 5/8 | Confirmed |
| PERF-02 | Parallel A* creates and destroys seven-array worker scratch contexts for every batch. A future retained pool can remove churn, but it needs an explicit memory cap and must preserve generation-reset semantics. | 5/8 | Confirmed |
| PERF-03 | FoW changed-source footprint generation is roughly cubic in radius. | 6 | Confirmed |
| PERF-04 | Any dynamic FoW blocker change invalidates all sources rather than spatially affected sources. | 6 | Confirmed |
| PERF-05 | Minimap performs dense point queries, buffer churn, and full texture recreation/upload. | 6 | Confirmed |
| PERF-06 | Cover provider/slot work has quadratic paths and duplicated allocation bodies. | 5/8 | Confirmed |
| PERF-07 | Squad, avoidance, and collision scan broad entity sets and allocate avoidable per-tick containers. | 7/8 | Confirmed |
| PERF-08 | Replay and completed/hash/turn histories can grow without practical bounds. | 5/8 | Confirmed |
| PERF-09 | High effect stack counts materialize one resolved modifier copy per stack. | 8 | Confirmed |
| PERF-10 | Merely enabling Cover binds the authority resolver and serializes NavContainment even without an authoritative cover destination. Collision parallelism is also disabled only for projects that opted into the non-default parallel resolver. | 5/8 | Confirmed |
| PERF-11 | StateHash parallel dispatch is budgeted by storage count, so the default minimum batch normally leaves the expensive entity walk serial. | 5/8 | Confirmed |

## API, modularity, and extensibility

| ID | Finding | Phase | Status |
|---|---|---:|---|
| API-01 | Command dispatch/validation is centralized as hardcoded branching rather than stable registered handlers/policies. | 2/4 | Verified |
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
| API-12 | Direct ability activate/end/cancel paths do not centrally maintain `ActiveAbilityID`/`ActivePassiveIDs`; snapshot restore preserves IDs but deliberately deactivates opaque passive execution. | 3/7 | Confirmed |
| API-13 | Core and Net state machines are over-concentrated in 9,615-line and 6,401-line implementation files; split internal responsibilities without widening public seams or changing execution order. | 8 | Confirmed |
| API-14 | Public mutable entity-pool, component-storage, and player-state accessors bypass the guarded mutation/bootstrap facade and can violate lifecycle invariants. | 5 | Confirmed |

## Approved feature/completeness scope

| ID | Work | Phase | Status |
|---|---|---:|---|
| FEAT-01 | Checkpoint plus command-tail reconnect and exact catch-up. | 5 | Queued |
| FEAT-02 | Replay journaling, checkpoints, seeking, validation, and bounded streaming storage. The current 64 MiB cap aborts and discards the complete buffered recording and per-turn sizing performs a full candidate encode. | 5 | In progress |
| FEAT-03 | Tactical cover matching, stable slot identities, reservations, lifecycle, and shared preview/commit planning. | 5 | Approved |
| FEAT-04 | Faster height-aware FoW default plus spatial invalidation and performance/quality A/B. | 6 | Approved |
| FEAT-05 | Rich targeter/gesture registry and public policy composition. | 7 | Gate |
| FEAT-06 | Team/shared FoW policy. | 7 | Gate |
| FEAT-07 | Production/voting/reinforcement completeness and stable identities. | 7 | Gate |
| FEAT-08 | Vehicle typed-path producer/validation and flight/3D behavior, consistent with steering-first and offline-authored curves. | 7 | Gate |
| FEAT-09 | Adaptive input delay after observability and policy review. | 7/8 | Gate |

Replay's foundation now uses a bounded v6 executable format owned exclusively by
`SeinARTSNet`: full recordings start at tick 0, retain every applied assembled turn (including empty
heartbeats), stop on the exact inclusive `EndTick`, bind executable command decoding to the world's
frozen schema/name catalog, and carry the agreed bootstrap receipt. The similarly named CoreEntity
Blueprint helpers are bounded v5 **header-metadata-only** documents; the ambiguous legacy nodes
remain as deprecated wrappers and cannot create or load an executable journal. FEAT-02 remains open
for checkpoints, seeking, and long-session streaming/retention policy.

## Design gates

| Gate | Decision |
|---|---|
| A | **Approved 2026-07-18.** Owner-by-default deterministic authority with scoped grants; coordinator and match-admin capabilities remain separate from gameplay slots; exact versioned native/BP handler registry; topology-neutral core with Unreal relay as the first shipped transport adapter. |
| B | Exact persistence contract for active Blueprint latent execution and opaque continuation fallback. |
| C | Contextual authoritative-destination provider registry and allowed bypass flags. |
| D | Cover scoring, contention, reservation lifecycle, queued orders, and moving providers. |
| E | Height-aware FoW quality policy and evidence required to replace the default. |
| F | Public API clusters: targeters, modifiers, terrain, production, team vision, and movement feel. |
| G | **Approved 2026-07-22.** Canonical match rules and exact peer receipt consensus; world-lifetime inert facade plus one self-culling transient transaction; immediate Blueprint deterministic-value contributions plus native registered contributors; GameMode remains the Unreal authority shell without duplicated deterministic defaults. |

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
