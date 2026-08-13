# SeinARTS Open Risk Register

This is the actionable remainder after consolidating the historical audits. It intentionally omits fixed historical narrative.

## Release-blocking foundation risks

1. **Launcher UE cannot prove Client/Server targets.** Epic's installed 5.8 distribution rejects
   Client targets before project compilation. Development Client and Dedicated Server must run in
   CI or another source/installed engine distribution that supports those target types. The real
   packaged listen-server/client/reconnect/replay harness is green, but a Game-target listen server
   does not prove a true headless dedicated-server binary.

## Gameplay-backbone gaps

1. Cover now has one exact deterministic max-cardinality/min-wrong-side/min-distance allocator
   shared by the ordinary and Squad resolver adapters. It still lacks cross-broker aggregation,
   stable reservations, and the exact preview-artifact commit/reject lifecycle, so separate
   squad/loose resolver calls can claim the same authored slot.
2. Squad reinforcement requests now have exact slot and monotonic request identity, atomic
   payer/cost snapshots, exact cancel/refund, deterministic completion, reciprocal membership, and
   snapshot continuation. Explicit squad destruction still needs product policy for queued refunds,
   wipe/recreation, retreat, and queue replacement UX.
3. FoW still needs an explicit team/shared vision policy and consumer. The directional,
   source-attributed pair-capability substrate now exists in the current development wave; the known
   blocker-height, authored-Z, and cone terrain-scaling defects are closed.
4. Public targeting lacks the complete line/corridor/gesture policy surface needed by a modern tactical RTS.
5. Movement+ needs the human behavior/performance and real multi-client network matrix. Typed
   render-only vehicle telemetry plus replay-file and bounded reconnect continuation are automated.
   Flight is not a production 3D avoidance/collision model.

## Online-product gaps

1. No backend-neutral auth/party/matchmaking/ranked/stats/leaderboard service layer exists.
2. Canonical divergence detection is not anti-cheat; lockstep clients can possess hidden world state.
3. Co-op campaign save ownership, schema migration, cloud conflict, account identity, and cross-map bootstrap are unbuilt.
4. True listen-host migration is unbuilt. Dedicated-server co-op can defer peer host migration but cannot defer crash recovery.
5. WAN/backend-adapter behavior, true dedicated-server reconnect, process-crash recovery, and
   adversarial network conditions remain runtime/product validation gates. Local packaged
   listen-server reconnect and replay checkpoint seek are qualified.

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
   and failure/write denial retain the partial journal. Real-device long-session hitch, allocator
   high-water/RSS, GC interaction, latency, and exhausted-storage behavior still need Insights and
   soak evidence.
5. Debug navigation rendering is intentionally expensive and can invalidate profiling if left enabled.

## Explicit product decisions still required

- Full cover scoring/contention/reservation policy, requester-aware post-processing, and moving-provider behavior.
- Squad destruction, wipe/recreation, retreat, and reinforcement queue-replacement policy.
- Public targeter/modifier/terrain/production/team-vision API shapes.
- Flight and advanced vehicle-feel defaults after Vehicle Gym evidence.
- Listen-host migration versus dedicated-only supported topology for each game mode.
- Co-op campaign persistence/migration/ownership policy.
- Adaptive input-delay policy after observability data.

These decisions should be presented with live-code options and a recommendation. Do not silently choose them during cleanup or unrelated fixes.
