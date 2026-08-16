# DECIDED — Frozen-Destination Conflict Policy

**Status:** DECIDED by RJ 2026-08-15, implemented same day (branch
`codex/feat03-frozen-destinations`). PIE feel check batched with the rest of the frozen-destination
wave.

## The ruling (RJ, verbatim policy)

> "Units should ALWAYS do what user SAW they 'should' do."

Options A (strict all-or-nothing rejection) and B (subset for non-reserving, strict for reserving)
were both rejected. The decided policy — call it **D** — is:

- A member that died between preview and admission simply **drops its own slot**; every surviving
  member keeps its **exact displayed destination** (cover slots included — if squad A was shown
  slots 1,3,4,7,8 and one member dies, the survivors still take their shown slots).
- **Contention is never a reason to reject or re-plan an order.** If a reservation appeared in the
  race window or a provider moved, the survivors are still delivered to the shown points; physical
  reality (collision layer, arrival settling) resolves any double-claim. Both claims coexist in
  the reservation ledger until one is released.
- Hostile-input validation stays strict: forged radii, forged reserving provenance, entries for
  members the recipients do not own, duplicate members, out-of-order artifacts, and an artifact
  whose own reserving entries overlap each other all still reject (`InvalidTarget` /
  `DestinationReserved`).

Historical note: the B recommendation in the previous revision of this memo was authored by the
Claude session, not Codex — Codex recorded no recommendation; strict rejection was its fail-closed
interim only.

## Implementation (landed)

- `SeinWorldSubsystem.cpp` `TryHandleBrokerOrderCommand`: subset admission filter (dead members
  drop, survivors keep exact entries, ordered-subset + canonical-radius + provenance validation),
  world-contention veto removed, pairwise self-overlap kept as malformed-input rejection.
- Snapshot preflight relaxed from index-exact artifact↔member alignment to ordered-subset.
- `SeinBrokerOrderProtocol::SchemaVersion` bumped 2→3 (`BrokerOrder.V3`) — behavior revision;
  replay/join compatibility fails closed against v2 peers.
- Tests: `FrozenDestinationAdmissionKeepsShownDestinationsAndReleases` (contention admits, per-order
  release), `DeadMemberDropsOnlyItsSlotFromAdmittedArtifact` (RJ's slots example), existing
  dead-subset/settled/snapshot tests retained.

## Red-team follow-up (same day)

An adversarial pass on the landed commit confirmed one real, PRE-EXISTING hole (not introduced by
the policy change): a queued order routed onto an existing shared ephemeral broker stores its
artifact in the click's selection order, while the restore preflight validates against the
broker's stored member order — re-selecting the same loose units in a different order then
shift-queueing produced a snapshot that failed restore preflight. Fixed by re-keying the artifact
to broker member order at the shared-broker full-match queue site (pure canonicalization;
dispatch resolves entries per member). Regression:
`ReorderedReselectionQueuedArtifactSurvivesRestore`.

Speculative residual (recorded, not fixed): the empty-survivors rejection scans only artifact
entries, so a live expected member with NO artifact entry (only plausible if reinforcement
completes inside the input-delay window) would have its order dropped with the rest. Not
reachable in practice today; revisit if reinforcement timing ever shrinks into the admission
window.
