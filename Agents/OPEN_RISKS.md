# SeinARTS Open Risk Register

This is the actionable remainder after consolidating the historical audits. It intentionally omits fixed historical narrative.

## Release-blocking foundation risks

1. **Checkpoint-safe Blueprint continuation is bounded, not arbitrary.** Wait/MoveTo and registered native codecs are covered; arbitrary Blueprint VM latent/async frames are not. Define the supported authoring contract and fail unsupported graphs early.
2. **Custom stateful providers need complete admission contracts.** Collision and Cover have explicit coverage claims; remaining Level Data, formation/resolver statefulness, and conditional ownership/orphan evaluation require a closed design.
3. **Downstream packaging is unproven.** The monorepo builds, but clean consumer matrices and plugin-stripping packages are not automated.

## Gameplay-backbone gaps

1. Cover has deterministic provider/query infrastructure but lacks stable reservations and a shared selection-wide tactical allocator.
2. Squad reinforcement/loss lifecycle is not complete enough for production tactics gameplay.
3. Per-unit terrain restrictions and nav layers do not yet propagate through every MoveTo/repath/containment path.
4. FoW still needs an explicit team/shared vision policy; the known blocker-height, authored-Z, and cone terrain-scaling defects are closed.
5. Public targeting lacks the complete line/corridor/gesture policy surface needed by a modern tactical RTS.
6. Movement+ needs behavior qualification and telemetry; Flight is not a production 3D avoidance/collision model.
7. Cover's optional Squad declaration is not physically strip-safe because the declared bridge module hard-links Squad.

## Online-product gaps

1. No backend-neutral auth/party/matchmaking/ranked/stats/leaderboard service layer exists.
2. Canonical divergence detection is not anti-cheat; lockstep clients can possess hidden world state.
3. Co-op campaign save ownership, schema migration, cloud conflict, account identity, and cross-map bootstrap are unbuilt.
4. True listen-host migration is unbuilt. Dedicated-server co-op can defer peer host migration but cannot defer crash recovery.
5. Multi-process/cooked client-server reconnect and replay workflows remain runtime validation gates.

## Performance and scale risks

1. Large continuous formation previews remain the clearest measured framework authoring hotspot.
2. Dense active collision beyond 148 movers lacks a measured scale curve.
3. Game-specific animation complexity can exceed the default mannequin baseline.
4. Replay checkpoints and durable flushes are synchronous; long-session hitch and storage behavior need soak evidence.
5. Debug navigation rendering is intentionally expensive and can invalidate profiling if left enabled.

## Explicit product decisions still required

- Full cover scoring/contention/reservation policy and moving-provider behavior.
- Public targeter/modifier/terrain/production/team-vision API shapes.
- Flight and advanced vehicle-feel defaults after Vehicle Gym evidence.
- Listen-host migration versus dedicated-only supported topology for each game mode.
- Co-op campaign persistence/migration/ownership policy.
- Adaptive input-delay policy after observability data.

These decisions should be presented with live-code options and a recommendation. Do not silently choose them during cleanup or unrelated fixes.
