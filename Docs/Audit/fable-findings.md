# Fable Findings — Adversarial Re-Review of the Audit Remediation

**Author:** Claude (Fable 5) · **Date:** 2026-07-29 · **Audience:** Codex (Sol) — the remediation owner
**Branch reviewed:** `codex/audit-remediation` at the working tree that became `aea047a`
("commit WIP audit all done by codex Sol, before integrating fable", 2026-07-29 09:30 EDT)

This file logs the outcome of the second adversarial pass (multi-agent, with a live build run)
over the remediation campaign, plus the still-open items from the first pass. The first pass
(2026-07-19) is written up in full at `Docs/Engineering/claude-adversarial-audit.md`; this file
is the delta and the consolidated punch list. The ledger referenced throughout is
`Docs/Engineering/AuditRemediation.md`.

---

## 1. Headline verdict

**The ledger is honest everywhere checked.** Nothing claimed Fixed/Verified was found unfixed,
in either pass. Specifically confirmed in live code on 2026-07-29:

- **COR-01 (Verified) — confirmed.** Real deny-by-default authority policy. `IssuerKind` is
  decoded from the wire but discarded: server-side identity stamping
  (`StampAuthoritativeCommandBatch`) re-derives PlayerID/IssuerKind/DerivedResourcePayer/Tick
  at all four ingress paths.
- **STATE-03 (Fixed) — confirmed.** Receipt-consensus bootstrap transaction with terminal
  failure paths, matching approved Gate G.
- **API-01 (Verified) — confirmed.** Registry-driven command dispatch is real; note the
  built-in if/else chain was *demoted behind one registered handler*, not removed — fine as a
  mechanism, but the ledger wording could be read as a full decomposition.
- **Phase 5 is well past its ledger checkpoint:** full canonical-state registry (BLAKE3-128
  root; Authoritative/Continuation/DerivedCache roles; restore DAG), snapshot v13 with latent
  action codecs (Wait + MoveTo only so far), movement/FoW/nav-async capture, wired into
  snapshot + hash + replay + peer compare. Test suite grew 210 → 292 in the week before the
  commit; tallies were verified against `index.json` files on disk, not just the ledger.
- **Build green** (25.5 s incremental at review time).

**Currently red:** `SeinARTS.Editor.Snapshot.Movement` — 4 failures, all in BP MoveTo
continuation capture. This is the Gate B frontier, not a regression.

**Resolution (2026-07-29):** the focused movement snapshot suite is now green 8/8. This clears the
four concrete BP MoveTo failures; Gate B remains open for the complete continuation fallback and
future-affecting system/provider coverage contracts tracked by `STATE-01`.

---

## 2. New residuals found in the 2026-07-29 pass (not yet in the ledger)

These were found *around* the verified fixes — none invalidates a Verified/Fixed status, but
each deserves a ledger row or an amendment.

### 2.1 Snapshot restore re-trusts stored `IssuerKind` (hole in the COR-01 perimeter)

`SeinWorldSubsystem.cpp:7852-7855`: restore clears then re-adds `InSnapshot.PendingCommands`
verbatim. The restore-side validation (`:7126-7132`) checks the enum value is valid and not
`Unauthenticated`, but the commands are **not re-stamped** through
`StampAuthoritativeCommandBatch` — a doctored snapshot can inject
`MatchAdministrator`/`DeterministicSystem` commands that live ingress would have rejected.
Contained fix: re-stamp (or at minimum re-authenticate issuer claims against the server-side
binding) on the restore path, same as the four live ingress paths.

**Resolution (2026-07-29) — reframed and remediated.** The original remediation
recommendation does not apply to checkpoint adoption: snapshot `PendingCommands` are already
canonical continuation state, not new live ingress. Re-stamping them would corrupt captured
`DeterministicSystem`, `MatchAdministrator`, or `Player` issuer provenance, resource-payer
identity, and scheduled tick, breaking exact continuation and peer agreement. Core now preserves
pending commands exactly and requires an exact, one-shot, world-scoped trusted-envelope authority
to adopt a snapshot. The design is topology-neutral: an authenticated coordinator selects the
checkpoint source and claims the destination world's adoption capability; Core neither reconstructs
nor assumes a host/server topology.

The capability authorizes adoption procedurally; it does **not** authenticate hostile snapshot
bytes or make arbitrary storage a trusted source. Production authenticated/bounded envelope
adapters, plus full checkpoint-and-authenticated-command-tail catch-up/resync, remain **FEAT-01**.

### 2.2 Ledger wording overclaims admin gating on pause/concede

Pause and concede are **Self-scope by design**; only `EndMatch` is actually admin-gated. The
behavior is fine (and arguably correct), but the ledger's authority-policy row reads as if all
match-control verbs are admin-gated. Amend the wording, not the code.

### 2.3 `GetTypeHash(FName)` still feeds the state hash in two places (STATE-02 residual)

- `ComponentStorage.h:302` — `GetTypeHash(Prop->GetFName())` per property.
- `SeinActiveEffect.h:114` (and the same pattern at `:37`) — effect/grant class FName hashes.

`GetTypeHash(FName)` is process-local (name-table index), so these are exactly the class of
identity STATE-02 exists to purge. The canonical-state registry's frozen name catalog is the
obvious replacement source.

### 2.4 Canonical-provider coverage is narrow

Only 3 canonical providers are registered. No Squad, no Cover, no collision-broadphase
provider — those systems' future-affecting state rides outside the canonical registry's
root/restore DAG. Fine as a phased rollout; worth an explicit coverage list in the ledger so
the gap is a decision, not an accident.

### 2.5 Test-tally blind spot: aborted runs leave no `index.json`

Some Automation output dirs lack `index.json` (aborted runs), making those runs invisible to
any tally that walks index files. A silently aborted suite can read as "no failures."

---

## 3. Still outstanding from the 2026-07-19 report (full detail in claude-adversarial-audit.md)

| # | Item | Severity | Status as of 2026-07-29 |
|---|---|---|---|
| 1 | **`main` ships seven silently-zeroed FFixedPoint assets** since serializer flip `3f10eef` (2026-06-09): `SA_Build` MaxRange=0, `SA_ThrowSmoke` AreaRadius=0, `SE_UnlockVehicleDepot` Duration=0, **four production costs empty (free production)**. A mixed resaved/stale peer fleet silently desyncs; a uniform stale fleet is deterministic-but-wrong. | **HIGH** | No `CONTENT-03` ledger row; resaves not cherry-picked to `main`. Unaddressed. |
| 2 | **No test pins the FFixedPoint 8-byte native layout / `WithSerializer` trait** (proposed `SER-01`). An accidental trait removal silently flips the on-disk format. | Med | Unaddressed. |
| 3 | **NAV-02 only half-fixed.** The request-identity guard landed; the actual mechanism — drain-wipe-before-poll (`DrainAsyncPathQueue` starts with `AsyncResults.Reset()`) plus interval repaths waiting a full interval on Throttled — remains, so interval repaths can still starve indefinitely in busy scenes. | Med | Half-fixed. |
| 4 | **Replay 64 MiB cap = abort-and-discard** (`SeinReplayWriter.cpp`): cap exhaustion discards the *entire* recording; plus a per-turn full wire-encode just to measure candidate size. Trades unbounded growth for losing long-match replays. | Med | Unchanged (FEAT-02 still open for streaming/retention, so may be planned). |
| 5 | **`GetNormalized` numeric change is BP-exposed** via `NormalizeQuaternion` — shipped C++ sim is A/B-inert, but designer BP graphs calling it change results. MATH-01's Verified status should carry this note for the PIE A/B gate. | Low | Ledger note still missing. |
| 6 | **Amend NAV-03 / PERF-10 / PERF-02 framings** (consequence unsupported by shipped code; NavContainment is the real PERF-10 cost; PERF-02 is churn not retention). Details in §2.3–2.5 of the 7-19 report. | Low | Rows unchanged. |

---

## 4. Suggested priority order for the next work

1. **CONTENT-03** — the zeroed-assets-on-main hazard is the only item that can desync (or
   silently mis-run) a real fleet *today*. Add the ledger row; cherry-pick or early-merge the
   seven resaves.
2. **Gate B frontier (focused failures cleared)** — keep the now-green BP MoveTo coverage pinned,
   then close the complete continuation fallback and future-affecting system/provider inventory
   tracked by `STATE-01`.
3. **§2.1 trusted snapshot boundary (Core complete; FEAT-01 next)** — the one-shot,
   world-scoped adoption authority now preserves exact canonical continuation provenance. Next,
   add topology-neutral production authenticated/bounded adapters and coordinator-selected
   checkpoint-plus-tail catch-up under FEAT-01.
4. **§2.3 FName hashes** — mechanical once the frozen catalog is the source.
5. Ledger hygiene: rows/amendments from §2.2, §2.4, §3.5, §3.6.

The perf workstream (11 rows, all Confirmed) had not been started as of this review — no
finding here changes that sequencing.

---

## 5. Codex rebaseline — 2026-07-29 current branch boundary

This section preserves the review above as a dated checkpoint while superseding its
current-status statements with the final branch evidence from the continued remediation pass.

### 5.1 Correctness, state, content, and test status

- **SER-01 — Verified.** The native eight-byte `FFixedPoint` serialization layout and
  `WithSerializer` contract are now pinned by automation.
- **CONTENT-03 — In progress.** All seven affected asset blobs are corrected and committed on
  the remediation branch/match hotfix. `origin/main` remains stale until that work is merged.
- **STATE-01 — In progress.** Snapshot v13 now exactly covers Core
  world/entity/component state, ability and resolver pools, canonical Blueprint value slots,
  Wait/MoveTo continuations, Movement/Movement+ policy instances, navigation async
  continuation, and FoW authoritative state. Squad state is component-backed and shipped
  broadphase, blocker, overlap, and cover indexes are derived. Exact continuation remains
  incomplete for arbitrary Blueprint VM latent/async frames, post-freeze navigation substrate
  mutation, unenforced stateful formation/resolver preview paths, and custom
  navigation/collision/cover implementations without explicit state-coverage claims. Quiescent
  capture continues to fail closed on deferred effect/destroy and replay-ingress work.
- The shipped Cover restore path now rebuilds its default and custom derived registry in
  canonical order with deterministic tie-breaks. That closes the concrete Cover restore seam;
  it does not close the remaining `STATE-01` custom-provider coverage contract.
- **STATE-02 — Verified.** Peer comparison and pause success use authoritative canonical
  BLAKE3-128 roots. The legacy 32-bit hash is deprecated and restricted to local diagnostics.
  A fresh-process 120-tick serial/parallel run matched exact roots and poses.
- **TEST-01 — Verified.** `attempt.json` is written before launch and finalized for build failure,
  missing-report/test failure, and pass outcomes. Suite/profile baseline lookup is case-insensitive;
  canonical broad suites fail closed when a baseline is absent; ancestry is checked. Clean commit
  `9a991f544a59d1b63395fb8a9a783c6d7d1c2e30` reproduced all six broad-suite floors, including Unit
  321/317, and now owns their checked-in provenance.

### 5.2 Performance and API rebaseline

- **PERF-01 — In progress.** Authoritative peer/pause comparison has moved to canonical roots,
  but canonical root construction is still a synchronous full-state walk and is not shared
  across consumers.
- **PERF-05 — Confirmed.** The minimap keeps a stable texture allocation, but each refresh
  still allocates full CPU buffers, performs dense resolution-squared world-to-fixed fog
  lookups plus optional blur, copies the full mip, and calls `UpdateResource`.
- **PERF-08 — In progress.** Net histories are pruned to 256 turns and replay memory is capped
  at 64 MiB, but exhausting that cap still aborts and discards the complete recording; streaming
  and retention remain open.
- **PERF-11 — Confirmed.** The deprecated local 32-bit hash still batches storage work while
  walking storage/entity structure serially. The authoritative canonical root is a separate
  synchronous cost.
- **API-08 — Confirmed.** Navigation request/query/A* types support blocked-terrain tags and a
  nav-layer mask, but shipped movement does not carry per-unit blocked tags end to end; MoveTo
  escalation remains empty and containment uses the default mask.
- **API-09 — Confirmed.** FoW initialization, substrate mutation, and canonical-state plumbing
  are wired. Source/blocker registration APIs have no shipped callers, so the default path still
  scans component storage.
- **API-13 — Confirmed.** Current live sizes are approximately 12,389 lines for
  `SeinWorldSubsystem.cpp` and 7,302 for `SeinNetSubsystem.cpp`; the canonical-root
  implementation has moved to a separate approximately 1,123-line unit.

### 5.3 Final evidence

- Cover restore/custom-provider seam: 2/2,
  `SeinARTS.Unit.Cover.SnapshotRestore-20260729-165659-c5d85929`.
- All/Framework Unit: 321/317, `SeinARTS.Unit-20260729-165216-1f242af9` and
  `SeinARTS.Unit-20260729-165243-eb433c77`.
- All/Framework Integration: 12/11, `SeinARTS.Integration-20260729-165310-74b30bc8` and
  `SeinARTS.Integration-20260729-165449-e2e8a874`.
- All/Framework Determinism: 16/15, `SeinARTS.Determinism-20260729-165506-960f69a1` and
  `SeinARTS.Determinism-20260729-165543-fc72725e`.
- Fresh-process serial/parallel A/B: 120/120 ticks with exact canonical-root and pose
  agreement,
  `SeinARTS.Determinism.Process.SerialCollisionTrace-20260729-165621-a09bcfae` and
  `SeinARTS.Determinism.Process.ParallelCollisionTrace-20260729-165638-ffd47bc8`.
- Ordinary Editor builds are green. Shipping initially exposed teardown/restore include gaps;
  the gate caught them, they were fixed, and the clean Shipping rerun was green at 16:57.

The live replay boundary is executable file version **v8** with header metadata **v6**.
Earlier v6/v5 references above or in companion documents are historical checkpoint evidence.

The reconciled 67-row ledger now has **18 closed** (**16 Verified**, **2 Fixed**),
**8 In progress**, **3 Approved**, **23 Confirmed**, **5 Queued**, and **10 Gate**.
The correctness workstream owns 17 of the closed rows; performance and feature work remain the
largest open delivery surfaces.

### 5.4 Remaining action order

1. Merge the seven corrected `CONTENT-03` blobs so `origin/main` no longer carries the stale
   serializer-era assets.
2. Continue `STATE-01` with explicit coverage claims and fail-closed behavior for arbitrary
   Blueprint continuations, mutable navigation substrate state, stateful formation/preview
   implementations, and custom navigation/collision/cover providers.
3. Build `FEAT-01` authenticated, bounded checkpoint-plus-command-tail catch-up on the proven
   canonical snapshot/root foundation.
4. Continue the performance workstream, prioritizing synchronous canonical-state cost,
   minimap refresh cost, and replay streaming/retention without weakening designer-facing seams.

---

## 6. Fable continuation — 2026-07-29 evening: API-14 const-migration completed

**Author:** Claude (Fable 5), picking up from Sol's usage-limit stop. Baseline: `db87535`
("codex wip"). All work below is in the working tree on top of that commit (git stays
RJ-controlled).

Sol's WIP flipped `USeinWorldSubsystem` reads to const-only (`GetComponent<T>` → `const T*`,
const `GetEntityPool()`/`GetEntity()`/`GetComponentStorageRaw()`/`GetPlayerState()`) and added
`RequireMutableStateAccess`-gated `GetComponentMutable` / `GetEntityMutable` /
`GetEntityPoolMutable` / `GetCollisionSpatialHashMutable` / `GetComponentStorageMutable`
(API-14). The call-site sweep was mid-flight — 78 sites across three build waves still failed
to compile. I finished the sweep following Sol's established pattern: genuinely-mutating sites
→ `*Mutable` accessors; read-only sites → const pointers / `const FSeinEntity&` lambda params.

**Files touched (working tree):** CoreEntity — SeinAbilityBPFL, SeinEntityControlBPFL,
SeinSimMutationBPFL, SeinAIBPFL, SeinEntityLookupBPFL, SeinDefaultCommandBrokerResolver,
SeinWorldSubsystem.cpp (14 internal sites incl. the deferred-teardown containment ternaries),
SeinActorBridgeSubsystem, CommandBroker/Production/Cooldown/AbilityTick system headers; FoW —
SeinFogOfWarDefault (3 pool lambdas), SeinFogOfWarVisibilitySubsystem; Movement —
SeinMovementDriverSystem (mutable pool + guard-bail), SeinARTSMovementModule debug draws
(const); Squad extension — SeinSquadSystem (mutable pool; 9 sites), SeinSquadMutationBPFL,
SeinSquadDispatchResolver, SeinAbility_SquadReinforce; tests —
AbilityCallbackSafetyTests, BrokerCallbackSafetyTests, ProductionCostOwnershipTests,
SnapshotBootstrapCheckpointTests, EffectLifecycleTests, MoveToContinuationEditorTests.

**Verification (all on the completed working tree):**

- Editor build green, all 17 modules relinked (incl. Cover, CoverSquad, MovementPlus).
- `SeinARTS.Unit` (All profile): **352/352** — `SeinARTS.Unit-20260729-203031-208f330e`.
- `SeinARTS.Determinism` (All): **19/19** — run 20260729 evening.
- `SeinARTS.Editor.Snapshot`: **8/8** — `SeinARTS.Editor.Snapshot-20260729-204118-127f01fb`
  (Gate B movement coverage stays green through the migration).
- Independent adversarial review (multi-agent): **zero confirmed problems.** It enumerated
  every set-site of `bReadOnlyCallbackInProgress`/`bObserverCallbackInProgress` (~23 scopes)
  and verified every migrated `*Mutable` site is unreachable under those guards today, every
  const-left site never writes, no `const_cast` was introduced, and both `ForEachEntity`
  overloads iterate identically — zero value/order change in any reachable context.

**Latent-hazard notes from the red-team (no action taken; recorded for the ledger):**

1. `SeinSquadDispatchResolver.cpp:138` — a guard-refused `GetComponentMutable` nullptr would
   not bail but silently flip `bHadBrokerData` into the "no broker data" semantic branch.
   Unreachable today (ResolveDispatch only runs from PostTick dispatch), but it is the one
   migrated site whose guard-failure mode is reinterpretation rather than refusal. A one-line
   early-bail would make it uniform.
2. `SeinMovementDriverSystem.h` / `SeinSquadSystem.h` — the new `if (!Pool) return;` skips a
   whole system tick if ever run under a guard. Dead branch today (systems tick only from
   `TickSystems`); flagged in case a future refactor ticks systems re-entrantly.
3. Compile proof covers the Editor Win64 Development target; Sol's 16:57 Shipping green
   predates this sweep, so the next Shipping build re-proves it (sources are target-agnostic —
   low risk).
