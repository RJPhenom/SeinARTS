# Upgrading SeinARTS

**Document version:** 0.7

## Release upgrade

1. Create a project branch and retain the previous SeinARTS release artifacts.
2. Verify every ZIP against `release-manifest.json`.
3. Replace the complete installed production-plugin cohort; do not mix versions.
4. Restart the Unreal Editor fully. Reflected property, category, or class changes are not a Live
   Coding migration.
5. Rebuild the Editor and Shipping targets, then regenerate the simulation-content manifest.
6. Re-bake ignored level, navigation, and fog data required by the project.
7. Run the relevant automated test profile and downstream consumer gate before merging.
8. Reopen representative maps and Blueprint assets, compile them, and perform the maintained PIE
   matrix for gameplay feel and multiplayer behavior.

Treat schema, digest, fingerprint, and behavior-revision rejection as an expected compatibility
boundary. Never weaken those checks to make an old snapshot, replay, or peer join a new build.

## Current development wave

The current development wave uses world snapshots and canonical envelopes at schema v15, advances
compiled deterministic behavior to `SeinARTS.Replay.6`, and advances the live network wire contract
to protocol v12. Older live peers, Replay.5 artifacts, and v14 snapshot payloads are intentionally
incompatible.

It also adds:

- directional, source-attributed player-pair capability state and its deterministic command path;
- a compatible UI disposition projection over the authoritative capability query;
- render-only Movement+ vehicle telemetry for AnimBlueprints, including settled wheel/track motion,
  driver-output throttle/brake, wrapped wheel phase, and movement/ability graph validation;
- bounded zlib reconnect-checkpoint transfer, paced acknowledged chunks, explicit retained-tail
  completion, and root-gated authorship activation;
- exact Cover assignment within one resolver invocation;
- deterministic Squad reinforcement request/accounting/snapshot lifecycle; and
- background durable append for automatic replay checkpoints; and
- container-local transport deployment offsets that rotate with the authoritative container
  transform.

After adopting this wave, regenerate the simulation-content manifest and run a full editor restart.
Keep the older executable/plugin cohort for any v14 snapshots or Replay.5 artifacts that must be
inspected; there is no automatic migration to v15/Replay.6. Protocol v11 peers cannot join a v12
session and must be upgraded as one cohort. Use `MOVEMENT_PLUS_ANIMATION.md` to wire and qualify the
new typed vehicle telemetry without reading raw render-state slots.
