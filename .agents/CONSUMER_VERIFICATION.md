# SeinARTS Downstream Consumer Verification

This is the repeatable boundary between “the monorepo builds” and “the plugins can be consumed by
another Unreal project.” Generated consumers are disposable evidence, not source artifacts.

## Tool

Run from the repository root:

```powershell
# Epic launcher engine: all source/package/runtime gates it supports.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -SkipClientServer

# Source/installed engine with Client and Server target support: release gate.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -EngineRoot "D:/Engines/UE_5.8"

# Qualify the exact ZIPs emitted by PackagePlugins.ps1, not repository source.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -EngineRoot "D:/Engines/UE_5.8" -ArtifactDirectory "D:/Projects/Unreal Engine/SeinARTS/.dist" -AuditPublicHeaders

# Complete build/test/package/artifact-consumer gate; publication is the final step.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/Release/Invoke-ReleaseGate.ps1" -Version 1.2.0 -EngineRoot "D:/Engines/UE_5.8"
```

The tool creates projects beneath ignored `Saved/ConsumerMatrix` for five profiles:

- `Framework`: the base Framework plugin only.
- `Cover`: Framework plus Cover, with Squad and the bridge physically absent.
- `Squad`: Framework plus Squad, with Cover and the bridge physically absent.
- `MovementPlus`: Framework plus Movement+.
- `Full`: Framework, Squad, Cover, the Cover+Squad bridge, and Movement+.

Repository-source mode copies distributable source/content/config/resources/shaders only, never
repository `Binaries` or `Intermediate` output. Artifact mode validates each required ZIP's root,
descriptor, version, `Installed:true` stamp, bounded expansion, stripped scratch/debug symbols,
required Framework setup/diagnostic files, and SHA-256 digest, snapshots each archive under a held
read lock, then installs only that immutable copy. Each consumer owns its map and generated
simulation-content manifest.

## Enforced checks

For every selected profile the tool:

1. rejects references to host `/Game/SeinARTS` packages;
2. optionally compiles every production `Public` header in its own non-unity, no-PCH consumer
   translation unit, then builds a fresh UE 5.8 Development Editor target;
3. generates and reloads the consumer's simulation-content manifest;
4. runs the installation diagnostic against that generated project and binds its project, engine,
   integration mode, version, plugin set, manifest, and exact JSON receipt hash;
5. loads `/Game/Maps/ConsumerMap` and verifies that exact editor world path;
6. builds Shipping;
7. cooks, stages, packages, and archives the consumer map; and
8. starts the real packaged `SeinConsumer-Win64-Shipping.exe`, requires it to remain alive through
   a bounded startup window, then terminates that exact process; and
9. for the Framework profile, drives a packaged listen server and client through lobby travel,
   lockstep command flow, a mandatory two-peer canonical-root comparison before reporter topology
   changes, forced checkpoint-plus-tail resync, physical disconnect/reconnect, reconnect
   resync/activation, directional pair-capability grant/revoke plus reconnect persistence and replay
   witness, streaming replay finalization, checkpoint seek, and exact terminal canonical-root
   agreement.

An initially empty consumer necessarily emits the two manifest-bootstrap simulation-content errors
before the manifest exists. The harness accepts only those exact bootstrap messages, and only when
manifest generation then succeeds. Later passes must use the generated manifest normally.

`-ReuseGenerated` preserves expensive generated maps and build products while synchronizing the
current checkout's distributable plugin files and qualification templates. It therefore cannot
silently test stale plugin source. Artifact mode and `-AuditPublicHeaders` forbid reuse. The public-
header audit writes an exact header manifest and count into the consumer evidence. `-SkipCook` is useful for iterating on source/map verification,
but it is not package evidence. `-SkipRuntimeQualification` and `-SkipClientServer` are diagnostic
escapes, not complete release evidence.

`Tools/Release/Invoke-ReleaseGate.ps1` is the one release entrypoint. It runs Development Editor
and Shipping builds, both test profiles across Unit/Integration/Determinism/Editor/Sim/Perf, all
five standalone packages, and the exact-artifact consumer matrix. It writes a JSON receipt under
`Saved/ReleaseGate`. Publication mode permits no skipped gate; `-PackageOnly` is the diagnostic mode.
The receipt binds exact test attempt IDs/index hashes/build provenance, consumer qualification run
IDs, engine fingerprint, installation receipt, public-header manifest, runtime-result hash,
artifact hashes, and evidence archive. Publication uses a draft GitHub release, verifies downloaded remote assets byte-for-byte,
then publishes; an interrupted run resumes only an exact matching draft. The manual self-hosted
Windows workflow at `.github/workflows/release-gate.yml` invokes this same entrypoint on a runner
with UE 5.8 and Client/Server target support.

## Current evidence and limits

On 2026-08-12 all five `0.0.120` exact-ZIP profiles passed fresh Editor and Shipping builds,
release-mode installation diagnostics, exact uncooked map loading, cook/package, and real packaged
Shipping startup. Cover-only and Squad-only contained no bridge plugin or module; Full mounted and
started the bridge and shut it down cleanly. Framework also passed the complete packaged
multiplayer/replay leg: two real Shipping processes completed a match start, a 2-of-2 equal
world-root checkpoint at turn 5, two resyncs around a real reconnect, directional pair-capability
grant/revoke with reconnect and replay persistence, and a standalone checkpoint-seek replay whose
end tick (239) and canonical root (`1CC3EC718160314CD4433EB0DCCB1C10`) exactly matched the
authoritative server. Matrix run `5136cb01289342429a61f491bc78e5d0` recorded runtime-result
SHA-256 `4544E616CA406F8787B492ACFEF69AD0FBF4E4803DF8379441ACEECB063CAD89`.
The exact Framework archive SHA-256 was
`147E27F94662F29F160AC9BCFC7ED964DBFBFDC0CE8721E89A8E14A8F478AB49`; the release manifest binds
the remaining four archive hashes and dependency closure. This dirty diagnostic cohort is not a
publishable release identity. Generated replay files and exact packaged processes are cleaned by
the harness.

The local Epic launcher engine cannot build Client targets; UnrealBuildTool reports that Client
targets are unsupported by that engine distribution before it reaches project compilation. A
source/installed UE build and CI runner must therefore prove Development Client and Dedicated
Server binaries and repeat the runtime topology with a true headless server. The local listen-
server qualification is real packaged multiplayer evidence, but it is not a substitute for that
dedicated-target gate or the remaining human PIE oracles.

The generated projects, packages, logs, and temporary Python scripts are regenerable and should be
deleted after evidence is recorded. Do not commit `Saved/ConsumerMatrix`.
