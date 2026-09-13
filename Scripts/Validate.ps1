#Requires -Version 5.1
<#
.SYNOPSIS
  Run an explicit development validation preset with compact output and a JSON receipt.
.DESCRIPTION
  Never publishes or infers coverage from filenames. Choose the preset from the behavior changed.
  Focused requires test prefixes. Simulation adds broad behavior suites and fresh-process A/B.
  Full runs six suites in both profiles plus Shipping and A/B, but is not release qualification.
.EXAMPLE
  .\Scripts\Validate.ps1 -Preset Focused -Suite SeinARTS.Unit.Core -Profile Framework
.EXAMPLE
  .\Scripts\Validate.ps1 -Preset Simulation -Profile All
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateSet('Documentation', 'Focused', 'Simulation', 'Full')]
    [string] $Preset,
    [string[]] $Suite,
    [ValidateSet('All', 'Framework')] [string] $Profile = 'All',
    [string] $EngineRoot,
    [switch] $SkipBuild,
    [ValidateRange(30, 7200)] [int] $TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ($Preset -eq 'Focused' -and ($null -eq $Suite -or $Suite.Count -eq 0)) { throw 'Focused validation requires -Suite.' }
if ($Preset -ne 'Focused' -and $Suite) { throw '-Suite applies only to Focused validation.' }
if ($Preset -eq 'Full' -and $PSBoundParameters.ContainsKey('Profile')) { throw 'Full always validates both profiles.' }
if ($Preset -eq 'Documentation' -and ($SkipBuild -or $EngineRoot -or $PSBoundParameters.ContainsKey('Profile'))) {
    throw 'Documentation validation does not accept build/profile options.'
}
foreach ($Prefix in $Suite) {
    if ($Prefix -notmatch '^SeinARTS\.[A-Za-z0-9_.]+$') { throw "Invalid test prefix: '$Prefix'." }
}
$RunRoot = Join-Path $RepoRoot ('Saved/Validation/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
$ResultFile = Join-Path $RunRoot 'validation-result.json'
$Commit = & git -C $RepoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
$State = @(& git -C $RepoRoot status --porcelain=v1)
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine working-tree state.' }
$Steps = [System.Collections.Generic.List[object]]::new()
$Result = [ordered]@{
    schemaVersion = 1; preset = $Preset; commit = [string]$Commit
    dirtyWorkingTree = $State.Count -gt 0; status = 'Running'
    startedAtUtc = [DateTime]::UtcNow.ToString('o'); completedAtUtc = $null
    steps = $Steps; failure = $null
    scope = 'Development checks only; excludes release consumer qualification and human/runtime acceptance.'
}
function Write-ValidationResult {
    $Result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ResultFile -Encoding UTF8
}
function Invoke-ValidationStep([string] $Name, [scriptblock] $Action, [string] $Evidence) {
    $Log = Join-Path $RunRoot (('{0:D2}' -f $Steps.Count) + '.log')
    $Step = [ordered]@{ name = $Name; status = 'Running'; logPath = $Log; evidence = $Evidence
        startedAtUtc = [DateTime]::UtcNow.ToString('o'); completedAtUtc = $null; failure = $null }
    $Steps.Add($Step); Write-ValidationResult
    Write-Host "[Validate] $Name"
    try {
        $global:LASTEXITCODE = 0
        & $Action *> $Log
        if ($LASTEXITCODE -ne 0) { throw "$Name returned exit code $LASTEXITCODE." }
        if ($Evidence) {
            $Receipt = Get-Content -Raw -LiteralPath $Evidence | ConvertFrom-Json
            if ($Receipt.status -cne 'Passed') { throw "$Name did not produce passing evidence." }
            $Step.evidenceSha256 = (Get-FileHash -LiteralPath $Evidence -Algorithm SHA256).Hash
            if ($null -ne $Receipt.discoveredTestCount) { $Step.testCount = $Receipt.discoveredTestCount }
        }
        $Step.status = 'Passed'
    }
    catch {
        $Step.status = 'Failed'; $Step.failure = $_.Exception.Message
        Get-Content -LiteralPath $Log -Tail 30 -ErrorAction SilentlyContinue | Write-Host
        throw
    }
    finally { $Step.completedAtUtc = [DateTime]::UtcNow.ToString('o'); Write-ValidationResult }
}
try {
    Write-ValidationResult
    Invoke-ValidationStep 'Diff whitespace' { & git -C $RepoRoot diff --check HEAD }
    if ($Preset -ne 'Documentation') {
        $Profiles = @(if ($Preset -eq 'Full') { 'All'; 'Framework' } else { $Profile })
        $Suites = switch ($Preset) {
            'Focused' { @($Suite | Select-Object -Unique) }
            'Simulation' { @('SeinARTS.Unit', 'SeinARTS.Sim', 'SeinARTS.Integration', 'SeinARTS.Determinism') }
            'Full' { @('SeinARTS.Unit', 'SeinARTS.Integration', 'SeinARTS.Determinism', 'SeinARTS.Editor', 'SeinARTS.Sim', 'SeinARTS.Perf') }
        }
        foreach ($CurrentProfile in $Profiles) {
            $First = $true
            foreach ($CurrentSuite in $Suites) {
                $Evidence = Join-Path $RunRoot "$CurrentProfile-$CurrentSuite.json"
                $Arguments = @{ Suite = $CurrentSuite; Profile = $CurrentProfile
                    QuietBuild = $true; ResultFile = $Evidence; TimeoutSeconds = $TimeoutSeconds
                    # Integration includes Canvas pixel assertions that require a real RHI.
                    KeepRendering = ($CurrentSuite -match '^SeinARTS\.Integration(?:\.|$)')
                    SkipBuild = ($SkipBuild -or -not $First) }
                if ($EngineRoot) { $Arguments.EngineRoot = $EngineRoot }
                Invoke-ValidationStep "$CurrentProfile $CurrentSuite" {
                    & (Join-Path $RepoRoot 'Plugins/SeinARTSTestSuite/RunTests.ps1') @Arguments
                } $Evidence
                $First = $false
            }
        }
        if ($Preset -in @('Simulation', 'Full')) {
            $ABRoot = Join-Path $RunRoot 'DeterminismAB'
            $Arguments = @{ ResultDirectory = $ABRoot; QuietBuild = $true; TimeoutSeconds = $TimeoutSeconds
                SkipBuild = ($SkipBuild -or $Profiles[-1] -eq 'Framework') }
            if ($EngineRoot) { $Arguments.EngineRoot = $EngineRoot }
            Invoke-ValidationStep 'Fresh-process collision A/B' {
                & (Join-Path $RepoRoot 'Plugins/SeinARTSTestSuite/RunDeterminismAB.ps1') @Arguments
            } (Join-Path $ABRoot 'ab-result.json')
        }
        if ($Preset -eq 'Full') {
            $Evidence = Join-Path $RunRoot 'shipping.json'
            $Arguments = @{ Target = 'SeinARTS'; Config = 'Shipping'; Quiet = $true; ResultFile = $Evidence }
            if ($EngineRoot) { $Arguments.EngineRoot = $EngineRoot }
            Invoke-ValidationStep 'Shipping build' { & (Join-Path $PSScriptRoot 'Build.ps1') @Arguments } $Evidence
        }
    }
    $Result.status = 'Passed'
}
catch { $Result.status = 'Failed'; $Result.failure = $_.Exception.Message; throw }
finally {
    $Result.completedAtUtc = [DateTime]::UtcNow.ToString('o'); Write-ValidationResult
    Write-Host "[Validate] $($Result.status): $($Steps.Count) step(s). Receipt: $ResultFile"
}
