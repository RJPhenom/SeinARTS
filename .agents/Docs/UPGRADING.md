# Upgrading SeinARTS

**Document version:** 0.4

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

The current development wave advances world snapshots and canonical envelopes to schema v15 and
compiled deterministic behavior to `SeinARTS.Replay.5`. Older live peers, Replay.4 artifacts, and
v14 snapshot payloads are intentionally incompatible.

It also adds:

- directional, source-attributed player-pair capability state and its deterministic command path;
- a compatible UI disposition projection over the authoritative capability query;
- render-only Movement+ vehicle telemetry for AnimBlueprints, including settled wheel/track motion,
  driver-output throttle/brake, wrapped wheel phase, and movement/ability graph validation;
- exact Cover assignment within one resolver invocation;
- deterministic Squad reinforcement request/accounting/snapshot lifecycle; and
- background durable append for automatic replay checkpoints.

After adopting this wave, regenerate the simulation-content manifest and run a full editor restart.
Keep the older executable/plugin cohort for any v14 snapshots or Replay.4 artifacts that must be
inspected; there is no automatic migration to v15/Replay.5.
