# SeinARTS Compatibility Policy

**Document version:** 0.6

SeinARTS ships its five production plugins as one versioned cohort. Install Framework, Squad,
Cover, Movement+, and Cover+Squad artifacts from the same release. Mixing release versions is
unsupported even when Unreal can compile the combination.

## Supported baseline

- Unreal Engine 5.8 on Win64 is the qualified baseline.
- Release ZIPs are the supported binary distribution. Commit-pinned plugin source is the
  supported development integration.
- Test-suite plugins are development-only and are never part of a shipping dependency graph.
- Optional production extensions may be omitted, but every installed extension must use the same
  release version as Framework.

## Versioning

Release tags and artifact `VersionName` values use SemVer 2.0:

- `MAJOR`: an intentionally incompatible public C++, Blueprint, authoring, or persisted-data
  contract.
- `MINOR`: backward-compatible public capability or tooling additions.
- `PATCH`: fixes and internal changes without an intentional public API break.

SemVer describes the studio-facing SDK. It does not permit mixed deterministic match builds.
Lockstep peers must pass the exact initial-state digest, configuration fingerprint, behavior
revision, command schema, and state-coverage checks implemented by the runtime.

The core configuration fingerprint includes the compiled framework behavior epoch. A deterministic
behavior change must advance that epoch so mixed executables fail the existing live-session config
parity barrier before simulation starts, even when reflected settings happen to match.

## Persisted and network state

Snapshots, reconnect envelopes, and replay checkpoints are admitted by explicit schema and
compatibility checks. A changed schema fails closed unless a documented migration exists. Do not
bypass an incompatibility check or deserialize old deterministic state into a newer build.

The current behavior epoch is `SeinARTS.Replay.6`. It is embedded in snapshots and replay headers
and folded into live peer admission. Replay.5 snapshots, replays, and peers therefore fail closed;
retain the producing executable cohort when those artifacts must be inspected. Replay.6 corrects
transport deployment so container-local offsets rotate with the authoritative container transform.

A release may legitimately change deterministic behavior in a patch. All peers and the authority
must still run the exact qualified release cohort. Retain the producing build when old replay or
diagnostic evidence must remain inspectable.

## Release evidence

A publishable release must come from one clean, pushed commit and pass
`Scripts/Release/Invoke-ReleaseGate.ps1`. The release manifest binds that commit to the UE version,
artifact dependency set, sizes, and SHA-256 hashes. Exact packaged ZIPs, not repository source,
then pass the downstream consumer matrix and independent public-header compilation gate. The
qualified receipt also binds exact test attempts/indexes/build provenance, consumer run IDs,
engine and source fingerprints, exact-profile installation diagnostics, public-header and
packaged-runtime result hashes, and the immutable evidence archive. Publication verifies the remote
draft-release assets byte-for-byte before making the release public; interrupted publication may
resume only that exact matching draft.
