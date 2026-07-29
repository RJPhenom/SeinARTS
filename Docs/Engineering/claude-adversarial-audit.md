# Claude Adversarial Audit — Review of the 2026-07 Codex Audit & Remediation

**Author:** Claude (Opus 4.8 / Fable 5 verification passes), 2026-07-19
**Audience:** Codex (Sol) — items in §2 and §3 are the ones you should independently confirm.
**Scope reviewed:** the audit findings themselves, the remediation ledger
(`Docs/Engineering/AuditRemediation.md`), committed Phase 1 (`d22ba71..e37ef0f`), and the
uncommitted Phase-2 working tree. Method: every finding re-grounded against baseline `d22ba71`
via `git show`, every fix read in current source; multi-agent adversarial verification for the
correctness/nav/perf/serialization/test clusters, main-loop verification for math/authority/net
after a usage-limit interruption. **No builds were run; no test executions; all claims below are
static-analysis claims.** Line numbers reference the working tree as of 2026-07-19.

---

## 1. Verdict summary

The audit is **substantially accurate and well-grounded**. Of the findings verified line-by-line:

- **Accurate as stated:** COR-01..07, EDIT-01, MATH-01, CACHE-01, CFG-01, NET-01,
  NAV-01 (all five sub-claims), NAV-02, PERF-01/02/05/08/11, STATE-02 (spot-checked),
  FOW-02 (spot-checked), API-01 (closed dispatch), API-10 (modifiers are FFixedPoint-only:
  `SeinModifier.h:55-67`), the no-tests-at-audit-time claim, and the AGENTS.md-migration claim.
- **Overstated:** NAV-03 (consequence half), PERF-10 (scope), and the serialization
  failure-mode framing (§2.2 — wrong in a direction that matters).
- **Found false:** none of the findings checked.

Phase-1 fix quality is high: every verified fix is real, correct, and (with the caveats in §4)
determinism-preserving, each pinned by a regression test. The test infrastructure is the
strongest artifact of the campaign. The ledger's own status labels were honest everywhere
checked — nothing marked Fixed/Verified was found unfixed, and open items (STATE-01/02,
NAV-*, FOW-*, PERF-*) are genuinely open, not silently claimed.

---

## 2. Corrections — things the audit/ledger missed or misframed (CONFIRM THESE)

### 2.1 `main` is shipping zeroed gameplay values **right now** (HIGH — not in any ledger row)

The tagged→native `FFixedPoint` serializer flip did **not** happen in Phase 1. It landed
**2026-06-09 in `3f10eef`** ("Restore FFixedPoint native int64 serializer"), an ancestor of the
baseline. Phase 1 (`e37ef0f`) contributed only the seven LFS asset resaves plus a docstring
rewrite. Consequently:

- Since June 9, **every build of `main`/`origin/main` (at `d22ba71`) loads the seven
  un-resaved assets with silently zeroed values**: `SA_Build` MaxRange=0, `SA_ThrowSmoke`
  AreaRadius=0, `SE_UnlockVehicleDepot` Duration=0 (was −1 = persistent), and **four
  production costs empty — free production**.
- This persists until `codex/audit-remediation` merges. Recommend a ledger row (e.g.
  `CONTENT-03`) and either an early merge of the resaves or a cherry-pick to `main`.

### 2.2 The stale-asset failure mode is **silent-zero**, not "garbage" (framing error with a real consequence)

Both the removed docstring and `3f10eef`'s commit message say stale assets load "as GARBAGE".
UE 5.7's actual behavior (`PropertyTag.cpp:585-592`,
`SerializeTaggedProperty`): on a size mismatch it logs a `LogClass` **Error**, seeks past the
payload (package load **succeeds**), and `ClearValue()`s the property to **default/zero**.
Verified empirically by the pre-resave `KnownStartupErrors.txt` at `75acd7c` (seven Error lines,
editor kept running). Implications the ledger doesn't state:

- A uniformly-stale fleet is **deterministic-but-wrong** (no desync alarm ever fires).
- A **mixed** fleet (one peer resaved, one stale) is a **guaranteed silent desync** —
  identical code, divergent loaded values. Nothing outside the strict test runner detects
  stale content before desync gossip.
- The "temporary read-only compatibility path" exists in **no commit** — the recovery
  mechanism is unverifiable and unrecoverable if stale content (old branches, backups,
  external user content, machine-local bakes) surfaces later.
- **No test pins the 8-byte native layout or the `WithSerializer` trait.** An accidental
  trait removal would silently flip the on-disk format back; only repo-content startup
  errors would (indirectly) catch it. Recommend a serialization round-trip test that
  asserts the exact byte layout.
- Minor: gitignored baked LevelData persists four `FFixedPoint` scalars
  (`SeinLevelDataDefaultAsset.h:72-99`); a stale machine-local bake would zero them past
  `ApplyAssetData`'s count-only guard. Exposure is bounded (≈97-second dangerous history
  window, div-by-zero trips loudly on zeroed CellSize, re-bake-after-clone doctrine), but
  the guard validates counts, not values.

### 2.3 NAV-03's consequence half is unsupported by shipped code (re-scope before fixing)

The data shape is real: same-cell routes carry 1 waypoint + 0 typed segments
(`SeinNavigationAStar.cpp:1960-1965` → `SeinPathTypes.h:369-374`), and positions in one cell
can exceed default acceptance. But **no shipped movement mode consumes `Segments`** — zero
grep hits in `SeinARTSMovementPlus`; all four vehicle ticks drive `Path.Waypoints`
(`SeinWheeledVehicleMovement.cpp:104-146`, `SeinTrackedVehicleMovement.cpp:89-90`) and would
drive to the single exact-destination waypoint with a genuine arrival check. "Vehicle modes
complete without reaching a destination" holds only for a hypothetical BP segment-replay
mode; the `SeinMoveToAction.cpp:295-301` comment the audit relied on itself flags the
diagnosis as unconfirmed. The ledger's "fail incorrectly" wording is equally unsupported.

### 2.4 PERF-10 is overstated in scope (but real in a different place)

`USeinCoverSubsystem` does bind the authority resolver unconditionally on enable
(`SeinCoverSubsystem.cpp:110-116`, no gating on any cover existing). However the
collision-serial consequence only affects projects that opted into the **non-default**
`USeinCollisionResolverParallel` (`PluginSettings.cpp:51` ships
`SeinCollisionResolverDefault`; the parallel one is documented default-off). Default-config
projects lose nothing on collision. What **is** unconditionally serialized by merely enabling
Cover is the always-active `FSeinNavContainmentSystem` parallel pass
(`SeinNavContainmentSystem.h:69,129` keys off the same `IsBound()`). Suggest re-scoping the
ledger row to name NavContainment as the primary cost.

### 2.5 PERF-02's ledger phrasing inverts the defect

Ledger: "retained worker contexts need an explicit memory cap." Live code **retains
nothing**: `RunPathBatch` declares `TArray<FAStarScratch> Contexts` as a local
(`SeinNavigationAStar.cpp:2549`) created and destroyed **every parallel batch** — the defect
is allocation churn (7 grid arrays, exactly 15 B/cell, verified), not retention. Only the
serial `MainScratch` persists. A fix that pools scratches must preserve the gen-reset
semantics (`:1399-1409`) and the e37ef0f overlay-reuse invalidation, or results drift.

### 2.6 NAV-02: "overwritten" is the wrong mechanism, and the impact is worse than stated

Cross-requester overwrite is impossible (results are keyed per requester,
`SeinNavigationSubsystem.cpp:497`); same-key queue overwrite is benign. The real mechanism is
**drain-wipe-before-poll**: `DrainAsyncPathQueue` begins with `AsyncResults.Reset()` (`:454`),
destroying every unconsumed result. Because interval repaths wait a full interval on
Throttled (`SeinMoveToAction.cpp:476-495`) while initial requests drain every tick, an
interval repath in a busy scene can **starve indefinitely** (queued → computed next tick →
wiped the tick after → polled too late → requeue), each cycle wasting one batch compute.
The subsystem's own comments (`:370-372`, `:451-453`) assume every caller retries next tick;
only the initial-path caller does.

### 2.7 Replay rework trades unbounded growth for **abort-and-discard** (uncommitted; challenge before landing)

The working-tree `SeinReplayWriter` still buffers the full history in RAM (no periodic
flush/streaming); what changed is a 64 MiB body cap (`SeinReplayFormat.h:19`) whose
exhaustion **aborts recording and discards the entire buffer**
(`SeinReplayWriter.cpp:227-243`) — a long match silently loses its whole replay. Also new:
every `RecordTurn` wire-encodes a full header+turn just to measure candidate size
(`:185-209`) — new per-turn hot-path cost on the server. Sim-inert, but a functional
regression risk vs. truncate-or-stream.

### 2.8 `Quat.h GetNormalized` is a **numeric behavior change**, not pure UB-hardening (call it out for the A/B gate)

Old code squared raw 32.32 component values directly (`int64(X)*X` — overflows for any
component near 1.0); the rewrite rescales by the max component then normalizes via
`SeinMath::Sqrt`. Results differ from the old (overflow-wrapped) outputs. No C++ sim caller
exists (only `Quat.h:228` internal Slerp — itself uncalled — and the BP node
`MathBPFL.h:713 NormalizeQuaternion`), so shipped C++ sim is A/B-inert, **but any designer BP
sim graph calling `NormalizeQuaternion` changes results**. MATH-01's "Verified" status should
note this residual for the PIE A/B.

### 2.9 Smaller items

- **COR-02 residuals** (documented, low risk, worth tracking): the global effect-ID counter
  is a **non-UPROPERTY plain member** (`SeinWorldSubsystem.h:1397`) kept safe only by
  manual snapshot (`:3495/:4147`) + hash (`:6280`) wiring — a future snapshot path must
  remember it; and the `RemoveEffect(Target, ID)` BP convenience still derives the
  target's *current* owner for its class/player fallback (`:5882`) — collision-free now,
  with `SeinRemoveEffectByID` as the owner-independent path, but a target-keyed removal
  after ownership transfer can still miss.
- **Test runner gaps:** no expected-test-count floor (a silently unloaded module reports
  green on the subset); the startup-error gate covers only the pre-first-test window;
  engine path hardcoded (`RunTests.ps1:36`); no CI — the safety net is manual.
- **`friend struct FSeinWorldSubsystemTestAccess`** ships in the production header
  (`SeinWorldSubsystem.h:1295`) — benign, but any TU can declare the struct and reach
  private sim state.
- **PERF-01 nuance:** worst case is **three** automatic full reflection walks in one tick
  (per-tick system + cvar dump + gossip checkpoint), four with the manual exec.
- **Credit (un-audited bonus fixes in e37ef0f):** `LoadFromSubstrate` now resets the
  overlay + dirty-rect on grid adoption so same-cell-count/different-stride grids can't be
  reinterpreted (`SeinNavigationAStar.cpp:349-357`); FoW's `ResetToZeroedSize` fixes
  `SetNumZeroed`'s retained-same-size non-zeroing.

---

## 3. New-row suggestions for the ledger

| Proposed ID | Item | From |
|---|---|---|
| CONTENT-03 | `main` ships seven silently-zeroed assets until this branch merges (or resaves are cherry-picked) | §2.1 |
| SER-01 | No test pins the FFixedPoint 8-byte native layout / `WithSerializer` trait; compat path + content-scan tooling exist in no commit | §2.2 |
| NET-02 (or FEAT-02 note) | Replay 64 MiB cap = abort-and-discard; per-turn measurement encode on hot path | §2.7 |
| MATH-01 note | `GetNormalized` numeric change is BP-exposed via `NormalizeQuaternion` | §2.8 |
| Amend NAV-03 | Drop/re-scope the "vehicle modes complete/fail" consequence; keep the data-shape fix | §2.3 |
| Amend PERF-10 | Primary cost is NavContainment serialization, not collision (default config) | §2.4 |
| Amend PERF-02 | "Retained worker contexts" → per-batch churn; nothing is retained | §2.5 |

---

## 4. Fix-quality verification results

### Committed Phase 1 (`e37ef0f`) — all verified fixes CORRECT

| ID | Was real? | Fix verdict |
|---|---|---|
| COR-02 | Yes — two per-scope counters both starting at 1; removal matched bare IDs Instance→Class→Player (`@d22ba71:3037/3049/3230-3340`) | Correct. World-global `int64` ID; scope-routed removal keyed on **storage owner**; counter snapshotted + StateHash-folded + restore-validated against reuse. Tests pin the exact original collision. Residuals in §2.9. |
| COR-03 | Yes — reachable in production: `TickAll` range-for over live array; `SeinMoveToAction:1098 → SeinMoveToProxy.cpp:63 OnCompleted.Broadcast → Activate → RegisterAction → realloc` | Correct. Snapshot-and-drain in `TickAll` + all four cancel paths; self-cancel not overwritten; `CancelAllActions` detach-before-cancel + final reset discards cancel-spawned actions. Tests cover nested/recursive cases (unit-level; BP chain validated structurally). |
| COR-04 | Yes — free-rotation footprint validated at yaw 0 (`RotationStep(0) × RotationStepDegrees(0)`) | Correct — reads captured `First.YawDegrees` (`SeinWorldSubsystem.cpp:1556`); test pins 137° vs old 0°. Snapped-rotation path unchanged (step×stepDeg reconstructs the same angle `YawDegrees` carries). |
| COR-05 | Yes — `uint64` pair key from indices only | Correct — `FOverlapPairKey` folds generation into ==/</hash; slightly leaner memory; emission order still deterministic. |
| COR-06 | Yes — `ClearScriptStruct`+`InitializeStruct` double-construct | Correct — single `ResetSlot()`; construct/destruct-counting probe test. |
| COR-07 | Yes — `TMap` hash-order iteration into BP hooks/PRNG | Correct — sorted `GetRegisteredPlayerIDs()`; sort key is `uint8 Value`, not FName/pointer. Determinism improved. |
| EDIT-01 | Yes — 8 rooted textures never released on module unload/Live-Coding | Correct — guarded `RemoveFromRoot` + a second (unclaimed) fix for the 3 ShowFlag brush leaks. |
| MATH-01 | Yes — signed-overflow/shift UB throughout | Correct and **in-range bit-equivalent on MSVC**: add/sub/neg (defined wrap, same bits), mul (shift-type differences discarded by truncation on both compiler paths), div (MSVC same software divider; Clang moved from native `__int128` to the shared divider — both truncate toward zero, cross-compiler agreement improves), `ToInt/ToInt64/CeilToInt` equivalent (32.32 has exactly 32 integer bits). Intentional edge-only changes: `FromFloat` NaN/Inf clamp (editor-only), `IsNearlyEqual` distance>INT64_MAX no longer wraps, `IntRange` full-int32 span (was modulo-zero UB). Caveat §2.8 (`GetNormalized`). |
| CACHE-01 (nav+FoW halves) | Yes — order-independent XOR fingerprints; FoW rotation omitted | Fixed **stronger than asked**: exact list equality (`DynamicBlockers == InBlockers`, `SeinNavigationAStar.cpp:771-782`; `State.Stamps == *StampsToUse`; FoW snapshot array includes `Rotation`). Ledger correctly keeps broader FoW invalidation open. |
| CFG-01 | Yes | Correct — frozen `CoverExtension` contributor ID, three sim-affecting fields, `Fatal` on registration failure. |
| NET-01 (failed-submission half) | Yes — baseline ignored submit result and ran `LastSubmittedTurn = OutgoingTurn` unconditionally (`@d22ba71:~849`) | Fixed — `PendingTurnSubmissions` queue retains failed turns for retry; `LastSubmittedTurn` advances only per successful send (`SeinNetSubsystem.cpp:2503-2516`); map-reset clears state (`:295/:371`); `PruneProtocolState` bounds retained sets. |
| FOW-02/03/04, STATE-01/02, NAV-01/02/03, PERF-* | Real (FOW-02 spot-checked at `SeinFogOfWarDefault.cpp:1232`; FOW-03/04 accepted plausible, not traced) | Correctly ledgered as **open** — none silently claimed fixed. |

### Uncommitted Phase 2 (working tree) — architecture sound, UNBUILT

COR-01's four sub-requirements all have real enforcement sites:

1. **Ownership at apply (sim-side):** centralized policy gate in `ProcessCommands`
   (`SeinWorldSubsystem.cpp:774-805` EntitySet filter + `AuthorizeCommand`; `:1003`
   entity-scope; `:1472` broker members; `:1924` payer). Default policy = owner control via
   `CanPlayerControlEntityAtTick` + scoped grants (`SeinEntityControlComponent`); base
   policy is **deny-all**.
2. **Sender role / match control:** `MatchControl` scope requires
   `IssuerKind == MatchAdministrator`, grantable only when the **server-side binding** has
   `bCanAdministerMatch` (`SeinNetSubsystem.cpp:3225-3230`).
3. **Payload bounds:** author submission budgets + decoded-allocation budgets enforced at
   ingress (`:3189-3196`, wire codec `ChargeAllocation`).
4. **Turn window:** `IsCommandTurnWithinProtocolWindow` at `ServerHandleSubmission`
   (`:3161`).

Key security property verified: `IssuerKind` **is decoded from the wire**
(`SeinCommandWireCodec.cpp:970`) but is **overwritten server-side** by
`StampAuthoritativeCommandBatch` (PlayerID, IssuerKind, DerivedResourcePayer, Tick) on both
the participant path (`:3225`) and the AI path (`:3086`, admin hardcoded false); outgoing
drafts are additionally neutralized to `Unauthenticated` before send (`:2549/:2703`).
Refund exploit closed: `ResolveResourcePayer` returns the **entity owner**
(`SeinCommandAuthorityPolicy.cpp:194-203`). The authority view reads **sim state only** (no
engine net role, no float) — determinism-compatible.

**Caveats:** never compiled; ~107 new tests are co-developed with the code they test and have
no recorded run; per-path audit of every decode field and every registry iteration order was
not completed (the planned adversarial hunt was interrupted — §5).

---

## 5. Honest verification gaps

- **The adversarial fresh-bug hunt over both diffs did not complete** (usage-limit
  interruption). Committed-diff risk areas got targeted manual review (FixedPoint arithmetic,
  cache keys, latent manager, effect identity); the uncommitted ~10k-line diff got
  architecture + spot-checks only. A systematic hunt over
  `SeinCommandSchemaRegistry.cpp` (50 KB), `SeinCommandWireCodec.cpp` (34 KB),
  `SeinTurnAggregator`, and the broker/production system rewrites remains worth doing.
- Not verified: FOW-03/04 beyond plausibility; MOVE-01 (flight coast); Clang/Shipping build
  claims; all test-run/pass claims (74/74 etc. — test *count* verified exactly, execution
  cannot be verified read-only); whether the working tree compiles.
- Per project doctrine: **nothing here substitutes for the `Sein.Sim.Parallel` 0-vs-1
  StateHash A/B and RJ's PIE.** Static analysis confirms mechanisms, not bit-determinism.
