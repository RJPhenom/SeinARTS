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
2. **Gate B frontier** — the 4 red `SeinARTS.Editor.Snapshot.Movement` failures (BP MoveTo
   continuation capture).
3. **§2.1 restore re-stamping** — small, contained, closes the last gap in an otherwise
   verified authority perimeter.
4. **§2.3 FName hashes** — mechanical once the frozen catalog is the source.
5. Ledger hygiene: rows/amendments from §2.2, §2.4, §3.5, §3.6.

The perf workstream (11 rows, all Confirmed) had not been started as of this review — no
finding here changes that sequencing.
