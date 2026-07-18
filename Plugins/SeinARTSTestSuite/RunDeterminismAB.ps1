#Requires -Version 5.1
<#
.SYNOPSIS
  Runs the collision StateHash workload in two fresh editor processes and compares every tick.

.EXAMPLE
  .\RunDeterminismAB.ps1

.EXAMPLE
  .\RunDeterminismAB.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
	[switch] $SkipBuild,

	[switch] $AllowKnownStartupErrors,

	[ValidateRange(30, 7200)]
	[int] $TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$PluginRoot = $PSScriptRoot
$ProjectRoot = (Resolve-Path (Join-Path $PluginRoot '..\..')).Path
$Runner = Join-Path $PluginRoot 'RunTests.ps1'
$AutomationRoot = Join-Path $ProjectRoot 'Saved\Automation'
$ExpectedFrames = 120
$SerialSuite = 'SeinARTS.Determinism.Process.SerialCollisionTrace'
$ParallelSuite = 'SeinARTS.Determinism.Process.ParallelCollisionTrace'

function Invoke-SeinTraceTest([string] $Suite, [bool] $UseExistingBuild)
{
	$Arguments = @{
		Profile = 'Framework'
		Suite = $Suite
		TimeoutSeconds = $TimeoutSeconds
	}
	if ($UseExistingBuild) {
		$Arguments.SkipBuild = $true
	}
	if ($AllowKnownStartupErrors) {
		$Arguments.AllowKnownStartupErrors = $true
	}
	& $Runner @Arguments
}

function Get-LatestSeinReport([string] $Suite)
{
	$SafeSuite = $Suite -replace '[^A-Za-z0-9_.-]', '_'
	$Report = Get-ChildItem -LiteralPath $AutomationRoot -Directory -Filter "$SafeSuite-*" |
		Sort-Object LastWriteTimeUtc -Descending |
		Select-Object -First 1
	if (-not $Report) {
		throw "No Automation report was produced for '$Suite'."
	}
	$LogPath = Join-Path $Report.FullName 'Automation.log'
	if (-not (Test-Path -LiteralPath $LogPath)) {
		throw "Automation report '$($Report.FullName)' has no Automation.log."
	}
	return $LogPath
}

function Read-SeinTrace([string] $LogPath)
{
	$Pattern = '\[SeinDeterminismTrace\]\s+(tick=.*)$'
	return @(Select-String -LiteralPath $LogPath -Pattern $Pattern | ForEach-Object {
		$_.Matches[0].Groups[1].Value.Trim()
	})
}

Write-Host '[RunDeterminismAB.ps1] Running serial trace in a fresh editor process.' -ForegroundColor Cyan
Invoke-SeinTraceTest -Suite $SerialSuite -UseExistingBuild:$SkipBuild
$SerialLog = Get-LatestSeinReport -Suite $SerialSuite

Write-Host '[RunDeterminismAB.ps1] Running parallel trace in a fresh editor process.' -ForegroundColor Cyan
Invoke-SeinTraceTest -Suite $ParallelSuite -UseExistingBuild:$true
$ParallelLog = Get-LatestSeinReport -Suite $ParallelSuite

$SerialTrace = Read-SeinTrace -LogPath $SerialLog
$ParallelTrace = Read-SeinTrace -LogPath $ParallelLog
if ($SerialTrace.Count -ne $ExpectedFrames -or $ParallelTrace.Count -ne $ExpectedFrames) {
	throw "Expected $ExpectedFrames trace frames; serial=$($SerialTrace.Count), parallel=$($ParallelTrace.Count). Logs: '$SerialLog', '$ParallelLog'."
}

for ($Index = 0; $Index -lt $ExpectedFrames; ++$Index) {
	if ($SerialTrace[$Index] -cne $ParallelTrace[$Index]) {
		$PayloadPattern = '^tick=\d+ state=(0x[0-9A-Fa-f]+) pose=(0x[0-9A-Fa-f]+)$'
		$SerialMatch = [regex]::Match($SerialTrace[$Index], $PayloadPattern)
		$ParallelMatch = [regex]::Match($ParallelTrace[$Index], $PayloadPattern)
		if ($SerialMatch.Success -and $ParallelMatch.Success -and
			$SerialMatch.Groups[2].Value -ceq $ParallelMatch.Groups[2].Value -and
			$SerialMatch.Groups[1].Value -cne $ParallelMatch.Groups[1].Value) {
			throw "StateHash canonicality divergence at frame $($Index + 1) despite identical raw fixed-point poses (STATE-02):`nserial:   $($SerialTrace[$Index])`nparallel: $($ParallelTrace[$Index])`nLogs: '$SerialLog', '$ParallelLog'."
		}
		throw "First serial/parallel divergence at frame $($Index + 1):`nserial:   $($SerialTrace[$Index])`nparallel: $($ParallelTrace[$Index])`nLogs: '$SerialLog', '$ParallelLog'."
	}
}

Write-Host "[RunDeterminismAB.ps1] Serial/parallel StateHash + raw-pose traces match for all $ExpectedFrames ticks." -ForegroundColor Green
Write-Host "Serial log:   $SerialLog"
Write-Host "Parallel log: $ParallelLog"
