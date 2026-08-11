# SeinARTSCoverExtension — Plugin Guide

This opt-in extension currently provides deterministic cover providers and queries, resolver-local
cover-aware destinations, preview quality, and editor authoring tools. Read the project-root guide
and `Agents/WORKFLOW.md` first.

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

- `FSeinCoverComponent` is deterministic provider data authored in `ComponentData`: quality,
  directionality, slots, area, and editor generation settings.
- `USeinCoverSystem` is currently the replaceable provider-lifecycle and query surface.
  `USeinCoverDefault` is the shipped query policy; allocation still lives as duplicated greedy
  logic in the ordinary and Squad resolvers and is an active remediation item.
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

The generic authority seam must compose multiple deterministic providers by stable key/order; a
single-cast Boolean hook is not sufficient for framework-grade composition or contextual policy.

## Dispatch and allocation

Cover post-processing runs after base formation layout and is shared by preview and commit.
Ordinary and Squad movement must delegate to one cover planning/allocation implementation rather
than carrying duplicated greedy snap bodies.

The intended tactical allocator is selection-wide and deterministic:

- Stable member and slot ordering.
- Maximum-cardinality/minimum-cost matching with fixed-point policy inputs.
- Pure preview planning followed by explicit commit-time reservation.
- No duplicate member or slot claims.
- Replaceable native allocator/system plus BlueprintNativeEvent eligibility/scoring policy.
- Reservation lifecycle included in canonical state, snapshot, replay, reset, and reconnect.

Do not silently replan an initial destination after preview. If reservation state changed before
execution, commit accepts the exact resolved artifact or rejects according to the approved policy;
later movement repaths may re-resolve.

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
