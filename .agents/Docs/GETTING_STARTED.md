# Getting Started with SeinARTS

**Document version:** 0.4

## Install

1. Use Unreal Engine 5.8 on Win64.
2. Choose one integration mode:
   - extract release ZIPs from one release into the consuming project's `Plugins` directory; or
   - add all required plugin source from one pinned repository commit.
3. Enable `SeinARTSFramework` and only the optional production extensions the game uses. Cover+Squad
   requires Framework, Cover, and Squad. Never enable the test-suite plugins in a shipping project.
4. Regenerate project files and build the Editor target. Restart the Editor fully after reflected
   property or class changes.

Do not mix release versions, release and source descriptors, or project and engine copies of the
same plugin. The diagnostic below rejects those ambiguous installations.

For source integration, keep the plugin files tracked in one clean Git checkout. The diagnostic
reports the common commit when it can prove that identity; dirty or copied/untracked source is
reported as unverifiable rather than being certified as commit-pinned.

## Configure deterministic content

1. In Project Settings, assign a project-owned `Simulation Content Manifest` under `/Game/`.
2. Run `Sein.SimulationContent.GenerateManifest` in the Unreal Output Log and save the generated
   asset whenever simulation-relevant Blueprint/native content or enabled plugins change.
3. Add `ASeinLevelVolume` to each gameplay map and use its `Bake Level Data` action after a fresh
   clone or relevant level/navigation/fog authoring change.
4. Configure the project's lobby/game maps, match manifest, player starts, game mode, resources,
   navigation, movement, fog, input, and UI classes for the intended game. Do not copy host-project
   `/Game/SeinARTS` references into a downstream project.

The first manifest generation may occur before a valid runtime manifest exists. Generate and save
the asset before entering PIE; do not weaken runtime or cook compatibility checks to bypass it.

## Diagnose

From the repository source integration:

```powershell
& ".\Scripts\Diagnostics\Test-SeinARTSInstallation.ps1" `
  -Project "C:\Projects\Game\Game.uproject"
```

From a release ZIP integration, the same tool is shipped under
`Plugins/SeinARTSFramework/Tools/Diagnostics`. Add `-Json` for a machine-readable receipt. The tool
is read-only and returns a nonzero exit code for actionable installation errors.
See `TROUBLESHOOTING.md` for stable finding codes and common runtime/build failures.
Use `FIRST_SKIRMISH.md` for the source-grounded project settings, unit, map, and two-player authoring
sequence after installation is healthy.

## Qualify

1. Build Editor and Shipping targets.
2. Regenerate the simulation-content manifest and re-bake required level data.
3. Compile representative maps and Blueprint assets.
4. Run project automation plus the maintained multiplayer PIE matrix.
5. For framework release work, run `Scripts/Release/Invoke-ReleaseGate.ps1`; its exact-ZIP consumers,
   public-header compile gate, bound installation receipts, packaged runtime, reconnect, and replay
   evidence are the release qualification contract.

Epic's launcher UE 5.8 build cannot prove Development Client or Dedicated Server targets. A
source/installed engine distribution and suitable CI runner must provide that final target evidence.
