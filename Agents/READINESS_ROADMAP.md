# SeinARTS Readiness Roadmap

The north star is a native Unreal AAA RTS development experience: designer-first Blueprint authoring over a deterministic, modular, multiplayer-ready C++ foundation. Flexibility, extension stripping, exact state, and good development ergonomics are requirements, not later polish.

This order minimizes rework. Do not start a later stage by weakening an earlier contract.

## 1. Stabilize and clean the shared baseline

**Status:** complete in the commit containing this file; performance/remediation was merged locally to `main` at `27cb490` and then cleaned/hardened.

- Durable engineering state is consolidated under `Agents/`; generated reports and historical audit scratch are gone.
- Proven-dead types/config/code are removed, and touched stale comments now match live behavior.
- Foundation fixed-vector, PRNG draw-order/unit, and ray/box defects have focused regression tests.
- Development/UHT, Shipping, Unit, Integration, Determinism, and fresh-process A/B passed after the trim.
- PIE-only gates remain explicitly recorded as human runtime oracles.
- The clean local `main` is not pushed unless the user requests it.

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
- The Framework consumer now drives real packaged Shipping processes through:
  - listen-server creation and a two-player lobby-to-match travel;
  - membership/config-gated lockstep command flow;
  - forced checkpoint-plus-command-tail resync;
  - physical client disconnect and reconnect, retained-slot relay reclaim, a second resync, and
    canonical-root-gated authorship activation;
  - streaming replay finalization and standalone checkpoint seek; and
  - exact end-tick and canonical-root agreement between replay and authoritative server.
- The packaged run exposed and closed three release-only integration defects: lobby maps now
  materialize relays from their final authoritative slot bindings, Shipping builds no longer hide
  restore work inside compiled-out assertions, and dropped slots can reclaim their retained relay
  without weakening live-slot collision rejection.
- Remaining external release gate: run Development Client and Dedicated Server builds, then repeat
  the runtime topology with a true headless server, under a source/installed UE distribution that
  supports those targets.

Exit: reached locally for the supported packaged Game/listen-server topology. The source-engine
Client/Dedicated Server CI gate remains explicitly red rather than inferred from this result.

## 3. Qualify the CoH-style gameplay backbone

### Movement+

**Automated baseline complete.** The deterministic Vehicle Gym covers MBT, IFV/APC, wheeled
scout, and logistics-truck contracts across open U-turns, reverse-behind goals, an explicit
K-turn, narrow-corridor reverse-out, S-turns, interval repaths, formation-facing arrival,
cancel/reissue, recovery, and mixed infantry/vehicle congestion with different body sizes,
speeds, and masses. Checkpoints taken during arcs, reverse legs, recovery, and close mixed traffic
continue with exact canonical roots. See `Agents/VEHICLE_GYM.md` for the evidence and PIE matrix.

Still required before Movement+ is production-qualified: the human PIE feel/performance matrix,
a Movement+-specific replay-file/network combination test, and presentation telemetry for
steering/yaw/throttle/brake plus track/wheel animation. The current general replay qualification
and Vehicle Gym snapshot proof cover the mechanisms separately, not their combined scenario.

Use evidence to decide whether the curated start-maneuver head plus coarse-route pursuit tail is sufficient or whether downstream A* corners need a broader curvature-shaping stage. Add animation telemetry for steering/yaw/reverse/throttle/brake and track/wheel presentation without making Unreal animation authoritative.

Flight remains a separate scope: current behavior is not a production 3D aircraft collision/avoidance solution.

### Tactical cover and squads

Implement FEAT-03 as one shared preview/commit planner:

- Stable provider/slot identities.
- Maximum-cardinality/minimum-cost deterministic allocation.
- Explicit reservations and lifecycle for cancel, failure, death, provider movement/destruction, snapshot, replay, reconnect, and queued orders.
- Context-rich authoritative-destination composition.
- No duplicated ordinary/squad greedy allocation bodies.

Complete reinforcement request identity, cancel/refund, queue replacement, squad wipe/recreation/retreat, and snapshot/replay coverage.

### Terrain, vision, targeting, and containment

- Author and qualify game terrain catalogs and movement profiles on top of the now-complete
  framework-level per-unit navigation/clearance policy. Shipped `AgentTags` remain classification
  metadata; forbidden terrain is expressed explicitly through the navigation component.
- Height-correct and team/shared FoW policy.
- Line/corridor targeters needed by tactical weapons and formations.
- Stable garrison/transport/containment state and shared observer/team policy.

Exit: designers can build representative infantry, squad, cover, vehicle, garrison, and tactical targeting gameplay without modifying framework internals.

## 4. Freeze the online service contracts

The existing Net module is deterministic match transport, not a complete online platform. Add an optional backend-neutral online extension rather than coupling Core/Net to EOS, Steam, or one vendor.

Freeze contracts for account/auth, party/invite, matchmaking tickets, queue attributes, server allocation, match/roster identity, ranked classification, reconnect credentials, results, idempotent stats/MMR, leaderboards, replay evidence, campaign-save ownership/storage, and telemetry.

Ranked PvP should use a trusted headless dedicated server as coordinator/referee. Canonical roots detect divergence; they do not prevent a modified client from reading hidden lockstep state. Server command legality/visibility gates, platform integrity/anti-cheat, backend-only rank mutation, and replay/anomaly evidence are separate requirements.

Co-op campaign needs an authenticated bounded save envelope, stable account identities, shared/per-player progression, cross-map bootstrap, same-build exact checkpoints, explicit versioned migrations, conflict/ownership policy, drop-in/out, and clear incompatibility UX. Dedicated co-op still needs server-crash recovery; listen-hosted co-op additionally requires real authenticated host migration.

Exit: game UI and progression can target stable provider-neutral interfaces while backend adapters evolve independently.

## 5. Public SDK and release automation

- Establish semantic plugin versions, compatibility policy, migration notes, and release tags.
- Add automated clean package/build/cook/test matrices on GitHub-hosted or self-hosted UE-capable runners.
- Test plugin stripping and downstream consumers from fresh checkouts.
- Publish packaged plugin artifacts for releases, while daily game development uses commit-pinned source integration.
- Provide purposeful user documentation, sample content, diagnostics, error UX, and upgrade guidance.

Exit: a studio can adopt the framework without relying on this repository's private history or an agent to explain hidden setup.
