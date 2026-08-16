# DECISION REQUIRED — Stage-4 Online Services Scope

**Status:** open product decision, reserved for RJ. Drafted 2026-08-15 (overnight autonomous
session) so stage 4 has a concrete fork to rule on instead of an amorphous "online platform"
bullet. Nothing blocks on this: stages 1–3 are code-complete and the release pipeline ships the
framework without any stage-4 surface.

## The question

READINESS_ROADMAP stage 4 ("freeze the online service contracts") covers account/auth,
party/invite, matchmaking, server allocation, ranked/stats/leaderboards, reconnect credentials,
co-op campaign persistence, and telemetry. None of it exists, deliberately. What is the
framework's 1.0 posture toward it?

## Options

**A. Stage 4 is out of scope for framework 1.0 — ship the toolkit, document the boundary.**
The framework's product is the deterministic sim + lockstep transport + gameplay toolkit. Online
platform services are game/studio infrastructure: every adopter already has a vendor preference
(a platform's online subsystem, a custom backend) and UE ships provider plugins for the common
ones. We document the integration seams that already exist (match settings/slot bootstrap, the
lockstep session lifecycle, reconnect credentials = the trusted-envelope machinery, replay
evidence files) and explicitly declare auth/matchmaking/ranked out of scope. Cheapest; honest;
risks adopters wiring lockstep bootstrap to their backend in subtly wrong ways (each team
re-derives the same glue).

**B. Freeze contracts only — a thin backend-neutral `SeinARTSOnline` extension with interfaces,
data contracts, and a loopback/fake adapter; NO vendor adapters.**
Define the provider-neutral interfaces the roadmap lists (auth identity, party, matchmaking
ticket, allocation request, match/roster identity, results submission, ranked classification,
save envelope ownership) as an opt-in extension plugin. Ship exactly one adapter: an in-process
loopback used by tests and local play. Vendor adapters (platform online subsystems, custom
backends) are downstream game work against a frozen contract. This is the roadmap's literal ask
("freeze contracts … rather than coupling Core/Net to one vendor"), keeps the extension-stripping
story intact, and gives adopters a shaped socket instead of a blank page. Medium cost: the
contract design is real work and freezing wrong contracts early is the classic failure — they
would need versioning discipline from day one.

**C. Build a reference online stack (contracts + one real vendor adapter + ranked referee).**
Full stage 4: everything in B plus a working adapter against one real backend and the trusted
dedicated-server referee flow for ranked. Only defensible if a first-party game is imminent and
its backend is chosen; otherwise we would be qualifying infrastructure against a vendor no
adopter may use, on top of the dedicated-server engine gate that is already external/red.

## Interactions

- The dedicated-server CI gate (launcher engine rejects Client/Server targets) is a prerequisite
  for any REAL ranked/referee qualification — B can be designed and loopback-tested without it,
  C cannot be finished without it.
- Listen-host migration vs dedicated-only per mode, co-op persistence policy, and adaptive input
  delay are separate reserved decisions; B's contracts should leave them open (e.g. the
  allocation interface must not assume dedicated-only).
- Anti-cheat: canonical roots detect divergence but lockstep clients can read hidden state.
  Whatever the option, ranked integrity = trusted referee + server-side legality gates + replay
  evidence, and that should be stated in the docs' honesty section rather than implied.

## Recommendation

**B, sequenced after the current PIE batch clears** — with two riders: (1) contracts ship marked
"provisional" for at least one release before freezing (adopters get the socket, we keep the
right to move it), and (2) the ranked-referee flow is specified in the contract but explicitly
gated on the external dedicated-server CI gate, so nothing in the plugin claims a capability the
engine distribution cannot prove. A is the fallback if you would rather keep 1.0's surface
smaller; C is not recommended without a committed first game + backend.

**Decide A/B/C (or amend).** If B: the next concrete step is a contract-surface proposal
(interface list + data shapes + loopback semantics) as its own reviewable memo before any code.
