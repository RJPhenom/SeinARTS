#Requires -Version 5.1
<#
.SYNOPSIS
  Export and qualify bookmark-bounded replay allocator growth with UE Insights.

.DESCRIPTION
  Validates a clean, passed replay operational-soak attempt and every artifact
  hash it binds, runs Unreal Insights headlessly, exports allocations created
  after the begin bookmark and still live at the end bookmark, then emits a
  receipt with exact trace, analyzer, CSV, and source identities.

.EXAMPLE
  .\Scripts\Qualification\Export-ReplayMemoryInsights.ps1 `
    -AttemptPath Saved\Automation\SeinARTS.Perf.Replay.OperationalSoak-<id>\attempt.json
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $AttemptPath,

	[string] $OutputDirectory,

	[ValidateRange(1, 60)]
	[int] $TimeoutMinutes = 30,

	[switch] $SelfTest,

	[switch] $AnalyzerTrustSelfTest
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$AutomationRoot = Join-Path $RepoRoot 'Saved\Automation'
$SelfTestRoot = Join-Path $AutomationRoot 'ReplayMemoryInsightsSelfTest'
$DefaultAnalysisRoot = Join-Path $RepoRoot `
	'Saved\Profiling\Insights\ReplayMemoryAnalysis'
$BeginBookmark = 'Sein.ReplayOperationalSoak.Begin'
$EndBookmark = 'Sein.ReplayOperationalSoak.End'
$ExpectedSuite = 'SeinARTS.Perf.Replay.OperationalSoak'
$ExpectedProfile = 'Framework'
$ExpectedFullTestPath =
	'SeinARTS.Perf.Replay.OperationalSoak.ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded.ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded_Method'
$ExpectedChannels = @('default', 'memory', 'metadata')
$MaximumReplayRetainedBytes = [uint64]4096
$MeasurementWarmupCheckpointCount = 8
$QualificationSentinelTag =
	'SeinARTS/Replay/Qualification/Sentinel'
$QualificationSentinelBytes = [uint64](16 * 1024 * 1024 + 64)
$QualificationSentinelCallstack =
	'ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded'
$ExpectedInsightsSha256 =
	'1ECDE931BCBB6150A85BAAD666CE76413E45B9D66F254BA3A6B4B833FD4FF206'
$ExpectedCsvHeader =
	'Size,Tag,AllocThread,AllocFunction,AllocSourceFile,AllocCallstack'
$ExpectedTestDlls = @(
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSTestSupport.dll',
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSFrameworkTests.dll',
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSEditorTests.dll')
$ExpectedProductionDlls = @(
	'Binaries\Win64\UnrealEditor-SeinARTS.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSCore.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSCoreEntity.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSEditor.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSFogOfWar.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSFogOfWarEditor.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSFramework.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSGraphNodes.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSLevelData.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSMovement.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSNavigation.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSNet.dll',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor-SeinARTSUIToolkit.dll')
$ExpectedMetadataFiles = @(
	'Binaries\Win64\SeinARTSEditor.target',
	'Binaries\Win64\UnrealEditor.modules',
	'Plugins\SeinARTSFramework\Binaries\Win64\UnrealEditor.modules',
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor.modules')
$QualificationLocks = [System.Collections.Generic.List[System.IDisposable]]::new()
$TestMode = $SelfTest -or $AnalyzerTrustSelfTest
if ($SelfTest -and $AnalyzerTrustSelfTest) {
	throw 'Choose only one replay-memory self-test mode.'
}

function Test-SeinPathWithin([string] $Candidate, [string] $Root)
{
	$FullCandidate = [System.IO.Path]::GetFullPath($Candidate)
	$FullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	return $FullCandidate.Equals(
		$FullRoot,
		[System.StringComparison]::OrdinalIgnoreCase) -or
		$FullCandidate.StartsWith(
			$FullRoot + [System.IO.Path]::DirectorySeparatorChar,
			[System.StringComparison]::OrdinalIgnoreCase)
}

function Get-SeinRequiredFile([string] $Path, [string] $Label)
{
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "$Label is missing: '$Path'."
	}
	return (Resolve-Path -LiteralPath $Path).Path
}

function Lock-SeinRequiredFile([string] $Path, [string] $Label)
{
	$ResolvedPath = Get-SeinRequiredFile $Path $Label
	$Lock = [System.IO.File]::Open(
		$ResolvedPath,
		[System.IO.FileMode]::Open,
		[System.IO.FileAccess]::Read,
		[System.IO.FileShare]::Read)
	$script:QualificationLocks.Add($Lock) | Out-Null
	return $ResolvedPath
}

function Assert-SeinSha256(
	[string] $Path,
	[string] $ExpectedHash,
	[string] $Label)
{
	if ($ExpectedHash -notmatch '^[0-9A-Fa-f]{64}$') {
		throw "$Label has no valid SHA-256 identity."
	}
	$ActualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
	if ($ActualHash -cne $ExpectedHash.ToUpperInvariant()) {
		throw "$Label SHA-256 mismatch for '$Path'."
	}
}

function ConvertTo-SeinDouble([string] $Value, [string] $Label)
{
	$Parsed = 0.0
	if (-not [double]::TryParse(
			$Value,
			[System.Globalization.NumberStyles]::Float,
			[System.Globalization.CultureInfo]::InvariantCulture,
			[ref]$Parsed)) {
		throw "$Label is not a valid invariant floating-point value: '$Value'."
	}
	return $Parsed
}

function Assert-SeinRequiredProperties(
	[object] $Value,
	[string[]] $RequiredProperties,
	[string] $Label)
{
	if ($null -eq $Value) {
		throw "$Label is missing."
	}
	$ActualProperties = @($Value.PSObject.Properties.Name)
	$MissingProperties = @($RequiredProperties | Where-Object {
		$ActualProperties -cnotcontains $_
	})
	if ($MissingProperties.Count -ne 0) {
		throw "$Label is missing required property '$($MissingProperties[0])'."
	}
}

function Assert-SeinCurrentCheckout([string] $ExpectedCommit)
{
	$CurrentCommit = (& git -C $RepoRoot rev-parse HEAD).Trim()
	if ($LASTEXITCODE -ne 0 -or $CurrentCommit -cne $ExpectedCommit) {
		throw 'Attempt commit is not the currently checked-out commit.'
	}
	$CurrentChanges = @(& git -C $RepoRoot status --porcelain=v1 `
		--untracked-files=normal)
	if ($LASTEXITCODE -ne 0 -or $CurrentChanges.Count -ne 0) {
		throw 'Production qualification requires a clean current checkout.'
	}
}

function Assert-SeinProvenanceMap(
	[object] $Map,
	[string[]] $ExpectedPaths,
	[string] $Label)
{
	if ($null -eq $Map) {
		throw "$Label is missing."
	}
	$ActualPaths = @($Map.PSObject.Properties.Name)
	if ((($ActualPaths | Sort-Object) -join "`n") -cne
		(($ExpectedPaths | Sort-Object) -join "`n")) {
		throw "$Label does not contain the exact Framework artifact set."
	}
	if ($ActualPaths.Count -eq 0) {
		throw "$Label is empty."
	}
	foreach ($RelativePath in $ActualPaths) {
		if ([System.IO.Path]::IsPathRooted($RelativePath)) {
			throw "$Label contains an absolute artifact path."
		}
		$ArtifactPath = [System.IO.Path]::GetFullPath(
			(Join-Path $RepoRoot $RelativePath))
		if (-not (Test-SeinPathWithin $ArtifactPath $RepoRoot)) {
			throw "$Label contains an artifact outside the repository."
		}
		$ArtifactPath = Lock-SeinRequiredFile $ArtifactPath `
			"$Label artifact"
		Assert-SeinSha256 $ArtifactPath `
			([string]$Map.PSObject.Properties[$RelativePath].Value) `
			"$Label artifact"
	}
}
try {
$QualificationScriptPath = Lock-SeinRequiredFile $PSCommandPath `
	'Qualification script'
$QualificationScriptSha256 = (Get-FileHash `
	-LiteralPath $QualificationScriptPath -Algorithm SHA256).Hash

$AttemptPath = Lock-SeinRequiredFile $AttemptPath 'Automation attempt'
if (-not (Test-SeinPathWithin $AttemptPath $AutomationRoot)) {
	throw "Automation attempt must be under '$AutomationRoot'."
}
if ($TestMode -and -not (Test-SeinPathWithin $AttemptPath $SelfTestRoot)) {
	throw "Self-test attempts must be under '$SelfTestRoot'."
}
if (-not $TestMode -and (Test-SeinPathWithin $AttemptPath $SelfTestRoot)) {
	throw 'Production qualification rejects replay-memory self-test evidence.'
}
$AttemptDirectory = Split-Path -Parent $AttemptPath
$AttemptSha256 = (Get-FileHash -LiteralPath $AttemptPath `
	-Algorithm SHA256).Hash
$Attempt = Get-Content -Raw -LiteralPath $AttemptPath | ConvertFrom-Json
Assert-SeinRequiredProperties $Attempt @(
	'schemaVersion', 'attemptId', 'suite', 'profile', 'commit',
	'dirtyWorkingTree', 'status', 'traceChannels', 'traceFile',
	'traceFileBytes', 'traceFileSha256', 'engineRoot',
	'engineBuildFingerprint', 'compileSourceFingerprint',
	'testBuildProvenance', 'testBuildProvenanceFile',
	'testBuildProvenanceSha256', 'expectedMinimumCount',
	'expectedCountSource', 'editorExitCode', 'discoveredTestCount',
	'unsuccessfulTestCount', 'testIndexFile', 'testIndexSha256',
	'reportPath') 'Automation attempt'
if ([int]$Attempt.schemaVersion -ne 4 -or
	[string]$Attempt.status -cne 'Passed' -or
	[string]$Attempt.suite -cne $ExpectedSuite -or
	[string]$Attempt.profile -cne $ExpectedProfile -or
	[bool]$Attempt.dirtyWorkingTree -ne $false -or
	[string]$Attempt.commit -notmatch '^[0-9a-f]{40}$' -or
	[string]$Attempt.testBuildProvenance -cne 'BuiltThisAttempt' -or
	[int]$Attempt.expectedMinimumCount -ne 1 -or
	[string]$Attempt.expectedCountSource -cne 'CommandLine' -or
	[int]$Attempt.editorExitCode -ne 0 -or
	[int]$Attempt.discoveredTestCount -lt 1 -or
	[int]$Attempt.unsuccessfulTestCount -ne 0) {
	throw 'Attempt is not a clean, passed Framework replay operational-soak result.'
}
if (-not $TestMode) {
	Assert-SeinCurrentCheckout ([string]$Attempt.commit)
}
if (-not [System.IO.Path]::GetFullPath([string]$Attempt.reportPath).Equals(
		[System.IO.Path]::GetFullPath($AttemptDirectory),
		[System.StringComparison]::OrdinalIgnoreCase)) {
	throw 'Attempt reportPath does not name its containing report directory.'
}

$ActualChannels = @(([string]$Attempt.traceChannels).Split(',') |
	ForEach-Object { $_.Trim().ToLowerInvariant() } | Sort-Object -Unique)
if (@(Compare-Object `
		($ExpectedChannels | Sort-Object) $ActualChannels).Count -ne 0) {
	throw "Attempt must use exact trace channels '$($ExpectedChannels -join ',')'."
}

$TracePath = Lock-SeinRequiredFile ([string]$Attempt.traceFile) 'Trace file'
if ([System.IO.Path]::GetExtension($TracePath) -ine '.utrace') {
	throw "Attempt trace is not a .utrace file: '$TracePath'."
}
$TraceItem = Get-Item -LiteralPath $TracePath
if ($TraceItem.Length -le 0 -or
	[int64]$Attempt.traceFileBytes -ne $TraceItem.Length) {
	throw 'Attempt trace byte count does not match the trace file.'
}
Assert-SeinSha256 $TracePath ([string]$Attempt.traceFileSha256) 'Trace file'

if ([string]$Attempt.testIndexFile -cne 'index.json') {
	throw 'Attempt must name its contained index.json artifact.'
}
$IndexPath = Lock-SeinRequiredFile `
	(Join-Path $AttemptDirectory 'index.json') 'Automation index'
Assert-SeinSha256 $IndexPath ([string]$Attempt.testIndexSha256) `
	'Automation index'
$Index = Get-Content -Raw -LiteralPath $IndexPath | ConvertFrom-Json
Assert-SeinRequiredProperties $Index @(
	'succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'inProcess',
	'tests') 'Automation index'
$Tests = @($Index.tests)
$UnsuccessfulTests = @($Tests | Where-Object {
	[string]$_.state -cne 'Success'
})
if ($Tests.Count -ne 1 -or
	[int]$Attempt.discoveredTestCount -ne 1 -or
	$UnsuccessfulTests.Count -ne 0 -or
	[int]$Index.succeeded -ne $Tests.Count -or
	[int]$Index.succeededWithWarnings -ne 0 -or
	[int]$Index.failed -ne 0 -or
	[int]$Index.notRun -ne 0 -or
	[int]$Index.inProcess -ne 0 -or
	[string]$Tests[0].fullTestPath -cne $ExpectedFullTestPath) {
	throw 'Automation index disagrees with the passed attempt receipt.'
}

if ([string]$Attempt.testBuildProvenanceFile -cne 'build-provenance.json') {
	throw 'Attempt must name its contained build-provenance.json artifact.'
}
$ProvenancePath = Lock-SeinRequiredFile `
	(Join-Path $AttemptDirectory 'build-provenance.json') `
	'Test build provenance'
Assert-SeinSha256 $ProvenancePath `
	([string]$Attempt.testBuildProvenanceSha256) 'Test build provenance'
$Provenance = Get-Content -Raw -LiteralPath $ProvenancePath | ConvertFrom-Json
Assert-SeinRequiredProperties $Provenance @(
	'schemaVersion', 'profile', 'compileSourceFingerprint', 'engineRoot',
	'engineBuildFingerprint', 'commit', 'dirtyWorkingTree', 'builtAtUtc',
	'dllSha256', 'productionDllSha256', 'metadataSha256') `
	'Test build provenance'
if ([int]$Provenance.schemaVersion -ne 4 -or
	[string]$Provenance.profile -cne $ExpectedProfile -or
	[string]$Provenance.commit -cne [string]$Attempt.commit -or
	[bool]$Provenance.dirtyWorkingTree -ne $false -or
	-not [System.IO.Path]::GetFullPath(
		[string]$Provenance.engineRoot).Equals(
		[System.IO.Path]::GetFullPath([string]$Attempt.engineRoot),
		[System.StringComparison]::OrdinalIgnoreCase) -or
	[string]$Provenance.engineBuildFingerprint -cne
		[string]$Attempt.engineBuildFingerprint -or
	[string]$Provenance.compileSourceFingerprint -cne
		[string]$Attempt.compileSourceFingerprint) {
	throw 'Test build provenance disagrees with the clean attempt receipt.'
}
$BuiltAtUtc = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse(
		[string]$Provenance.builtAtUtc,
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind,
		[ref]$BuiltAtUtc)) {
	throw 'Test build provenance has no valid builtAtUtc timestamp.'
}
Assert-SeinProvenanceMap $Provenance.dllSha256 `
	$ExpectedTestDlls 'Test DLL provenance'
Assert-SeinProvenanceMap $Provenance.productionDllSha256 `
	$ExpectedProductionDlls 'Production DLL provenance'
Assert-SeinProvenanceMap $Provenance.metadataSha256 `
	$ExpectedMetadataFiles 'Build metadata provenance'

$ResolvedEngineRoot = (Resolve-Path -LiteralPath `
	([string]$Attempt.engineRoot)).Path
if (-not [System.IO.Path]::GetFullPath(
		[string]$Attempt.engineRoot).Equals(
		$ResolvedEngineRoot,
		[System.StringComparison]::OrdinalIgnoreCase)) {
	throw 'Attempt engineRoot does not resolve to its recorded location.'
}
$EngineVersionPath = Lock-SeinRequiredFile `
	(Join-Path $ResolvedEngineRoot 'Engine\Build\Build.version') `
	'UE build identity'
$EngineVersion = Get-Content -Raw -LiteralPath $EngineVersionPath |
	ConvertFrom-Json
if ([int]$EngineVersion.MajorVersion -ne 5 -or
	[int]$EngineVersion.MinorVersion -ne 8) {
	throw "Replay Memory Insights qualification requires UE 5.8; '$ResolvedEngineRoot' reports $($EngineVersion.MajorVersion).$($EngineVersion.MinorVersion)."
}
Assert-SeinSha256 $EngineVersionPath `
	([string]$Attempt.engineBuildFingerprint) 'UE build identity'
$InsightsPath = Lock-SeinRequiredFile `
	(Join-Path $ResolvedEngineRoot 'Engine\Binaries\Win64\UnrealInsights.exe') `
	'Unreal Insights executable'
$InsightsSha256 = (Get-FileHash -LiteralPath $InsightsPath `
	-Algorithm SHA256).Hash
if (-not $SelfTest -and $InsightsSha256 -cne $ExpectedInsightsSha256) {
	throw 'Unreal Insights executable does not match the trusted UE 5.8 analyzer identity.'
}

$AttemptName = Split-Path -Leaf $AttemptDirectory
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $DefaultAnalysisRoot $AttemptName
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $RepoRoot $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($TestMode -and -not (Test-SeinPathWithin $OutputDirectory $SelfTestRoot)) {
	throw "Self-test output must be under '$SelfTestRoot'."
}
if (Test-Path -LiteralPath $OutputDirectory) {
	throw "Analysis output already exists: '$OutputDirectory'."
}

$TempParent = Join-Path ([System.IO.Path]::GetPathRoot($RepoRoot)) `
	'SeinARTSReplayMemoryInsights'
if ($TempParent -match '\s') {
	throw "Insights temporary root must not contain whitespace: '$TempParent'."
}
$TempDirectory = Join-Path $TempParent ([Guid]::NewGuid().ToString('N'))
$PublicationStagingDirectory = $null
$AnalyzerOutputLockStart = $null
$TempCsvPath = Join-Path $TempDirectory 'growth.csv'
$TempLogPath = Join-Path $TempDirectory 'insights.log'
New-Item -ItemType Directory -Path $TempDirectory -Force | Out-Null
try {
	$ExportCommand =
		'MemoryInsights.ExportAllocs -Rule=AaBf' +
		' -Output=' + $TempCsvPath +
		' -BookmarkA=' + $BeginBookmark +
		' -BookmarkB=' + $EndBookmark +
		' -Columns=Size,Tag,AllocThread,AllocFunction,AllocSourceFile,AllocCallstack'
	$NativeArguments =
		'-OpenTraceFile="' + $TracePath + '"' +
		' -ABSLOG=' + $TempLogPath +
		' -AutoQuit -NoUI' +
		' -ExecOnAnalysisCompleteCmd="' + $ExportCommand + '"' +
		' -log'
	$InsightsProcess = Start-Process -FilePath $InsightsPath `
		-ArgumentList $NativeArguments -WindowStyle Hidden -PassThru
	if (-not $InsightsProcess.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		Stop-Process -Id $InsightsProcess.Id -Force -ErrorAction SilentlyContinue
		throw "Unreal Insights exceeded $TimeoutMinutes minute(s)."
	}
	$InsightsProcess.Refresh()
	if ($InsightsProcess.ExitCode -ne 0) {
		throw "Unreal Insights exited with code $($InsightsProcess.ExitCode)."
	}
	$AnalyzerOutputLockStart = $QualificationLocks.Count
	$TempCsvPath = Lock-SeinRequiredFile $TempCsvPath `
		'Insights allocation CSV'
	$TempLogPath = Lock-SeinRequiredFile $TempLogPath `
		'Insights analysis log'
	$TempCsvSha256 = (Get-FileHash -LiteralPath $TempCsvPath `
		-Algorithm SHA256).Hash
	$TempLogSha256 = (Get-FileHash -LiteralPath $TempLogPath `
		-Algorithm SHA256).Hash

	$LogText = Get-Content -Raw -LiteralPath $TempLogPath
	if (-not $LogText.Contains($TracePath) -or
		-not $LogText.Contains('-Rule=AaBf') -or
		-not $LogText.Contains("-BookmarkA=$BeginBookmark") -or
		-not $LogText.Contains("-BookmarkB=$EndBookmark")) {
		throw 'Insights log does not bind the requested trace and growth query.'
	}
	$BeginMatches = [regex]::Matches(
		$LogText,
		"Found bookmark '$([regex]::Escape($BeginBookmark))' at time ([0-9]+(?:\.[0-9]+)?)")
	$EndMatches = [regex]::Matches(
		$LogText,
		"Found bookmark '$([regex]::Escape($EndBookmark))' at time ([0-9]+(?:\.[0-9]+)?)")
	$SuccessMatches = [regex]::Matches(
		$LogText,
		'SUCCESS! Exported ([0-9]+) allocations')
	if ($BeginMatches.Count -ne 1 -or
		$EndMatches.Count -ne 1 -or
		$SuccessMatches.Count -ne 1) {
		throw 'Insights did not resolve both bookmarks and one successful export.'
	}
	$BeginTime = ConvertTo-SeinDouble `
		$BeginMatches[0].Groups[1].Value 'Begin bookmark time'
	$EndTime = ConvertTo-SeinDouble `
		$EndMatches[0].Groups[1].Value 'End bookmark time'
	if ($EndTime -le $BeginTime) {
		throw 'Insights bookmark interval is empty or reversed.'
	}

	$HeapEventMatches = [regex]::Matches(
		$LogText,
		'\[MemAlloc\] HeapUnmarkAlloc: Could not find heap.*?Time=([0-9]+(?:\.[0-9]+)?)')
	$HeapSummaryMatches = [regex]::Matches(
		$LogText,
		'\[MemAlloc\] HEAP event errors: ([0-9]+)')
	$HeapErrorCount = 0
	$LatestHeapErrorTime = $null
	if ($HeapEventMatches.Count -gt 0) {
		if ($HeapSummaryMatches.Count -ne 1 -or
			[int]$HeapSummaryMatches[0].Groups[1].Value -ne
				$HeapEventMatches.Count) {
			throw 'Insights heap reconstruction summary disagrees with its events.'
		}
		$HeapErrorCount = $HeapEventMatches.Count
		foreach ($HeapEventMatch in $HeapEventMatches) {
			$HeapErrorTime = ConvertTo-SeinDouble `
				$HeapEventMatch.Groups[1].Value 'Heap reconstruction event time'
			if ($null -eq $LatestHeapErrorTime -or
				$HeapErrorTime -gt $LatestHeapErrorTime) {
				$LatestHeapErrorTime = $HeapErrorTime
			}
		}
		if ($LatestHeapErrorTime -ge $BeginTime) {
			throw 'Insights reported a heap reconstruction error inside the measured interval.'
		}
	} elseif ($HeapSummaryMatches.Count -ne 0) {
		throw 'Insights reported heap errors without attributable event times.'
	}
	$UnexpectedErrors = @(($LogText -split "`r?`n") | Where-Object {
		$_ -match '\bError:' -and
		$_ -notmatch '\[MemAlloc\] HeapUnmarkAlloc:' -and
		$_ -notmatch '\[MemAlloc\] HEAP event errors:'
	})
	if ($UnexpectedErrors.Count -gt 0) {
		throw "Insights emitted unexpected analysis errors: $($UnexpectedErrors[0])"
	}

	$CsvHeader = Get-Content -LiteralPath $TempCsvPath -First 1
	if ($CsvHeader -cne $ExpectedCsvHeader) {
		throw 'Insights allocation CSV has an unexpected column schema.'
	}
	$Rows = @(Import-Csv -LiteralPath $TempCsvPath)
	$ExpectedAllocationCount =
		[int]$SuccessMatches[0].Groups[1].Value
	if ($Rows.Count -ne $ExpectedAllocationCount) {
		throw 'Insights allocation CSV row count disagrees with the export log.'
	}
	[uint64]$RetainedBytes = 0
	[uint64]$ReplayRetainedBytes = 0
	$ReplayRows = [System.Collections.Generic.List[object]]::new()
	$ProductionReplayRows = [System.Collections.Generic.List[object]]::new()
	foreach ($Row in $Rows) {
		[uint64]$Size = 0
		if (-not [uint64]::TryParse([string]$Row.Size, [ref]$Size)) {
			throw "Insights allocation has an invalid size: '$($Row.Size)'."
		}
		if ([uint64]::MaxValue - $RetainedBytes -lt $Size) {
			throw 'Insights retained-byte total overflowed uint64.'
		}
		$RetainedBytes += $Size
		$RecordedCallstack = ([string]$Row.AllocCallstack).Trim()
		if ([string]::IsNullOrWhiteSpace($RecordedCallstack) -or
			$RecordedCallstack -ieq 'No Callstack Recorded') {
			throw 'Full memory trace contains an allocation without a callstack.'
		}
		if ([string]$Row.Tag -like 'SeinARTS/Replay/*') {
			$ReplayRows.Add($Row)
			if ([string]$Row.Tag -cne $QualificationSentinelTag) {
				if ([uint64]::MaxValue - $ReplayRetainedBytes -lt $Size) {
					throw 'Replay retained-byte total overflowed uint64.'
				}
				$ReplayRetainedBytes += $Size
				$ProductionReplayRows.Add($Row)
			}
		}
	}
	$QualificationSentinelRows = @($ReplayRows | Where-Object {
		[string]$_.Tag -ceq $QualificationSentinelTag
	})
	$ExactQualificationSentinelRows = @(
		$QualificationSentinelRows | Where-Object {
			[uint64]$_.Size -eq $QualificationSentinelBytes
		})
	if ($QualificationSentinelRows.Count -lt 1 -or
		$QualificationSentinelRows.Count -gt 2 -or
		$ExactQualificationSentinelRows.Count -ne 1 -or
		@($QualificationSentinelRows | Where-Object {
			-not ([string]$_.AllocCallstack).Contains(
				$QualificationSentinelCallstack) -or
			[uint64]$_.Size -lt $QualificationSentinelBytes -or
			[uint64]$_.Size -gt $QualificationSentinelBytes + 4MB
		}).Count -gt 0) {
		throw 'Replay allocator attribution sentinel is missing or invalid.'
	}
	[uint64]$QualificationSentinelRetainedBytes = 0
	foreach ($QualificationSentinelRow in $QualificationSentinelRows) {
		$QualificationSentinelRetainedBytes +=
			[uint64]$QualificationSentinelRow.Size
	}
	if ($ReplayRetainedBytes -gt $MaximumReplayRetainedBytes) {
		throw "Replay-attributed retained growth is $ReplayRetainedBytes bytes; the qualification ceiling is $MaximumReplayRetainedBytes bytes."
	}

	$ReplayTagSummaries = @($ReplayRows | Group-Object Tag |
		ForEach-Object {
			[uint64]$TagBytes = 0
			foreach ($TagRow in $_.Group) {
				$TagBytes += [uint64]$TagRow.Size
			}
			[pscustomobject][ordered]@{
				tag = $_.Name
				allocationCount = $_.Count
				retainedBytes = $TagBytes
			}
		} | Sort-Object tag)

	Assert-SeinSha256 $QualificationScriptPath `
		$QualificationScriptSha256 'Qualification script'
	Assert-SeinSha256 $AttemptPath $AttemptSha256 'Automation attempt'
	Assert-SeinSha256 $TracePath ([string]$Attempt.traceFileSha256) `
		'Trace file'
	Assert-SeinSha256 $IndexPath ([string]$Attempt.testIndexSha256) `
		'Automation index'
	Assert-SeinSha256 $ProvenancePath `
		([string]$Attempt.testBuildProvenanceSha256) 'Test build provenance'
	Assert-SeinSha256 $EngineVersionPath `
		([string]$Attempt.engineBuildFingerprint) 'UE build identity'
	Assert-SeinSha256 $InsightsPath $InsightsSha256 `
		'Unreal Insights executable'
	if (-not $TestMode) {
		Assert-SeinCurrentCheckout ([string]$Attempt.commit)
	}

	$OutputParent = Split-Path -Parent $OutputDirectory
	New-Item -ItemType Directory -Path $OutputParent -Force | Out-Null
	$OutputName = Split-Path -Leaf $OutputDirectory
	$PublicationStagingDirectory = Join-Path $OutputParent `
		(".$OutputName.staging-" + [Guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $PublicationStagingDirectory | Out-Null
	$CsvPath = Join-Path $PublicationStagingDirectory 'growth.csv'
	$LogPath = Join-Path $PublicationStagingDirectory 'insights.log'
	$ReceiptPath = Join-Path $PublicationStagingDirectory 'receipt.json'
	Copy-Item -LiteralPath $TempCsvPath -Destination $CsvPath
	Copy-Item -LiteralPath $TempLogPath -Destination $LogPath
	$CsvItem = Get-Item -LiteralPath $CsvPath
	$LogItem = Get-Item -LiteralPath $LogPath
	Assert-SeinSha256 $CsvPath $TempCsvSha256 'Staged allocation CSV'
	Assert-SeinSha256 $LogPath $TempLogSha256 'Staged analysis log'
	for ($LockIndex = $QualificationLocks.Count - 1;
		$LockIndex -ge $AnalyzerOutputLockStart;
		--$LockIndex) {
		$QualificationLocks[$LockIndex].Dispose()
		$QualificationLocks.RemoveAt($LockIndex)
	}
	$AnalyzerOutputLockStart = $null
	if (-not (Test-SeinPathWithin $TempDirectory $TempParent)) {
		throw "Refusing to clean temporary analysis outside '$TempParent'."
	}
	Remove-Item -LiteralPath $TempDirectory -Recurse -Force
	$TempDirectory = $null
	$ReceiptStatus = if ($TestMode) { 'SelfTestOnly' } else { 'Qualified' }
	$Receipt = [pscustomobject][ordered]@{
		schemaVersion = 4
		status = $ReceiptStatus
		qualificationMode = if ($SelfTest) {
			'MockAnalyzer'
		} elseif ($AnalyzerTrustSelfTest) {
			'AnalyzerTrustProbe'
		} else {
			'Production'
		}
		generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
		qualificationScript = $QualificationScriptPath
		qualificationScriptSha256 = $QualificationScriptSha256
		attemptId = [string]$Attempt.attemptId
		attemptFile = $AttemptPath
		attemptFileSha256 = $AttemptSha256
		suite = [string]$Attempt.suite
		profile = [string]$Attempt.profile
		commit = [string]$Attempt.commit
		dirtyWorkingTree = [bool]$Attempt.dirtyWorkingTree
		engineRoot = $ResolvedEngineRoot
		engineBuildFingerprint = [string]$Attempt.engineBuildFingerprint
		insightsExecutable = $InsightsPath
		insightsExecutableSha256 = $InsightsSha256
		traceChannels = [string]$Attempt.traceChannels
		traceFile = $TracePath
		traceFileBytes = [int64]$TraceItem.Length
		traceFileSha256 = [string]$Attempt.traceFileSha256
		queryRule = 'AaBf'
		beginBookmark = $BeginBookmark
		beginSeconds = $BeginTime
		endBookmark = $EndBookmark
		endSeconds = $EndTime
		durationSeconds = $EndTime - $BeginTime
		allocationCount = $Rows.Count
		retainedBytes = $RetainedBytes
		recordedCallstackCount = $Rows.Count
		replayAllocationCount = $ProductionReplayRows.Count
		replayRetainedBytes = $ReplayRetainedBytes
		maximumReplayRetainedBytes = $MaximumReplayRetainedBytes
		measurementWarmupCheckpointCount =
			$MeasurementWarmupCheckpointCount
		qualificationSentinelTag = $QualificationSentinelTag
		qualificationSentinelBytes = $QualificationSentinelBytes
		qualificationSentinelAllocationCount =
			$QualificationSentinelRows.Count
		qualificationSentinelRetainedBytes =
			$QualificationSentinelRetainedBytes
		replayTags = $ReplayTagSummaries
		heapReconstructionErrorCount = $HeapErrorCount
		latestHeapReconstructionErrorSeconds = $LatestHeapErrorTime
		heapErrorsPrecedeMeasurement = $true
		growthCsvFile = $CsvItem.Name
		growthCsvBytes = [int64]$CsvItem.Length
		growthCsvSha256 = $TempCsvSha256
		insightsLogFile = $LogItem.Name
		insightsLogBytes = [int64]$LogItem.Length
		insightsLogSha256 = $TempLogSha256
		testIndexFile = $IndexPath
		testIndexSha256 = [string]$Attempt.testIndexSha256
		testBuildProvenanceFile = $ProvenancePath
		testBuildProvenanceSha256 =
			[string]$Attempt.testBuildProvenanceSha256
	}
	[System.IO.File]::WriteAllText(
		$ReceiptPath,
		($Receipt | ConvertTo-Json -Depth 8),
		[System.Text.UTF8Encoding]::new($false))
	Assert-SeinSha256 $CsvPath $TempCsvSha256 `
		'Final staged allocation CSV'
	Assert-SeinSha256 $LogPath $TempLogSha256 `
		'Final staged analysis log'
	[System.IO.Directory]::Move(
		$PublicationStagingDirectory,
		$OutputDirectory)
	$PublicationStagingDirectory = $null
	$ReceiptPath = Join-Path $OutputDirectory 'receipt.json'
	if (-not (Test-Path -LiteralPath $ReceiptPath -PathType Leaf)) {
		throw 'Atomic publication did not produce the final receipt path.'
	}
	Write-Host `
		"[ReplayMemoryInsights] $ReceiptStatus $($Rows.Count) retained allocation(s); replay-attributed growth $ReplayRetainedBytes / $MaximumReplayRetainedBytes bytes." `
		-ForegroundColor Green
	Write-Host $ReceiptPath
}
finally {
	if ($null -ne $AnalyzerOutputLockStart) {
		for ($LockIndex = $QualificationLocks.Count - 1;
			$LockIndex -ge $AnalyzerOutputLockStart;
			--$LockIndex) {
			$QualificationLocks[$LockIndex].Dispose()
			$QualificationLocks.RemoveAt($LockIndex)
		}
	}
	if ($TempDirectory -and (Test-Path -LiteralPath $TempDirectory)) {
		if (-not (Test-SeinPathWithin $TempDirectory $TempParent)) {
			throw "Refusing to clean temporary analysis outside '$TempParent'."
		}
		Remove-Item -LiteralPath $TempDirectory -Recurse -Force
	}
	if ($PublicationStagingDirectory -and
		(Test-Path -LiteralPath $PublicationStagingDirectory)) {
		if (-not (Test-SeinPathWithin `
				$PublicationStagingDirectory (Split-Path -Parent $OutputDirectory))) {
			throw 'Refusing to clean publication staging outside its output parent.'
		}
		Remove-Item -LiteralPath $PublicationStagingDirectory -Recurse -Force
	}
}
}
finally {
	for ($Index = $QualificationLocks.Count - 1; $Index -ge 0; --$Index) {
		$QualificationLocks[$Index].Dispose()
	}
}
