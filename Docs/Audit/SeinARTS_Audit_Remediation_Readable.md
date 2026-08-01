# SeinARTS Audit Remediation

## Reader's edition

This document is a presentation-oriented rendering of the canonical
[AuditRemediation.md](../Engineering/AuditRemediation.md). It preserves the findings, phases,
statuses, approved decisions, evidence, and completion gates recorded in that ledger while replacing
the dense tracking tables with narrative sections and checklists.

The engineering ledger remains the canonical audit trail. This copy reflects its working-tree state
on August 1, 2026; it is not a replacement for it and its completion counts are not effort-weighted.
PDF exports are point-in-time review artifacts and are regenerated at campaign closure; this
Markdown file is the live reader's edition.

## Executive status

The campaign tracks **67 findings and approved work items**.

- **23 are formally closed:** 17 Verified and 6 Fixed.
- **5 are In progress.**
- **3 are Approved for implementation but are not complete.**
- **22 are Confirmed and awaiting their implementation or verification wave.**
- **4 are Queued for phase-local revalidation.**
- **10 are Gates requiring a product or API decision.**
- Nothing is currently marked Disproved or Deferred.

That is **23 of 67 rows, or 34.33%, formally closed** under the ledger's strict Definition of
Complete. It is deliberately not an estimate of engineering effort completed: foundational work can
unlock several rows, while a single acceptance row can require substantial multi-process or PIE
evidence.

By workstream:

- **Correctness, determinism, and lifecycle:** 31 items; 18 formally closed.
- **Performance and memory:** 11 items; 1 formally closed.
- **API, modularity, and extensibility:** 14 items; 2 formally closed.
- **Feature and completeness scope:** 11 items; 2 formally closed.

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
  Applying-phase mutators require the exact lexical materializer capability. `API-14` later closed
  the legacy mutable-storage escape hatches with const-only reads and guarded explicit mutation.
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
- Evidence recorded at that checkpoint passes the Framework-profile snapshot-restore authority suite 7/7
  (`SeinARTS.Unit.Authority.SnapshotRestore-20260729-152203`), final-state Unit 318/318 including
  45 expected-warning passes (`SeinARTS.Unit-20260729-152336`), Integration 12/12
  (`SeinARTS.Integration-20260729-152016`), Determinism 16/16
  (`SeinARTS.Determinism-20260729-152039`), and movement snapshot Editor 8/8
  (`SeinARTS.Editor.Snapshot.Movement-20260729-152129`). `COR-08` remained **Fixed** at that
  checkpoint; the bounded coordinator-selected reconnect/catch-up path is now built under `FEAT-01`,
  with true multi-process PIE/cooked behavior retained as the runtime oracle.
- Legacy `ComputeStateHash` still uses process-local `FName` identity. This does not disprove the new
  canonical root, but it prevents `STATE-02` closure.
- Movement, Navigation, and FoW are the only separately registered subsystem providers. A complete
  system-by-system inventory must distinguish component-backed state from private continuation state,
  derived caches, presentation-only state, and truly stateless systems before adding providers.
- The test runner rejects an invoked run with no report or no matched tests. Retrospective directory
  tallies can nevertheless omit aborted attempts; `TEST-01` tracks durable run evidence.

The full second-pass delta is recorded in [fable-findings.md](fable-findings.md).

### July 29 current branch rebaseline

This current boundary supersedes the evidence snapshot above without erasing its historical record:

- `SER-01` is **Verified**. A focused regression pins `FFixedPoint`'s native eight-byte serializer,
  exact raw-bit round trip, and `WithSerializer` trait.
- `CONTENT-03` is **In progress**. The seven corrected fixed-point asset blobs, including the match
  hotfix, are committed on this branch, but `origin/main` remains stale. The fleet hazard stays open
  until those exact blobs reach the shared production baseline.
- `STATE-01` remains **In progress**. Snapshot v13 now exactly covers Core
  world/entity/component state, ability and resolver pools, canonical Blueprint value slots,
  Wait/MoveTo continuations, Movement/Movement+ policy instances, navigation async continuation, and
  FoW authoritative state. Squad state is component-backed and shipped broadphase, blocker, overlap,
  and cover indexes are derived. Exact continuation remains incomplete for arbitrary Blueprint VM
  latent/async frames, post-freeze navigation substrate mutation, unenforced stateful
  formation/resolver preview paths, and custom navigation/collision/cover implementations without
  explicit state-coverage claims. Quiescent capture continues to fail closed on deferred
  effect/destroy and replay-ingress work.
- Cover restore now rebuilds the default or custom implementation's derived provider registry from
  authoritative entities with canonical handle ordering and generation-safe tie-breaks. That closes
  the discovered restore-index hole but not `STATE-01`'s broader custom-provider claim boundary.
- `STATE-02` is **Verified**. Authoritative peer and successful pause-control evidence uses
  fail-closed BLAKE3-128 canonical roots. The legacy 32-bit hash is explicitly deprecated and
  local-diagnostic-only. Independent fresh-process serial and parallel collision traces matched exact
  canonical root and pose for all 120 ticks.
- `TEST-01` is **Verified**. The runner writes `attempt.json` before launch and finalizes it for
  build failure, no report, test failure, and success. Baseline matching is case-insensitive,
  canonical broad suites fail closed without one, and baseline ancestry is checked. Clean commit
  `9a991f544a59d1b63395fb8a9a783c6d7d1c2e30` reproduced all six broad-suite floors, including Unit
  321/317 for All/Framework, and now owns their checked-in provenance.
- Final clean evidence is Cover restore/custom seam 2/2
  (`SeinARTS.Unit.Cover.SnapshotRestore-20260729-165659-c5d85929`), All/Framework Unit 321/317
  (`SeinARTS.Unit-20260729-165216-1f242af9`,
  `SeinARTS.Unit-20260729-165243-eb433c77`), All/Framework Integration 12/11
  (`SeinARTS.Integration-20260729-165310-74b30bc8`,
  `SeinARTS.Integration-20260729-165449-e2e8a874`), and All/Framework Determinism 16/15
  (`SeinARTS.Determinism-20260729-165506-960f69a1`,
  `SeinARTS.Determinism-20260729-165543-fc72725e`). Independent serial and parallel traces are
  `SeinARTS.Determinism.Process.SerialCollisionTrace-20260729-165621-a09bcfae` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260729-165638-ffd47bc8`.
- Ordinary Editor builds are green. The Shipping gate caught branch-level teardown/restore include
  gaps, those gaps were repaired, and the clean `SeinARTS Win64 Shipping` rerun succeeded at 16:57.
- The current replay boundary is executable file v8 and CoreEntity header metadata v6. Earlier v6/v5
  references remain the correct historical Phase-2 boundary.

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
  - **Finding:** Snapshot v13 now exactly covers Core world/entity/component state, ability and
    resolver pools, canonical Blueprint value slots, Wait/MoveTo continuations, Movement/Movement+
    policy instances, navigation async continuation, and FoW authoritative state. Squad state is
    component-backed and shipped broadphase, blocker, overlap, and cover indexes are derived. Exact
    continuation remains incomplete for arbitrary Blueprint VM latent/async frames, post-freeze
    navigation substrate mutation, unenforced stateful formation/resolver preview paths, and custom
    navigation/collision/cover implementations without explicit state-coverage claims. Quiescent
    capture continues to fail closed on deferred effect/destroy and replay-ingress work.
- [x] **COR-03 - Verified**
  - **Phase:** 1
  - **Finding:** Latent-action iteration can be invalidated by synchronous Blueprint callbacks.
- [x] **STATE-02 - Verified**
  - **Phase:** 5
  - **Finding:** Authoritative BLAKE3-128 canonical roots drive peer comparison and successful
    pause-control evidence. Legacy 32-bit `ComputeStateHash` is deprecated and
    local-diagnostic-only; independent fresh-process serial and parallel traces matched exact
    canonical root and pose for all 120 ticks.
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
- [x] **NET-01 - Fixed**
  - **Phase:** 1/4/5/8
  - **Finding:** Failed submission, map restart, retained protocol history, pruning, reconnect, and
    replay lifecycle were incomplete. Readiness, ownership, travel binding, failure cleanup/retry,
    bounded history, checkpoint-plus-tail catch-up, and one replay journal per lockstep epoch are now
    built.
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
- [ ] **CONTENT-03 - In progress**
  - **Phase:** Immediate
  - **Finding:** Seven corrected fixed-point asset blobs, including the match hotfix, are committed
    on this branch, but `origin/main` remains stale; a mixed stale/resaved fleet can silently desync
    until the shared baseline receives them.
- [x] **SER-01 - Verified**
  - **Phase:** Immediate
  - **Finding:** Focused regression coverage pins the native eight-byte `FFixedPoint` serializer,
    exact raw-bit round trip, and `WithSerializer` trait.
- [x] **TEST-01 - Verified**
  - **Phase:** 5/8
  - **Finding:** Durable `attempt.json` receipts cover launch and every terminal outcome. Canonical
    broad suites fail closed without case-insensitive suite/profile baselines whose commits are
    verified ancestors. Clean commit `9a991f544a59d1b63395fb8a9a783c6d7d1c2e30` reproduced all six
    floors, including Unit 321/317, and owns their checked-in provenance.

## Performance and memory checklist

- [ ] **PERF-01 - In progress**
  - **Phase:** 5/8
  - **Finding:** Peer and pause-control evidence now uses canonical BLAKE3-128 roots and the legacy
    32-bit walk is opt-in local diagnostics only, but full canonical walks remain synchronous at
    independent checkpoint cadences and are not shared or cached.
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
  - **Finding:** Minimap reuses its texture at a stable resolution, but every refresh still allocates
    full pixel buffers, performs dense world-to-fixed fog lookups and optional blur, copies the
    complete mip, and calls `UpdateResource`.
- [ ] **PERF-06 - Confirmed**
  - **Phase:** 5/8
  - **Finding:** Cover provider and slot work has quadratic paths and duplicated allocation bodies.
- [ ] **PERF-07 - Confirmed**
  - **Phase:** 7/8
  - **Finding:** Squad, avoidance, and collision scan broad entity sets and allocate avoidable
    per-tick containers.
- [x] **PERF-08 - Fixed**
  - **Phase:** 5/8
  - **Finding:** Network turn/root histories remain pruned to a 256-turn window. Replay v9 streams
    applied opaque turns and checkpoints, retains only the bounded future-input tail, and indexes
    bounded frames for lazy decode. A 68.28 MiB regression crossed the retired whole-body ceiling
    with at most one resident turn batch. Synchronous checkpoint/file-flush cost remains performance
    measurement work, not an unbounded-retention defect.
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
  - **Finding:** Legacy `ComputeStateHash` parallel dispatch is budgeted by storage count and normally
    leaves the entity walk serial. That path is now deprecated and local-only; production
    canonical-root encoding and hashing is a separate synchronous cost to profile.

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
- [ ] **API-08 - Confirmed**
  - **Phase:** 5
  - **Finding:** Nav request/query types and A* honor `BlockedTerrainTags` and `NavLayerMask`, but
    shipped movement authoring does not carry per-unit blocked-terrain policy end to end: MoveTo
    escalation leaves the tags empty and containment still uses the default ground mask.
- [ ] **API-09 - Confirmed**
  - **Phase:** 6/7
  - **Finding:** FoW initialization, substrate mutation, and canonical state are wired, but the
    advertised `RegisterSource`/`UnregisterSource` and `RegisterBlocker`/`UnregisterBlocker` hooks have
    no callers; the default scans component storage directly.
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
  - **Finding:** Core and Net state machines remain concentrated in roughly 12.4k-line and 7.3k-line
    implementation files even after canonical-root serialization moved to its own unit; split
    internal responsibilities without widening public seams or changing execution order.
- [x] **API-14 - Verified**
  - **Phase:** 5
  - **Finding:** Public reads are const-only; mutation uses explicit guarded `*Mutable` accessors.
    The framework, extensions, and tests were migrated, adversarially reviewed, and passed Editor,
    Shipping, Unit, Integration, Determinism, and Editor.Snapshot gates.

## Feature and completeness checklist

- [x] **FEAT-01 - Fixed**
  - **Phase:** 5
  - **Work:** Coordinator-selected bounded checkpoint plus exact opaque command-tail reconnect is
    built. The receiver adopts stopped, catches up through the normal gate, and activates only after
    exact frontier continuity and canonical-root agreement. True multi-process PIE/cooked behavior
    remains the final runtime oracle.
- [x] **FEAT-02 - Fixed**
  - **Phase:** 5
  - **Work:** Trusted-local replay v9 now provides append-only digest-chained frames, mandatory
    tick-zero and periodic checkpoints, exact opaque turns, durable frontiers, crash-tail recovery,
    bounded indexed/lazy reads, and checkpoint seek/catch-up. One journal is closed per lockstep
    epoch across NewMatch/ContinueMatch travel; frozen v8 remains readable. Focused automation is
    green; PIE replay/load/seek remains the final runtime oracle. Hostile import/cloud authentication
    and frozen-time replay are explicit future adapter/policy boundaries.
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

The current writer emits v9 append-only executable journals owned exclusively by `SeinARTSNet`; the
reader also preserves frozen bounded v8 compatibility. Full recordings start at tick zero, retain
every applied assembled turn including empty heartbeats, stop on the exact inclusive `EndTick`, bind
command decoding to the world's frozen schema/name catalog, and carry the agreed bootstrap receipt.
V9 adds bounded long-session storage, crash-tail recovery, periodic checkpoints, and seek/catch-up
without changing the exact command bytes used by multiplayer fan-out.

The similarly named CoreEntity Blueprint helpers are bounded v6
**header-metadata-only** documents. The ambiguous legacy nodes remain as deprecated wrappers and
cannot create or load an executable journal. Shared/imported/cloud replay remains an adapter boundary
that must authenticate and bound the complete artifact before placing it in the trusted-local lane.

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
