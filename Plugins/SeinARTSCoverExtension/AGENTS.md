# SeinARTSCoverExtension — Plugin Guide

This opt-in extension currently provides deterministic cover providers and queries, resolver-local
cover-aware destinations, preview quality, and editor authoring tools. Read the project-root
`AGENTS.md` first. The adjacent `CLAUDE.md` remains for Claude compatibility and must not be
deleted. It may lag live code, so live behavior and this concise guide win when they conflict.

Nothing in the framework may depend on Cover.

## Modules and dependencies

| Module | Type | Responsibility |
|---|---|---|
| `SeinARTSCover` | Runtime | Provider data, cover system/default, subsystem, default broker resolver, settings, tags, and Blueprint queries. |
| `SeinARTSCoverEditor` | Editor | Details customization, slot generation, and entity-bridge authoring visualization. |

Framework is required. Cover has no Squad dependency and must build/cook with Squad physically
absent. Cover-aware Squad dispatch lives in the separate, explicitly enabled
`SeinARTSCoverSquadExtension`, which depends on both parent plugins.

## Provider and query model

- `FSeinCoverPayload` is deterministic provider data authored in `ComponentData`: quality,
  directionality, slots, area, and editor generation settings.
- `USeinCoverSystem` is currently the replaceable provider-lifecycle and query surface.
  `USeinCoverDefault` is the shipped query policy. `FSeinCoverAssignmentPlanner` owns the shared
  pure ordinary/Squad max-cardinality, min-wrong-side, min-distance assignment path.
- `USeinCoverSubsystem` owns the selected system and registers providers from entity lifecycle
  events.
- Shipped geometry queries with a valid observer gate providers through the active FoW
  implementation. An invalid observer deliberately requests ground truth. Terrain-derived cover is
  not hidden by provider visibility.
- `TerrainCoverQuality` maps baked terrain gameplay tags to cover quality; navigation is currently
  the runtime terrain-query owner, so keep this seam explicit and deterministic.
- Editor-time scatter may use nondeterministic randomness only because the resulting fixed-point
  slots are serialized before runtime.

Provider and slot identities must be stable and generational. A slot identity is conceptually the
provider handle plus authored slot identity/index; proximity alone is not sufficient for authority,
reservation, snapshot, or replay state.

## Destination authority

The root invariant applies without exception: preview destinations equal the command's first path
destinations, and an authored cover slot is authoritative over a coarse-nav false negative.

Keep these concepts separate:

1. Raw authored-slot discovery/identity.
2. Static coarse-nav reachability.
3. The owning provider's own obstruction.
4. Unrelated dynamic blockers or physical occupants.
5. Reservation and gameplay eligibility policy.

Authority may bypass the coarse static bake and owning provider obstruction. It must not
accidentally make unrelated wrecks, deployables, reservations, or hazards disappear. Never
recognize authority by recursively calling a query that has already filtered out the authored
slot.

The generic authority seam is Core's stable-keyed authoritative-destination provider registry.
Providers register before topology freeze, execute in canonical key order, and bind their stable ID
plus behavior revision into the match StateContract. Do not bind the legacy single-cast compatibility
hook from shipped code; deterministic bootstrap rejects it because it cannot identify provider
behavior or support requester-aware policy.

## Dispatch and allocation

Cover post-processing runs after base formation layout and is shared by preview and commit.
Ordinary and Squad movement delegate to `FSeinCoverAssignmentPlanner`; do not fork allocation logic
back into either resolver adapter.

The selection-plan provider aggregates ordinary and persistent-Squad broker members before one
exact deterministic solve. The tactical allocator provides:

- Stable member and slot ordering.
- Maximum-cardinality/minimum-cost matching with fixed-point policy inputs.
- Pure preview planning followed by explicit commit-time reservation and frozen artifacts.
- No duplicate member or slot claims.
- Replaceable native allocator/system plus BlueprintNativeEvent eligibility/scoring policy.
- Reservation lifecycle included in canonical state, snapshot, replay, reset, and reconnect.

Reservations filter future previews; they never veto or reshape an already displayed order. Under
RJ's policy D, admitted survivors retain the exact world destinations in their frozen artifact,
dead members drop only their own entries, and physical collision resolves any racing double-claim.
Moving providers expose current slot transforms to new previews while issued destinations remain
fixed in world space. Artifact/reservation state is canonical across cancellation, settlement,
provider loss, snapshot, replay, reset, and reconnect. Later movement repaths may still re-resolve a
destination that the changing world made unreachable.

## Editor and preview

- Slot generation must use transaction/property APIs that propagate correctly to archetypes.
- `SeinARTSCoverEditor` registers and unregisters its unique draw key with the framework editor.
- Formation-preview rendering lives in the framework; Cover supplies quality and slot planning,
  not a second render pipeline.
- Preview is read-only. It may create provisional claims but never mutate authoritative sim state.

`CoverSystemClass`, `CoverSnapRadius`, and `TerrainCoverQuality` affect sim outcomes. The module
registers all three under the frozen `CoverExtension` config-fingerprint ID. Map entries are
canonically sorted by the base registry, so insertion order is not compatibility state.

## Verification

Cover changes need synthetic and world-level scenarios for:

- Coarse-red authored slots and exact arrival.
- Long providers and authored slots beyond area bounds.
- Owning-provider blocker exemption versus unrelated live blocker rejection.
- Fog-observer filtering and every supported vision layer.
- Multi-squad/loose-unit contention and deterministic matching.
- Reservation acquire/release, provider destruction, cancellation, failed movement, and restore.
- Preview artifact versus every first path request.
- Provider registration-order permutations and serial/parallel state agreement.
- Cover without Squad, the separate Cover+Squad bridge, and framework-only builds.

For small allocation matrices, compare the optimized solver against exhaustive brute force. Use
PIE for tactical plausibility, approach/facing feel, preview clarity, and visual quality.
