# SeinARTS Open Risk Register

This is the actionable remainder after consolidating the historical audits. It intentionally omits fixed historical narrative.

## Release-blocking foundation risks

1. **Launcher UE cannot prove Client/Server targets or the Test configuration.** Epic's installed
   5.8 distribution rejects Client targets before project compilation and refuses the `Test`
   configuration outright ("Targets cannot be built in the Test configuration with this engine
   distribution", verified 2026-08-23). Development Client, Dedicated Server, and packaged Test
   builds must run in CI or another source/installed engine distribution that supports them. The
   real packaged listen-server/client/reconnect/replay harness is green (Shipping), but a
   Game-target `-server` process does not prove a true headless dedicated-server binary. Separate-
   process PIE (editor host or editor client + `-game` server/clients) is qualified as of
   2026-08-23: the config fingerprint now uses the canonical FText-blind exporter (an editor
   process exported config display-name FText with the package-localization namespace, a `-game`
   process with an empty one, so every editor↔game pairing failed parity and kicked — a latent
   bug surfaced by the first editor-vs-game pairing); a kicked/leaving client resets its lobby
   session contract before traveling to the menu; ambient world auto-start never consumes a stale
   lobby contract. `Sein.Config.DumpFingerprint` prints the exact hashed text for future parity
   triage.
2. **The public documentation website is owner-authored and not yet present.** Root `Docs/` is
   intentionally empty until RJ builds the GitHub Pages site. Release publication warns and omits
   documentation while it is empty. Agents must preserve internal contracts in `.agents/` and must
   not independently populate the website tree.

## Gameplay-backbone gaps

1. Cover's selection-wide plan provider aggregates ordinary and persistent-Squad members through
   one exact allocator. Stable reservations and the exact native-controller preview artifact survive
   admission, queueing, settlement, cancellation, death, provider movement/destruction, snapshot,
   replay, and reconnect under RJ's policy D. Real-command automation moves the provider after
   preview, observes the executing reservation, reaches and settles the exact shown world slot, then
   reproduces every replay tick in a fresh world. An in-flight snapshot now restores the executing
   broker, reservation, latent movement, and every subsequent canonical root through exact
   settlement. A shipped-A* path with an unrelated blocker added at its endpoint also proves the
   authoritative final step refuses that blocker until it clears. Public Blueprint input paths now
   have an opaque one-use `Plan Formation Order` / `Issue Formation Order` token that carries the
   complete preview key, exact artifact, and recipient boundaries through authenticated issue and
   deterministic admission. Moving/destroyed providers do not invalidate frozen destinations;
   session, principal, authority, overlap, and live roster drift do. Remaining: PIE feel/visual and
   Blueprint-graph ergonomics checks for moving and contended cover, plus the larger-selection
   performance risk recorded below.
2. Squad reinforcement requests now have exact slot and monotonic request identity, atomic
   payer/cost snapshots, exact cancel/refund, deterministic completion, reciprocal membership, and
   snapshot continuation. Real player-command continuation and per-tick replay-root coverage exercise
   the authored test subclass; an independent checkpoint-transfer test exercises the exact shipped
   native reinforcement provider. Destruction settlement DECIDED by RJ 2026-08-16 and shipped: a
   per-squad authored toggle (`Reinforce Refund On Destruction` = Refund default / Forfeit /
   PartialRefund with tunable fraction) settled by the deterministic teardown sweep; snapshot v17.
   Still open: wipe/recreation, retreat, and queue-replacement UX policy.
3. FoW now consumes the directional ShareVision pair capability: entity visibility, seen
   latches, cell queries, the fog overlay, and the minimap union every granting ally's vision
   (zero-grant worlds take a fast path with legacy cost). Focused directional/revocation
   regression coverage exists. Team-vision policy DECIDED by RJ 2026-08-16: team seeding is the
   match-start default, runtime updates are first-class and asymmetric, nothing prescriptive —
   shipped as Grant/Revoke Pair Capability nodes on the ability/effect-restricted Sim Mutation
   Library (player-driven changes route through abilities; the MatchControl wire command stays
   for admin/scenario tooling). Remaining: PIE verification of the shared overlay/minimap
   presentation.
4. Line/corridor targeting shipped 2026-08-15 (RJ's ruling: drag-line and multi-click polyline are
   both first-class, selectable per ability on `USeinLineTargeterSpec`; segments ride the existing
   `TargeterPoints` wire field). Corridor-fit validation shipped same day: the targeter samples the
   segment centerline and corridor edges against the dynamic passability resolver (blocked line →
   Blocked, pinched lane → Warning; opt-out per spec for over-wall abilities). Remaining: PIE
   feel/visual verification of the line preview and corridor tinting.
5. Movement+ needs the human behavior/performance, true dedicated-server, and WAN/backend
   matrices. Typed render-only vehicle telemetry and a real packaged two-process listen-server flow
   through deterministic adverse latency/jitter/loss/duplication/reordering, resync, physical
   reconnect, and checkpoint-seek replay are automated. A first fixed-tick vehicle scale curve now
   exists (`SeinARTS.Perf.MovementPlus.Scale`, 2026-08-15): two mixed wheeled/tracked columns
   crossing an open field through real A*, maneuver planning, steering, avoidance, and collision
   measure 3.254/7.214/14.225 ms medians at 100/200/400 vehicles — near-linear, 400 inside the
   30 Hz budget on the current machine. PIE-with-presentation scale remains open. Flight is not a
   production 3D avoidance/collision model.
6. Containment now has fail-closed acyclic/reciprocal structural state, overflow-safe mutation,
   quiescent-root/checkpoint validation, fresh-world snapshot continuation, representative
   ability-command/checkpoint/replay mutation workflows, and measured 100/500/1,000-occupant
   root/checkpoint curves. Multi-client PIE and shared observer/team presentation policy remain.

## Online-product gaps

1. The backend-neutral Online Services contract and Loopback reference provider are complete;
   production vendor adapters, authenticated backend credentials, and service operations remain.
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
   remains about 1.3 ms coverless; exact selection-wide dense Cover now measures 9.905 ms median /
   10.130 ms p95 and is cadence-mitigated as detailed in risk 6. Larger selections, multi-world PIE,
   drag-time dense-cover refreshes, and configured game renderers remain open scale gates.
2. The isolated real fixed-tick dense-collision curve is measured at 64/128/256 packed movers
   (1.257/3.114/7.214 ms median in the All profile on the current machine). A moving-combat
   fixed-tick curve now exists too (`SeinARTS.Perf.Combat.Scale`): two armies crossing an open
   field with real Move To actions, pathing, avoidance, collision, and containment measure
   6.129/10.512/19.813 ms medians at 300/500/1,000 units — near-linear, within the 30 Hz budget
   at 1,000. The acquisition workload (`SeinARTS.Perf.Combat.AcquisitionScale`, the 2026-08-23 verb-only
   successor of `ArmedScale`) adds real target queries over a designer-style vitals struct plus a
   Check Target + Apply Field Delta engagement batch: indexed warm acquisition measures
   3.198/5.242/10.705 ms, forced-rebuild acquisition 3.217/5.284/10.581 ms, the 1,000-unit
   engagement batch 1.446 ms, and active ticks 0.457/0.734/1.350 ms at 300/500/1,000 units. Every
   unit acquires and damages a target. Still open:
   the same populations in PIE with animation, fog, UI, and presentation on top, plus an Insights
   capture of a representative large battle and authored Blueprint scorer costs.
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
   ~10.7 ms). Suspects: the exact whole-selection allocator running per preview refresh and the
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
7. Always-on movement scan costs flagged by the 2026-09-03 avoidance/re-seek audit (none yet
   measured hot; recorded as the scaling terms to profile first at larger armies).
   `FSeinCommandBrokerReseek::CollectLooseReturnCandidates` walks every live movement component
   every PostTick, uncadenced (unlike `ProcessBroker`'s watch interval), and its first per-entity
   check is the `FSeinCommandBrokerData` component-map lookup — before the cheap
   `bHomeSeeded`/`bHasTarget`/velocity field early-outs, so every actively moving unit pays a
   hashmap probe per tick for a ~seconds-latency feature. Reordering the pure-read early-outs is
   behavior-identical; adding a scan cadence changes sim timing and takes the deterministic
   ceremony. The avoidance kernel's idle-dodge branch (`ComputeIdleDodge`) runs one spatial-hash
   query per idle avoidance-enabled unit per tick even in an all-idle world; a global "no movers"
   skip is NOT bit-exact as written (`ClearAvoidanceOutput` hard-zeros residual smoothed steer, so
   any skip must preserve that write). The parallel kernel body heap-allocates a neighbour
   `TArray` and a usually-empty blob-broker `TSet` per mover per tick (inline allocators are the
   standard fix), and `IsGenuineCrossing` is evaluated eagerly per neighbour before the gates
   that reject most of them.

## Movement deflation and suspected-defect remainder (2026-09-03 avoidance/re-seek audit)

The concrete target list for the active movement-depth deflation initiative (root guide's
"TickAction re-seek tangle / avoidance kernel" note), from a full read of
`SeinAvoidanceDefaultKernel.cpp`, `SeinMoveToAction.{h,cpp}`, `SeinCommandBrokerReseek.h`, and the
harness consumption path. The seams themselves (`USeinAvoidance`, the PreTick delegator, the
pure-read `ApplyAvoidanceSteer`/`GetAvoidanceSpeedScale` discipline) and the re-seek kernel are
clean — do not redesign them under this list.

1. **Suspected defect — the avoidance arrival fade and past-goal gates key on planar crow-flies
   distance to the order's final `TargetLocation`, never path distance.** A unit ordered to a
   destination a few footprints away through a wall (the path loops around) walks the entire
   detour with avoidance fully released, and the past-goal neighbour gate in
   `AccumulateIndividualResponse` (also the blob branch) discards nearly every neighbour for the
   same reason. Reproduce: destination just across a wall from a standing crowd. Candidate fixes
   are RJ's fork (gate the fade on final-leg `CurrentWaypointIndex` AND planar distance, or key
   on remaining-path length); either is a sim-behavior change — full ceremony plus PIE A/B.
2. **Done 2026-09-04** (`claude/avoidance-deflation-pass`). `USeinMoveToAction`'s stuck recovery
   is now the explicit `ESeinMoveStuckPhase` machine — Free / Holding / HoldingRepathed / Escaping,
   transition table on the enum — plus its clocks and two counters; the 0.3s boundary clock is an
   integer boundary counter (exact re-encoding), and the escape leg no longer impersonates the
   order's `Path` (`Path` is empty while Escaping; the harness and debug viz drive
   `GetDrivenPath`). Behavior-identical by construction: `HoldTime` and the spent stage 1 survive
   a FAILED escape (the immediate stage-2 re-escalation the three-attempt cadence depends on), and
   Escaping is reachable only through HoldingRepathed. Continuation schema 4→5, codec revision
   5→6, behavior revision unchanged; no migration by design (older checkpoints/replays with a Move
   To in flight are rejected at restore, mismatched peers at join). The audit's codec-size premise
   was wrong: the codec is ~700 lines of Blueprint-frame residue certification and ~390 of test
   access; the ladder's share was ~150 lines, so the codec did not shrink. **Open fork for RJ's
   PIE A/B:** while Escaping, the top-of-tick `bOnFinalLeg` is published from the two-point leg
   (true after its first advance) while `TargetLocation` still names the order destination, so the
   avoidance arrival fade / past-goal gates can engage mid-escape for Tier-1 units with footprints
   over ~50cm pinned between the stall band and three footprints of the destination. This is not
   new — the leg lived in `Path` before, so the flag was computed off it there too. The guard
   (`bOnFinalLeg` false while Escaping) lands as the next commit on the branch, revertable alone.
3. `ReportPinnedMover` (the kernel's grind dump) re-implements the whole neighbour gate chain by
   hand (~180 non-shipping lines) and will silently drift from the real gates on the next model
   change — the same disease as the retired A* diagnostics bloat. Factor the gates into one
   classify-one-neighbour helper returning a reason enum consumed by both the kernel and the
   dump. Intended behavior-identical but touches the hot parallel path: build green plus
   serial-vs-parallel state hash.
4. Kernel legibility deflation, behavior-identical: `FinalizeMovingOutput`'s inner and outer
   cohesion terms are twin ~50-line deadband/span blocks differing only in mean source (one
   helper halves them), and the inner/outer combination rule (boost multiplies, hold-back
   min-clamps) deserves a stated invariant; `FAvoidanceWorkerParameters` re-carries four knobs
   already inside its nested NeighborParameters/OutputParameters members (drift risk).
5. The off-path drift predicate's exact `TBigInt<512>` + `__int128` fallback
   (`IsPointWithinSegmentDistanceXYExact`) protects a repath heuristic from saturation-boundary
   imprecision whose worst outcome is one spurious repath. A saturating conservative "off-path"
   answer is equally deterministic across peers and deletes ~120 lines of the file's
   hardest-to-review code. Behavior change at extreme-coordinate boundaries only.
6. PIE A/B feel checklist (no code until observed): movers at or below the Moving Speed Floor get
   their avoidance output cleared, so units accelerating from rest into traffic steer late
   (masked by the collision floor — watch chokepoint restarts); idle-dodge perception is two
   footprints and speed-blind while the mover side is speed-scaled, so fast approaches out-run
   the shuffle; confirm Movement+ modes clamp the 2x cohesion catch-up for vehicles; cohesive
   (blob) squads hide strung-out members from individual avoidance (collision floor only);
   pairwise do-si-do resolves mass head-on collisions as local slide-pasts, not lanes — inherent
   to the steering approach and consistent with the ORCA-out-of-scope ruling.

## Explicit product decisions still required

- Any cover allocator approximation or async preview tradeoff after PIE performance evidence.
- Public Blueprint formation preview/issue shape: carry the full gesture plus frozen artifact in one
  command value/API, or expose a lower-level artifact handoff with explicit stale-input rejection.
- Squad wipe/recreation, retreat, and reinforcement queue-replacement policy.
- Public modifier, terrain, and production API shapes.
- Flight and advanced vehicle-feel defaults after Vehicle Gym evidence.
- Listen-host migration versus dedicated-only supported topology for each game mode.
- Co-op campaign persistence/migration/ownership policy.
- Adaptive input-delay policy after observability data.
- Arrival-fade re-keying for the wall-detour case (final-leg gate versus remaining-path length),
  and any avoidance-model deflation that changes observable behavior — after PIE evidence.

These decisions should be presented with live-code options and a recommendation. Do not silently choose them during cleanup or unrelated fixes.
