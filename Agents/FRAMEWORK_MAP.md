# SeinARTS Live Framework Map

This is a compact code-navigation map, not an exhaustive user manual. Live source wins if it changes.

## Product model

SeinARTS is a deterministic lockstep RTS framework for Unreal Engine 5.7. Authoritative simulation uses 32.32 fixed-point values and generational entity handles. Unreal actors, animation, UI, effects presentation, and rendering consume simulation output; input enters authoritative state through registered commands.

The Blueprint actor is the unit authoring surface. `ASeinActor` owns one `USeinEntityComponent`, whose `ComponentData` array is copied into reflection-backed simulation storage at spawn.

## Plugin topology

```text
SeinARTSFramework
├── SeinARTSSquadExtension
├── SeinARTSCoverExtension
│   └── SeinARTSCoverSquad bridge -> Squad
└── SeinARTSMovementPlusExtension

SeinARTSTestSuite + SeinARTSExtensionTestSuite are disabled, non-shipping consumers.
```

The Framework must not depend on an extension. Cover's current descriptor still declares a bridge module that hard-links Squad, so a physically stripped Squad plugin is not yet a supported Cover packaging combination.

## Framework modules and core algorithms

| Module | Core responsibility and algorithms |
|---|---|
| `SeinARTSCore` | Fixed-point scalar/vector/transform/quaternion geometry, deterministic trigonometry and PRNG. Leaf dependency. |
| `SeinARTSCoreEntity` | Generational entity pool; reflection-backed sparse component storage; ordered fixed-tick systems; ability/latent/effect/production/containment state; command brokers; snapshots; canonical roots; visual-event emission. |
| `SeinARTSLevelData` | Shared baked substrate, coordinate system, layer-provider registry, and regenerable channel data. |
| `SeinARTSNavigation` | Deterministic grid A*, connectivity/reachability, footprint-aware placement, height sampling, dynamic blockers, async request/result plumbing, and typed paths. |
| `SeinARTSMovement` | Persistent per-entity movement policies, planner/mover handles, MoveTo continuation, shared steering/avoidance, navigation containment, movement driver, and typed-segment flattening. |
| `SeinARTSFogOfWar` | Grid visibility, deterministic source stamping, blocker layers, explored/visible state, canonical state codec, render texture, actor visibility, and debug rendering. |
| `SeinARTSNet` | Turn aggregation, Unreal relay transport, lobby/session state, command fan-out, root gossip, resync, replay, and lifecycle boundaries. |
| `SeinARTSFramework` | Player controller, camera/HUD, selection, targeters, previews, game mode, and match bootstrap. |
| `SeinARTSUIToolkit` | Read-only view models, selection aggregation, minimap data, and widget pooling. |
| `SeinARTSEditor` / `SeinARTSGraphNodes` | Authoring factories, validation, Details/graph tooling, custom Blueprint nodes, and uncooked source-asset support. |

### Simulation order

`USeinWorldSubsystem` advances fixed ticks through `PreTick`, `CommandProcessing`, `AbilityExecution`, and `PostTick`. Phase, priority, and stable system ID are compatibility state. Parallel work must read immutable snapshots, write disjoint local/self state, and merge in canonical order.

### Collision

- `USeinCollisionResolverDefault` performs deterministic in-place Gauss-Seidel relaxation. It is the current project default because it wins at the measured 100-148 mover scale.
- `USeinCollisionResolverParallel` performs deterministic Jacobi-style snapshot/compute/serial-apply passes. It remains useful when a much larger collision workload amortizes task and snapshot overhead.
- Both use broadphase candidate sets, exact fixed-point overlap tests, navigation/authority gates, and exact no-write exits. They are different policies and are not expected to produce identical intermediate layouts; configuration fingerprints prevent peers from mixing them.

### Navigation and movement

The shipped navigation A* emits a coarse straight-segment route. `USeinMovement::PlanPath` is the per-unit shaping seam; `FSeinPath` supports `Straight`, `AbstractEdge`, `Field`, `Arc`, and `Jump` segments.

`FSeinNavAgentProfile` is the module-neutral policy passed across navigation, movement, collision,
formation, and extension boundaries. `USeinWorldSubsystem` builds it from the entity's navigation,
tags, and extents components: nav-layer mask selects dynamic blockers; `BlockedTerrainTags` defines
hard terrain topology; wall padding and the complete compound collider define clearance. The same
profile reaches command pathability, initial and replacement paths, direction/escape/floor probes,
collision barriers, navigation containment, formation projection, requester-aware Blueprint calls,
and Movement+ maneuver probes.

The A* implementation uses a bounded cache of static connected components keyed by exact agent
profile. It ignores transient dynamic blockers for fundamental order admission, while individual
path searches still route around those blockers. Cache eviction can cause a later deterministic
rebuild but cannot change a result. Forbidden terrain participates in full-footprint clearance and
cannot be bypassed by authoritative-destination handling. `AgentTags` remain available to custom
navigation implementations but the shipped A* does not reinterpret them as terrain exclusions.
Cover's final post-processing can still replace a generic formation destination without the full
requester context; closing that seam belongs to the shared tactical allocation work.

Movement+ is not a full arbitrary Reeds-Shepp/Dubins route solver. Its wheeled and tracked modes can run a deterministic curated Reeds-Shepp-style **start-maneuver** planner at plan/repath time. That planner considers bounded closed-form candidates such as a departure arc, straight reverse, and K-turn, probes clearance, emits typed `Arc`/`Straight` legs, then hands the remaining coarse route to the normal runtime follower. Wheeled driving uses bicycle kinematics and arc/pursuit tracking; tracked driving selects pivot/arc/reverse behavior. This live behavior supersedes older notes claiming that no shipped vehicle mode emits arcs.

### Canonical state, resync, and replay

- Exact snapshots use explicit native codecs/coverage contracts and fail closed when required state is unsupported.
- Canonical BLAKE3-128 roots cover authoritative and continuation leaves under stable schema/descriptor identities.
- Routine multiplayer roots use mutation revisions plus indexed Merkle trees to re-digest changed leaves only. They are sealed at due network checkpoint boundaries, not every tick.
- Forced rebuilds exist to verify that cache acceleration has not changed the root.
- Resync transfers an authenticated bounded checkpoint envelope plus the exact retained command tail, catches up through the normal gate, and reactivates on an agreed root.
- Replay v9 is an append-only digest-chained journal with periodic checkpoints, opaque turn batches, durable frontiers, bounded indexes, lazy decode, crash-tail recovery, and atomic publication. Frozen v8 reading remains supported.

### Fog of war

FoW source stamps are deterministic grid shapes with radial, rectangle, or cone range semantics.
Terrain vision multipliers scale only the active shape range (`Radius`, `HalfExtents`, or
`ConeLength`), keeping cache identity and behavior free of irrelevant-field churn. Extents-authored
blockers fold their local Z offset into the snapshotted world-space base before computing the top.

The dynamic blocker hot path keeps one dense maximum-top grid plus the layer mask. Cells whose
overlapping layers have different tops carry a sparse eight-entry exact-height exception. Opacity
queries therefore use the true maximum for the requested layer subset without multiplying the dense
grid footprint by every layer. The sparse exceptions participate in capture, restore, reset, and
canonical comparison; behavior revision 2 / codec revision 5 deliberately rejects older semantic
descriptors even though the serialized payload schema itself did not grow.

### Presentation performance policy

Ordinary RTS visual meshes use Unreal update-rate optimization and skip animation-to-physics bone/overlap work; crowd skinned meshes are excluded from hardware ray-tracing geometry. Designers can opt actors back into UE component defaults or the physics-mesh policy. These choices are render-only and never enter lockstep state.

## Extension responsibilities

- **Squad:** persistent heterogeneous slots, member lifecycle, centroid, broker synchronization, formation dispatch, reinforcement state.
- **Cover:** provider geometry/slots, visibility-gated queries, cover-aware destination post-processing, editor generation. Stable reservation/allocation is still missing.
- **Movement+:** Infantry, Wheeled, Tracked, Hover, and Flight policies plus class-specific deterministic tuning/state.

## Sacred contracts

- Preview destinations equal the command's first path requests.
- An authored cover destination may overrule its coarse static-nav false negative, but not unrelated blockers, occupants, reservations, or hazards.
- Component pointers are invalid across storage additions; reacquire after mutation.
- Entity identity includes generation.
- Runtime state that affects a future tick must participate in hash/capture/restore/reset/replay/reconnect.
- Lockstep settings and implementation choices participate in frozen compatibility fingerprints.
