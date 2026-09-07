#Requires -Version 5.1
<#
.SYNOPSIS
  Compare the collision workload in two fresh processes and retain bound evidence.
.EXAMPLE
  .\RunDeterminismAB.ps1 -SkipBuild -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [switch] $AllowKnownStartupErrors,
    [ValidateRange(30, 7200)] [int] $TimeoutSeconds = 600,
    [string] $EngineRoot,
    [string] $ResultDirectory,
    [switch] $QuietBuild
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $ProjectRoot 'Scripts/Validation/SeinDeterminismEvidence.ps1')
if (-not $ResultDirectory) {
    $ResultDirectory = Join-Path $ProjectRoot ('Saved/Automation/DeterminismAB-' + [Guid]::NewGuid().ToString('N'))
}
$ResultDirectory = [System.IO.Path]::GetFullPath($ResultDirectory)
if (Test-Path -LiteralPath $ResultDirectory) { throw "A/B output already exists: '$ResultDirectory'." }
New-Item -ItemType Directory -Path $ResultDirectory -Force | Out-Null
$ResultFile = Join-Path $ResultDirectory 'ab-result.json'
$Result = [ordered]@{
    schemaVersion = 1; status = 'Running'; expectedFrames = 120
    startedAtUtc = [DateTime]::UtcNow.ToString('o'); completedAtUtc = $null
    allowKnownStartupErrors = [bool]$AllowKnownStartupErrors
    attempts = @(); failure = $null
}
try {
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ResultFile -Encoding UTF8
    foreach ($Role in @('serial', 'parallel')) {
        $RoleRoot = Join-Path $ResultDirectory $Role
        New-Item -ItemType Directory -Path $RoleRoot -Force | Out-Null
        $AttemptFile = Join-Path $RoleRoot 'attempt.json'
        $Suite = if ($Role -eq 'serial') { 'SeinARTS.Determinism.Process.SerialCollisionTrace' }
            else { 'SeinARTS.Determinism.Process.ParallelCollisionTrace' }
        $Arguments = @{
            Profile = 'Framework'; Suite = $Suite; TimeoutSeconds = $TimeoutSeconds
            SkipBuild = ($SkipBuild -or $Role -eq 'parallel')
            AllowKnownStartupErrors = [bool]$AllowKnownStartupErrors
            ResultFile = $AttemptFile; QuietBuild = [bool]$QuietBuild
        }
        if ($EngineRoot) { $Arguments.EngineRoot = $EngineRoot }
        Write-Host "[DeterminismAB] $Role in a fresh editor process"
        $global:LASTEXITCODE = 0
        & (Join-Path $PSScriptRoot 'RunTests.ps1') @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Role runner returned $LASTEXITCODE." }
        $Attempt = Get-Content -Raw -LiteralPath $AttemptFile | ConvertFrom-Json
        if ($Attempt.status -cne 'Passed' -or $Attempt.suite -cne $Suite) { throw "Invalid $Role attempt." }
        foreach ($Name in @('index.json', 'build-provenance.json', 'Automation.log')) {
            Copy-Item -LiteralPath (Join-Path $Attempt.reportPath $Name) -Destination (Join-Path $RoleRoot $Name)
        }
        if ($Role -eq 'serial') {
            foreach ($Field in @('commit', 'dirtyWorkingTree', 'engineRoot', 'engineBuildFingerprint', 'compileSourceFingerprint')) {
                $Result[$Field] = $Attempt.$Field
            }
        }
        $Result.attempts += [ordered]@{
            role = $Role; attemptId = $Attempt.attemptId
            files = @(foreach ($Name in @('attempt.json', 'index.json', 'build-provenance.json', 'Automation.log')) {
                [ordered]@{ file = "$Role/$Name"; sha256 = (Get-FileHash -LiteralPath (Join-Path $RoleRoot $Name) -Algorithm SHA256).Hash }
            })
        }
    }
    $Result.status = 'Passed'
    $Result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ResultFile -Encoding UTF8
    $null = Assert-SeinDeterminismEvidence -ResultFile $ResultFile
    Write-Host "[DeterminismAB] Passed: 120 canonical-root + raw-pose frames. Receipt: $ResultFile"
}
catch {
    $Result.status = 'Failed'; $Result.failure = $_.Exception.Message
    $Result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ResultFile -Encoding UTF8
    throw
}
