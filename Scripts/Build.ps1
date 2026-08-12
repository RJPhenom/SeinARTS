#Requires -Version 5.1
<#
.SYNOPSIS
  Compile a SeinARTS C++ target without rediscovering the UE engine each time.

.DESCRIPTION
  Thin wrapper over UnrealBuildTool's Build.bat for THIS project. Defaults to the
  editor target (SeinARTSEditor / Win64 / Development). The engine is resolved
  from a known install first, then the registry via the .uproject's
  EngineAssociation, so this keeps working if 5.8 moves or bumps a patch path.
  Returns UBT's exit code (0 = success).

.EXAMPLE
  .\Scripts\Build.ps1
  # SeinARTSEditor Win64 Development — the usual incremental compile (~20s)

.EXAMPLE
  .\Scripts\Build.ps1 -ExtraArgs '-Clean'
  # force a clean rebuild

.NOTES
  Close the editor (or hot-patch in-editor with Live Coding: Ctrl+Alt+F11) before
  a command-line build — a running editor locks the module DLLs and the *link*
  step fails on "*.dll in use". The compile step still runs either way.
#>
param(
    [string]   $Target   = 'SeinARTSEditor',
    [string]   $Platform = 'Win64',
    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping', 'Test')]
    [string]   $Config   = 'Development',
    [string[]] $ExtraArgs,
    [string]   $EngineRoot
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Uproject    = Join-Path $ProjectRoot 'SeinARTS.uproject'

# --- Resolve the engine (explicit path, known path, registry fallback) --------
$Engine = if ($EngineRoot) {
    (Resolve-Path -LiteralPath $EngineRoot).Path
} else {
    'C:\Program Files\Epic Games\UE_5.8'
}
if (-not (Test-Path $Engine)) {
    $assoc = (Get-Content $Uproject -Raw | ConvertFrom-Json).EngineAssociation
    foreach ($root in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine',
                        'HKLM:\SOFTWARE\Epic Games\Unreal Engine')) {
        try {
            $dir = (Get-ItemProperty -Path "$root\$assoc" -ErrorAction Stop).InstalledDirectory
            if ($dir -and (Test-Path $dir)) { $Engine = $dir; break }
        } catch {}
    }
}

$BuildBat = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $BuildBat)) {
    throw "UE Build.bat not found at '$BuildBat'. Pass -EngineRoot with a UE 5.8 installation."
}
$EngineVersionPath = Join-Path $Engine 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $EngineVersionPath -PathType Leaf)) {
    throw "UE build identity is missing: '$EngineVersionPath'."
}
$EngineVersion = Get-Content -Raw -LiteralPath $EngineVersionPath | ConvertFrom-Json
if ([int]$EngineVersion.MajorVersion -ne 5 -or
    [int]$EngineVersion.MinorVersion -ne 8) {
    throw "SeinARTS requires UE 5.8; '$Engine' reports $($EngineVersion.MajorVersion).$($EngineVersion.MinorVersion)."
}

# --- Warn if the editor is open (locked DLLs -> link failure) -----------------
if (Get-Process -Name UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    Write-Warning 'UnrealEditor is running - the link step may fail on a locked DLL. Use in-editor Live Coding (Ctrl+Alt+F11), or close the editor and re-run.'
}

# --- Build --------------------------------------------------------------------
$ubtArgs = @($Target, $Platform, $Config, "-Project=$Uproject", '-WaitMutex')
if ($ExtraArgs) { $ubtArgs += $ExtraArgs }

Write-Host "[Build.ps1] $Target | $Platform | $Config" -ForegroundColor Cyan
Write-Host "[Build.ps1] engine: $Engine" -ForegroundColor DarkGray
& $BuildBat @ubtArgs
$code = $LASTEXITCODE
if ($code -eq 0) { Write-Host '[Build.ps1] Succeeded.' -ForegroundColor Green }
else            { Write-Host "[Build.ps1] FAILED (exit $code)." -ForegroundColor Red }
exit $code
