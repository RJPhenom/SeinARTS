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
- Define a checkpoint-safe latent-authoring contract. Supported Blueprint latent nodes require exact codecs; unsupported continuation shapes must fail during authoring/compile/save rather than during a live reconnect.
- Resolve remaining state-coverage admission contracts for mutable Level Data, stateful formation/resolver implementations, and conditional provider ownership.
- Add automated package/consumer proof for:
  - Framework only.
  - Framework + Movement+.
  - Framework + Squad + Cover + Movement+.
  - UE 5.7 Editor, Development Client, Dedicated Server, and Shipping targets.
  - Clean cook/load of a minimal consumer map and consumer-owned simulation-content manifest.
- Ensure consuming projects have no hidden dependency on this host project's `/Game/SeinARTS` content.

Exit: a commit-pinned source consumer can build, cook, start a match, snapshot, replay, and reconnect using supported plugin combinations.

## 3. Qualify the CoH-style gameplay backbone

### Movement+

Build a deterministic Vehicle Gym before rewriting algorithms. Cover MBT, IFV/APC, wheeled scout, and logistics-truck archetypes across open U-turns, reverse-behind goals, K-turns, narrow corridors/gates, S-turns, dynamic repaths, formation-facing arrival, stop/reissue, mixed infantry/vehicle congestion, and different body sizes/speeds. Snapshot/replay in the middle of arcs, reverse cusps, and recovery.

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
