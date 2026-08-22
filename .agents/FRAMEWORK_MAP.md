# SeinARTS Live Framework Map

This is a compact code-navigation map, not an exhaustive user manual. Live source wins if it changes.

## Product model

SeinARTS is a deterministic lockstep RTS framework for Unreal Engine 5.8. Authoritative simulation uses 32.32 fixed-point values and generational entity handles. Unreal actors, animation, UI, effects presentation, and rendering consume simulation output; input enters authoritative state through registered commands.

The Blueprint actor is the unit authoring surface. `ASeinActor` owns one `USeinEntityComponent`, whose `ComponentData` array is copied into reflection-backed simulation storage at spawn.

## Plugin topology

```text
SeinARTSFramework
├── SeinARTSSquadExtension
├── SeinARTSCoverExtension
├── SeinARTSMovementPlusExtension
└── SeinARTSCoverSquadExtension -> Framework + Cover + Squad

SeinARTSTestSuite + SeinARTSExtensionTestSuite are disabled, non-shipping consumers.
```

The Framework must not depend on an extension. Cover and Squad are physically independent. Their
only cross-extension code is the optional `SeinARTSCoverSquadExtension`, which owns the existing
`SeinARTSCoverSquad` module and requires both parent plugins.

Framework-owned runtime UI content remains inside the Framework plugin. Example maps, example
gameplay Blueprints, and mannequin assets belong to the host project under
`/Game/SeinARTSExamples`; downstream consumers must provide their own project content and
simulation-content manifest. This boundary prevents the distributable plugin from silently relying
on this repository's host `/Game` packages or on opt-in extensions.

## Framework modules and core algorithms

| Module | Core responsibility and algorithms |
|---|---|
| `SeinARTSCore` | Fixed-point scalar/vector/transform/quaternion geometry, deterministic trigonometry and PRNG. Leaf dependency. |
| `SeinARTSCoreEntity` | Generational entity pool; reflection-backed sparse component storage; ordered fixed-tick systems; ability/latent/effect/production/containment state; command brokers; snapshots; canonical roots; visual-event emission. |
| `SeinARTSCombat` | Genre-neutral vitals, deterministic damage/healing, weapon cycling, on-demand target acquisition, and instant/projectile delivery. |
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

### Ability lifecycle

Runtime abilities are world-pooled UObjects referenced from `FSeinAbilityComponent` by stable pool
IDs. Grant ownership is source-aware and refcounted across native/anonymous and exact effect IDs.
Passive abilities activate on first grant; primary abilities enter through commands or an explicit
simulation-only direct seam.

Activity identity has one owner. Activation publishes the exact primary/passive component locator
before `OnActivate`; deactivation removes it before refunds, latent cancellation, and `OnEnd`.
This makes callback reads truthful and lets re-entrant revoke/regrant reuse a pool slot without the
old instance clearing its replacement. An entity has one ticked primary; a second primary fails
until broker or cancellation-tag arbitration ends the current one. Passives remain an ordered set.
Snapshot admission requires both directions of the invariant: active objects are indexed in the
correct role, and indexed objects are active. The base ability pool provider behavior revision is 2.

### Designer authoring boundaries

Ordinary gameplay activates an Ability through **Issue Ability Command**, which enters the
lockstep queue and re-runs command authority, targeting, pathability, cooldown, tag, capacity,
cost, and cancellation gates. **Activate Ability (Direct)** intentionally bypasses those gates and
is limited to debug, cheat, or reconstruction work. **Get Ability Availability** is an advisory UI
query; the queued command remains authoritative.

Ability Blueprint member state is canonical. Multi-tick work may cross simulation time only through
registered Sein latent actions with checkpoint codecs. Values needed after an async boundary must
be persisted in deterministic Ability or component state rather than compiler-frame temporaries.
Self-state writes during lifecycle callbacks are tracked; external mutation must call **Mark
Deterministic State Dirty**. The determinism and continuation validators fail closed on unsafe member
types, untrusted or presentation-only calls, unseeded randomness, and unsupported latent work. Start
code investigation at `SeinAbility.h`, `SeinAbilityBPFL.h`,
`SeinAbilityDeterminismValidator.cpp`, and `SeinAbilityContinuationValidator.cpp`.

Balance Data is an editor-only bulk editing view over authoritative entity Blueprint components and
Ability defaults; the generated DataTable is never runtime simulation state. Gather is destructive,
Check Sync detects authored drift, and Push writes only validated changes back to bound source
properties. Stable source identity, schema checks, mounted content roots, and the filtered native or
designer-component picker fail closed. After a successful Push, save the source assets and regenerate
the Simulation Content Manifest. Start at `SeinBalanceProfile.h`, `SeinBalanceTableExport.cpp`, and
`SeinBalanceProfileDetails.cpp`.

Auto-tag derivation persists missing tags into the dedicated generated Gameplay Tags source and
enforces project-wide uniqueness across Ability, Effect, and entity identity tags. Auto-owned asset
renames report an actionable notification when a collision or unmapped prefix leaves the old tag in
place; **Reset to Auto** reports updated, already-current, or exact failure state. Bulk regeneration
uses one suspended write pass, resumes the tag tree once, then applies newly available tags in the
same command rather than requiring a second click. Start at `SeinAutoTagGenerator.h`,
`SeinAutoTagGenerator.cpp`, and `SeinAutoTagDetails.cpp`.

Economy is ability composition over generic deterministic data, not a hardcoded worker subsystem.
Resource-node stock and worker cargo belong in components accessed through typed get/set nodes;
dropoff uses **Grant Income** inside an authorized simulation callback. The whole income map
validates atomically and valid uncapped overflow saturates. Construction workers call **Add
Construction Progress** on `FSeinConstructionComponent`; only positive non-overflowing progress
mutates. Completion removes the component and releases only the framework-owned
`State.UnderConstruction` grant, preserving an identical designer-authored base grant.

Combat participation is opt-in through `FSeinVitalsComponent` and `FSeinWeaponComponent`.
`USeinDamageFormula` and `USeinTargetScorer` are stateless Blueprint policy CDOs with neutral
built-ins when no class is selected. Target acquisition is an on-demand service; abilities own
engagement cadence and call the restricted fire/damage/heal mutations. Instant delivery resolves in
the fire tick. Projectile delivery creates ordinary pooled entities, so projectile state follows the
normal canonical snapshot, replay, and reconnect lifecycle. Start at `SeinWeaponFire.h`,
`SeinTargetQueryService.h`, `SeinDamageFormula.h`, and `SeinTargetScorer.h`.

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
The authoritative-destination registry composes providers by canonical stable ID, includes requester
context, and binds provider identity plus behavior revision into the match StateContract. Cover's
selection-wide destination-plan provider produces the frozen artifact used by command admission,
initial path requests, reservation settlement, replay, and reconnect. The shipped native
`USeinFormationPreviewSubsystem` -> `ASeinPlayerController` path carries the exact displayed artifact
into the command. The public Blueprint `Compute Formation Preview` / `Issue Broker Order` pair does
not: preview accepts guide points and a formation tag but returns only a layout, while issue cannot
accept those inputs or the displayed artifact and recomputes with defaults. Exact parity for custom
Blueprint input paths is an open public-API decision in `.agents/OPEN_RISKS.md`.

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

Movement+ exposes render-only steering angle and yaw rate in radians, normalized throttle/brake in
`0..1`, wrapped wheel phase in radians, and signed left/right track velocity in cm/s. Settled
post-collision transforms drive motion telemetry while movement-driver velocity drives
throttle/brake, so collision correction cannot masquerade as input. Telemetry clears across
spawn/restore/class-loss boundaries and may never feed simulation. The exact implementation contract
lives in `Plugins/SeinARTSMovementPlusExtension/AGENTS.md`; `.agents/VEHICLE_GYM.md` owns its
qualification and PIE matrix.

## Extension responsibilities

- **Squad:** persistent heterogeneous slots, member lifecycle, centroid, broker synchronization, formation dispatch, reinforcement state.
- **Cover:** provider geometry/slots, visibility-gated queries, exact selection-wide assignment,
  frozen destination artifacts, stable reservations, loose-unit cover-aware destination
  post-processing, and editor generation.
- **Cover+Squad bridge:** the cover-aware Squad dispatch resolver and its stable codec/content contributor registrations; no parent-plugin behavior is duplicated here.
- **Movement+:** Infantry, Wheeled, Tracked, Hover, and Flight policies plus class-specific deterministic tuning/state.

## Sacred contracts

- Preview destinations equal the command's first path requests.
- An authored cover destination may overrule its coarse static-nav false negative, but not unrelated blockers, occupants, reservations, or hazards.
- Component pointers are invalid across storage additions; reacquire after mutation.
- Entity identity includes generation.
- Runtime state that affects a future tick must participate in hash/capture/restore/reset/replay/reconnect.
- Lockstep settings and implementation choices participate in frozen compatibility fingerprints.

## Downstream verification

`Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1` creates disposable projects under ignored
`Saved/ConsumerMatrix`, copies only selected distributable plugin inputs, and verifies Framework,
Cover-only, Squad-only, Framework+Movement+, and all-production-plugin profiles. It generates a consumer-owned map and
manifest, rejects host-project package references, builds Editor and Shipping, loads the exact map,
cooks/packages it, and starts the real packaged Shipping executable. Client/Dedicated Server target
proof is intentionally still open because Epic's launcher UE distribution does not expose those
target builds.
