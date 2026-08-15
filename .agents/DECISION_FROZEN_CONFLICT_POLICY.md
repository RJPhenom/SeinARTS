# DECISION REQUIRED — Frozen-Destination Conflict Policy

**Status:** open product decision, reserved for RJ (roadmap: "Freeze the conflict policy:
exact artifact rejection versus an explicitly approved preview-changing fallback").
**Owner of implementation once decided:** any agent session; the change is localized to
`TryHandleBrokerOrder` admission in `SeinWorldSubsystem.cpp`.

## The question

When a displayed selection plan (the frozen destination artifact) no longer matches reality at
command admission — because a member died during the input-delay window, or a reserving entry now
collides with a reservation that appeared after the click — what does the sim do with the order?

## What is implemented today (the fail-closed interim)

Admission is all-or-nothing: the artifact must exactly match the alive flattened member set, and
every reserving entry must be contention-free. Any mismatch rejects the ENTIRE order with
`Command_Reject_DestinationReserved` / `Command_Reject_InvalidTarget` (a `CommandRejected` visual
event fires; no automatic retry or fallback). Note two softenings already landed outside the
reserved question: client-side plan-provider *failure* now degrades to a legacy artifact-less
order instead of eating the command, and the artifact-less legacy path now respects reservations.

## Exposure

Every ground move order carries an artifact by default (`bShowNavigationPreview` defaults true).
A member dying in the ~200-300 ms submission-to-admission window rejects the whole order — most
likely during combat retreats, exactly when large move orders matter. Self-heals on re-click
(the new preview computes from survivors), but reads as "my order did nothing."

## Options

**A. Keep strict rejection everywhere (today's behavior).**
Purest invariant: what was shown is exactly what executes, or nothing does. Cost: combat-window
rejections on plain moves; feel risk. UI can mitigate (rejection feedback + auto-reissue client
side), but the framework default stays harsh.

**B. Tolerant subset for NON-reserving artifacts; strict for reserving entries (recommended).**
Plain formation moves (no reservations — the vast majority) admit by filtering dead members from
the artifact and proceeding with the surviving exact positions; positions never move, only
vanished members drop out. Cover/provider-backed orders (any `bReserveFootprint` entry) stay
all-or-nothing, because the exact plan IS the point and partial admission could strand mixed
squads half-in-cover. Rationale: a dead member's own destination disappearing is not a
"preview-changing recomputation" — every surviving member still goes exactly where shown.

**C. Full fallback: on any mismatch, strip the artifact and re-resolve as a legacy order.**
Most forgiving, but genuinely preview-changing (survivors may be re-laid-out elsewhere), and it
reintroduces the silent-recompute behavior FEAT-03 exists to eliminate. Not recommended.

## Recommendation

**B**, with one refinement: if a *reserving* order loses only members whose entries were
non-reserving (mixed artifact), admit the surviving set as long as every reserving entry's member
is alive and contention-free; reject only when a reserving entry itself is invalidated.
Deterministic, minimal-surprise, keeps cover semantics exact. Requires: admission filter pass +
matched artifact/member alignment (the broker sweep already maintains it post-admission), a
behavior-revision bump on the command schema, and 3-4 focused admission tests (dead-subset cases
exist in `FrozenDestinationTests.cpp` asserting today's rejection — they would flip to assert
subset admission).

**Decide A/B/C (or amend B).** Nothing blocks on this: current behavior is safe/strict, and the
implementation is a bounded follow-up either way.
