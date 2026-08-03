# SeinARTS Downstream Consumer Verification

This is the repeatable boundary between “the monorepo builds” and “the plugins can be consumed by
another Unreal project.” Generated consumers are disposable evidence, not source artifacts.

## Tool

Run from the repository root:

```powershell
# Epic launcher engine: all source/package/runtime gates it supports.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -SkipClientServer

# Source/installed engine with Client and Server target support: release gate.
& "D:/Projects/Unreal Engine/SeinARTS/Tools/ConsumerMatrix/Verify-ConsumerMatrix.ps1" -EngineRoot "D:/Engines/UE_5.7"
```

The tool creates projects beneath ignored `Saved/ConsumerMatrix` for five profiles:

- `Framework`: the base Framework plugin only.
- `Cover`: Framework plus Cover, with Squad and the bridge physically absent.
- `Squad`: Framework plus Squad, with Cover and the bridge physically absent.
- `MovementPlus`: Framework plus Movement+.
- `Full`: Framework, Squad, Cover, the Cover+Squad bridge, and Movement+.

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
   a bounded startup window, then terminates that exact process; and
8. for the Framework profile, drives a packaged listen server and client through lobby travel,
   lockstep command flow, forced checkpoint-plus-tail resync, physical disconnect/reconnect,
   reconnect resync/activation, streaming replay finalization, checkpoint seek, and exact terminal
   canonical-root agreement.

An initially empty consumer necessarily emits the two manifest-bootstrap simulation-content errors
before the manifest exists. The harness accepts only those exact bootstrap messages, and only when
manifest generation then succeeds. Later passes must use the generated manifest normally.

`-ReuseGenerated` preserves expensive generated maps and build products while synchronizing the
current checkout's distributable plugin files and qualification templates. It therefore cannot
silently test stale plugin source. `-SkipCook` is useful for iterating on source/map verification,
but it is not package evidence. `-SkipRuntimeQualification` and `-SkipClientServer` are diagnostic
escapes, not complete release evidence.

## Current evidence and limits

On 2026-08-03 all five post-split profiles passed Editor and Shipping builds, exact uncooked map
loading, cook/package, and real packaged Shipping startup. Cover-only and Squad-only contained no
bridge plugin or module; Full mounted and started the bridge and shut it down cleanly. Framework
also passed the complete packaged multiplayer/replay leg: two real Shipping processes completed a
match start, two resyncs around a real reconnect, and a standalone checkpoint-seek replay whose end
tick (272 in the qualifying run) and canonical root
(`EC430EB37C82744C60C69D6C8805748B`) exactly matched the authoritative server. Generated replay
files and exact packaged processes are cleaned by the harness.

The local Epic launcher engine cannot build Client targets; UnrealBuildTool reports that Client
targets are unsupported by that engine distribution before it reaches project compilation. A
source/installed UE build and CI runner must therefore prove Development Client and Dedicated
Server binaries and repeat the runtime topology with a true headless server. The local listen-
server qualification is real packaged multiplayer evidence, but it is not a substitute for that
dedicated-target gate or the remaining human PIE oracles.

The generated projects, packages, logs, and temporary Python scripts are regenerable and should be
deleted after evidence is recorded. Do not commit `Saved/ConsumerMatrix`.
