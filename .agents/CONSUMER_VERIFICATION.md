# SeinARTS Downstream Consumer Verification

This is the repeatable boundary between “the monorepo builds” and “the plugins can be consumed by
another Unreal project.” Generated consumers are disposable evidence, not source artifacts.

## Tool

Run from the repository root:

```powershell
# Epic launcher engine: all source/package/runtime gates it supports.
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -SkipClientServer

# Source/installed engine with Client and Server target support: release gate.
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -EngineRoot "D:/Engines/UE_5.8"

# Qualify the exact ZIPs emitted by PackagePlugins.ps1, not repository source.
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -EngineRoot "D:/Engines/UE_5.8" -ArtifactDirectory "D:/Projects/Unreal Engine/SeinARTS/.dist" -AuditPublicHeaders

# Complete build/test/package/artifact-consumer gate; publication is the final step.
& "D:/Projects/Unreal Engine/SeinARTS/Scripts/Release/Invoke-ReleaseGate.ps1" -Version 1.2.0 -EngineRoot "D:/Engines/UE_5.8"
```

The tool creates projects beneath ignored `Saved/ConsumerMatrix` for six profiles:

- `Framework`: the base Framework plugin only.
- `Cover`: Framework plus Cover, with Squad and the bridge physically absent.
- `Squad`: Framework plus Squad, with Cover and the bridge physically absent.
- `MovementPlus`: Framework plus Movement+.
- `OnlineServices`: Framework plus the backend-neutral Online Services extension.
- `Full`: Framework, Squad, Cover, the Cover+Squad bridge, Movement+, and Online Services.

Repository-source mode copies distributable source/content/config/resources/shaders only, never
repository `Binaries` or `Intermediate` output. Artifact mode validates each required ZIP's root,
descriptor, version, `Installed:true` stamp, bounded expansion, stripped scratch/debug symbols,
the required Framework diagnostic, and SHA-256 digest, snapshots each archive under a held
read lock, then installs only that immutable copy. Each consumer owns its map and generated
simulation-content manifest.

## Enforced checks

For every selected profile the tool:

1. rejects host example-content dependencies in source/config and serialized assets;
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
9. for the Framework and Movement+ profiles, drives a packaged listen server and client through lobby travel,
   lockstep command flow, a mandatory two-peer canonical-root comparison before reporter topology
   changes, forced checkpoint-plus-tail resync, physical disconnect/reconnect, reconnect
   resync/activation, directional pair-capability grant/revoke plus reconnect persistence and replay
   witness, streaming replay finalization, checkpoint seek, and exact terminal canonical-root
   agreement. Movement+ additionally issues a real wheeled Move command and requires exact movement
   state on both peers, after reconnect, and after replay checkpoint seek.

An initially empty consumer MAY emit the tolerated manifest-bootstrap simulation-content messages
before the manifest exists (since the manifest became optional, an unconfigured consumer instead
synthesizes a records-free code-contract profile and plays without them; the harness filters are
subtractive, so their absence is also accepted). Manifest generation must still succeed, and later
passes must use the generated manifest normally.

`-ReuseGenerated` preserves expensive generated maps and build products while synchronizing the
current checkout's distributable plugin files and qualification templates. It therefore cannot
silently test stale plugin source. Artifact mode and `-AuditPublicHeaders` forbid reuse. The public-
header audit writes an exact header manifest and count into the consumer evidence. `-SkipCook` is useful for iterating on source/map verification,
but it is not package evidence. `-SkipRuntimeQualification` and `-SkipClientServer` are diagnostic
escapes, not complete release evidence.

`Scripts/Release/Invoke-ReleaseGate.ps1` is the one release entrypoint. It runs Development Editor
and Shipping builds, both test profiles across Unit/Integration/Determinism/Editor/Sim/Perf,
fresh-process collision serial/parallel A/B, all six standalone packages, and the exact-artifact
consumer matrix. The version-4 release receipt retains 14 test attempts (12 suite runs plus two
fresh-process traces); the evidence archive includes the bound A/B receipt, reports, provenance,
and logs under `determinism-ab/`. It writes a JSON receipt under
`Saved/ReleaseGate`. Console output is limited to stage progress and failure excerpts; each receipt
step names its retained full log. Test receipts are handed off by exact path, not rediscovered from
old runs. Publication mode permits no skipped gate; `-PackageOnly` is the diagnostic mode.
The receipt binds exact test attempt IDs/index hashes/build provenance, consumer qualification run
IDs, engine fingerprint, installation receipt, public-header manifest, runtime-result hash,
artifact hashes, and evidence archive. Publication uses a draft GitHub release, verifies downloaded remote assets byte-for-byte,
then publishes; an interrupted run resumes only an exact matching draft. The manual self-hosted
Windows workflow at `.github/workflows/release-gate.yml` invokes this same entrypoint on a runner
with UE 5.8 and Client/Server target support.

Host example-path screening scans ANSI and both UTF-16 byte alignments. Binary matches are
classified by the consumer editor after building: hard/soft package dependencies and live object
serialization are checked before ordinary save callbacks, then a disposable package copy is scanned.
Only level-origin URLs and the bridge's two retained editor inheritance-history arrays are excluded
in that audit process. Original packages are never saved, and their hashes must remain unchanged.
The history remains in distributed assets because unopened levels need it for default reconciliation.

## Consumer integration and upgrade contract

UE 5.8 on Win64 is the qualified baseline. A consumer installs either one complete release-ZIP
cohort or plugin source from one clean pinned commit. Optional production extensions may be omitted,
but installed extensions must match Framework; test-suite plugins never belong in a shipping graph.
Do not mix source and release installs or retain duplicate project/engine copies.

The consuming project owns its maps, gameplay classes, settings, Simulation Content Manifest, and
baked level data. Host assets under `/Game/SeinARTSExamples` are references, not distributable
dependencies. A minimal playable integration uses project-owned `ASeinActor` subclasses and
component templates, unique `ASeinPlayerStart` slots, an `ASeinLevelVolume` with baked data, and a
automatic build-owned compatibility evidence produced by cook. Ordinary editor iteration needs no
saved manifest asset; the optional saved reference is only for explicit strict recovery testing.

`Scripts/Diagnostics/Test-SeinARTSInstallation.ps1` is the read-only installation authority. Use
`-Json` when evidence must be retained; its live implementation owns the stable finding codes and
actions. Do not weaken manifest, schema, digest, fingerprint, or behavior-revision failures to admit
an old peer, snapshot, replay, or reconnect payload.

An upgrade replaces the complete plugin cohort, fully restarts the Editor after reflected changes,
rebuilds Editor and Shipping, cooks fresh compatibility evidence, re-bakes required level data, and repeats the
relevant automated, consumer, multiplayer, replay/resync, and PIE gates. Retain the producing build
when incompatible persisted evidence must remain inspectable.

Root `Docs/` is intentionally empty until RJ authors the GitHub Pages documentation website.
Package-only, consumer qualification, and publication remain available; publication warns and omits
the Documentation payload while the tree is empty. Once website content is deliberately present,
packaging includes it with Framework and release evidence binds every documentation file.

## Current evidence and limits

On 2026-08-14 the clean committed `0.0.178` cohort from source commit
`9d7efc3708cc07dfbb76c3f6f8c528b1290ed264` passed all five exact-ZIP profiles. Every profile
passed fresh Development Editor, Shipping, release-mode installation diagnostics, exact uncooked
map loading, cook/package, and real packaged Shipping startup. The independent public-header
audits passed Framework 325, Cover 340, Squad 333, Movement+ 337, and Full 362 headers. Framework,
Cover, and Squad share matrix run `eb501f7de8394dccbf16527b89f86f6d`; Movement+ is run
`9320b6e769d04ae781653eedb9b3bd2b`; Full is run `df0633f28f804c9ea8fd666e8bb69af5`.
Cover-only and Squad-only contained no bridge plugin or module, while Full mounted the complete
five-plugin cohort and shut it down cleanly.

Framework and Movement+ also passed their complete packaged multiplayer/replay legs. Framework
ended at tick 233 and canonical root `2CB5A25B8BA8AF43D930821BBBC20E3F`; its runtime receipt SHA-256
is `5F637CE19076F7DB4D4C7EFD0C7B7B118C887BCBF6512E454B7C53E07D5AD2AC`. Movement+ completed the
real wheeled Move, resync, physical reconnect, pair-capability persistence, telemetry witnesses,
and checkpoint-seek replay at tick 251 and root `DE0526096296F01DF1AEB465F071FF7B`; its runtime receipt
SHA-256 is `A90E69D39D9DEDB084859F98754E9D5695A49F6A52719BDBC491F5A7FD0F2DB0`. The release manifest
records `sourceDirty:false`, the UE 5.8.1 fingerprint, dependency closure, and exact archive hashes;
the Framework archive SHA-256 is
`80D1CC88CFC63E55929B7C2536338A0F4585DB24A3A12BB6ECD2C4D788090A35`. Generated replay files and
exact packaged processes are cleaned by the harness.

On 2026-08-22 the new Online Services profile passed an exact-ZIP package-only qualification from
source commit `4463e0e3979f7fe5cf48085282812f7dae23f15f` (version `0.0.236`, matrix run
`9b15bf74ff1d4cb0ba584ff210ff622f`). Its fresh consumer passed 350 independent public-header
translation units, Development Editor and Shipping builds, release-mode installation diagnostics,
exact uncooked map loading, clean cook/package, and real packaged Shipping startup. The Framework
archive SHA-256 was `9E1FC527AF1D28C08DCDC6ED10C0513BDD959BB8FC9D18A042B4A17EFF016075`;
Online Services was `9826C25239FB6B1BC814A5EA04C0191316E30B8147C9095312C1763E2A864EF0`.
This diagnostic run deliberately preserved unrelated host-project WIP, so its release manifest
records `sourceDirty:true`; it qualifies the exact plugin artifacts but is not publication evidence.

The local Epic launcher engine cannot build Client targets; UnrealBuildTool reports that Client
targets are unsupported by that engine distribution before it reaches project compilation. A
source/installed UE build and CI runner must therefore prove Development Client and Dedicated
Server binaries and repeat the runtime topology with a true headless server. The local listen-
server qualification is real packaged multiplayer evidence, but it is not a substitute for that
dedicated-target gate or the remaining human PIE oracles.

On 2026-09-13 the checkpoint's repository-source Framework consumer passed Editor and Shipping,
installation diagnostics, automatic cook compatibility, exact map loads, packaging, and packaged
two-player travel, root gossip, checkpoint-tail resync, physical reconnect, capability persistence,
and replay checkpoint seek. Matrix run `34fbe5d5f0ec42aead9852930e57d708` ended at tick 2342 with
canonical root `9F9341D3859484A1F39663250732126D`; runtime receipt SHA-256 is
`9380CE4D2F20D0D948BD58AB8939810EA4EEB4E628B2CE4EC2778368EC8A7CFA`.
This run uses source inputs and the launcher-engine listen-server topology; public-header audit,
Client/Server targets, adverse networking, and other consumer profiles were not qualified here.
The intended Consumer delivery remains an iteration prerelease, not a milestone release-gate result.

The run exposed missing seamless-travel controller rebinding: the same valid client identity
was rejected because the destination lobby never recovered its ownership records. Engine-handoff
automation now covers accepted and rejected replacements, and native tests cover exact-seat
authorization, stale logout, conflicting ownership, foreign worlds, and wrong reconnect identities.
The existing terrain, abstract-selection, UDS save, and replay-worker fixtures were also corrected
to establish their required state. Integration runners keep the RHI for Canvas pixel assertions.

Final checkpoint validation passed all 15 steps in
`Saved/Validation/1dec40b070344a2fba2e0f6838090b52/validation-result.json`: 1,789 test executions
across twelve All/Framework suite runs, a 120-frame fresh-process serial/parallel comparison,
and the host Shipping build. Validation orchestration passed 60 self-checks
(`Saved/ValidationSelfTest/97d3aad385944e4daf684823ce012ff3/self-test-result.json`), and installation
diagnostic adversarial fixtures passed on Windows PowerShell 5.1. Independent review found no
blocking defect in the admission fixes. These automated results do not certify the outstanding
human PIE oracles or the omitted milestone consumer matrix.
The staged-new-file check subsequently removed surplus terminal blank lines from twelve source
files only. Final Editor (`Saved/Build/e6b2c199768b411b87e516e9c2c78e14/build-result.json`) and
Shipping (`Saved/Build/04ad11826f2844ebabfe03772df8c569/build-result.json`) builds passed afterward;
no behavior changed after the full suite run.
Standalone BuildPlugin subsequently exposed missing direct `UObject/Package.h` includes in ability
input construction and `Templates/SubclassOf.h` in the movement payload. Adding those dependencies
fixed the isolated UnrealGame Development target (`Saved/Validation/Checkpoint-IsolatedRuntimeBuild.log`,
exit 0); these are compile-boundary fixes, with no gameplay logic change.

The generated projects, packages, logs, and temporary Python scripts are regenerable and should be
deleted after evidence is recorded. Do not commit `Saved/ConsumerMatrix`.
