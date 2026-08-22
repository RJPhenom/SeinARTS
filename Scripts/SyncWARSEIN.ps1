#Requires -Version 7.0
<#
.SYNOPSIS
  Package the six production plugins FAB-style and deliver them into the
  local WARSEIN project: install, commit, push, and build WARSEINEditor.

.DESCRIPTION
  The push-to-main automation. A pre-push git hook (see .githooks/README or
  Scripts/Install-GitHooks.ps1) launches this detached whenever main is pushed,
  so WARSEIN receives every framework update automatically:

    1. Scripts/PackagePlugins.ps1 -PackageOnly  (FAB-standard artifacts, .dist)
    2. install the zips into <WARSEIN>/Plugins (replace SeinARTS*)
    3. commit + push WARSEIN ("Install SeinARTS <version>")
    4. build WARSEINEditor (skipped with a notice if the editor is open —
       use Live Coding, or rerun this script after closing it)

  Everything is local: no GitHub release, no workflow, no tokens. GitHub
  releases via Scripts/Release/Invoke-ReleaseGate.ps1 remain the deliberate
  milestone/FAB path and are not part of this loop.

  Output is logged to <WARSEIN>/Saved/SeinSync/<stamp>.log when launched by
  the hook (the hook redirects); run it interactively for console output.

.EXAMPLE
  .\Scripts\SyncWARSEIN.ps1
  # full sync: package, install, commit, push, build
#>
param(
    [string] $WarseinRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'WARSEIN'),
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $ProjectRoot '.dist'
$Plugins = @(
    'SeinARTSFramework',
    'SeinARTSSquadExtension',
    'SeinARTSCoverExtension',
    'SeinARTSMovementPlusExtension',
    'SeinARTSOnlineServicesExtension',
    'SeinARTSCoverSquadExtension'
)

if (-not (Test-Path (Join-Path $WarseinRoot 'WARSEIN.uproject'))) {
    throw "WARSEIN project not found at '$WarseinRoot'."
}

$Sha = (git -C $ProjectRoot rev-parse --short HEAD).Trim()
$Version = "0.0.$((git -C $ProjectRoot rev-list --count HEAD).Trim())+$Sha"
Write-Host "[SyncWARSEIN] packaging SeinARTS @ $Sha as $Version" -ForegroundColor Cyan

& (Join-Path $PSScriptRoot 'PackagePlugins.ps1') -Version $Version -PackageOnly
if ($LASTEXITCODE -ne 0) { throw "Packaging failed (exit $LASTEXITCODE)." }

# --- Install into WARSEIN -----------------------------------------------------
Add-Type -AssemblyName System.IO.Compression.FileSystem
$WarseinPlugins = Join-Path $WarseinRoot 'Plugins'
New-Item -ItemType Directory -Path $WarseinPlugins -Force | Out-Null
foreach ($p in $Plugins) {
    $target = Join-Path $WarseinPlugins $p
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    $zip = Join-Path $Dist "$p.zip"
    if (-not (Test-Path $zip)) { throw "Packaged zip missing: $zip" }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $WarseinPlugins)
    Write-Host "[SyncWARSEIN] installed $p" -ForegroundColor Green
}

# --- Commit + push WARSEIN ----------------------------------------------------
git -C $WarseinRoot add -A Plugins
git -C $WarseinRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git -C $WarseinRoot commit -m "Install SeinARTS $Version" -m "Local push-to-main sync from SeinARTS commit $Sha."
    if ($LASTEXITCODE -ne 0) { throw 'WARSEIN commit failed.' }
    git -C $WarseinRoot push origin main
    if ($LASTEXITCODE -ne 0) { Write-Warning 'WARSEIN push failed (offline?). The install commit is local; push later.' }
} else {
    Write-Host '[SyncWARSEIN] no plugin changes to commit.' -ForegroundColor DarkGray
}

# --- Build WARSEINEditor ------------------------------------------------------
if ($SkipBuild) {
    Write-Host '[SyncWARSEIN] build skipped by request.' -ForegroundColor Yellow
    exit 0
}
if (Get-Process -Name UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    Write-Warning 'UnrealEditor is running - skipping the WARSEIN build (a link would fail on locked DLLs). Use Live Coding (Ctrl+Alt+F11) or close the editor and rerun.'
    exit 0
}
$Engine = 'C:\Program Files\Epic Games\UE_5.8'
$BuildBat = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $BuildBat)) { throw "UE Build.bat not found at '$BuildBat'." }
Write-Host '[SyncWARSEIN] building WARSEINEditor' -ForegroundColor Cyan
& $BuildBat WARSEINEditor Win64 Development "-Project=$(Join-Path $WarseinRoot 'WARSEIN.uproject')" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "WARSEINEditor build failed (exit $LASTEXITCODE)." }
Write-Host "[SyncWARSEIN] done - WARSEIN is current with SeinARTS $Sha and built." -ForegroundColor Green
