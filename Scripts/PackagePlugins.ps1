#Requires -Version 7.0
<#
.SYNOPSIS
  Package the six production SeinARTS plugins FAB-style for qualification or
  publication by the release orchestrator.

.DESCRIPTION
  For each production plugin (in dependency order) this runs UAT BuildPlugin
  against the installed UE 5.8 engine (-Rocket) - the same standalone packaging
  path a FAB/Marketplace submission uses. Output per plugin: .uplugin (stamped
  with the release version + Installed:true), Source/, Content/, Resources/,
  Config/ (via FilterPlugin.ini), and prebuilt Win64 Binaries/ (PDBs stripped).
  The Framework artifact also ships the read-only installation diagnostic and,
  when the deliberate public Docs/ tree exists, its customer documentation.

  BuildPlugin compiles each plugin in isolation, so the extensions can only see
  the framework if the engine can: each packaged plugin is staged into
  Engine/Plugins/Marketplace while its dependents build, then removed again
  (exactly how FAB resolves plugin-on-plugin dependencies).

  This helper never publishes. Scripts/Release/Invoke-ReleaseGate.ps1 is the only
  publication entrypoint; it owns host builds, tests, exact-artifact consumer
  qualification, immutable hash verification, evidence, and GitHub release creation.

.EXAMPLE
  .\Scripts\PackagePlugins.ps1 -Version 1.2.0 -PackageOnly
  # package all six for release-gate qualification

.EXAMPLE
  .\Scripts\PackagePlugins.ps1 -Only SeinARTSFramework -PackageOnly
  # dry-run: package just the framework, no release

.NOTES
  Packaging is a full standalone compile per plugin - expect many minutes, not
  the ~20s incremental Build.ps1. Safe to run with the editor open (BuildPlugin
  links into its own host project, not this project's Binaries).
#>
param(
    [string]   $Version,       # e.g. '1.2.0' -> release tag v1.2.0. Default: 0.0.<commit count>
    [switch]   $PackageOnly,   # package + zip only; skip the GitHub release
    [string[]] $Only,          # subset of plugin names, for debugging one package
    [string]   $EngineRoot     # optional source/installed engine root; default resolves UE 5.8
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PipelineMutex = [System.Threading.Mutex]::new(
    $false, 'Local\SeinARTS.ArtifactPipeline')
$PipelineMutexAcquired = $false
try {
    $PipelineMutexAcquired = $PipelineMutex.WaitOne(0)
}
catch [System.Threading.AbandonedMutexException] {
    $PipelineMutexAcquired = $true
}
if (-not $PipelineMutexAcquired) {
    $PipelineMutex.Dispose()
    throw 'Another SeinARTS release, packaging, or consumer qualification run owns the artifact pipeline.'
}
$LockRoot = Join-Path $ProjectRoot 'Saved\Locks'
$LockPath = Join-Path $LockRoot 'package-plugins.lock'
try {
    New-Item -ItemType Directory -Path $LockRoot -Force | Out-Null
    $PackageLock = [System.IO.File]::Open(
        $LockPath,
        [System.IO.FileMode]::OpenOrCreate,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
}
catch {
    $PipelineMutex.ReleaseMutex()
    $PipelineMutex.Dispose()
    throw "Another SeinARTS packaging run owns '$LockPath'. Wait for it to finish before retrying."
}

try {
if (-not $PackageOnly) {
    throw 'PackagePlugins.ps1 is package-only. Use Scripts/Release/Invoke-ReleaseGate.ps1 to qualify and publish, or pass -PackageOnly for diagnostics.'
}

function Test-SeinSemVer([string] $Candidate)
{
    return $Candidate -match (
        '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)' +
        '(?:-(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)' +
        '(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*)?' +
        '(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')
}

# Dependency order matters: the framework must be staged into the engine before
# any extension packages; Cover+Squad bridge needs all three of its deps staged.
$AllPlugins = @(
    'SeinARTSFramework',
    'SeinARTSSquadExtension',
    'SeinARTSCoverExtension',
    'SeinARTSMovementPlusExtension',
    'SeinARTSOnlineServicesExtension',
    'SeinARTSCoverSquadExtension'
)
$UnknownPlugins = @($Only | Where-Object { $_ -notin $AllPlugins } |
    Sort-Object -Unique)
if ($UnknownPlugins.Count -ne 0) {
    throw "Unknown -Only plugin(s): $($UnknownPlugins -join ', '). Expected: $($AllPlugins -join ', ')."
}
$Plugins = if ($Only) { $AllPlugins | Where-Object { $Only -contains $_ } } else { $AllPlugins }
if (-not $Plugins) { throw "No plugins selected. -Only must name plugins from: $($AllPlugins -join ', ')" }
$PackagingHead = (git -C $ProjectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $PackagingHead) {
    throw 'Could not resolve the source commit for plugin packaging.'
}

if (-not $Version) {
    $Version = "0.0.$(git -C $ProjectRoot rev-list --count HEAD)"
    if ($LASTEXITCODE -ne 0) { throw 'Could not derive the default package version from git history.' }
    Write-Host "[PackagePlugins] no -Version given; using '$Version'" -ForegroundColor Yellow
}

if (-not (Test-SeinSemVer $Version)) {
    throw "Version '$Version' is not a valid SemVer 2.0 version (expected MAJOR.MINOR.PATCH with optional prerelease/build metadata)."
}

# --- Resolve the engine (same logic as Build.ps1) -----------------------------
$Engine = if ($EngineRoot) {
    (Resolve-Path -LiteralPath $EngineRoot).Path
} else {
    'C:\Program Files\Epic Games\UE_5.8'
}
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
$EngineBuildVersionPath = Join-Path $Engine 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $EngineBuildVersionPath -PathType Leaf)) {
    throw "UE build identity is missing: '$EngineBuildVersionPath'."
}
$EngineBuildVersion = Get-Content -Raw -LiteralPath $EngineBuildVersionPath | ConvertFrom-Json
$EngineBuildFingerprint = (Get-FileHash -LiteralPath $EngineBuildVersionPath `
    -Algorithm SHA256).Hash
if ([int]$EngineBuildVersion.MajorVersion -ne 5 -or
    [int]$EngineBuildVersion.MinorVersion -ne 8) {
    throw "SeinARTS packaging requires UE 5.8; '$Engine' reports $($EngineBuildVersion.MajorVersion).$($EngineBuildVersion.MinorVersion)."
}

$Marketplace = Join-Path $Engine 'Engine\Plugins\Marketplace'
$Dist        = Join-Path $ProjectRoot '.dist'
$Logs        = Join-Path $Dist 'logs'
$StagingMarkerName = '.seinarts-package-staging.json'

# A prior interrupted run may leave one of this script's staging copies behind.
# Reclaim only copies carrying this repository's marker; never delete a real
# engine-installed plugin merely because it has the same product name. Validate
# this boundary before deleting prior generated output.
foreach ($p in $AllPlugins) {
    $stale = Join-Path $Marketplace $p
    if (Test-Path $stale) {
        $markerPath = Join-Path $stale $StagingMarkerName
        if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
            throw "Engine plugin '$stale' already exists and is not a recognized SeinARTS packaging-stage copy. Move or remove it explicitly; this script will not delete installed plugins."
        }
        $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
        if ([string]$marker.repositoryRoot -cne $ProjectRoot) {
            throw "Engine plugin '$stale' carries a staging marker for another repository ('$($marker.repositoryRoot)')."
        }
    }
}

if (Test-Path $Dist) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Path $Logs -Force | Out-Null
foreach ($p in $AllPlugins) {
    $stale = Join-Path $Marketplace $p
    if (Test-Path $stale) {
        Write-Host "[PackagePlugins] removing interrupted staging copy: $stale" -ForegroundColor Yellow
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

        if ($p -ceq 'SeinARTSFramework') {
            $PublicDocs = Join-Path $ProjectRoot 'Docs'
            $PublicDiagnostics = Join-Path $ProjectRoot 'Scripts\Diagnostics'
            if (-not (Test-Path -LiteralPath $PublicDiagnostics -PathType Container)) {
                throw 'Framework public diagnostics are missing.'
            }
            $PackagedDiagnostics = Join-Path $out 'Tools\Diagnostics'
            New-Item -ItemType Directory -Force `
                -Path $PackagedDiagnostics | Out-Null
            if (Test-Path -LiteralPath $PublicDocs -PathType Container) {
                $PublicDocItems = @(Get-ChildItem -LiteralPath $PublicDocs -Force)
                if ($PublicDocItems.Count -gt 0) {
                    $PackagedDocs = Join-Path $out 'Documentation'
                    New-Item -ItemType Directory -Force -Path $PackagedDocs | Out-Null
                    foreach ($PublicDocItem in $PublicDocItems) {
                        Copy-Item -LiteralPath $PublicDocItem.FullName `
                            -Destination $PackagedDocs -Recurse -Force
                    }
                }
            }
            Copy-Item -LiteralPath (Join-Path $PublicDiagnostics `
                'Test-SeinARTSInstallation.ps1') `
                -Destination $PackagedDiagnostics -Force
        }

        # Stage the FULL package into the engine so downstream plugins can
        # compile against it - Intermediate/ must survive here, it holds the
        # UHT *.generated.h headers dependents include (same layout a real
        # FAB engine install ships).
        $engineCopy = Join-Path $Marketplace $p
        New-Item -ItemType Directory -Path $Marketplace -Force | Out-Null
        $Staged.Add($engineCopy)
        Copy-Item $out $engineCopy -Recurse
        [ordered]@{
            schemaVersion = 1
            repositoryRoot = $ProjectRoot
            sourceCommit = $PackagingHead
            plugin = $p
        } | ConvertTo-Json | Set-Content `
            -LiteralPath (Join-Path $engineCopy $StagingMarkerName) `
            -Encoding utf8

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
        # forward-slash entry names - safe for unzip on the linux runner.)
        $zip = Join-Path $Dist "$p.zip"
        [System.IO.Compression.ZipFile]::CreateFromDirectory($out, $zip, 'Optimal', $true)
        Write-Host "[PackagePlugins] packaged $p ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)" -ForegroundColor Green
    }
}
finally {
    foreach ($s in $Staged) { Remove-Item $s -Recurse -Force -ErrorAction SilentlyContinue }
}

$ArtifactRecords = @(
    foreach ($p in $Plugins) {
        $zip = Join-Path $Dist "$p.zip"
        if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) {
            throw "Packaged artifact is missing: '$zip'."
        }
        $sourceManifest = Get-Content -Raw -LiteralPath `
            (Join-Path $ProjectRoot "Plugins\$p\$p.uplugin") | ConvertFrom-Json
        [ordered]@{
            plugin = $p
            file = [System.IO.Path]::GetFileName($zip)
            bytes = (Get-Item -LiteralPath $zip).Length
            sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
            versionName = $Version
            dependencies = @($sourceManifest.Plugins |
                Where-Object { $_.Name -like 'SeinARTS*' } |
                ForEach-Object { [string]$_.Name })
        }
    }
)
$ManifestGitStatus = @(git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0) { throw 'Could not capture source state for the release manifest.' }
$ReleaseManifestPath = Join-Path $Dist 'release-manifest.json'
[ordered]@{
    schemaVersion = 2
    version = $Version
    sourceCommit = $PackagingHead
    sourceDirty = $ManifestGitStatus.Count -ne 0
    engineRoot = $Engine
    engineBuildFingerprint = $EngineBuildFingerprint
    engineVersion = if ($EngineBuildVersion) {
        "$($EngineBuildVersion.MajorVersion).$($EngineBuildVersion.MinorVersion).$($EngineBuildVersion.PatchVersion)"
    } else {
        'unknown'
    }
    engineChangelist = if ($EngineBuildVersion) {
        [int64]$EngineBuildVersion.Changelist
    } else {
        $null
    }
    targetPlatform = 'Win64'
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    artifacts = $ArtifactRecords
} | ConvertTo-Json -Depth 12 | Set-Content `
    -LiteralPath $ReleaseManifestPath -Encoding utf8

$ConsumerMatrix = Join-Path $ProjectRoot `
    'Scripts\ConsumerMatrix\Verify-ConsumerMatrix.ps1'
if (-not (Test-Path -LiteralPath $ConsumerMatrix -PathType Leaf)) {
    throw "Release consumer-matrix gate is missing: '$ConsumerMatrix'."
}
$PackagedSet = $Plugins -join ','
$ArtifactValidationProfile = switch ($PackagedSet) {
    'SeinARTSFramework' { 'Framework' }
    'SeinARTSFramework,SeinARTSSquadExtension' { 'Squad' }
    'SeinARTSFramework,SeinARTSCoverExtension' { 'Cover' }
    'SeinARTSFramework,SeinARTSMovementPlusExtension' { 'MovementPlus' }
    'SeinARTSFramework,SeinARTSOnlineServicesExtension' { 'OnlineServices' }
    ($AllPlugins -join ',') { 'All' }
    default { $null }
}
if ($ArtifactValidationProfile) {
    & $ConsumerMatrix `
        -Profile $ArtifactValidationProfile `
        -EngineRoot $Engine `
        -ArtifactDirectory $Dist `
        -ValidateArtifactsOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged artifact validation failed with exit code $LASTEXITCODE."
    }
}
else {
    Write-Warning 'The selected partial package set has no complete consumer profile; archive validation is deferred until its dependencies are packaged together.'
}

Write-Host "[PackagePlugins] done. Zips and release-manifest.json are in $Dist" -ForegroundColor Green
}
finally {
    $PackageLock.Dispose()
    $PipelineMutex.ReleaseMutex()
    $PipelineMutex.Dispose()
}
