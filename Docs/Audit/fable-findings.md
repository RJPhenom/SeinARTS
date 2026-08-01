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

**Post-merge status:** committed as `af19b64` on `codex/audit-remediation`; local `main`
fast-forwarded `d22ba71` → `af19b64` (closes the CONTENT-03 hazard locally; `origin/main`
still stale until pushed). API-14 ledger row → Verified (Editor + Shipping builds green;
Unit 352 / Integration 12 / Determinism 19 / Editor.Snapshot 8, All profile).

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

---

## 7. STATE-01 next chunk — design map (2026-07-29 late evening; investigation only, no code yet)

Deep-read of the canonical-state seams (full file:line detail preserved in the session log;
summary here is the working spec). Sol's claim/fail-closed pattern already ships for
**Navigation** (`ComputeStateCoverageClaim` + `ComputeStaticEnvironmentDigest`, fail-closed
base virtuals, native-subclass tripwire, claim folded into the world-binding frame),
**FoW** (class-keyed codec registry with subclass admission policy), **Movement/Avoidance**
(per-native-layer coverage registry), and **pool objects** (resolver/ability codec registry).
The remaining STATE-01 scope decomposes into four deltas:

1. **Reverse coverage check** — `TryBuildExecutionTopologyCandidate` rejects a system that
   names a missing contributor, but a registered contributor claimed by NO system is silently
   accepted (orphaned state gets captured/restored with no declared owner). Needs a descriptor
   marker before it can be enforced, because legitimately system-less contributors exist
   (FoW's authoritative provider is owned by a subsystem, not a ticked system).
2. **Port the claim pattern to Collision + Cover (+ Level Data)** — `USeinCollisionResolver`
   and `USeinCoverSystem` have zero coverage surface: no claim virtual, no static digest, no
   world-binding frame, silent snapshot proceed with a custom class active. The collision
   base's "ActiveOverlaps is transient" contract is docstring-only, and `OnSnapshotRestored`
   can simply not be forwarded by a subclass. `USeinLevelData` has the post-freeze mutation
   gate but contributes no binding frame. Mechanism is prescribed (copy the nav shape);
   no design fork.
3. **A* static-digest tamper-evidence** — `StaticGridDigest` is computed once at
   `LoadFromSubstrateImpl` and cached; per-tick revalidation re-emits the cached GUID and
   checks only array LENGTHS. An in-place `CellCost[i]`/`CellConnections[i]` write that keeps
   lengths is invisible to `ValidateFrozenCanonicalStateWorldBindings`. Fork: per-tick content
   re-hash (exact, but a full grid walk every tick) vs. a write-barrier/generation counter
   folded into the frame (cheap, catches all gated writers; a rogue direct memory write still
   escapes, as it would under any non-rehash scheme).
4. **Formation/resolver preview isolation** — `SeinComputeFormationPreview` hands the LIVE
   pooled resolver (whose reflected state is captured in the canonical root) to
   `USeinFormationPreviewSubsystem`, a per-render-frame, hover-driven, one-client-only path.
   A designer subclass writing a member UPROPERTY inside `ResolveFormationLayout` diverges
   the canonical root from mouse movement. Note statefulness on the COMMIT path is
   deterministic and captured — the hazard is exclusively the preview borrow. `USeinFormation`
   is CDO-invoked with a docstring-only statelessness contract and no admission gate; the
   editor determinism validator warns on float/RNG but not on member writes, and covers
   neither C++ subclasses nor resolvers. Fork: scratch-instance preview (prevention) vs.
   reflected-property digest tripwire around the preview call (detection→invalidate) vs.
   CDO-only preview (breaks preview===commit for stateful-config resolvers).

Recommendations put to RJ (decisions pending as of this writing): generation counter for (3);
scratch preview instance refreshed off the pool-codec field digest for (4); descriptor-level
`ExternallyOwned` marker + orphan rejection for (1). Delta (2) proceeds regardless once
sequencing is confirmed.

### 7.1 Implementation status (same evening — RJ approved all three recommendations)

- **Delta 3 SHIPPED** — `USeinNavigation::GetStaticEnvironmentGeneration()` (base returns 0);
  the A* bumps a private `StaticGridGeneration` on every substrate adoption (its only grid
  writer); the nav subsystem latches the value at freeze/commit and fail-stops per-tick
  recapture on drift ("Navigation static topology mutated in place after the match
  StateContract froze."). Deliberately NOT in the peer-compared digest/frame — it counts local
  adoption events, not content, so identical bakes loaded a different number of times still
  agree across peers. Regression test `PostFreezeInPlaceGridMutationFailStopsDespiteCachedDigest`
  proves a bumped generation with an UNCHANGED cached digest still fail-stops.
- **Delta 1 SHIPPED** — `FSeinCanonicalStateDescriptor::bExternallyOwned` (folded into the
  descriptor digest as "externally-owned"/"system-claimed"); topology freeze now rejects any
  frozen contributor neither system-claimed nor marked ("Canonical-state contributor '%s' is
  claimed by no registered simulation system and is not marked externally owned."). Nav + FoW
  providers marked (their claiming systems register conditionally; nav-/FoW-disabled worlds
  must still bootstrap); movement stays system-claimed. Contributors referenced only by a
  PROVIDER's coverage claim (e.g. a stateful custom nav's supplemental contributors) also mark
  ExternallyOwned — the owning subsystem separately verifies their existence. Four test
  fixtures needed markers; two new topology tests cover reject + accept.
- **Delta 4 SHIPPED** — `SeinComputeFormationPreview` never drives the live pooled resolver
  (or shared CDO): it swaps in a transient scratch clone materialized through the pool-object
  codec (`CaptureObject`/`MaterializeObject`, so "captured state" keeps exactly one
  definition), cached per source instance and refreshed only when the source's captured bytes
  change. Codec-clone failure falls back to the source object with a Verbose log (equals
  pre-isolation behavior). Residual: `ComputeMultiBrokerAnchors`' INTERNAL CDO use is shared
  with the commit path and stays unswapped — a stateful formation/resolver CDO remains a
  documented hazard until the formation admission gate exists.
- **Delta 2 Collision half SHIPPED** — `USeinCollisionResolver::ComputeStateCoverageClaim` +
  `ComputeResolutionConfigDigest`, both fail-closed on the base; shipped Default/Parallel claim
  Stateless with native-subclass tripwires (Parallel's digest covers NumPasses + Relaxation);
  new binding-only contributor `seinarts.collision/resolver-binding` (DerivedCache,
  ExternallyOwned) folds claim + config digest into the per-tick-revalidated world-binding
  frames; None=OFF worlds emit an explicit "disabled" frame. Tests (3/3 green):
  unclaimed-custom bootstrap reject, claimed-subclass accept, post-freeze NumPasses drift
  fail-stop.
- **Delta 2 Cover half SHIPPED** — same pattern ported to the Cover extension (claim types +
  fail-closed base on `USeinCoverSystem`, `USeinCoverDefault` tripwire + reusable Stateless
  claim, `USeinCoverSubsystem` freeze/latch/invalidate lifecycle, binding-only contributor
  `seinarts.cover/system-binding` DerivedCache + ExternallyOwned, module-lifetime registration,
  4 extension tests). Deliberate deviations from nav: no static digest (no baked topology), no
  PrepareWorldBinding, no generation counter; restore-into-fresh-world drift is backstopped by
  core's stored-frame per-tick compare (verified: a cover frame mismatch fails restore via the
  sealed contract-digest check, it cannot silently pass).
- **Deferred:** Level Data substrate coverage claim (post-freeze mutation gate already
  exists; a full claim/binding frame mirrors the same pattern when prioritized); formation
  statelessness admission gate (Gate F adjacent).

### 7.2 Adversarial review of the wave (same night) and fixes applied

Independent red-team over the full working-tree diff. Three confirmed findings, all addressed
or dispositioned:

1. **Preview scratch cache lifecycle (CONFIRMED bug, FIXED)** — the scratch was outered to the
   calling world's subsystem while held by a process-lifetime rooted cache: a preview in any
   match would pin the dead world at map travel (fatal world-cleanup check), and CDO-keyed
   entries were immortal and shared one scratch across PIE clients. Fix: scratch now outers to
   the transient package (never pins a world), the cache keys per (source, world) so clients
   never share a scratch, and entries prune when source OR world dies.
2. **Fail-open class fallback (pre-existing, partially addressed)** — a set-but-unloadable
   custom collision/cover class silently falls back to the shipped default, so the new gates
   certify the default rather than failing bootstrap. This is the established picker
   convention ("a mistake is not an off-switch": fallback + logged error) — collision already
   logged; Cover's silent fallback now logs an Error. RESIDUAL for the ledger: the coverage
   gate guarantees apply to the class that actually LOADED; asymmetric peer staleness fails
   loudly via the class path in the binding frame, but symmetric misconfiguration bootstraps
   on the default with only the error log. Making load-failure fail-closed is a convention
   change that belongs to RJ.
3. **Nav/FoW `ExternallyOwned` weakens the orphan gate for shipped state (design residual,
   RECORDED)** — because their claiming systems register only in enabled worlds, both shipped
   payload-bearing contributors are marked ExternallyOwned, so the reverse check materially
   protects only the movement contributor today. The airtight version is a per-world
   evaluation ("claimed by a system OR the owning subsystem reports itself disabled"), which
   needs an enabled-query seam on the contributor descriptor — queued as a refinement
   decision for RJ/Sol rather than redesigned unilaterally.

Sharpenings also applied from the review: preview clone-failure fallback logs upgraded
Verbose→Warning; the system-claim key comparison re-lowercases FName round-trips (closes a
load-order-dependent false bootstrap failure if a mixed-case name ever pre-exists);
`ContractFormatVersion` bumped 1→2 to make the descriptor-digest ownership-frame change an
explicit compatibility cut (pre-wave checkpoints/replays refuse to restore by design).
Accepted narrowing (documented, no change): a resolver whose layout output depends on mutable
reflected members previews from a snapshot that refreshes only when the source's captured
bytes change — such members are already a commit-path desync bug; prevention beats fidelity.
One pre-existing test invariant corrected: contributor-record capture skips binding-only
DerivedCache contributors by design, so the reload-retarget test now compares against the
persistent-contributor count.

**Final evidence for the wave:** Editor build green; `SeinARTS.Unit` 362 (All profile, incl.
4 Cover + 3 collision + 3 core new tests), `SeinARTS.Determinism` 19, `SeinARTS.Integration`
12, `SeinARTS.Editor.Snapshot` 8 — all green on the completed tree (final Unit rerun after
the red-team fixes pending at this line's writing; superseded by the run IDs in Saved/Automation).

---

## 8. FEAT-01 — authenticated checkpoint + command-tail resync (2026-07-30, Fable)

Built on the trusted-envelope adoption foundation, per RJ's picks: **both** flows in one wave
(resync of a stalled/desynced peer AND late-join into an existing/vacated slot — growing the
membership mid-match stays FEAT-10) with **immediate root-handshake activation**.

### 8.1 What shipped

- **Core catch-up window** (`Begin/EndResyncCatchUpWindow`): while open, CaptureSnapshot
  refuses (a pre-frontier peer must not emit checkpoints) and the scheduler pump runs a
  **catch-up burst** — the wall-clock accumulator is topped to a full MaxTicksPerFrame budget
  each pump, because real-time accumulation can never close a wall-clock deficit. Sim-safe:
  the accumulator is a scheduler, not sim state; the lockstep gate still bounds the burst to
  the turns actually available.
- **Coordinator retained tail**: the EXACT opaque fan-out bytes of every committed turn,
  bounded by the shared 256-turn protocol window (the replay writer is deliberately NOT the
  tail source — its cap is abort-and-discard). Serving a tail re-sends those bytes through
  the same Client_ReceiveTurn as live delivery.
- **Bounded transfer**: `SeinSnapshotTransfer` maps a captured v13 snapshot into the (until
  now consumer-less) snapshot envelope codec — one Authoritative section, constant schema/
  descriptor digests, prefix cross-checks — chunked 48 KB × 4 per turn boundary over new
  reliable relay RPCs (a one-frame burst would overflow the reliable buffer and kick the
  peer). 10 new RPCs total, all identity-from-relay-ownership per the authority decisions.
- **The flow**: request → slot flips Reconnecting (heartbeats keep the gate healthy; open
  turns are back-filled at the flip, mirroring OnLogout — this is what lets a resync REVIVE
  an already-stalled session) → live-boundary capture (no sim stall) → paced transfer →
  client adopts stopped (PreserveCurrent + RemainStopped) under a one-shot restore authority
  → tail through the NORMAL delivery path → burst catch-up → ready → activation root
  handshake at an agreed future boundary (server turn + InputDelay + 2; re-ready
  reschedules) → on exact root agreement the slot returns to Connected with authorship from
  FirstAuthoredTurn (heartbeat coverage guaranteed through FirstAuthoredTurn − 1, so the
  first real submission can neither collide with a heartbeat nor leave a gap).
- **Self-healing triggers**: a peer receiving a live turn far beyond its window, or a world
  still Awaiting bootstrap while turns flow (late join), auto-requests a resync; a slot
  parked in Reconnecting with no serve demotes to Dropped after the AI-takeover window so
  the AI fallback resumes; serves time out at 120 s; a client-side failure aborts the serve
  and reconciles its cursors. Reconnect into a launched match (bootstrap Consumed) now lands
  in Reconnecting instead of instantly Connected-with-stale-state; pre-launch rejoins keep
  the legacy Connected path so session start cannot deadlock.

### 8.2 The red-team cycle (this is why the loop exists)

The first implementation passed all suites — and an adversarial review then found a FATAL
flaw plus nine independent wedge paths, none reachable by the automation harness: no
fast-forward mechanism (catch-up could never terminate under real latency), the flip never
back-filled open turns (wedging exactly the stalled session resync exists to recover),
one-frame chunk bursts (reliable-buffer kick loop), stranded serves with no timeout, the
wrong "launched" signal (pre-launch rejoin deadlocked match start), terminal Reconnecting
parking, stale received-turn blockage, a delayed root-checkpoint session kill, activation
boundary races, and a failure-path submission burst. All ten were fixed; a verification pass
then confirmed 9 fully closed with mechanism-level evidence, caught that fix #8 introduced a
NEW inverse race (a root boundary completed during suppression can never be back-reported →
expiry kills the session blaming the recovered peer), which was closed with per-participant
root-report exemptions through FirstAuthoredTurn − 1.

### 8.3 Evidence and residuals

Evidence: Editor build green; Unit 365 / Determinism 19 / Integration 12 / Editor.Snapshot 8
(All profile). New tests pin: the catch-up burst (>1 tick per pump under the window), the
capture gate, envelope round-trip + tamper/truncation rejection, and the load-bearing
end-to-end — a checkpoint captured on a LIVE world, transferred through real envelope bytes,
adopted stopped, caught up, produces a bit-identical canonical root at a common boundary.

Residuals (recorded, deliberate):
1. **True multi-process E2E is PIE/cooked verification** — the automation harness cannot run
   two networked processes; the RPC state machine's live behavior is RJ's-oracle territory
   (`Sein.Net.RequestResync` on a client; `Sein.Net.SimulateDisconnect` to stage it).
2. **No activation-failure retry signal**: a root-divergent activation parks the slot until
   a manual re-request (then demotes to AI) — divergence warrants a deliberate retry, but
   there is no player-facing surface yet.
3. **Practical checkpoint ceiling ~49 MB** (pacing × 120 s timeout) despite the codec's
   256 MiB bound — oversized checkpoints fail cleanly and retry; worth a guard someday.
4. Host self-resync refused by design (the coordinator IS the timeline).

---

## 9. PIE regression: factionless bootstrap refusal (2026-07-30, fixed as `04d75d4`)

RJ's first PIE since the audit branch began could not load the baseline Sandbox map:
"Match bootstrap failed closed: Local match bootstrap settings are invalid
(SeinARTS.Command.Reject.Malformed)." Root cause: the `aea047a` hardening made occupied
match slots require a valid `FactionID` at THREE independent sites (ValidateMatchSettings,
the bootstrap transaction's active-slot check, and the initial-state digest). Factions are
an opt-in catalog — RJ's project has zero faction assets, the lobby auto-claim assigns
none, and the sim registers such players as Faction(0) (digest-covered either way) — so the
requirement refused bootstrap for every factionless project.

**Why 396 green tests missed it:** every fixture that authored slots authored factions, and
the shared `FSeinMatchSettings()` fixture has EMPTY slots, which bypasses the occupied-slot
checks entirely. A genuinely PIE-only hole. All three sites now accept factionless slots
(faction-required stays available as game-mode policy), and a regression test walks the full
materialization path with a factionless Human slot. Note for Codex: when hardening a
validation surface, add the *baseline-project* fixture (default-config, content-less) to the
suite alongside the fully-authored one — the shipped Sandbox is the real contract.

---

## 10. PIE regression: unbaked PlayerStart transform (2026-07-30/31, fixed) + deferred PIE triage

Second gate behind §9, same `aea047a` era: "PlayerStart ... has no baked fixed transform;
re-save the level." The invariant is CORRECT (editor-baked `PlacedSimTransform` — load-time
float→fixed is not cross-arch identical), but the remedy was broken twice over:
`PostEditMove` only bakes on an actual MOVE, and a clean (non-dirty) package never reaches a
save at all, so "re-save the level" could not repair a legacy level. Fixed in
`ASeinPlayerStart` with three bake paths, all touching ONLY unbaked starts (a baked value is
never recomputed — sim data must not change silently on save):
- `PostEditMove` — authoritative re-bake on designer move (pre-existing);
- `PostLoad` (editor-only body) — self-heal a legacy placement in memory at level load, so
  PIE passes with no save ritual; reads the root's serialized RELATIVE transform (composed
  world transform is not trustworthy at PostLoad); cooked builds compile it out and still
  fail closed;
- `PreSave` — belt-and-suspenders for an unbaked start reaching a real save.
Lesson for the ledger's next hardening wave: a fail-closed check on ASSET state needs a
shipping upgrade path for legacy content, verified from a stale asset, not just fresh fixtures.

### Deferred PIE triage (2026-08-01, RJ standalone session) — none are red flags
1. **Legacy-fingerprint exclusion warnings** (`Component '...' has field(s) excluded from
   the legacy local state fingerprint`): benign BY DESIGN — dev-only, once per component
   type at storage creation; the canonical world root covers those fields. Noisy in every
   PIE. Deferred polish: emit only when the legacy fingerprint is actually consulted
   (`Sein.Sim.StateHash` / its log CVar). NOTE: several suites `AddExpectedError` on this
   text — changing emission timing requires a full suite run.
2. **Ray-tracing geometry over budget** (3.2 GiB vs 400 MiB default pool): render config,
   long-standing, orthogonal to the audit. Perf-pass item (HWRT settings vs budget raise —
   RJ's visual-intent call).
3. **Video memory exhausted** (~884 MB over): new symptom, likely the same RT-geometry
   pressure — standalone PIE is a second process duplicating GPU residency. Perf-pass item.
RJ reports further issues in 2-player listen-server PIE — not yet enumerated here.
