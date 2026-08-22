# SeinARTS Readiness Roadmap

The north star is a native Unreal AAA RTS development experience: designer-first Blueprint authoring over a deterministic, modular, multiplayer-ready C++ foundation. Flexibility, extension stripping, exact state, and good development ergonomics are requirements, not later polish.

The framework never decides which type of RTS a game is — that is the consuming game's call
(the host WARSEIN project's own game is squad-tactical-inspired; the framework must equally
support massive-scale designs, so mechanisms are sized for the largest scale). The combat
substrate SHIPPED 2026-08-16 as the 13th framework module (`SeinARTSCombat`): vitals + damage
resolution with a Blueprint formula policy seam, weapon cycling, an on-demand target query
service with a Blueprint scorer seam, instant + projectile-entity delivery, and starter
attack content — mechanisms only; feel stays designer-authored (see `.agents/FRAMEWORK_MAP.md`).
The 2026-08-21 armed-population curve proved the prior acquisition sweep exceeded one 30 Hz turn
at 1,000 units; a transparent derived Combat spatial index now brings both warm and rebuild-inclusive
acquisition to about 11 ms without changing canonical state or scorer semantics. Remaining under the
any-design standard: a representative game-world battle capture with fog/presentation. Designer-
style harvest/dropoff and worker-construction loops are now qualified through real commands,
snapshot restore, and exact active-ability continuation.

This order minimizes rework. Do not start a later stage by weakening an earlier contract.

## 1. Stabilize and clean the shared baseline

**Status:** complete in the commit containing this file; performance/remediation was merged locally to `main` at `27cb490` and then cleaned/hardened.

- Durable engineering state is consolidated under hidden `.agents/`; generated reports and historical audit scratch are gone.
- Proven-dead types/config/code are removed, and touched stale comments now match live behavior.
- Foundation fixed-vector, PRNG draw-order/unit, and ray/box defects have focused regression tests.
- Development/UHT, Shipping, Unit, Integration, Determinism, and fresh-process A/B passed after the trim.
- PIE-only gates remain explicitly recorded as human runtime oracles.
- The consolidated baseline is preserved on synchronized local and remote `main`.

Exit: clean tree, no obsolete tracked artifacts, exact gate evidence, and one unambiguous roadmap.

## 2. Produce the first downstream Integration Candidate

**Status:** complete for the local Epic-launcher-engine scope. The clean packaged Game-target
listen-server/runtime proof is green. Development Client and Dedicated Server remain an external
CI gate because this engine distribution rejects those target types before project compilation.

- Async path continuation ownership is closed: budget tails persist, results survive unrelated
  drains, and terminal move actions cancel their state before re-entrant hooks.
- FoW overlapping layer heights, authored local-Z blocker tops, and cone terrain scaling are
  closed with canonical-state coverage.
- Per-unit terrain, nav-layer, wall-padding, and compound-footprint policy now propagates through
  command admission, pathing/repathing, movement, collision, containment, formation projection,
  requester-aware Blueprint queries, and Movement+ maneuver probes.
- Ability/passive lifecycle identity is centralized: activity is coherent during callbacks,
  primary ownership is singular and fail-closed, re-entrant replacements are pointer-safe, and
  snapshot admission requires exact active/index agreement.
- Checkpoint-safe latent authoring is admitted at compile/save: supported Blueprint continuation
  nodes require exact codecs, while unsupported shapes fail before runtime.
- Mutable Level Data, formation/resolver statefulness, and conditional provider ownership now have
  explicit state-coverage/admission contracts. Movement+ batch withdrawal is atomic.
- Automated package/consumer proof now covers:
  - Framework only.
  - Framework + Cover, with Squad and the bridge physically stripped.
  - Framework + Squad, with Cover and the bridge physically stripped.
  - Framework + Movement+.
  - Framework + Squad + Cover + CoverSquad bridge + Movement+.
  - UE 5.8 Editor and Shipping targets locally (originally proven on 5.7; re-proven on 5.8 by the 2026-08 packaging pipeline builds).
  - Clean cook/load of a minimal consumer map and consumer-owned simulation-content manifest.
- Clean consumers have no hidden dependency on this host project's `/Game/SeinARTS` content.
- The Framework and Movement+ consumers now drive real packaged Shipping processes through:
  - listen-server creation and a two-player lobby-to-match travel;
  - membership/config-gated lockstep command flow;
  - forced checkpoint-plus-command-tail resync;
  - physical client disconnect and reconnect, retained-slot relay reclaim, a second resync, and
    canonical-root-gated authorship activation;
  - streaming replay finalization and standalone checkpoint seek; and
  - exact end-tick and canonical-root agreement between replay and authoritative server.
- The Movement+ packaged flow also passes a deterministic adverse UDP profile with latency, jitter,
  loss, duplication, and reordering in both directions. The receipt records actual forwarded faults,
  observed order inversions, bounded delay, per-direction seeded jitter, endpoint relearn, zero
  unroutable traffic, and zero delayed datagrams discarded at shutdown. The runtime receipt binds
  both the exact proxy evidence and packaged Shipping executable by SHA-256.
- Movement+ additionally executes a real long-range wheeled Move command and requires the exact
  instantiated wheeled class, raw target, bounded nonzero typed telemetry, and exact movement state
  across both peers, forced resync, physical reconnect, and checkpoint-seek replay.
- Replay v9 automatic periodic checkpoints encode and durably append through one ordered background
  pipeline; persistence state advances only after worker success. Integration coverage holds that
  append after file open/positioning but before byte writes, accumulates eligible turns to the exact resident bound without false
  durability or overtaking, and proves the pressure-forced wait drains and publishes a valid ordered
  journal after release. Worker failure and real write denial stop recording without deleting the
  partial journal. Periodic snapshot capture remains main-thread work, but exact revision tracking now
  reuses unchanged process-local component-storage blobs only while no mutable payload pointer has
  escaped; retained bytes are capped at 64 MiB per world. The measured 100/500/1,000-entity moving
  capture curve improved from 4.033/14.245/27.831 ms to 2.037/7.815/15.288 ms. Mandatory writes,
  final publication, and pressure drains remain main-thread work. A compressed 128-entity,
  25-periodic-checkpoint integration session now proves every ordered encode/append cycle, alternating
  authoritative mutation replay, exact seek/root at every checkpoint, terminal continuation, and
  stable cache payload/allocation across restore. A separate eight-cycle, 128-entity integration
  fixture advances fixed ticks and authoritative mutations while checkpoint encode is paused after
  payload serialization and checkpoint append is paused with the real file open at the verified
  offset. Exact resident command bytes, no false durability/overtaking, callback-only catch-up,
  checkpoint index/seek/root, and full playback remain exact. An accelerated real-file operational
  soak now adds 449 turns (448 across the periodic cycles plus one journal catch-up turn), 64 natural
  periodic checkpoints, eight full-GC boundaries, 128 entities,
  sampled exact seek/root/capability checks, full playback, process-memory/late-growth sentinels,
  and checkpoint latency tails. A configured 64 MiB file-policy exhaustion test also proves the
  preserved partial replays to its exact last durable tick and root. Full bookmark-bounded Memory
  Insights attribution found an 8 MiB retained completed-future envelope copy. Completed-future
  consumption, explicit checkpoint-buffer ownership, and operation-matched worker drains removed
  the retained production replay allocations. Clean commit `8178dec` has a same-attempt build and
  production `Qualified` receipt over the warmed final 56 checkpoints: zero production replay bytes
  retained against the fixed 4 KiB ceiling, with complete callstacks and separately validated
  allocator-attribution sentinels. The headless exporter binds source, build, trace, engine,
  trusted-analyzer, and output identities. This closes the local warmed allocator-retention gate.
  Multi-hour real-device timing and allocator-high-water behavior, platform storage matrices, and
  true OS disk-full behavior remain open.
- The packaged run exposed and closed three release-only integration defects: lobby maps now
  materialize relays from their final authoritative slot bindings, Shipping builds no longer hide
  restore work inside compiled-out assertions, and dropped slots can reclaim their retained relay
  without weakening live-slot collision rejection.
- Remaining external release gate: run Development Client and Dedicated Server builds, then repeat
  the runtime topology with a true headless server, under a source/installed UE distribution that
  supports those targets.

Exit: reached locally for the supported packaged Game/listen-server topology. The source-engine
Client/Dedicated Server CI gate remains explicitly red rather than inferred from this result.

## 3. Qualify the squad-tactical gameplay backbone

### Designer authoring workflows

The Balance Data entity/ability round trip is automation-qualified for destructive Gather,
changed-cell Push, exact Check Sync accounting, stale-schema repair, sparse union columns,
duplicate Blueprint names, exact source-class rebinding, Blueprint reinstance, and saved-package
reload. Generated tables bind every row and column to stable source identity and fail closed until
re-gathered when the matched class or schema changes. Output paths must resolve beneath a mounted
content root. The explicit component picker accepts both native component structs and eligible
designer-authored UDS types found on the matched entities, while an empty Tracked Components list
retains track-all behavior.
The durable Preview, Gather, Check Sync, Push, save, and reseal contract is consolidated in
`.agents/FRAMEWORK_MAP.md` pending the owner-authored public website.

Ability authoring now has a consolidated model-facing contract for creation/granting, smart
commands, targeting, activation/lifecycle semantics, deterministic state, and checkpoint-safe Move
To continuation in `.agents/FRAMEWORK_MAP.md`. A blocking editor validator now applies the
shared deterministic member/call contract to every Ability Blueprint. Untrusted calls fail closed,
presentation conversions override otherwise trusted fixed-point libraries, and unseeded randomness
remains denied. `FFixedRandom`'s complete state participates in canonical Ability snapshots with
exact next-draw restoration coverage, and the host example Ability assets pass an all-Blueprint
Data Validation commandlet run.

### Movement+

**Automated baseline complete.** The deterministic Vehicle Gym covers MBT, IFV/APC, wheeled
scout, and logistics-truck contracts across open U-turns, reverse-behind goals, an explicit
K-turn, narrow-corridor reverse-out, S-turns, interval repaths, formation-facing arrival,
cancel/reissue, recovery, and mixed infantry/vehicle congestion with different body sizes,
speeds, and masses. Checkpoints taken during arcs, reverse legs, recovery, and close mixed traffic
continue with exact canonical roots. See `.agents/VEHICLE_GYM.md` for the evidence and PIE matrix.

Still required before Movement+ is production-qualified: the human PIE feel/performance matrix,
true dedicated-server topology, WAN/backend behavior, and scale evidence. A generated packaged
Shipping listen server and client now qualify live Movement+ command flow, root gossip, resync,
physical reconnect, exact movement continuation, checkpoint-seek replay, and a deterministic local
adverse profile with latency, jitter, loss, duplication, and reordering. Typed
render-only presentation telemetry for steering/yaw/throttle/
brake plus track/wheel animation is complete, including correction-resistant driver intent,
wrapped long-run wheel phase, teardown/restore reset, hidden raw render state, and rejection from
deterministic movement and Ability Blueprints.
Movement+-specific in-process automation also drives live wheeled movement through replay-file
checkpoint continuation and the bounded reconnect envelope into a fresh world, then proves exact
terminal roots and canonical movement state.

Use evidence to decide whether the curated start-maneuver head plus coarse-route pursuit tail is
sufficient or whether downstream A* corners need a broader curvature-shaping stage. Qualify the
new animation telemetry in PIE without making Unreal animation authoritative.

Flight remains a separate scope: current behavior is not a production 3D aircraft collision/avoidance solution.

### Tactical cover and squads

Ordinary and Squad adapters delegate to one pure Cover-owned allocator. It maximizes assignment
cardinality, minimizes wrong-side use, then minimizes exact fixed-point squared distance;
invalid/duplicate inputs and unrepresentable distance ranges fail closed. The selection-plan
provider aggregates ordinary and persistent-Squad members before that solve, so allocation is exact
across the whole mixed selection. The dense 128x128 solver-only stress averages 11.998 ms; the
selection-wide 128-member public preview measures 9.905 ms median / 10.130 ms p95 after FEAT-03.
An unchanged-input skip capped at five ticks avoids repeating that solve under a stationary cursor.
Drag-time per-solve cost and configured-game presentation remain PIE/performance gates.

Fresh UE 5.8 full-game profiling now qualifies continuous preview for the current 100-owned-mover
Sandbox workload. Exact selected-member blocker exclusions preserve group placement semantics, and
a derived sparse blocker-cell index removes repeated shape rasterization from candidate probes.
Repeated matched captures measured a 1.60-2.86 ms complete-frame preview delta; Unit, Integration,
Determinism, and independent fresh-process serial/parallel roots remain exact. Larger-selection,
multi-world, and 300/500/1,000-unit moving-combat captures remain scale gates rather than inferred.

Core's authoritative-destination seam now composes deterministic providers by canonical stable ID,
passes requester context, and binds provider identity plus behavior revision into the match
StateContract. Cover uses the keyed registry, and all shipped movement/collision consumers query the
composed authority result. The legacy position-only hook remains compatibility-only and is not a
shipping integration surface; deterministic bootstrap rejects it while bound.

FEAT-03's native-controller mechanism is landed (2026-08-15): selection-wide destination plans
aggregate ordinary and persistent-Squad members through the selection-plan provider seam; the exact
displayed artifact rides native command admission; reservations have a full queued→settled lifecycle
across cancel, failure, death, provider movement/destruction, snapshot, replay, and reconnect; and RJ
froze the conflict policy as policy D — survivors always receive their exact shown destinations,
dead members drop only their own slot, contention never rejects or re-plans
(`.agents/DECISION_FROZEN_CONFLICT_POLICY.md`, BrokerOrder schema V3). A real BrokerOrder test now
moves the provider between preview and issue, proves exact authoritative arrival and settlement, and
reconstructs every replay tick in a fresh world. The public Blueprint preview/issue pair cannot carry
the full preview key or displayed artifact through issue; its API shape requires RJ's decision. PIE
feel remains in RJ's batch.

Squad reinforcement request identity, exact slot selection, atomic charge, exact cancel/refund,
completion membership, structural restore admission, snapshot continuation, and designer-authored
destruction settlement (Refund / Forfeit / PartialRefund) are complete. Real player commands now
qualify pending-checkpoint continuation, per-tick replay roots, the authored test subclass, and a
separate exact checkpoint transfer through the shipped native reinforcement provider. Remaining
Squad work requires product policy for queue replacement, wipe/recreation, and retreat; after those
decisions, qualify the new policy paths through commands, replay, and PIE tactics coverage.

### Terrain, vision, targeting, and containment

- Author and qualify game terrain catalogs and movement profiles on top of the now-complete
  framework-level per-unit navigation/clearance policy. Shipped `AgentTags` remain classification
  metadata; forbidden terrain is expressed explicitly through the navigation component.
- Height-correct FoW is complete. The deterministic directional pair-capability ledger required by
  team/shared vision is complete through command timing, cache validation, canonical lifecycle,
  packaged reconnect, and replay. FoW consumes ShareVision directionally across entity visibility,
  seen latches, cell queries, overlay, and minimap; shared-vision presentation still needs PIE.
- Line/corridor targeters shipped 2026-08-15 (`USeinLineTargeterSpec`: drag and multi-click both
  first-class per RJ's ruling; see `.agents/DECISION_TARGETER_LINE_CORRIDOR.md`). Footprint-aware
  corridor-fit validation is shipped; only PIE feel and tint verification remain.
- Containment structural integrity is complete: admission prevents cycles and overflow, reciprocal
  load/slot/attachment state fails closed at bootstrap and canonical/checkpoint boundaries, malformed
  restores are failure-atomic, and fresh-world mutation continuation is exact. Test-only
  designer-style transport abilities now qualify canonical command timing, encoded checkpoint
  continuation, and per-tick replay roots; container-local deploy offsets rotate with authoritative
  facing under behavior epoch `SeinARTS.Replay.6`. Valid 100/500/1,000-occupant roots plus
  invalidated/warm checkpoint captures are measured. Multi-client PIE and shared observer/team
  presentation policy remain.

Exit: designers can build representative infantry, squad, cover, vehicle, garrison, and tactical targeting gameplay without modifying framework internals.

## 4. Freeze the online service contracts

**Status: provider-neutral contract and Loopback reference implementation complete on 2026-08-22.**
The optional Online Services extension now covers the frozen account, party, matchmaking,
allocation, roster, reconnect, result, stat, leaderboard, replay-evidence, campaign-save, and
telemetry schemas. Durable mutations have principal-scoped retry safety plus semantic uniqueness;
connection admission uses a non-secret correlation ID, type-qualified transport identity,
provider-owned exact-seat resolution, and one-use consumption. Provider callbacks are bounded,
game-thread deferred, cancellation/reset guarded, and synchronously quiesced before module unload.
Loopback is refused for Shipping, dedicated-server, and authenticated-admission configurations.

Focused contract/security/lifecycle tests, the broad All Unit/Integration/Determinism suites,
Development and Shipping builds, standalone plugin packaging, and fresh source and exact-artifact
consumers with public-header, Shipping, package, and startup checks qualify the implementation.
In-process integration now drives the production `ASeinGameMode::PreLogin` -> `InitNewPlayer`
over a manifest-backed exact seat, including typed-identity rejection, one-use admission, and
connected-seat exclusion.
Vendor adapters and real dedicated/WAN `PreLogin` -> `InitNewPlayer` qualification remain deferred
until their concrete integration wave.

The existing Net module is deterministic match transport, not a complete online platform. Add an optional backend-neutral online extension rather than coupling Core/Net to EOS, Steam, or one vendor.

Freeze contracts for account/auth, party/invite, matchmaking tickets, queue attributes, server allocation, match/roster identity, ranked classification, reconnect credentials, results, idempotent stats/MMR, leaderboards, replay evidence, campaign-save ownership/storage, and telemetry.

Ranked PvP should use a trusted headless dedicated server as coordinator/referee. Canonical roots detect divergence; they do not prevent a modified client from reading hidden lockstep state. Server command legality/visibility gates, platform integrity/anti-cheat, backend-only rank mutation, and replay/anomaly evidence are separate requirements.

Co-op campaign needs an authenticated bounded save envelope, stable account identities, shared/per-player progression, cross-map bootstrap, same-build exact checkpoints, explicit versioned migrations, conflict/ownership policy, drop-in/out, and clear incompatibility UX. Dedicated co-op still needs server-crash recovery; listen-hosted co-op additionally requires real authenticated host migration.

Exit: game UI and progression can target stable provider-neutral interfaces while backend adapters evolve independently.

## 5. Public SDK and release automation

- Semantic release tags and one-version production-plugin cohorts are defined in
  `.agents/WORKFLOW.md`; deterministic compatibility, installation, upgrade, and release-evidence
  contracts are consolidated in `.agents/CONSUMER_VERIFICATION.md`. Root `Docs/` is intentionally
  empty until RJ authors the GitHub Pages website. Publication warns and omits documentation while
  empty; once present, every website file is bound into immutable release evidence.
- Release packaging now validates SemVer and refuses to publish dirty or mid-build-drifted source
  under a clean commit/tag identity. It emits SHA-256 artifact/dependency provenance and release
  publication is gated on fresh consumers built from the exact six ZIPs. Package-only diagnostics
  may still exercise local changes.
- A machine-readable release-gate entrypoint and manual self-hosted Windows workflow now compose
  Editor/Shipping builds, both test profiles, standalone packaging, and the exact-ZIP consumer
  matrix. Receipts bind exact attempt/run IDs, source and engine identity, indexes, binary and
  metadata hashes, public-header manifest, packaged-runtime result, artifacts, and evidence archive.
  Publication requires the deterministic adverse runtime profile and binds its exact proxy receipt,
  standalone proxy self-test detail, and packaged Shipping executable; baseline or skipped runtime
  evidence cannot be published.
  Publication compiles each shipped `Public` header independently, verifies remote draft assets
  byte-for-byte, and resumes only an exact matching interrupted draft. Executing the workflow still
  requires a UE-capable runner with Client/Server support.
- Test-plugin stripping and downstream consumers from fresh checkouts are automated in the exact-ZIP
  consumer matrix.
- Publish packaged plugin artifacts for releases, while daily game development uses commit-pinned source integration.
- Compatibility, upgrade, and getting-started contracts are consolidated for model use in
  `.agents/CONSUMER_VERIFICATION.md`. A read-only installation
  diagnostic validates engine identity, exact plugin closure/cohort, test stripping, recursive
  duplicate installs, source identity, and canonical project-owned manifest containment; it ships
  in the Framework ZIP, binds every consumer profile, and is retained in release evidence. Public
  stable diagnostic codes and common build, manifest, compatibility, and lockstep-latency failures.
  A Windows PowerShell 5.1 self-test prevents the shipped diagnostic's pass/adversarial contract
  from drifting. The consumer contract separates host examples from shipped plugin content and
  records the project-owned settings, unit, map, manifest, and qualification boundaries. Movement+
  telemetry is preserved in the plugin guide and `.agents/VEHICLE_GYM.md`; Ability, economy,
  combat, and Balance Data engineering boundaries remain in `.agents/FRAMEWORK_MAP.md` pending the
  owner-authored website. Broader in-editor error UX and public documentation remain.

Exit: a studio can adopt the framework without relying on this repository's private history or an agent to explain hidden setup.
