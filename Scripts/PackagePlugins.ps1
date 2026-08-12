#Requires -Version 7.0
<#
.SYNOPSIS
  Package the five production SeinARTS plugins FAB-style and publish them as a
  GitHub release on the SeinARTS repo.

.DESCRIPTION
  For each production plugin (in dependency order) this runs UAT BuildPlugin
  against the installed UE 5.8 engine (-Rocket) — the same standalone packaging
  path a FAB/Marketplace submission uses. Output per plugin: .uplugin (stamped
  with the release version + Installed:true), Source/, Content/, Resources/,
  Config/ (via FilterPlugin.ini), and prebuilt Win64 Binaries/ (PDBs stripped).

  BuildPlugin compiles each plugin in isolation, so the extensions can only see
  the framework if the engine can: each packaged plugin is staged into
  Engine/Plugins/Marketplace while its dependents build, then removed again
  (exactly how FAB resolves plugin-on-plugin dependencies).

  Unless -PackageOnly, the script then creates GitHub release v<Version> with
  the five zips attached. From there, GitHub takes over: the SeinARTS
  notify-warsein workflow fires on the published release and dispatches
  WARSEIN's seinarts-update workflow, which installs the packages into
  WARSEIN/Plugins/ and commits. Your side of the loop: run this, then
  `git pull` in WARSEIN.

.EXAMPLE
  .\Scripts\PackagePlugins.ps1 -Version 1.2.0
  # package all five + publish release v1.2.0 (HEAD must be pushed to origin)

.EXAMPLE
  .\Scripts\PackagePlugins.ps1 -Only SeinARTSFramework -PackageOnly
  # dry-run: package just the framework, no release

.NOTES
  Packaging is a full standalone compile per plugin — expect many minutes, not
  the ~20s incremental Build.ps1. Safe to run with the editor open (BuildPlugin
  links into its own host project, not this project's Binaries).
#>
param(
    [string]   $Version,       # e.g. '1.2.0' -> release tag v1.2.0. Default: 0.0.<commit count>
    [switch]   $PackageOnly,   # package + zip only; skip the GitHub release
    [string[]] $Only           # subset of plugin names, for debugging one package
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

# Dependency order matters: the framework must be staged into the engine before
# any extension packages; Cover+Squad bridge needs all three of its deps staged.
$AllPlugins = @(
    'SeinARTSFramework',
    'SeinARTSSquadExtension',
    'SeinARTSCoverExtension',
    'SeinARTSMovementPlusExtension',
    'SeinARTSCoverSquadExtension'
)
$Plugins = if ($Only) { $AllPlugins | Where-Object { $Only -contains $_ } } else { $AllPlugins }
if (-not $Plugins) { throw "No plugins selected. -Only must name plugins from: $($AllPlugins -join ', ')" }
if ($Only -and -not $PackageOnly) {
    throw 'Refusing to publish a release from a partial (-Only) package set. Add -PackageOnly, or drop -Only.'
}

if (-not $Version) {
    $Version = "0.0.$(git -C $ProjectRoot rev-list --count HEAD)"
    Write-Host "[PackagePlugins] no -Version given; using '$Version'" -ForegroundColor Yellow
}

# --- Resolve the engine (same logic as Build.ps1) -----------------------------
$Engine = 'C:\Program Files\Epic Games\UE_5.8'
if (-not (Test-Path $Engine)) {
    $assoc = (Get-Content (Join-Path $ProjectRoot 'SeinARTS.uproject') -Raw | ConvertFrom-Json).EngineAssociation
    foreach ($root in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine',
                        'HKLM:\SOFTWARE\Epic Games\Unreal Engine')) {
        try {
            $dir = (Get-ItemProperty -Path "$root\$assoc" -ErrorAction Stop).InstalledDirectory
            if ($dir -and (Test-Path $dir)) { $Engine = $dir; break }
        } catch {}
    }
}
$RunUAT = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $RunUAT)) { throw "RunUAT.bat not found at '$RunUAT'." }

$Marketplace = Join-Path $Engine 'Engine\Plugins\Marketplace'
$Dist        = Join-Path $ProjectRoot '.dist'
$Logs        = Join-Path $Dist 'logs'

if (Test-Path $Dist) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Path $Logs -Force | Out-Null

# Stale staged copies from a previous (failed) run would shadow/duplicate the
# plugins we're about to build — clear them all up front.
foreach ($p in $AllPlugins) {
    $stale = Join-Path $Marketplace $p
    if (Test-Path $stale) {
        Write-Host "[PackagePlugins] removing stale engine copy: $stale" -ForegroundColor Yellow
        Remove-Item $stale -Recurse -Force
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Staged = [System.Collections.Generic.List[string]]::new()

try {
    foreach ($p in $Plugins) {
        $src = Join-Path $ProjectRoot "Plugins\$p\$p.uplugin"
        $out = Join-Path $Dist $p
        $log = Join-Path $Logs "$p.log"
        if (-not (Test-Path $src)) { throw "Plugin manifest not found: $src" }

        Write-Host "[PackagePlugins] BuildPlugin $p -> $out" -ForegroundColor Cyan
        & $RunUAT BuildPlugin "-Plugin=$src" "-Package=$out" -TargetPlatforms=Win64 -Rocket 2>&1 |
            Tee-Object -FilePath $log | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "---- tail of $log ----" -ForegroundColor Red
            Get-Content $log -Tail 40 | Write-Host
            throw "BuildPlugin failed for $p (exit $LASTEXITCODE). Full log: $log"
        }

        # Stamp the packaged manifest like an installed FAB plugin.
        $manifestPath = Join-Path $out "$p.uplugin"
        $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
        $manifest | Add-Member -NotePropertyName VersionName -NotePropertyValue $Version -Force
        $manifest | Add-Member -NotePropertyName Installed   -NotePropertyValue $true    -Force
        $manifest | ConvertTo-Json -Depth 16 | Set-Content $manifestPath -Encoding utf8

        # Stage the FULL package into the engine so downstream plugins can
        # compile against it — Intermediate/ must survive here, it holds the
        # UHT *.generated.h headers dependents include (same layout a real
        # FAB engine install ships).
        $engineCopy = Join-Path $Marketplace $p
        New-Item -ItemType Directory -Path $Marketplace -Force | Out-Null
        Copy-Item $out $engineCopy -Recurse
        $Staged.Add($engineCopy)

        # Then strip what the shipped zip doesn't need: build scratch (WARSEIN
        # recompiles project plugins from source, regenerating UHT headers),
        # debug symbols, and regenerable baked level data (re-bake in WARSEIN).
        Remove-Item (Join-Path $out 'Intermediate') -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path (Join-Path $out 'Binaries')) {
            Get-ChildItem (Join-Path $out 'Binaries') -Recurse -Filter '*.pdb' | Remove-Item -Force
        }
        foreach ($baked in @('Content\LevelData', 'Content\NavData', 'Content\FogOfWarData', 'Content\Levels\Data')) {
            Remove-Item (Join-Path $out $baked) -Recurse -Force -ErrorAction SilentlyContinue
        }

        # Zip with the plugin folder as the zip root, so extracting into
        # WARSEIN/Plugins/ yields Plugins/<Name>/ directly. (.NET 7+ writes
        # forward-slash entry names — safe for unzip on the linux runner.)
        $zip = Join-Path $Dist "$p.zip"
        [System.IO.Compression.ZipFile]::CreateFromDirectory($out, $zip, 'Optimal', $true)
        Write-Host "[PackagePlugins] packaged $p ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)" -ForegroundColor Green
    }
}
finally {
    foreach ($s in $Staged) { Remove-Item $s -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($PackageOnly) {
    Write-Host "[PackagePlugins] done (package only). Zips in $Dist" -ForegroundColor Green
    exit 0
}

# --- Publish the release ------------------------------------------------------
# The tag must point at a commit GitHub already has; require HEAD to be pushed.
$head = git -C $ProjectRoot rev-parse HEAD
git -C $ProjectRoot fetch origin --quiet
$onRemote = git -C $ProjectRoot branch -r --contains $head
if (-not $onRemote) {
    throw "HEAD ($head) is not on origin — push your branch first, then re-run. The release tag must point at a pushed commit."
}

$repo = git -C $ProjectRoot remote get-url origin
$zips = $AllPlugins | ForEach-Object { Join-Path $Dist "$_.zip" }
$notes = @"
Packaged from ``$head`` (UE 5.8, Win64, UAT BuildPlugin -Rocket).

Plugins: $($AllPlugins -join ', ').

Published releases are installed into WARSEIN automatically by its seinarts-update workflow.
"@

Write-Host "[PackagePlugins] creating release v$Version on $repo" -ForegroundColor Cyan
gh release create "v$Version" @zips -R $repo --target $head --title "SeinARTS v$Version" --notes $notes
if ($LASTEXITCODE -ne 0) { throw "gh release create failed (exit $LASTEXITCODE)." }

# Kick WARSEIN's install workflow directly — your gh login already spans both
# repos, so no cross-repo PAT is needed. The install itself runs on GitHub.
gh workflow run seinarts-update.yml -R RJPhenom/WARSEIN -f "tag=v$Version"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Release v$Version published, but triggering WARSEIN's workflow failed. Run it manually from WARSEIN's Actions tab with tag 'v$Version'."
} else {
    Write-Host "[PackagePlugins] release v$Version published; WARSEIN's seinarts-update workflow is running. 'git pull' WARSEIN once it commits." -ForegroundColor Green
}
