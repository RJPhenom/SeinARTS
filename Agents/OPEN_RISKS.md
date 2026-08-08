# SeinARTS Open Risk Register

This is the actionable remainder after consolidating the historical audits. It intentionally omits fixed historical narrative.

## Release-blocking foundation risks

1. **Launcher UE cannot prove Client/Server targets.** Epic's installed 5.8 distribution rejects
   Client targets before project compilation. Development Client and Dedicated Server must run in
   CI or another source/installed engine distribution that supports those target types. The real
   packaged listen-server/client/reconnect/replay harness is green, but a Game-target listen server
   does not prove a true headless dedicated-server binary.

## Gameplay-backbone gaps

1. Cover has deterministic provider/query infrastructure but lacks stable reservations and a shared selection-wide tactical allocator.
2. Squad reinforcement/loss lifecycle is not complete enough for production tactics gameplay.
3. FoW still needs an explicit team/shared vision policy; the known blocker-height, authored-Z, and cone terrain-scaling defects are closed.
4. Public targeting lacks the complete line/corridor/gesture policy surface needed by a modern tactical RTS.
5. Movement+ needs behavior qualification and telemetry; Flight is not a production 3D avoidance/collision model.

## Online-product gaps

1. No backend-neutral auth/party/matchmaking/ranked/stats/leaderboard service layer exists.
2. Canonical divergence detection is not anti-cheat; lockstep clients can possess hidden world state.
3. Co-op campaign save ownership, schema migration, cloud conflict, account identity, and cross-map bootstrap are unbuilt.
4. True listen-host migration is unbuilt. Dedicated-server co-op can defer peer host migration but cannot defer crash recovery.
5. WAN/backend-adapter behavior, true dedicated-server reconnect, process-crash recovery, and
   adversarial network conditions remain runtime/product validation gates. Local packaged
   listen-server reconnect and replay checkpoint seek are qualified.

## Performance and scale risks

1. Large continuous formation previews remain the clearest measured framework authoring hotspot.
2. Dense active collision beyond 148 movers lacks a measured scale curve.
3. Game-specific animation complexity can exceed the default mannequin baseline.
4. Replay checkpoints and durable flushes are synchronous; long-session hitch and storage behavior need soak evidence.
5. Debug navigation rendering is intentionally expensive and can invalidate profiling if left enabled.

## Explicit product decisions still required

- Full cover scoring/contention/reservation policy, requester-aware post-processing, and moving-provider behavior.
- Public targeter/modifier/terrain/production/team-vision API shapes.
- Flight and advanced vehicle-feel defaults after Vehicle Gym evidence.
- Listen-host migration versus dedicated-only supported topology for each game mode.
- Co-op campaign persistence/migration/ownership policy.
- Adaptive input-delay policy after observability data.

These decisions should be presented with live-code options and a recommendation. Do not silently choose them during cleanup or unrelated fixes.
