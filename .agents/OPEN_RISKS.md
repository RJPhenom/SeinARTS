# SeinARTS Open Risk Register

This is the actionable remainder after consolidating the historical audits. It intentionally omits fixed historical narrative.

## Release-blocking foundation risks

1. **Launcher UE cannot prove Client/Server targets.** Epic's installed 5.8 distribution rejects
   Client targets before project compilation. Development Client and Dedicated Server must run in
   CI or another source/installed engine distribution that supports those target types. The real
   packaged listen-server/client/reconnect/replay harness is green, but a Game-target listen server
   does not prove a true headless dedicated-server binary.
2. **The public documentation product now has its spine but is not complete.** `Docs/` carries a
   deliberate customer tree (README, Installation, First Skirmish, Determinism Rules, Authoring
   Units), so publication's fail-closed docs gate is satisfiable. Still to author: multiplayer/
   lobby setup, replays, formations/gestures, extension deep-dives, and a project-settings
   reference; docs accuracy review belongs in each release's gate.

## Gameplay-backbone gaps

1. Cover now has one exact deterministic max-cardinality/min-wrong-side/min-distance allocator
   shared by the ordinary and Squad resolver adapters, plus a stable-keyed authoritative-destination
   provider registered in Core's StateContract. It still lacks cross-broker aggregation, stable
   reservations, and the exact preview-artifact commit/reject lifecycle, so separate squad/loose
   resolver calls can claim the same authored slot.
2. Squad reinforcement requests now have exact slot and monotonic request identity, atomic
   payer/cost snapshots, exact cancel/refund, deterministic completion, reciprocal membership, and
   snapshot continuation. Explicit squad destruction still needs product policy for queued refunds,
   wipe/recreation, retreat, and queue replacement UX.
3. FoW now consumes the directional ShareVision pair capability: entity visibility, seen
   latches, cell queries, the fog overlay, and the minimap union every granting ally's vision
   (zero-grant worlds take a fast path with legacy cost). Focused directional/revocation
   regression coverage exists. Remaining: team-vision UX policy (who may grant/revoke in
   gameplay), and PIE verification of the shared overlay/minimap presentation.
4. Public targeting lacks the complete line/corridor/gesture policy surface needed by a modern tactical RTS.
5. Movement+ needs the human behavior/performance, scale, true dedicated-server, and WAN/backend
   matrices. Typed render-only vehicle telemetry and a real packaged two-process listen-server flow
   through deterministic adverse latency/jitter/loss/duplication/reordering, resync, physical
   reconnect, and checkpoint-seek replay are automated. Flight is not a production 3D
   avoidance/collision model.
6. Containment now has fail-closed acyclic/reciprocal structural state, overflow-safe mutation,
   quiescent-root/checkpoint validation, fresh-world snapshot continuation, representative
   ability-command/checkpoint/replay mutation workflows, and measured 100/500/1,000-occupant
   root/checkpoint curves. Multi-client PIE and shared observer/team presentation policy remain.

## Online-product gaps

1. No backend-neutral auth/party/matchmaking/ranked/stats/leaderboard service layer exists.
2. Canonical divergence detection is not anti-cheat; lockstep clients can possess hidden world state.
3. Co-op campaign save ownership, schema migration, cloud conflict, account identity, and cross-map bootstrap are unbuilt.
4. True listen-host migration is unbuilt. Dedicated-server co-op can defer peer host migration but cannot defer crash recovery.
5. WAN/backend-adapter behavior, true dedicated-server reconnect, and process-crash recovery remain
   runtime/product validation gates. Local packaged listen-server reconnect, replay checkpoint seek,
   and deterministic adverse UDP fault injection are qualified.

## Performance and scale risks

1. Continuous 100-unit formation preview is now qualified in a fresh UE 5.8 one-world Sandbox A/B.
   Sparse dynamic-blocker indexing reduced refresh from 11.490 to 4.518 ms; repeated matched captures
   observed a 1.60-2.86 ms complete-frame delta. The independent 128-member public-layout sentinel
   remains 1.337 ms p95 coverless and 3.324 ms p95 with dense Cover. Larger selections, multi-world
   PIE, configured game renderers, and 300/500/1,000-unit moving combat remain open scale gates.
2. The isolated real fixed-tick dense-collision curve is now measured at 64/128/256 packed movers
   (1.257/3.114/7.214 ms median in the All profile on the current machine). It does not replace a large moving-combat
   PIE/Insights curve with movement, avoidance, navigation, abilities, animation, and presentation.
3. Game-specific animation complexity can exceed the default mannequin baseline.
4. Replay automatic periodic checkpoint envelope encoding and full-flush use one ordered background
   pipeline. Periodic snapshot capture remains synchronous, but unchanged cache-safe component
   storages now reuse process-local serialized blobs under exact revision checks; any storage that
   exposes a mutable payload pointer always serializes live. The cache copies into each snapshot and
   is capped at 64 MiB per world. The measured moving-storage capture curve improved from
   4.033/14.245/27.831 ms to 2.037/7.815/15.288 ms at 100/500/1,000 entities without changing
   snapshot or canonical schemas. Mandatory initial/direct writes, final publication, and
   pressure-forced drains remain synchronous. Automated integration proves ordered bounded pressure,
   failure/write denial retain the partial journal, and a compressed 25-periodic-checkpoint session
   proves exact repeated encode/append, authoritative mutation replay, every-checkpoint seek/root,
   stable cache payload/allocation, and cold/hot restore behavior. A separate eight-cycle,
   128-entity fixture advances fixed ticks and mutations while encode is paused after payload
   serialization and append is paused with the real file open at its verified offset. It proves
   exact resident bytes, no false durability/overtaking, and production-callback catch-up across
   controlled operation overlap. Accelerated real-file automation now runs 449 turns (448 across
   natural checkpoint cycles plus one uncheckpointed command catch-up turn), 64 natural
   periodic checkpoints, eight full GCs, exact sampled seeks/full playback, bounded process working
   set/private commit/late growth, and measured latency tails. Configured 64 MiB policy exhaustion
   preserves a partial that replays to the exact last durable root. Bookmark-bounded full Memory
   Insights attribution found and removed an 8 MiB completed-future envelope copy. Clean commit
   `8178dec` now has a same-attempt build and production `Qualified` receipt over the warmed final 56
   periodic checkpoints: every retained allocation has a callstack and production replay retention
   is zero bytes against the fixed 4 KiB ceiling. The allocator-attribution sentinel is validated
   separately and cannot consume that budget. This closes the local warmed retention gate;
   multi-hour real-device hitch and allocator-high-water distributions, platform storage matrices,
   and true OS disk-full behavior remain open.
5. Debug navigation rendering is intentionally expensive and can invalidate profiling if left enabled.
6. The dense-cover 128-member public preview tripled with the FEAT-03 selection-plan provider:
   9.905 ms median / 10.130 ms p95 measured 2026-08-15 versus the pre-FEAT-03 3.181/3.324 ms
   baseline (coverless 128 unchanged at ~1.3 ms; the solver-only 128x128 stress is unchanged at
   ~12.2 ms). Suspects: the exact whole-selection allocator running per preview refresh and the
   per-slot reservation scans. A CPU trace with the named preview scopes is at
   `Saved/Profiling/ShareVisionPerf-20260815-203853.utrace`. Attribution: the exact Hungarian
   allocator (`Sein_Cover_Assignment_Hungarian` scope) dominates — the preview now runs the
   ~10 ms-class solve the 128x128 stress measures. Mitigated 2026-08-15: an unchanged-input
   re-solve skip (gesture + displayed-member pose fingerprint, capped at 5 ticks) cuts a
   stationary preview from 30 solves/second to at most 6 and typically zero once units settle;
   per-solve cost is unchanged and drag-time refreshes still pay it. Remaining options if PIE
   feels it: an async preview solve (click path already recomputes exactly), or eligible-edge
   reduction. Per-solve reduction is a product/feel decision — do not silently change the
   preview's exactness.

## Explicit product decisions still required

- Full cover scoring/contention/reservation policy, requester-aware post-processing, and moving-provider behavior.
- Squad destruction, wipe/recreation, retreat, and reinforcement queue-replacement policy.
- Public targeter/modifier/terrain/production/team-vision API shapes.
- Flight and advanced vehicle-feel defaults after Vehicle Gym evidence.
- Listen-host migration versus dedicated-only supported topology for each game mode.
- Co-op campaign persistence/migration/ownership policy.
- Adaptive input-delay policy after observability data.

These decisions should be presented with live-code options and a recommendation. Do not silently choose them during cleanup or unrelated fixes.
