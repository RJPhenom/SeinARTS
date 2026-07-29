# SeinARTS Audit Remediation

## Reader's edition

This document is a presentation-oriented rendering of the canonical
[AuditRemediation.md](../Engineering/AuditRemediation.md). It preserves the findings, phases,
statuses, approved decisions, evidence, and completion gates recorded in that ledger while replacing
the dense tracking tables with narrative sections and checklists.

The engineering ledger remains the canonical audit trail. This copy reflects its working-tree state
on July 29, 2026; it is not a replacement for it and its completion counts are not effort-weighted.
PDF exports are point-in-time review artifacts and are regenerated at campaign closure; this
Markdown file is the live reader's edition.

## Executive status

The campaign tracks **67 findings and approved work items**.

- **15 are formally closed:** 13 Verified and 2 Fixed.
- **6 are In progress.**
- **3 are Approved for implementation but are not complete.**
- **26 are Confirmed and awaiting their implementation or verification wave.**
- **7 are Queued for phase-local revalidation.**
- **10 are Gates requiring a product or API decision.**
- Nothing is currently marked Disproved or Deferred.

That is **15 of 67 rows, or 22.39%, formally closed** under the ledger's strict Definition of
Complete. It is deliberately not an estimate of engineering effort completed: foundational work can
unlock several rows, while a single acceptance row can require substantial multi-process or PIE
evidence.

By workstream:

- **Correctness, determinism, and lifecycle:** 31 items; 14 formally closed.
- **Performance and memory:** 11 items; none formally closed.
- **API, modularity, and extensibility:** 14 items; 1 formally closed.
- **Feature and completeness scope:** 11 items; none formally closed.

## How to read the checklists

- `[x]` means the row is formally closed as **Verified** or **Fixed**.
- `[ ]` means the row remains open, including **In progress**, **Approved**, **Confirmed**,
  **Queued**, and **Gate** states.
- The explicit status beside every item is authoritative. **Approved** means the direction is
  approved, not that implementation is complete. **Fixed** and **Verified** remain distinct:
  Verified includes the required broader build, determinism, or integration gates.

Status vocabulary:

- **Queued:** the audit finding awaits phase-local revalidation.
- **Confirmed:** the finding was reproduced or re-grounded against the branch-point code.
- **In progress:** implementation or verification is active.
- **Fixed:** implementation and focused regression coverage are complete.
- **Verified:** required build, determinism, and integration gates are also green.
- **Disproved:** live behavior or a test refuted the audit claim and the evidence is recorded.
- **Gate:** RJ must make the product or API decision before implementation.
- **Deferred:** RJ explicitly accepted deferral with a rationale.

## Baseline and evidence timeline

### Starting point

The campaign started from branch point `d22ba7101ea74a88d88dbf99294ab391384535d5` on
`codex/audit-remediation`. The baseline `SeinARTSEditor Win64 Development` build succeeded on
July 18, 2026 in 25.77 seconds.

Production source at baseline, counting `.h`, `.cpp`, and `.cs` files:

- **Host Source:** 5 files, 65 lines, 16 blank lines, 10 comment-like lines.
- **Framework:** 508 files, 96,707 lines, 11,943 blank lines, 29,192 comment-like lines.
- **Squad extension:** 18 files, 2,018 lines, 228 blank lines, 527 comment-like lines.
- **Cover extension:** 33 files, 4,083 lines, 431 blank lines, 1,386 comment-like lines.
- **Movement+ extension:** 18 files, 2,361 lines, 283 blank lines, 828 comment-like lines.

These are review metrics, not reduction quotas. Production code, tests, and comments are tracked
separately.

### Phase 0 - test infrastructure

- The disabled base `Plugins/SeinARTSTestSuite` and disabled
  `Plugins/SeinARTSExtensionTestSuite` separate tests from production and deny all test modules in
  Shipping.
- The extension suite has separate DeveloperTool and Editor modules, permitting a true
  framework-only build and run profile without mounting extension assets.
- Production modules depend on neither the test plugins nor `CQTest`.
- The test-enabled Editor build succeeded on July 18, 2026.
- A headless `SeinARTS.Unit` smoke test was discovered and passed. The runner parsed the exported
  `index.json` instead of trusting only the process exit code.
- Both `All` and extension-disabled `Framework` profiles built and executed. The Framework receipt
  marked Squad, Cover, Movement+, and the extension test suite disabled.
- The strict runner rejected seven pre-Automation asset-load errors that Unreal otherwise reported
  as green. `-AllowKnownStartupErrors` accepts only the exact sorted checked-in signature multiset
  while `CONTENT-01` remains tracked.
- Test-profile builds restored the ordinary Editor receipt before Automation launched, so a test
  run does not leave test plugins enabled or production extensions suppressed.
- The ordinary Editor build after scaffolding contained no Sein test plugin or module entries.
- The baseline Shipping build correctly excluded tests but failed on an editor-only
  `UTexture2D::MipGenSettings` reference in FoW rendering. That became `BUILD-01`.

### Phase 1 - fixed-point content migration

An all-content `LoadPackage -all -projectonly` scan found exactly seven packages retaining the
pre-native `FFixedPoint` tagged layout. A temporary read-only compatibility path recovered each raw
32.32 value, the packages were resaved individually, and the compatibility code was removed. A
native full-content reload then produced zero tagged-property mismatches and reproduced this exact
manifest:

- `SA_Build`: `1,288,490,188,800`
- `SA_Place_Barracks`: `214,748,364,800`
- `SA_Place_VehicleDepot`: `214,748,364,800`
- `SA_Produce_CombatCar`: `64,424,509,440`
- `SA_Produce_Research_VehicleDepot`: `107,374,182,400`
- `SA_ThrowSmoke`: `2,147,483,648,000`
- `SE_UnlockVehicleDepot`: `-4,294,967,296`

The same scan found four unrelated obsolete root assets with broken redirected imports:
`BP_Unit`, `SU_Basic`, `SU_Wheeled`, and `LVL_Lobby`. They remain isolated as `CONTENT-02`
rather than being mixed into the serializer migration.

### Phase 1 - correctness verification

- The full `All` profile passed 74 of 74 tests. Focused evidence included Effects 30/30, Snapshot
  4/4, Determinism 3/3, placement/cover integration 2/2, and editor-style lifecycle 1/1. The
  Framework-only unit profile passed 68/68 before the final all-category run.
- Effect tests covered globally unique IDs, source-aware ability ownership, callback-time
  removal/destruction, stack/tag saturation, transactional rollback, and deterministic iteration.
  An adversarial concern about nested away/back transfer losing one of multiple detached ability
  classes was disproved by an exact two-class lifecycle test.
- Snapshot v4 rejected dirty free slots, abstract/deprecated/newer classes, malformed or trailing
  tagged bytes, cross-wired ability owners, and contradictory primary/passive roles before mutating
  live state. A custom `Within=SeinWorldSubsystem` implementation also round-tripped.
- A same-process serial/parallel collision workload passed. Two fresh Editor processes ran the same
  100-collider workload for 120 production ticks: all 120 raw fixed-point pose digests matched while
  all 120 then-existing `StateHash` values differed. This isolated `STATE-02`; fresh-process
  StateHash agreement was **not** claimed.
- Clean MSVC Development and Shipping builds succeeded. Their receipts excluded Sein test plugins
  and products as required.
- Clang compiled and linked `SeinARTSCore` and `SeinARTSCoreEntity` after fixed-point division was
  moved off compiler-emitted 128-bit runtime helpers. A full Clang Editor executable could not link
  against Epic's MSVC launcher binaries, so Clang execution was **not** claimed.
- `CompileAllBlueprints` found zero Blueprint compile errors. Seven warnings remained: two Python
  name collisions (`ExtentsShape`, `StampShape`), two missing BrandKit
  `SeinAssetIcon92.png` references, and three invalid gameplay tags
  (`SeinARTS.Ability.Garrison`, `SeinARTS.Unit.Vehicle`, and
  `SeinARTS.Tech.VehicleDepot`). The native all-content load had zero serializer mismatches and only
  the four separately tracked `CONTENT-02` failures.

Phase 1 added 2,534 production lines overall. That increase documents correctness and explicit
network/snapshot contracts; it is evidence, not a target. Production counts at that checkpoint:

- **Host Source:** 5 files, 65 lines, delta 0, 16 blank, 10 comment-like.
- **Framework:** 508 files, 99,222 lines, delta +2,515, 12,150 blank, 29,255 comment-like.
- **Squad extension:** 18 files, 2,026 lines, delta +8, 229 blank, 526 comment-like.
- **Cover extension:** 33 files, 4,094 lines, delta +11, 432 blank, 1,377 comment-like.
- **Movement+ extension:** 18 files, 2,361 lines, delta 0, 283 blank, 828 comment-like.

### Phase 2 - command protocol and replay foundation

- The test-enabled `All` profile passed 172/172 `SeinARTS.Unit` tests. The exported report is
  `Saved/Automation/SeinARTS.Unit-20260722-122403/index.json`: 153 clean passes, 19
  expected-warning passes, no failures, and no skipped tests.
- Raw `FName` command payloads moved to a frozen manifest-bound catalog with bounded `uint32` wire
  indices. Hostile bytes cannot intern names, and standalone, network, and replay paths normalize to
  the same catalog representative.
- The recorded compatibility boundary was command wire v3, command protocol domain 6, replay file
  v6, and header metadata v5.
- Executable replay and header-metadata serialization use the active world's immutable catalog.
  Literal-null metadata calls retain an explicit worldless-tooling fallback; a supplied context
  without a frozen simulation world fails closed.
- Adversarial review exposed the tick-zero bootstrap barrier and shared materialization problem
  despite the green suite. It remained visible as `STATE-03`.

### Phases 3 and 4 - tick zero, authority, and lifecycle

Gate G was implemented as a world-lifetime inert facade and one transient bootstrap transaction:
freeze contract, materialize, seal and preflight, compare exact local receipts, authorize every
simulation participant, release the transaction, and launch the already-reserved dormant tick-zero
scheduler. Failure is terminal for that world; reconnect and checkpoint adoption do not reopen
bootstrap.

Additional evidence and contracts:

- `ASeinGameMode` remains Unreal's match authority/controller shell without owning parallel
  deterministic defaults. Legacy default-faction and GameMode starting-resource paths were removed.
  Active Human/AI slots require explicit valid factions. Framework starting resources are a
  canonical match extension. The default materializer requires exactly one `ASeinPlayerStart` per
  active Human/AI slot; custom materializers remain pluggable.
- The canonical receipt binds the frozen contract, authorization context, materialization plan, and
  initial state. Native contributors use registration handles with teardown. Blueprint and extension
  code can contribute immediate deterministic `FInstancedStruct` values. Seed installation and
  Applying-phase mutators require the exact lexical materializer capability. Legacy mutable storage
  access remains tracked as `API-14`.
- Bootstrap consensus is transport-neutral. The shipped Unreal relay supports standalone,
  dedicated, and listen-server topologies without encoding those shapes into authority policy.
  Pending travel state is bound to source world and destination, same-map travel is explicit,
  failure delegates are removed during deinitialization, and irreversible launch occurs only after
  unanimous authorized-ready.
- Command ingress remains closed until `Consumed + running`. Local drafts, authenticated transport,
  replay, deterministic-system, AI, and observer routes have distinct capabilities and lifecycle
  gates. AI emission is exact-controller and lexical; replay primes commands at a private exact-tick
  boundary; public completion/bootstrap callbacks run in the guarded read-only observer scope.
- State hardening includes live-only guarded entity/component reads, exact deferred-destroy
  inspection, generation-preserving component iteration, generation-exhaustion retirement, staged
  snapshot-v7 restore, snapshot reentrancy/GC protection, and explicit ownership-transition
  quiescence. Full continuation snapshots and canonical cross-process hashing remained Phase 5.
- Independent adversarial review found no source-level Phase-4 blocker. The `All` profile passed Unit
  210/210, Sim 27/27, Determinism 5/5, Integration 10/10, and Editor 1/1. The extension-disabled
  Framework profile passed Unit 210/210, Sim 26/26, Determinism 5/5, and Integration 9/9.
  Representative reports are `Saved/Automation/SeinARTS.Unit-20260723-000315`,
  `SeinARTS.Sim-20260723-000947`, `SeinARTS.Determinism-20260723-001010`,
  `SeinARTS.Integration-20260723-001409`, and
  `SeinARTS.Unit-20260723-001507`.
- Ordinary Development and Shipping builds succeeded. Neither receipt enabled a Sein test plugin or
  contained a Sein test build product, and production source had no `CQTest` or test-suite
  dependency.
- A real multi-peer travel/launch `SeinARTS.Network.PIE` scenario was still absent, so `STATE-03`
  remained **Fixed** instead of **Verified**.

### Phase 4 - source-size checkpoint

The Phase-4 correctness work did **not** shrink production source. Relative to Phase 1, Framework
production grew by 23,631 lines and 40 files. Most of the growth is executable protocol, validation,
serialization, and lifecycle machinery rather than comments, but it is not an acceptable final
readability posture: `SeinWorldSubsystem.cpp` reached 9,615 lines and `SeinNetSubsystem.cpp`
reached 6,401. `API-13` tracks internal decomposition after the exact-state contracts are frozen.
Reduction must preserve tested seams and execution order.

Counts at that checkpoint:

- **Host Source:** 5 files, 65 lines; delta from baseline 0, delta from Phase 1 0; 16 blank and 10
  comment-like.
- **Framework:** 548 files, 122,853 lines; delta from baseline +26,146, delta from Phase 1 +23,631;
  13,737 blank and 30,219 comment-like.
- **Squad extension:** 18 files, 2,056 lines; delta from baseline +38, delta from Phase 1 +30; 231
  blank and 531 comment-like.
- **Cover extension:** 33 files, 4,093 lines; delta from baseline +10, delta from Phase 1 -1; 432
  blank and 1,376 comment-like.
- **Movement+ extension:** 18 files, 2,361 lines; delta from baseline 0, delta from Phase 1 0; 283
  blank and 828 comment-like.

### Phase 5 - live checkpoint and second adversarial pass

The July 29 Fable re-review inspected the working tree that became `aea047a`, ran an incremental
editor build, and confirmed every sampled Fixed or Verified row. It also confirmed that implementation
had advanced far beyond the Phase-4 narrative:

- Canonical state now has a BLAKE3-128 world root, authoritative, continuation, and derived-cache
  roles, a frozen provider registry and restore DAG, snapshot v13, Wait and MoveTo latent codecs, and
  Movement, Navigation, and FoW providers wired through snapshot, replay, and peer comparison.
- At the re-audit checkpoint, Unit passed 292/292 and Determinism 15/15, while
  `SeinARTS.Editor.Snapshot.Movement` was red in four Blueprint MoveTo continuation cases. Those
  cases are now green 8/8. Gate B and `STATE-01` remain open for the complete continuation fallback
  and future-affecting system/provider coverage contracts.
- The re-review identified `COR-08` because snapshot restore structurally validated pending commands
  before re-adopting their stored authority fields. The implemented remediation does not re-stamp
  them through live ingress. Restore now requires a world-scoped, one-shot trusted-envelope authority,
  consumes it before parsing or staging, validates the already-canonical continuations, and preserves
  their player, issuer kind, payer, tick, payload, and order exactly. This capability is procedural
  authorization between trusted native modules, not cryptographic byte authentication; network,
  campaign, cloud-save, and replay adapters still owe a bounded authenticated outer envelope.
- Final current evidence passes the Framework-profile snapshot-restore authority suite 7/7
  (`SeinARTS.Unit.Authority.SnapshotRestore-20260729-152203`), final-state Unit 318/318 including
  45 expected-warning passes (`SeinARTS.Unit-20260729-152336`), Integration 12/12
  (`SeinARTS.Integration-20260729-152016`), Determinism 16/16
  (`SeinARTS.Determinism-20260729-152039`), and movement snapshot Editor 8/8
  (`SeinARTS.Editor.Snapshot.Movement-20260729-152129`). `COR-08` deliberately remains **Fixed**,
  not **Verified**, until the authenticated production-envelope reconnect/catch-up path tracked by
  `FEAT-01` exists for end-to-end acceptance.
- Legacy `ComputeStateHash` still uses process-local `FName` identity. This does not disprove the new
  canonical root, but it prevents `STATE-02` closure.
- Movement, Navigation, and FoW are the only separately registered subsystem providers. A complete
  system-by-system inventory must distinguish component-backed state from private continuation state,
  derived caches, presentation-only state, and truly stateless systems before adding providers.
- The test runner rejects an invoked run with no report or no matched tests. Retrospective directory
  tallies can nevertheless omit aborted attempts; `TEST-01` tracks durable run evidence.

The full second-pass delta is recorded in [fable-findings.md](fable-findings.md).

## Approved architectural and product decisions

- Effects use one simulation-global deterministic identity namespace.
- Authored destinations may overrule coarse static-nav false negatives and their owning provider's
  obstruction, but not unrelated live blockers, occupants, reservations, or hazards.
- Tactical cover matching and reservations are implementation scope, not an idea backlog.
- Legacy FoW cell output is not a compatibility requirement. A faster deterministic default wins
  when it meets or improves truthfulness, resolution, responsiveness, shapes, layers, and visual
  quality.
- Crisp contracts are automated; movement, tactical, and presentation feel remains an RJ PIE
  decision.
- Tests stay in clearly separated non-shipping organization. Production modules do not acquire
  `CQTest` dependencies merely to host a suite.
- Touched production code should become smaller and clearer when that follows naturally from the
  fix. Raw line reduction never outranks correctness.
- Plugin-local `AGENTS.md` guides are canonical for Codex. All `CLAUDE.md` files remain for possible
  future Claude use.
- Work remains in the main checkout; worktrees are forbidden.
- Command authority is topology-neutral. Transport-authenticated participants, gameplay player
  slots, simulation/hash peers, coordinator capability, and match-administration capability are
  separate identities and roles. Coordinating a turn does not grant gameplay or administration
  rights.
- The default authority policy is owner control plus explicit deterministic scoped grants. Broker
  orders retain mixed-recipient behavior by filtering unauthorized or stale members; single-entity
  commands reject unauthorized control. Delegated actions spend the entity owner's resources by
  default, while custom policies may implement shared-resource games.
- Commands use an exact registered tag and schema-version contract with bounded deterministic
  payloads. Native built-ins and Blueprint/extension handlers share the frozen compatibility
  manifest. Missing or conflicting handlers fail before tick zero instead of being silently skipped.
- The framework ships the existing Unreal dedicated/listen relay as its first transport adapter,
  while authority and turn aggregation remain compatible with future elected-P2P and all-peer
  adapters. Authenticated fail-stop peers are the P2P baseline; Byzantine consensus and anti-DoS are
  outside the framework guarantee.
- Snapshot adoption is topology-neutral. A trusted native adapter must authenticate and authorize the
  complete outer envelope before claiming the destination world's one-shot restore capability. The
  core preserves already-canonical pending commands exactly; it neither infers transport trust nor
  treats its process-local capability as cryptographic authentication.
- Reconnect/catch-up and any future host migration transfer coordinator capability, not implicit
  gameplay-player or match-administrator authority. “Host” is an adapter/session role; the
  deterministic core stays neutral among dedicated, listen, elected-P2P, and all-peer topologies.

## Correctness, determinism, and lifecycle checklist

- [x] **COR-01 - Verified**
  - **Phase:** 2/4
  - **Finding:** Multiplayer command ownership, sender-role, payload, and turn-window validation were
    incomplete. Match administration applies to `EndMatch`; pause and concede intentionally remain
    authenticated Self-scope commands.
- [x] **COR-02 - Verified**
  - **Phase:** 1
  - **Finding:** Effect IDs collide across scope/storage; removal identity is ambiguous.
- [ ] **STATE-01 - In progress**
  - **Phase:** 5
  - **Finding:** Canonical state, provider roles, snapshot v13, Wait/MoveTo codecs, and the focused
    Blueprint MoveTo continuation suite are green, but the complete continuation fallback and
    system/provider coverage inventory are not closed.
- [x] **COR-03 - Verified**
  - **Phase:** 1
  - **Finding:** Latent-action iteration can be invalidated by synchronous Blueprint callbacks.
- [ ] **STATE-02 - In progress**
  - **Phase:** 5
  - **Finding:** Canonical BLAKE3-128 roots and peer comparison are implemented, but legacy
    `ComputeStateHash` still includes process-local `FName` identity and final fresh-process coverage
    proof is incomplete.
- [x] **STATE-03 - Fixed**
  - **Phase:** 3/4
  - **Finding:** Tick-zero bootstrap lacked a canonical completion barrier and replay-compatible
    shared materialization contract; server and client derived GameMode defaults and failure paths
    differently.
- [x] **COR-08 - Fixed**
  - **Phase:** 5
  - **Finding:** Snapshot adoption now requires a world-scoped, one-shot trusted-envelope authority
    consumed before validation and staging. Already-canonical pending commands are structurally
    validated and preserved exactly, including player, issuer kind, payer, tick, payload, and order;
    they are not re-stamped through live ingress. This process-local capability is procedural native
    authorization, not cryptographic authentication of snapshot bytes.
- [ ] **NAV-01 - Confirmed**
  - **Phase:** 5
  - **Finding:** Initial destinations can be silently moved after preview by partial A*, wall push,
    authority recognition, or endpoint restoration.
- [ ] **NAV-02 - In progress**
  - **Phase:** 5
  - **Finding:** Request-identity protection is implemented, but the async drain still resets
    unconsumed results before interval repaths poll, allowing repeated compute-and-discard starvation.
- [ ] **CACHE-01 - In progress**
  - **Phase:** 1/6
  - **Finding:** Structured-XOR and fingerprint keys can collide; FoW source/blocker
    rotation/invalidation is incomplete. Exact nav/FoW cache identities and equal-cell reset are
    fixed; broader invalidation remains.
- [ ] **NET-01 - In progress**
  - **Phase:** 1/4/5/8
  - **Finding:** Failed local submission, map restart, retained hash/turn sets, and pruning lifecycle
    were incomplete. Readiness, ownership, exact travel binding, failure cleanup, retry, bounds,
    slot/turn, and reset contracts are fixed; checkpoint catch-up and long-session retention remain.
- [x] **COR-04 - Verified**
  - **Phase:** 1
  - **Finding:** Free-rotation placement validates an incorrect or default yaw.
- [x] **COR-05 - Verified**
  - **Phase:** 1
  - **Finding:** Collision overlap pair identity omits entity generation.
- [ ] **NAV-03 - Queued**
  - **Phase:** 5
  - **Finding:** Same-cell routes can contain one waypoint and no typed segment. Shipped movement
    follows waypoints, so the earlier vehicle failure consequence is unsupported; revalidate the
    path-data extension contract before changing behavior.
- [ ] **MOVE-01 - Gate**
  - **Phase:** 7
  - **Finding:** Fixed-wing idle/coast behavior can violate continuous-flight expectations.
- [x] **COR-06 - Verified**
  - **Phase:** 1
  - **Finding:** Generic component initialization can run twice.
- [x] **EDIT-01 - Verified**
  - **Phase:** 1
  - **Finding:** Editor-owned rooted resources are not reliably released.
- [x] **COR-07 - Verified**
  - **Phase:** 1
  - **Finding:** Player-effect callbacks may depend on unordered storage iteration.
- [ ] **FOW-01 - Queued**
  - **Phase:** 6
  - **Finding:** No-bake/runtime fallback crosses nondeterministic float/editor behavior into
    authoritative visibility.
- [ ] **FOW-02 - Confirmed**
  - **Phase:** 6
  - **Finding:** FoW stores one blocker height beside an OR'd layer mask, so overlapping layers
    incorrectly inherit the tallest blocker.
- [ ] **FOW-03 - Confirmed**
  - **Phase:** 6
  - **Finding:** Dynamic FoW blocker height ignores the authored extents `LocalOffset.Z`.
- [ ] **FOW-04 - Confirmed**
  - **Phase:** 6
  - **Finding:** Terrain vision scaling adjusts radial and rectangular shapes but omits cone length.
- [x] **MATH-01 - Verified**
  - **Phase:** 1
  - **Finding:** Extreme fixed-point construction and arithmetic contained signed-overflow and
    undefined-behavior edges. The verified quaternion normalization fix intentionally changes
    overflow-wrapped behavior exposed by the Blueprint `NormalizeQuaternion` node and remains part of
    the final PIE A/B.
- [x] **CFG-01 - Verified**
  - **Phase:** 1
  - **Finding:** Cover's simulation-affecting settings are not yet a stable config-fingerprint
    contributor.
- [x] **BUILD-01 - Verified**
  - **Phase:** 1
  - **Finding:** Shipping compile references editor-only `UTexture2D::MipGenSettings` in FoW
    rendering.
- [ ] **BUILD-02 - Confirmed**
  - **Phase:** 7
  - **Finding:** Uncooked headless `-game` loads `SW_UnitBanner`, whose generated class depends on
    editor-only `USeinWidgetBlueprint`; the asset class needs an UncookedOnly/runtime-safe ownership
    seam.
- [x] **CONTENT-01 - Verified**
  - **Phase:** 1/7
  - **Finding:** Existing Blueprint assets emit tagged-property deserialization errors during
    headless startup.
- [ ] **CONTENT-02 - Confirmed**
  - **Phase:** 7
  - **Finding:** Four obsolete root-level assets have broken redirected imports during an
    all-content load.
- [ ] **CONTENT-03 - Confirmed**
  - **Phase:** Immediate
  - **Finding:** `main` still contains seven pre-resave fixed-point assets that load silently zeroed;
    a mixed stale/resaved fleet can silently desync.
- [ ] **SER-01 - Confirmed**
  - **Phase:** Immediate
  - **Finding:** No focused test pins the native eight-byte `FFixedPoint` serializer, exact raw-bit
    round trip, and `WithSerializer` trait.
- [ ] **TEST-01 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Retrospective Automation evidence can omit aborted/no-report attempts and has no
    durable expected-count floor bound to suite, profile, and commit.

## Performance and memory checklist

- [ ] **PERF-01 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Redundant full StateHash walks occur at incompatible cadences.
- [ ] **PERF-02 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Parallel A* creates and destroys seven-array worker scratch contexts for every
    batch. A future retained pool needs an explicit cap and must preserve generation-reset semantics.
- [ ] **PERF-03 - Confirmed**
  - **Phase:** 6
  - **Finding:** FoW changed-source footprint generation is roughly cubic in radius.
- [ ] **PERF-04 - Confirmed**
  - **Phase:** 6
  - **Finding:** Any dynamic FoW blocker change invalidates all sources rather than only spatially
    affected sources.
- [ ] **PERF-05 - Confirmed**
  - **Phase:** 6
  - **Finding:** Minimap performs dense point queries, buffer churn, and full texture recreation and
    upload.
- [ ] **PERF-06 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Cover provider and slot work has quadratic paths and duplicated allocation bodies.
- [ ] **PERF-07 - Confirmed**
  - **Phase:** 7/8
  - **Finding:** Squad, avoidance, and collision scan broad entity sets and allocate avoidable
    per-tick containers.
- [ ] **PERF-08 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Replay and completed/hash/turn histories can grow without practical bounds.
- [ ] **PERF-09 - Confirmed**
  - **Phase:** 8
  - **Finding:** High effect stack counts materialize one resolved modifier copy per stack.
- [ ] **PERF-10 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Merely enabling Cover binds the authority resolver and serializes NavContainment
    without an authoritative cover destination. It also disables collision parallelism only in
    projects that opted into the non-default parallel resolver.
- [ ] **PERF-11 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** StateHash parallel dispatch is budgeted by storage count, so the default minimum
    batch normally leaves the expensive entity walk serial.

## API, modularity, and extensibility checklist

- [x] **API-01 - Verified**
  - **Phase:** 2/4
  - **Finding:** Command dispatch and validation are centralized as hardcoded branching rather than
    stable registered handlers and policies.
- [ ] **API-02 - Queued**
  - **Phase:** 1/7
  - **Finding:** Some public headers rely on dependencies declared private in `Build.cs`.
- [ ] **API-03 - Confirmed**
  - **Phase:** 7
  - **Finding:** Cover's optional Squad dependency is metadata-only; the bridge module hard-links
    Squad.
- [ ] **API-04 - Gate**
  - **Phase:** 7
  - **Finding:** Terrain query ownership leaks through Navigation rather than a neutral terrain
    contract.
- [ ] **API-05 - Confirmed**
  - **Phase:** 5/7
  - **Finding:** Single-cast delegates prevent deterministic composition of multiple providers.
- [ ] **API-06 - Queued**
  - **Phase:** 3/7
  - **Finding:** LevelData channel/schema identity and compatibility versioning are weak.
- [ ] **API-07 - Gate**
  - **Phase:** 5/7
  - **Finding:** Typed path kinds lack an extensible custom payload and validation story.
- [ ] **API-08 - Queued**
  - **Phase:** 5
  - **Finding:** Per-unit terrain restrictions are not wired through the full request and planner
    path.
- [ ] **API-09 - Queued**
  - **Phase:** 6/7
  - **Finding:** FoW lifecycle hooks and implementation contracts are advertised more broadly than
    wired.
- [ ] **API-10 - Gate**
  - **Phase:** 7
  - **Finding:** Targeter gestures and attribute modifier types are narrower than their
    Blueprint-facing promise.
- [ ] **API-11 - Confirmed**
  - **Phase:** 5
  - **Finding:** Cover authority is a single-cast Boolean with no requester, source, stable slot, or
    override-policy context.
- [ ] **API-12 - Confirmed**
  - **Phase:** 3/7
  - **Finding:** Direct ability activate/end/cancel paths do not centrally maintain
    `ActiveAbilityID` and `ActivePassiveIDs`; snapshot restore preserves IDs but deliberately
    deactivates opaque passive execution.
- [ ] **API-13 - Confirmed**
  - **Phase:** 8
  - **Finding:** Core and Net state machines are over-concentrated in 9,615-line and 6,401-line
    implementation files; split internal responsibilities without widening public seams or changing
    execution order.
- [ ] **API-14 - Confirmed**
  - **Phase:** 5
  - **Finding:** Public mutable entity-pool, component-storage, and player-state accessors bypass the
    guarded mutation/bootstrap facade and can violate lifecycle invariants.

## Feature and completeness checklist

- [ ] **FEAT-01 - Queued**
  - **Phase:** 5
  - **Work:** Authenticated checkpoint plus command-tail reconnect/catch-up. A topology-neutral
    coordinator selects the exact checkpoint frontier; transfer is bounded; the receiver restores
    stopped with local gameplay input and checkpoint recapture gated; the canonical tail is installed
    and caught up; activation requires tail continuity plus canonical-root agreement.
- [ ] **FEAT-02 - In progress**
  - **Phase:** 5
  - **Work:** Replay journaling, checkpoints, seeking, validation, and bounded streaming storage. The
    current 64 MiB limit aborts and discards the complete buffered recording, while per-turn sizing
    performs a full candidate encode.
- [ ] **FEAT-03 - Approved**
  - **Phase:** 5
  - **Work:** Tactical cover matching, stable slot identities, reservations, lifecycle, and shared
    preview/commit planning.
- [ ] **FEAT-04 - Approved**
  - **Phase:** 6
  - **Work:** Faster height-aware FoW default plus spatial invalidation and performance/quality A/B.
- [ ] **FEAT-05 - Gate**
  - **Phase:** 7
  - **Work:** Rich targeter and gesture registry plus public policy composition.
- [ ] **FEAT-06 - Gate**
  - **Phase:** 7
  - **Work:** Team and shared FoW policy.
- [ ] **FEAT-07 - Gate**
  - **Phase:** 7
  - **Work:** Production, voting, and reinforcement completeness with stable identities.
- [ ] **FEAT-08 - Gate**
  - **Phase:** 7
  - **Work:** Vehicle typed-path producer and validation plus flight/3D behavior, consistent with
    steering-first movement and offline-authored curves.
- [ ] **FEAT-09 - Gate**
  - **Phase:** 7/8
  - **Work:** Adaptive input delay after observability and policy review.
- [ ] **FEAT-10 - Gate**
  - **Phase:** 5/7
  - **Work:** Authenticated host migration as topology-neutral coordinator succession: higher-term
    election, membership transition, agreed-root checkpoint selection, committed turn/control-ledger
    transfer, stale-term rejection, local-input gating, and root-gated reactivation.
- [ ] **FEAT-11 - Approved**
  - **Phase:** 5/7
  - **Work:** Co-op campaign persistence with exact same-schema checkpoint continuation and explicit
    versioned campaign-state migration into a new bootstrap; stable participant identities,
    host/backend source authentication, identical peer distribution, shared and per-player
    progression, and UE-native Blueprint/C++ authoring seams.

### Replay foundation boundary

The replay foundation recorded in the ledger is a bounded v6 executable format owned exclusively by
`SeinARTSNet`. Full recordings start at tick zero, retain every applied assembled turn including
empty heartbeats, stop on the exact inclusive `EndTick`, bind executable command decoding to the
world's frozen schema/name catalog, and carry the agreed bootstrap receipt.

The similarly named CoreEntity Blueprint helpers are bounded v5
**header-metadata-only** documents. The ambiguous legacy nodes remain as deprecated wrappers and
cannot create or load an executable journal. `FEAT-02` remains open for checkpoints, seeking,
long-session streaming, and retention policy.

## Design gates

### Gate A - approved July 18, 2026

Owner-by-default deterministic authority with scoped grants; coordinator and match-administration
capabilities remain separate from gameplay slots; exact versioned native/Blueprint handler registry;
topology-neutral core with the Unreal relay as the first shipped transport adapter.

### Gate B - open

Exact persistence contract for active Blueprint latent execution and opaque continuation fallback.

### Gate C - open

Contextual authoritative-destination provider registry and allowed bypass flags.

### Gate D - open

Cover scoring, contention, reservation lifecycle, queued orders, and moving providers.

### Gate E - open

Height-aware FoW quality policy and evidence required to replace the default.

### Gate F - open

Public API clusters: targeters, modifiers, terrain, production, team vision, and movement feel.

### Gate G - approved July 22, 2026

Canonical match rules and exact peer receipt consensus; world-lifetime inert facade plus one
self-culling transient transaction; immediate Blueprint deterministic-value contributions plus native
registered contributors; GameMode remains the Unreal authority shell without duplicated
deterministic defaults.

### Gate H - open

Authenticated host migration policy: topology-neutral higher-term coordinator election, membership
transition, agreed-root checkpoint selection, committed turn/control-ledger transfer, stale-term
rejection, local-input gating, root-gated reactivation, split-brain rejection, and failure or rollback
guarantees.

### Gate I - scope approved July 29, 2026; design open

Co-op campaign persistence and migration are product scope. The design must choose exact-checkpoint
compatibility versus versioned campaign-state migration into a new bootstrap, save ownership and
signing, host/backend source authentication, identical peer distribution, cloud-conflict policy, and
cross-map bootstrap. It must preserve explicit native and Blueprint migration seams, stable
participant identities, and shared and per-player progression.

Related approved feature rows do not automatically close Gates C through E, Gate H, or the open
portions of Gate I. Their full design questions remain open wherever the canonical ledger still labels
them as gates.

## Definition of complete

The campaign is complete only when every ledger row is:

- **Fixed** or **Verified**;
- **Disproved** with recorded evidence; or
- explicitly **Deferred** by RJ with a rationale.

The final acceptance matrix must include:

- [ ] Development, clean, and Shipping builds.
- [ ] Framework-only and supported extension combinations.
- [ ] Downstream public-consumer compilation.
- [ ] Canonical serial/parallel and fresh-process digest agreement.
- [ ] Snapshot restore plus next-N-tick equivalence.
- [ ] Replay, peer, and reconnect equivalence where applicable.
- [ ] Hostile-network and malformed-payload scenarios.
- [ ] Long-match memory and performance soak.
- [ ] Blueprint compile and content validation.
- [ ] Scripted PIE tactics-gym review and RJ acceptance for behavior-sensitive defaults.
