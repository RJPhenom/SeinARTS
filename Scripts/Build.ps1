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
  # clean outputs; run again without -Clean to rebuild

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
    [string]   $EngineRoot,
    [switch]   $Quiet,
    [string]   $ResultFile
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Uproject    = Join-Path $ProjectRoot 'SeinARTS.uproject'
if ($ResultFile) {
    $ResultFile = [System.IO.Path]::GetFullPath($ResultFile)
    if (Test-Path -LiteralPath $ResultFile) { throw "Result file already exists: '$ResultFile'." }
    New-Item -ItemType Directory -Path (Split-Path -Parent $ResultFile) -Force | Out-Null
}

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
$Started = [DateTime]::UtcNow
$LogRoot = Join-Path $ProjectRoot ('Saved/Build/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
$LogPath = Join-Path $LogRoot 'Build.log'
# Native stderr is build output, not a PowerShell terminating error in Windows PS 5.1.
$PreviousErrorPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    if ($Quiet) { & $BuildBat @ubtArgs *> $LogPath }
    else { & $BuildBat @ubtArgs 2>&1 | Tee-Object -FilePath $LogPath }
    $code = $LASTEXITCODE
}
finally { $ErrorActionPreference = $PreviousErrorPreference }
$Receipt = [ordered]@{
    schemaVersion = 1
    status = if ($code -eq 0) { 'Passed' } else { 'Failed' }
    target = $Target; platform = $Platform; configuration = $Config
    extraArgs = @($ExtraArgs); engineRoot = $Engine
    engineBuildFingerprint = (Get-FileHash -LiteralPath $EngineVersionPath -Algorithm SHA256).Hash
    startedAtUtc = $Started.ToString('o'); completedAtUtc = [DateTime]::UtcNow.ToString('o')
    exitCode = $code; logPath = $LogPath
    # Keep the changed compilation/link evidence available without streaming the whole log.
    buildActions = @(Select-String -LiteralPath $LogPath -Pattern '(Compile|Link) \[x64\]' |
        ForEach-Object { $_.Line })
}
$ReceiptPath = Join-Path $LogRoot 'build-result.json'
$Receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReceiptPath -Encoding UTF8
if ($ResultFile) { Copy-Item -LiteralPath $ReceiptPath -Destination $ResultFile -Force }
Write-Host "[Build.ps1] receipt: $ReceiptPath"
if ($Quiet -and $code -ne 0) { Get-Content -LiteralPath $LogPath -Tail 40 | Write-Host }
if ($code -eq 0) { Write-Host '[Build.ps1] Succeeded.' -ForegroundColor Green }
else            { Write-Host "[Build.ps1] FAILED (exit $code)." -ForegroundColor Red }
exit $code
