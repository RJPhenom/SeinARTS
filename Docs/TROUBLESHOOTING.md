# Troubleshooting SeinARTS

**Document version:** 0.1

## Installation diagnostic

Run `Tools/Diagnostics/Test-SeinARTSInstallation.ps1` with `-Json` for a machine-readable receipt.
Errors return exit code 1; warnings keep exit code 0 but identify evidence that was not provable.

| Codes | Meaning | Action |
|---|---|---|
| `SEIN001`-`SEIN003` | Project path or descriptor is invalid | Pass exactly one valid `.uproject`. |
| `SEIN010`-`SEIN013` | UE installation is missing, unreadable, or not 5.8 | Install or select the qualified UE 5.8 engine. |
| `SEIN020`-`SEIN022` | Test plugins are enabled or no production root is enabled | Disable test suites and enable the intended production plugins. |
| `SEIN030`-`SEIN036` | Plugin install or dependency closure is missing, duplicate, invalid, or unexpected | Install one complete qualified cohort and remove duplicate project/engine copies. |
| `SEIN040`-`SEIN043` | Versions, release/source mode, or release SemVer disagree | Replace the full cohort from one release or source revision. |
| `SEIN044`-`SEIN047` | Source Git identity spans repositories, is dirty, or cannot be proved | Use one clean tracked commit for distributable source integration. |
| `SEIN050`-`SEIN054` | Simulation-content manifest is absent, external, noncanonical, or missing | Assign a `/Game/` asset, regenerate it, and save the asset. |

## Common failures

**Manifest bootstrap:** Run `Sein.SimulationContent.GenerateManifest`, save the generated asset,
and restart qualification. Do not bypass runtime/cook compatibility checks. Re-bake level data after
a fresh clone or relevant level/navigation/fog authoring change.

**Editor build cannot link a module DLL:** Close Unreal Editor and rebuild, or use Live Coding for
an intentional in-editor patch. Reflected property/class/category changes require a full restart.

**Mixed-build join, snapshot, replay, or reconnect rejection:** Treat schema, digest, fingerprint,
behavior revision, and state-coverage rejection as a real compatibility boundary. Replace every
peer and authority with the same qualified plugin cohort; do not weaken the admission check.

**Commands feel delayed but turns remain healthy:** Baseline command admission latency is roughly
`InputDelayTurns * TicksPerTurn / SimulationHz`, plus turn-boundary and network alignment. At 30 Hz,
three ticks per turn, and two delayed turns, roughly 200-300 ms is expected. Changing those values is
a lockstep configuration/tuning decision and all peers must use the same fingerprinted settings.

**Downstream project resolves host content:** Remove `/Game/SeinARTS` references. The consuming
project owns its manifest, maps, settings, gameplay assets, and baked level data.
