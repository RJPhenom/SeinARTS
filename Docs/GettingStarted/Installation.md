# Installation

## Requirements

- **Unreal Engine 5.8** (the plugins are built and qualified against 5.8; other engine versions
  are rejected by the installation diagnostic).
- Windows 64-bit development environment.

## Installing the plugins

1. Close the Unreal Editor.
2. Copy each SeinARTS plugin folder into your project's `Plugins/` directory (or install the
   packaged release into the engine's `Marketplace` plugin directory):
   - `SeinARTSFramework` — always required.
   - `SeinARTSSquadExtension`, `SeinARTSCoverExtension`, `SeinARTSMovementPlusExtension` —
     optional; install only what your game uses.
   - `SeinARTSCoverSquadExtension` — only if you installed BOTH Cover and Squads.
3. Enable the plugins in your `.uproject` (Edit → Plugins, or add `"Enabled": true` entries).
   Every extension requires `SeinARTSFramework`.
4. Open the project and let the editor compile/refresh.

Install exactly one copy of each plugin. Duplicate installs (for example one copy in the project
and one in the engine) produce undefined class resolution and are rejected by the diagnostic.

## Validating the installation

The release ships a read-only diagnostic that verifies engine identity, plugin dependency
closure, duplicate installs, version consistency, and test-plugin stripping:

```powershell
Scripts\Diagnostics\Test-SeinARTSInstallation.ps1 -ProjectRoot <your project root>
```

It prints stable result codes and a JSON report, and never modifies your project. Run it after
installation and after every plugin upgrade.

## What a correct installation looks like

- The Plugins panel lists the installed SeinARTS plugins as enabled.
- Project Settings contains a **SeinARTS** page (under Game) and one page per installed
  extension (under Plugins).
- The Content Browser's right-click menu offers SeinARTS asset types (Unit, Ability, Effect,
  Formation) under a SeinARTS category.

Continue with [Your First Skirmish](FirstSkirmish.md).
