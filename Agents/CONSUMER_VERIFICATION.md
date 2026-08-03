# SeinARTS Downstream Consumer Verification

This is the repeatable boundary between “the monorepo builds” and “the plugins can be consumed by
another Unreal project.” Generated consumers are disposable evidence, not source artifacts.

## Tool

Run from the repository root:

```powershell
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1"
```

The tool creates projects beneath ignored `Saved/ConsumerMatrix` for three profiles:

- `Framework`: the base Framework plugin only.
- `MovementPlus`: Framework plus Movement+.
- `Full`: Framework, Squad, Cover, and Movement+.

It copies distributable source/content/config/resources/shaders only—never repository `Binaries`
or `Intermediate` output. Each consumer owns its map and generated simulation-content manifest.

## Enforced checks

For every selected profile the tool:

1. rejects references to host `/Game/SeinARTS` packages;
2. builds a fresh UE 5.7 Development Editor target;
3. generates and reloads the consumer's simulation-content manifest;
4. loads `/Game/Maps/ConsumerMap` and verifies that exact editor world path;
5. builds Shipping;
6. cooks, stages, packages, and archives the consumer map; and
7. starts the real packaged `SeinConsumer-Win64-Shipping.exe`, requires it to remain alive through
   a bounded startup window, then terminates that exact process.

An initially empty consumer necessarily emits the two manifest-bootstrap simulation-content errors
before the manifest exists. The harness accepts only those exact bootstrap messages, and only when
manifest generation then succeeds. Later passes must use the generated manifest normally.

`-ReuseGenerated` reuses the disposable project after a successful generation pass. `-SkipCook`
is useful for iterating on source/map verification, but it is not package evidence.

## Current evidence and limits

On 2026-08-03 all three profiles passed fresh Editor and Shipping builds, exact map loading,
cook/package, and real packaged Shipping startup. The host project also passed Development Editor
and Shipping builds, all checked-in Unit/Integration/Determinism/Editor floors, and the 120-tick
serial-versus-parallel process A/B.

The local Epic launcher engine cannot build Client targets; UnrealBuildTool reports that Client
targets are unsupported by that engine distribution before it reaches project compilation. A
source/installed UE build and CI runner must therefore prove Development Client and Dedicated
Server. The current harness also does not yet start a real multiplayer match or drive snapshot,
replay, forced resync, and reconnect in the clean consumer. Those are explicit remaining gates—not
inferred from package startup.

The generated projects, packages, logs, and temporary Python scripts are regenerable and should be
deleted after evidence is recorded. Do not commit `Saved/ConsumerMatrix`.
