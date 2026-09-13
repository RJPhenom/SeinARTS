# SeinARTS Live Framework Map

This is a compact code-navigation map, not an exhaustive user manual. Live source wins if it changes.

## Product model

SeinARTS is a deterministic lockstep RTS framework for Unreal Engine 5.8. Authoritative simulation uses 32.32 fixed-point values and generational entity handles. Unreal actors, animation, UI, effects presentation, and rendering consume simulation output; input enters authoritative state through registered commands.

The Blueprint actor is the unit authoring surface. Data-only `USeinEntityComponent` authoring components bake `FSein...Payload` values into `USeinEntityBridgeComponent::ComponentData`; those backend payloads are injected into reflection-backed simulation storage at spawn. Preserve the authoring/component and runtime/payload distinction.

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
| `SeinARTSCombat` | Genre-neutral target acquisition toolkit: deterministic Find Targets / Check Target over a derived spatial index, Blueprint scorer seam, and combat presentation notifications. No vitals/weapon/damage/projectile schema. |
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

Successful activation queues `AbilityActivated` before `OnActivate`; deactivation queues
`AbilityEnded` after activity detachment and before cleanup callbacks. Natural completion and
cancellation share this end notification. Rejected activation and repeated end calls emit none.
The actor bridge drains the FIFO on the render side and calls the existing `On Ability Activated`
and `On Ability Ended` Blueprint events. These describe the ability lifetime, not individual work
pulses or pauses; animations that need that precision consume designer-owned work state or cues.
This presentation repair adds no canonical state or snapshot schema fields.
Five focused regressions cover ordered completion/cancellation, immediate completion, callback
replacement, rejected activation, and deferred bridge delivery to Blueprint event entrypoints.
Independent review found no production defect; the test fixture's SpawnActor reference handling
was corrected. Validation has not executed these tests: the test translation unit compiled, but
concurrent targeter-preview compile/link failures blocked the test-enabled and ordinary editor
builds (`Saved/Validation/51e3305fe41e4439802329c9b4c3faec/validation-result.json`).
Rerun `Validate.ps1 -Preset Focused -Profile Framework -Suite SeinARTS.Unit.Abilities` when that
checkout build is healthy, then verify demo build/harvest montage start/stop in PIE.

### Ability activation inputs

Ability variables opt in through **Expose on Activate** (`SeinExposeOnActivate` Blueprint
metadata). Native subclasses declare reflected, mutable fields and call
`ExposeActivationInput(GET_MEMBER_NAME_CHECKED(UMyAbility, QueueIndex))` in their constructor.
Blueprint metadata is baked into the ability CDO at compilation/save for cooked execution;
native constructor declarations remain available without editor metadata.

**Activate Ability** takes the invoking local Sein Player Controller, an explicit entity, and an
ability class picker that expands exposed variables into typed pins. It submits player input
using the controller's player identity, never the recipient's owner. **Issue Ability** is the
simulation-only equivalent. **Make Ability Inputs** builds a reusable packet for dynamic-tag
submission, and **Issue Broker Order With Inputs** preserves it through broker fan-out.
Existing tag-based APIs remain available and initialize exposed fields from class defaults.
Caller pins refresh after declaration compilation, preserve variable GUIDs across renames, and
follow changed defaults unless the caller entered a literal override.
The player node was renamed from Submit Ability to Activate Ability without changing serialized
node/function identities. The old Activate Ability (Direct) Blueprint wrapper still bypasses
command admission and remains pending caller migration; the queue-item widget still references it.
Rename verification was blocked before tests by active Live Coding and unrelated entity/widget
include-order errors (`Saved/Validation/f85a1525b8234d16bab07e7799544326/validation-result.json`).
The prior feature verification below predates this display-name change.

`FSeinAbilityActivationInputs` captures only exposed values with an exact class/property schema
fingerprint and bounded canonical bytes (64 fields, 4096 value bytes, 256 aggregate container
entries). No class is loaded from incoming data. Concrete deterministic structs and ordered
arrays are supported; float/reference/unordered/opaque types, transient fields, dynamic
Instanced Struct values, native fixed C arrays/bitfield input declarations, and native
EditDefaultsOnly input fields are rejected. Connected native bitfield sources are normalized
when supplying an ordinary boolean input. Native callers
use `SeinMakeAbilityInputs<UMyAbility>(Configure, OutInputs, Error)` or capture an ability template.

Command admission decodes against the granted ability's trusted class before side effects.
**Can Activate With Inputs** can inspect the proposal with **Get Activation Input** without
replacing the running instance's variables. Ownership is revalidated after each callback.
Committed activation assigns every exposed variable before On Activate, including defaults for
an empty request. Ordinary state is preserved, queued requests own their values, and direct
native activation retains its documented low-level gate bypass. Command wire v4, broker schema
v5, and pool root-class contract v2 reject incompatible older protocols; the exposure schema
participates in native and Blueprint pool admission. Production queue entry identity is unchanged.

Regression suites contain `ActivationInputs` in their names and exercise native capture,
compiled Blueprint execution, pin reconstruction, transport, broker dispatch, fixed ticks,
serial/parallel roots and fresh-world snapshot continuation. Receipt status remains the source
of truth for completed verification; visual authoring and multiplayer PIE are separate gates.

Verification (2026-09-11): Development and Shipping built successfully. The six runtime feature
checks passed in `Saved/Automation/activation-inputs-final.json`; the final six Blueprint checks
(including distinct menu actions, compiled setter/getter execution, stale-bytecode abort,
bitfield conversion, rename/default refresh and declaration errors) passed in
`Saved/Validation/5e78ec59749d4d21b622a0bc1226c137/validation-result.json`.
Framework simulation passed 95 tests (`Saved/Automation/activation-inputs-sim.json`), integration
passed 22 with rendering enabled (`Saved/Automation/activation-inputs-integration-rendering.json`),
and determinism passed 45 (`Saved/Automation/activation-inputs-determinism.json`). Fresh-process
serial/parallel roots and raw poses matched all 120 frames (`Saved/Automation/activation-inputs-ab/ab-result.json`).
Shipping receipt: `Saved/Build/4ee03e7b691245dba372dedc87136370/build-result.json`.
The broad Simulation preset remains failed at its Unit step: 434 passed, five replay-version/navigation
assertions failed (`Saved/Validation/60ed40bf635a487f836f8f5ee6537354/validation-result.json`).
Those assertions are outside the changed activation implementation and were not relaxed.
Independent adversarial review findings were resolved. The user stopped Computer Use with Escape;
actual variable Details presentation, widget cancellation PIE, and packaged Blueprint execution remain
manual gates. Public documentation impact is recorded in PUBLIC_DOCS_BACKLOG.md.

### Designer authoring boundaries

Component Get/Set graph actions are refreshed by `FSeinARTSGraphNodesModule` on a
deferred editor tick after payload synchronization. Saved component Blueprints and legacy
standalone UDS are discovered through the Asset Registry before rebuilding the node-class
actions; embedded payload structs intentionally are not registry assets. Discovery loads
these asset classes, including UDS that must be inspected for component eligibility. It
does not run in cook/other commandlets. `SeinARTS.Editor.ComponentActions` covers creation
after menu initialization, recompilation without duplicate actions, and rediscovery after
both the Blueprint and embedded payload were unloaded. Development evidence:
`Saved/Automation/component-actions-verified-result.json` (2 passed, no test warnings/errors).
The preset wrapper stopped on pre-existing EOF whitespace in `Config/Tags/SeinGameplayTags.ini`;
the focused runner and owned-file whitespace check passed. The rendered ability search-menu check
remains a human/editor gate. Documentation impact: private-agent; public workflow unchanged.

Component deletion also owns generated payload deletion. `SeinComponentDeletion` adds
proven-owned payloads to Unreal's `OnAddExtraObjectsToDelete` set, so force deletion runs
the engine's UDS reinstancing/reference replacement for Apply Field Delta's `StructType`
pin and other references. Owner stamps or embedded companion/redirector identity establish
ownership; inherited payloads and ambiguous unstamped standalone structs are excluded.
Get/Set templates are released before deletion, and successful hidden-payload deletion
publishes the path removal that Struct Viewer cannot receive from `IsAsset()==false`.
Cancellation snapshots are scoped to each pre-delete notification. Blueprint rename events
update ownership in both live and compiler metadata and dirty the payload's original package;
this works even when Asset Tools omits a redirector. Sync repairs legacy compiler metadata
and notifies existing Struct Viewers without registering hidden payloads as assets.
Development evidence (2026-09-09): `Saved/Automation/component-deletion-complete-result.json`
passed all 5 ComponentActions tests after test-enabled and ordinary editor builds. Coverage
includes actual Apply Field Delta reference replacement, a cached Struct Viewer, moved owner
and payload save/unload/reload, inherited/shared payload protection, metadata-only persistence
repair, registry invisibility, canceled deletion, and unrelated deletion after cancel/reload.
One Asset Registry warning concerns the temporary moved fixture's old package being observed
on disk after a deletion notification; no test errors. Independent adversarial source review
found no remaining destructive blocker. Owned-file whitespace checks passed. The rendered
designer workflow remains a human/editor gate; pre-existing orphaned project assets were not
deleted by this fix. Documentation impact remains private-agent.

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

RJ changed the generated identity rename policy on 2026-09-11: Content Browser asset-name
changes must automatically migrate the generated identity and its entire subtree, remove the old
picker definitions, and preserve serialized references through hidden redirects. Folder-only moves
and manually owned identities keep their tags. Prefix settings propose names without dictionary
writes; factories and fresh copies assign unique identities. Entity identity is authored through
its Identity component and rebaked into the bridge, with inherited overrides isolated from parents.

Automatic migration repairs loaded references before native rename saves the owner and defers
Blueprint compilation until the rename stack returns. Dirty content is supported. Undo of unrelated
edits retains its history and canonicalizes restored tag values afterward. Rename-back reclaims the
previous alias without creating a redirect cycle. Initialize Tag can reconcile a remembered generated
identity after its field is reset. Guarded recovery can consolidate an already-initialized generated
destination; conflicting map keys/set entries reject recovery before any dictionary write.

The full hierarchy, including implicit intermediate nodes, is preflighted and written together.
Mixed-source descendants, shared identity owners, cross-source aliases, and foreign destinations
block migration. Explicit Rename Tag remains available; explicit migration and cleanup require
saved content and clear Undo after confirmation. Automatic rename does not clear Undo.

**Inspect and Clean Up Generated Tags** explains ownership and retention reasons. Cleanup rechecks
saved asset references, loaded identity owners, all source redirects, source/config strings, implicit
ancestors, generated provenance, and exclusive editable source ownership before removing each leaf.
Unknown/manual/native/shared/restricted entries are retained. Quote the commandlet argument in
PowerShell: `'-run=SeinARTSEditor.SeinTagAuditCommandlet'`. It writes an ignored `Saved/TagAudit` receipt; `-Cleanup` additionally reconciles eligible entries. Exact entity-tag lookup
and hierarchical Has Tag remain distinct; runtime grant ownership behavior has not changed.
Start at `SeinAutoTagGenerator.cpp`, `SeinTagMigration.cpp`, `SeinTagAudit.cpp`, and
`SeinIdentityTagValidator.cpp`.

2026-09-11 implementation remains uncommitted. The editor build and all **19** focused
`SeinARTS.Editor.AutoTag` tests pass. Current receipt:
`Saved/Validation/78c10b27258e42b7b84405c694d26944/validation-result.json`.
Coverage includes native AssetTools rename, full hierarchy and implicit nodes, dirty references,
unrelated Undo, rename-back, real consumer Blueprint graph save/reload, reset/reinitialize,
unloaded saved DataTable map-key conflicts, source drift, rollback and protected cleanup.
Independent review identified and informed the unloaded-reference and persistence checks.

`SA_Produce_Cancel` was repaired: the old `SeinARTS.Ability.Cancel.Production` definition is gone,
`SeinARTS.Ability.Produce.Cancel` remains, and the old name is a hidden redirect. A fresh editor
commandlet loaded the saved Blueprint and verified identity, old subtree absence and redirect
resolution with zero errors/warnings: `Saved/TagAudit/final-cancel-verification-20260911.log`.
The corresponding audit is `Saved/TagAudit/927A96C0455839CBAE487B8F4EF2A04E.txt`.
The fixture-generated Consumer entry was removed through guarded single-tag cleanup. Tests also
compare the production tag source before/after: `Saved/TagAudit/test-production-source-check.json`.
The current audit has no duplicate identity owners; the earlier Factory/Truck duplicates are no
longer present in its owner records.

Deferred compilation can leave dependent Blueprint packages dirty; normal Save All persists those
compiled changes. Hidden redirects preserve older serialized references. Automated save/reload and
Undo checks pass; interactive Details presentation and runtime graph execution remain human gates.

Production queue policies are authored on the producible's `FSeinProduciblePayload`:
Multi-Queueable (default), Once per Production Unit / Player, and Fixed Amount per
Production Unit / Player. Fixed policies expose Queue Amount >= 1. Limits count pending
queue entries plus successful lifetime completions of the exact queued actor class.
Cancellation releases pending allowance; spawned-unit death does not undo a completed purchase.
History is recorded even under Multi and retained separately from the production component.
Player history survives producer destruction; producer history uses the full generational handle.

`SeinWorldProductionPolicy.cpp` owns admission and mutable overrides. Producer overrides take
precedence over player overrides, then authored class defaults. Lowering a limit or switching
policy preserves accepted entries and history; it blocks new enqueueing where usage exceeds the
effective allowance. Set/Clear Production Unit Queue Policy and Set/Clear Player Queue Policy
are authorized simulation mutations. Can Enqueue Production is the read-only eligibility query
for ability Can Activate or UI; arbitrary ability graphs do not declare their queued class in
advance, so button availability needs that query wired explicitly. Enqueue Production always
enforces admission and refunds its captured funding on rejection, even without the UI preflight.

User decision (2026-09-12): ownership transfer cancels all pending production with ordinary
per-entry refunds to captured payers. Cancellation walks backwards to preserve front progress.
Successful completions remain charged to the completing player; per-producer history stays with
the producer on capture. Completion reserves history before callback-capable spawn/effect work
and rolls it back on failure, keeping reentrant enqueue checks accurate. Both histories and
overrides are canonical, snapshot-restored state. Snapshot and envelope semantics are v19;
Core simulation-content contributor and built-in command implementation revisions are 6.
Verification and remaining acceptance gates are recorded in PUBLIC_DOCS_BACKLOG.md.

Economy is ability composition over generic deterministic data, not a hardcoded worker subsystem.
Resource-node stock and worker cargo belong in components accessed through typed get/set nodes;
dropoff uses **Grant Income** inside an authorized simulation callback. The whole income map
validates atomically and valid uncapped overflow saturates. Construction uses persistent `FSeinConstructionPayload` settings and explicit job handles. Start Queued for Construction defaults off; Queue, Start, Pause and Complete Construction own lifecycle transitions. Work fields live in the existing FSeinConstructionPayload and Sein Construction authoring component. Add Construction Work and work queries use those fields; the threshold does not change lifecycle or gate completion. There is no separate work component. Explicit entity presentation groups replace automatic mesh hiding, and generic entity binding supplies widget and managed-actor context. Completion preserves the component, applies the captured effect once and releases only the framework-owned `State.UnderConstruction` grant. Stage tags and render delegates support designer presentation; see [.agents/CONSTRUCTION_LIFECYCLE.md](CONSTRUCTION_LIFECYCLE.md).

Combat is designer-owned (re-cut 2026-08-23 from the prescriptive 2026-08-16 substrate). The
framework ships no vitals, weapon, damage, or projectile schema and no combat tick systems; a
game authors its own vitals/weapon structs (native or UDS) and drives them from abilities and
effects. `SeinARTSCombat` owns the acquisition mechanism only: **Find Targets** and **Check
Target** share one gate chain (alive → `RequiredComponent` → range → arc → `RequiredTargetTags`
→ fog LoS → scorer validity) over a derived, non-canonical position index that covers every
live entity; `USeinTargetScorer` is the stateless Blueprint policy CDO (neutral built-in: same
owner excluded, nearest wins). Stat mutation is the generic **Apply Field Delta** in
`USeinSimMutationBPFL` (saturating add with opt-in `bClampMin`/`MinValue` and
`bClampMax`/`MaxValue` — flags default off so an unwired node is never a silent zeroing; a no-op
never dirties the mutation revision; reports `bChanged`/`bAtMin`/`bAtMax`; UDS fields resolve by
authored name identically in editor and cooked builds). Status is
**Apply Effect**; death is the designer's rule followed by **Destroy Entity**; presentation is
**Notify Damage Applied / Heal Applied / Death** (restricted to Ability/Effect), enqueueing the
existing DamageApplied / HealApplied / Death / Kill visual events. Start at
`SeinTargetQueryService.h`, `SeinCombatTypes.h`, `SeinTargetScorer.h`,
`SeinCombatMutationBPFL.h`, and `SeinSimMutationBPFL.h` (Apply Field Delta).

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
into the command. Public Blueprint input code can use `Plan Formation Order` -> `Issue Formation
Order`: planning returns an opaque transient one-use token that freezes the complete preview key,
authenticated player, exact displayed artifact, and BrokerOrder V5 recipient boundaries. Issue
revalidates world/session, principal, authority, and each surviving recipient segment; moving or
destroyed destination providers do not retarget the frozen points. The older `Compute Formation
Preview` and `Issue Broker Order` nodes remain compatibility surfaces and still recompute defaults
when used as a pair.

Movement+ is not a full arbitrary Reeds-Shepp/Dubins route solver. Its wheeled and tracked modes can run a deterministic curated Reeds-Shepp-style **start-maneuver** planner at plan/repath time. That planner considers bounded closed-form candidates such as a departure arc, straight reverse, and K-turn, probes clearance, emits typed `Arc`/`Straight` legs, then hands the remaining coarse route to the normal runtime follower. Wheeled driving uses bicycle kinematics and arc/pursuit tracking; tracked driving selects pivot/arc/reverse behavior. This live behavior supersedes older notes claiming that no shipped vehicle mode emits arcs.

### Canonical state, resync, and replay

- Exact snapshots use explicit native codecs/coverage contracts and fail closed when required state is unsupported.
- Canonical BLAKE3-128 roots cover authoritative and continuation leaves under stable schema/descriptor identities.
- Routine multiplayer roots use mutation revisions plus indexed Merkle trees to re-digest changed leaves only. They are sealed at due network checkpoint boundaries, not every tick.
- Forced rebuilds exist to verify that cache acceleration has not changed the root.
- Resync transfers an authenticated bounded checkpoint envelope plus the exact retained command tail, catches up through the normal gate, and reactivates on an agreed root.
- Replay v9 is an append-only digest-chained journal with periodic checkpoints, opaque turn batches, durable frontiers, bounded indexes, lazy decode, crash-tail recovery, and atomic publication. Frozen v8 reading remains supported.

### Placement admission

Target capture supplies geometry; the ability's Placement definition supplies the authoritative
footprint actor class. Empty Placement retains the legacy Point + Facing Building Class fallback.
The definition applies to any capture gesture and is immutable ability configuration. Visual
component source/mesh overrides never change admission. Both sources remain serialized during
the compatibility migration; a nonempty Placement definition takes precedence.

Abilities with Requires Free Footprint use SeinPlacementValidation for preview, confirm,
broker admission, and final activation (including deferred approach follow-ups). The Point +
Facing spec's Building Class supplies every authored extents shape. Baked navigation is
queried through the existing resolver; live simulation extents with Blocks Nav and a nonzero
layer mask also reject planar overlap, including queued construction sites. Entities without
extents use their navigation fallback radius. Current state is queried instead of the PreTick
overlay so same-tick spawns, removals, and edits are visible immediately. Captured yaw and local
shape offsets are shared between preview and admission. Missing required placement data fails
closed; ungated abilities are unchanged. Core content and built-in command revisions are 8.
Validation evidence is recorded in PUBLIC_DOCS_BACKLOG.md.

### Targeter presentation

ASeinTargeterPreview is a Blueprintable general presentation actor. The subsystem supplies
FSeinTargeterPreviewContext with source identity, input phase, explicit anchor presence,
command-rounded target pose, validity/reason, captured points, dimensions, and an optional actor
visual source. Preview Initialized runs after deferred spawning and optional visual helpers
initialize; Validity Changed runs initially and on result/reason changes; Preview Updated retains
its existing override; Point Captured sees the updated capture array; Preview Ended distinguishes
Submitted, Cancelled, Replaced, and Unavailable. Submitted is not gameplay success. Module unload
suppresses Blueprint teardown callbacks. Context and visual helper state are render-only.

Optional USeinTargeterMeshComponent and USeinTargeterDecalComponent own visual sources, sizing,
and explicit Valid/Warning/Blocked material choices. Missing warning/blocked material falls back
to Valid. Materials are swapped on validity changes without a parameter-name contract. Mesh
cloning preserves actor-local attachment transforms and source component material overrides.
Existing point/facing preview classes remain compatibility presets with the same named inherited
mesh/decal components and legacy fields; the old TintColor path applies only when no explicit
materials are configured. New custom previews inherit the general base and add only the helpers
they need. Construction-site visuals remain owned by entity presentation.

### Fog of war

FoW source stamps are deterministic grid shapes with radial, rectangle, or cone range semantics.
The default grid suppresses every vision layer from entities with unfinished construction
(Queued, Building, Paused, or legacy ReadyToComplete). Complete entities retain authored vision;
merely carrying a construction payload does not suppress it. Filtering precedes the source cache,
so entering construction removes prior footprints on the next scheduled fog update, preserving
other sources and sticky exploration. Completion resumes normal stamping at the same cadence.
Both placed and spawned entities use this lifecycle rule. Fog behavior revision 3, stamp-system
revision 2, and content-contributor revision 2 identify this change; validation is recorded in
PUBLIC_DOCS_BACKLOG.md.
Terrain vision multipliers scale only the active shape range (`Radius`, `HalfExtents`, or
`ConeLength`), keeping cache identity and behavior free of irrelevant-field churn. Extents-authored
blockers fold their local Z offset into the snapshotted world-space base before computing the top.

The dynamic blocker hot path keeps one dense maximum-top grid plus the layer mask. Cells whose
overlapping layers have different tops carry a sparse eight-entry exact-height exception. Opacity
queries therefore use the true maximum for the requested layer subset without multiplying the dense
grid footprint by every layer. The sparse exceptions participate in capture, restore, reset, and
canonical comparison; behavior revision 3 / codec revision 5 deliberately rejects older semantic
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

## Seamless controller identity

UE 5.8 replaces controllers during seamless travel without broadcasting PostLogin.
`ASeinGameMode::SwapPlayerControllers` transfers the old controller's retained lobby seat and
optional external admission record before Unreal destroys the old local controller. The destination
lobby actor is rebuilt from the frozen roster and stamped from the replacement PlayerState;
the completed handoff refreshes its connection address. Neither copied `SeinPlayerID` nor a free
seat grants handoff authority. Missing bindings, conflicting owners, foreign worlds, and non-Human
frozen slots reject the replacement before gameplay binding. A stale old-controller logout cannot
release the transferred seat. Subsequent physical reconnect still requires the matching transport
identity or the configured external authorizer.

## Downstream verification

`Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1` creates disposable projects under ignored
`Saved/ConsumerMatrix`, copies only selected distributable plugin inputs, and verifies Framework,
Cover-only, Squad-only, Framework+Movement+, and all-production-plugin profiles. It generates a consumer-owned map and
manifest, rejects host-project package references, builds Editor and Shipping, loads the exact map,
cooks/packages it, and starts the real packaged Shipping executable. Client/Dedicated Server target
proof is intentionally still open because Epic's launcher UE distribution does not expose those
target builds.
