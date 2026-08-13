#Requires -Version 5.1
[CmdletBinding()]
param(
	[switch] $KeepFixtures
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Exporter = Join-Path $PSScriptRoot 'Export-ReplayMemoryInsights.ps1'
$WindowsPowerShell = Join-Path $env:SystemRoot `
	'System32\WindowsPowerShell\v1.0\powershell.exe'
$FixtureParent = Join-Path $RepoRoot `
	'Saved\Automation\ReplayMemoryInsightsSelfTest'
$FixtureRoot = Join-Path $FixtureParent ([Guid]::NewGuid().ToString('N'))
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

function Write-Utf8NoBom([string] $Path, [string] $Text)
{
	$Parent = Split-Path -Parent $Path
	if ($Parent) {
		New-Item -ItemType Directory -Path $Parent -Force | Out-Null
	}
	[System.IO.File]::WriteAllText(
		$Path,
		$Text,
		[System.Text.UTF8Encoding]::new($false))
}

function Write-Json([string] $Path, [object] $Value)
{
	Write-Utf8NoBom $Path ($Value | ConvertTo-Json -Depth 8)
}

function New-ArtifactHashMap([string[]] $RelativePaths)
{
	$Result = [ordered]@{}
	foreach ($RelativePath in $RelativePaths) {
		$ArtifactPath = Join-Path $RepoRoot $RelativePath
		if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) {
			throw "Self-test requires built artifact '$ArtifactPath'."
		}
		$Result[$RelativePath] = (Get-FileHash -LiteralPath $ArtifactPath `
			-Algorithm SHA256).Hash
	}
	return $Result
}

function New-MockEngine([string] $Root)
{
	$VersionPath = Join-Path $Root 'Engine\Build\Build.version'
	Write-Json $VersionPath ([ordered]@{
		MajorVersion = 5
		MinorVersion = 8
	})
	$InsightsPath = Join-Path $Root `
		'Engine\Binaries\Win64\UnrealInsights.exe'
	New-Item -ItemType Directory -Path (Split-Path -Parent $InsightsPath) `
		-Force | Out-Null
	$CompilerCandidates = @(
		(Join-Path $env:WINDIR `
			'Microsoft.NET\Framework64\v4.0.30319\csc.exe'),
		(Join-Path $env:WINDIR `
			'Microsoft.NET\Framework\v4.0.30319\csc.exe'))
	$Compiler = @($CompilerCandidates | Where-Object {
		Test-Path -LiteralPath $_ -PathType Leaf
	} | Select-Object -First 1)
	if ($Compiler.Count -ne 1) {
		throw 'Replay Memory Insights self-test requires .NET Framework csc.exe.'
	}
	$Source = @'
using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

public static class MockUnrealInsights
{
    private static string FindValue(string[] args, string prefix)
    {
        foreach (string arg in args)
        {
            if (arg.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return arg.Substring(prefix.Length).Trim('"');
            }
        }
        return null;
    }

    public static int Main(string[] args)
    {
        string trace = FindValue(args, "-OpenTraceFile=");
        string log = FindValue(args, "-ABSLOG=");
        string command = FindValue(args, "-ExecOnAnalysisCompleteCmd=");
        if (String.IsNullOrEmpty(trace) || String.IsNullOrEmpty(log) ||
            String.IsNullOrEmpty(command))
        {
            return 2;
        }
		Match outputMatch = Regex.Match(command, @"-Output=([^\s""]+)");
        if (!outputMatch.Success)
        {
            return 3;
        }
        string csv = outputMatch.Groups[1].Value;
		ulong size = trace.IndexOf("Oversized", StringComparison.OrdinalIgnoreCase) >= 0
			? 8388608UL
			: 80UL;
		string callstack = trace.IndexOf("NoCallstack", StringComparison.OrdinalIgnoreCase) >= 0
			? "  no callstack recorded  "
			: "Mock!Resolved";
		string tag = "SeinARTS/Replay/DurableAppend";
		string sentinelTag = trace.IndexOf("NoReplayTag", StringComparison.OrdinalIgnoreCase) >= 0
			? "Untagged"
			: "SeinARTS/Replay/Qualification/Sentinel";
		bool emptyExport = trace.IndexOf("EmptyExport", StringComparison.OrdinalIgnoreCase) >= 0;
		bool twoRowSentinel = trace.IndexOf("TwoRowSentinel", StringComparison.OrdinalIgnoreCase) >= 0;
		string sentinelRows =
			"16777280," + sentinelTag + ",2,Mock!Allocate,Mock.cpp,Mock!ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded\r\n" +
			(twoRowSentinel
				? "18874368," + sentinelTag + ",2,Mock!Allocate,Mock.cpp,Mock!ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded\r\n"
				: "");
		Directory.CreateDirectory(Path.GetDirectoryName(csv));
		File.WriteAllText(
			csv,
			emptyExport
				? "Size,Tag,AllocThread,AllocFunction,AllocSourceFile,AllocCallstack\r\n"
				: "Size,Tag,AllocThread,AllocFunction,AllocSourceFile,AllocCallstack\r\n" +
				  sentinelRows +
				  size.ToString() +
				  "," + tag + ",2,Mock!Allocate,Mock.cpp," + callstack + "\r\n",
            new UTF8Encoding(false));
        Directory.CreateDirectory(Path.GetDirectoryName(log));
        string logText =
            "LogInit: Command Line: -OpenTraceFile=\"" + trace + "\" " + command + "\r\n" +
            "LogMemoryExporter: Found bookmark 'Sein.ReplayOperationalSoak.Begin' at time 10.000\r\n" +
            "LogMemoryExporter: Found bookmark 'Sein.ReplayOperationalSoak.End' at time 20.000\r\n" +
            "LogMemoryProfiler: SUCCESS! Exported " +
			(emptyExport ? "0" : (twoRowSentinel ? "3" : "2")) +
			" allocations\r\n";
        File.WriteAllText(log, logText, new UTF8Encoding(false));
        return 0;
    }
}
'@
	$SourcePath = Join-Path $Root 'MockUnrealInsights.cs'
	Write-Utf8NoBom $SourcePath $Source
	& $Compiler[0] /nologo /target:exe "/out:$InsightsPath" $SourcePath
	if ($LASTEXITCODE -ne 0 -or
		-not (Test-Path -LiteralPath $InsightsPath -PathType Leaf)) {
		throw "Could not compile the mock Unreal Insights executable with '$($Compiler[0])'."
	}
	return [pscustomobject]@{
		Root = $Root
		VersionPath = $VersionPath
		VersionHash = (Get-FileHash -LiteralPath $VersionPath `
			-Algorithm SHA256).Hash
	}
}

function New-AttemptFixture(
	[string] $Root,
	[object] $Engine,
	[string] $TraceName)
{
	New-Item -ItemType Directory -Path $Root -Force | Out-Null
	$TracePath = Join-Path $Root $TraceName
	[System.IO.File]::WriteAllBytes(
		$TracePath,
		[System.Text.Encoding]::ASCII.GetBytes('mock-trace'))
	$IndexPath = Join-Path $Root 'index.json'
	Write-Json $IndexPath ([ordered]@{
		succeeded = 1
		succeededWithWarnings = 0
		failed = 0
		notRun = 0
		inProcess = 0
		tests = @([ordered]@{
			testDisplayName = 'Replay operational soak'
			fullTestPath = 'SeinARTS.Perf.Replay.OperationalSoak.ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded.ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded_Method'
			state = 'Success'
		})
	})
	$ProvenancePath = Join-Path $Root 'build-provenance.json'
	$Commit = '0123456789abcdef0123456789abcdef01234567'
	$CompileFingerprint = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
	Write-Json $ProvenancePath ([ordered]@{
		schemaVersion = 4
		profile = 'Framework'
		commit = $Commit
		dirtyWorkingTree = $false
		builtAtUtc = (Get-Date).ToUniversalTime().ToString('o')
		engineRoot = $Engine.Root
		engineBuildFingerprint = $Engine.VersionHash
		compileSourceFingerprint = $CompileFingerprint
		dllSha256 = New-ArtifactHashMap $ExpectedTestDlls
		productionDllSha256 = New-ArtifactHashMap $ExpectedProductionDlls
		metadataSha256 = New-ArtifactHashMap $ExpectedMetadataFiles
	})
	$AttemptPath = Join-Path $Root 'attempt.json'
	Write-Json $AttemptPath ([ordered]@{
		schemaVersion = 4
		attemptId = [Guid]::NewGuid().ToString('N')
		suite = 'SeinARTS.Perf.Replay.OperationalSoak'
		profile = 'Framework'
		commit = $Commit
		dirtyWorkingTree = $false
		status = 'Passed'
		traceChannels = 'default,memory,metadata'
		traceFile = $TracePath
		traceFileBytes = (Get-Item -LiteralPath $TracePath).Length
		traceFileSha256 = (Get-FileHash -LiteralPath $TracePath `
			-Algorithm SHA256).Hash
		engineRoot = $Engine.Root
		engineBuildFingerprint = $Engine.VersionHash
		compileSourceFingerprint = $CompileFingerprint
		testBuildProvenance = 'BuiltThisAttempt'
		testBuildProvenanceFile = 'build-provenance.json'
		testBuildProvenanceSha256 = (Get-FileHash `
			-LiteralPath $ProvenancePath -Algorithm SHA256).Hash
		editorExitCode = 0
		expectedMinimumCount = 1
		expectedCountSource = 'CommandLine'
		discoveredTestCount = 1
		unsuccessfulTestCount = 0
		testIndexFile = 'index.json'
		testIndexSha256 = (Get-FileHash -LiteralPath $IndexPath `
			-Algorithm SHA256).Hash
		reportPath = $Root
	})
	return $AttemptPath
}

function Invoke-Exporter(
	[string] $Attempt,
	[string] $Output,
	[switch] $Production,
	[switch] $AnalyzerTrust)
{
	if ($Production -and $AnalyzerTrust) {
		throw 'Choose only one exporter invocation mode.'
	}
	$PreviousErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = 'Continue'
		$ModeArguments = @()
		if ($AnalyzerTrust) {
			$ModeArguments += '-AnalyzerTrustSelfTest'
		} elseif (-not $Production) {
			$ModeArguments += '-SelfTest'
		}
		$OutputLines = @(& $WindowsPowerShell `
			-NoProfile `
			-ExecutionPolicy Bypass `
			-File $Exporter `
			-AttemptPath $Attempt `
			-OutputDirectory $Output `
			@ModeArguments 2>&1)
		$ExitCode = $LASTEXITCODE
	}
	finally {
		$ErrorActionPreference = $PreviousErrorActionPreference
	}
	return [pscustomobject]@{
		ExitCode = $ExitCode
		Output = $OutputLines -join "`r`n"
	}
}

function Update-AttemptArtifactHash(
	[string] $AttemptPath,
	[string] $AttemptProperty,
	[string] $ArtifactPath)
{
	$Attempt = Get-Content -Raw -LiteralPath $AttemptPath | ConvertFrom-Json
	$Attempt.$AttemptProperty = (Get-FileHash -LiteralPath $ArtifactPath `
		-Algorithm SHA256).Hash
	Write-Json $AttemptPath $Attempt
}

if (-not (Test-Path -LiteralPath $Exporter -PathType Leaf) -or
	-not (Test-Path -LiteralPath $WindowsPowerShell -PathType Leaf)) {
	throw 'Replay Memory Insights self-test requires the exporter and Windows PowerShell 5.1.'
}

New-Item -ItemType Directory -Path $FixtureRoot -Force | Out-Null
try {
	$Engine = New-MockEngine (Join-Path $FixtureRoot 'FakeUE58')

	$PassAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'Pass') $Engine 'ReplayPass.utrace'
	$PassOutput = Join-Path $FixtureRoot 'PassOutput'
	$Pass = Invoke-Exporter $PassAttempt $PassOutput
	$PassReceiptPath = Join-Path $PassOutput 'receipt.json'
	if ($Pass.ExitCode -ne 0 -or
		-not (Test-Path -LiteralPath $PassReceiptPath -PathType Leaf)) {
		throw "Valid fixture failed qualification: $($Pass.Output)"
	}
	$PassReceipt = Get-Content -Raw -LiteralPath $PassReceiptPath |
		ConvertFrom-Json
	if ([int]$PassReceipt.schemaVersion -ne 4 -or
		[string]$PassReceipt.status -cne 'SelfTestOnly' -or
		[string]$PassReceipt.qualificationMode -cne 'MockAnalyzer' -or
		-not [System.IO.Path]::GetFullPath(
			[string]$PassReceipt.qualificationScript).Equals(
			[System.IO.Path]::GetFullPath($Exporter),
			[System.StringComparison]::OrdinalIgnoreCase) -or
		[string]$PassReceipt.qualificationScriptSha256 -cne
			(Get-FileHash -LiteralPath $Exporter -Algorithm SHA256).Hash -or
		[int]$PassReceipt.allocationCount -ne 2 -or
		[int]$PassReceipt.recordedCallstackCount -ne 2 -or
		[int]$PassReceipt.replayAllocationCount -ne 1 -or
		[int64]$PassReceipt.replayRetainedBytes -ne 80 -or
		[int]$PassReceipt.measurementWarmupCheckpointCount -ne 8 -or
		[int64]$PassReceipt.qualificationSentinelBytes -ne 16777280 -or
		[int]$PassReceipt.qualificationSentinelAllocationCount -ne 1 -or
		[int64]$PassReceipt.qualificationSentinelRetainedBytes -ne 16777280 -or
		[int]$PassReceipt.heapReconstructionErrorCount -ne 0) {
		throw 'Valid fixture produced an unexpected replay-memory receipt.'
	}
	$TwoRowAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'TwoRowSentinel') `
		$Engine 'ReplayTwoRowSentinel.utrace'
	$TwoRowOutput = Join-Path $FixtureRoot 'TwoRowSentinelOutput'
	$TwoRow = Invoke-Exporter $TwoRowAttempt $TwoRowOutput
	$TwoRowReceiptPath = Join-Path $TwoRowOutput 'receipt.json'
	if ($TwoRow.ExitCode -ne 0 -or
		-not (Test-Path -LiteralPath $TwoRowReceiptPath -PathType Leaf)) {
		throw "Two-row sentinel fixture failed qualification: $($TwoRow.Output)"
	}
	$TwoRowReceipt = Get-Content -Raw -LiteralPath $TwoRowReceiptPath |
		ConvertFrom-Json
	if ([int]$TwoRowReceipt.allocationCount -ne 3 -or
		[int]$TwoRowReceipt.replayAllocationCount -ne 1 -or
		[int64]$TwoRowReceipt.replayRetainedBytes -ne 80 -or
		[int]$TwoRowReceipt.qualificationSentinelAllocationCount -ne 2 -or
		[int64]$TwoRowReceipt.qualificationSentinelRetainedBytes -ne
			35651648) {
		throw 'Two-row sentinel fixture produced an unexpected receipt.'
	}
	$FakeProductionOutput = Join-Path $FixtureRoot 'FakeProductionOutput'
	$FakeProduction = Invoke-Exporter `
		$PassAttempt $FakeProductionOutput -Production
	if ($FakeProduction.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $FakeProductionOutput) -or
		$FakeProduction.Output -notmatch
			'Production qualification rejects replay-memory self-test evidence') {
		throw 'Production mode did not reject self-test evidence at its boundary.'
	}
	$AnalyzerTrustOutput = Join-Path $FixtureRoot 'AnalyzerTrustOutput'
	$AnalyzerTrust = Invoke-Exporter `
		$PassAttempt $AnalyzerTrustOutput -AnalyzerTrust
	if ($AnalyzerTrust.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $AnalyzerTrustOutput) -or
		$AnalyzerTrust.Output -notmatch
			'trusted UE 5.8 analyzer identity') {
		throw 'Mock analyzer minted a production qualification receipt.'
	}

	$OversizedAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'Oversized') $Engine 'ReplayOversized.utrace'
	$OversizedOutput = Join-Path $FixtureRoot 'OversizedOutput'
	$Oversized = Invoke-Exporter `
		$OversizedAttempt $OversizedOutput
	if ($Oversized.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $OversizedOutput)) {
		throw 'Oversized replay retention did not fail before publication.'
	}

	$NoCallstackAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'NoCallstack') $Engine 'ReplayNoCallstack.utrace'
	$NoCallstackOutput = Join-Path $FixtureRoot 'NoCallstackOutput'
	$NoCallstack = Invoke-Exporter $NoCallstackAttempt $NoCallstackOutput
	if ($NoCallstack.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $NoCallstackOutput)) {
		throw 'Missing-callstack evidence did not fail before publication.'
	}
	$NoReplayTagAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'NoReplayTag') $Engine 'ReplayNoReplayTag.utrace'
	$NoReplayTagOutput = Join-Path $FixtureRoot 'NoReplayTagOutput'
	$NoReplayTag = Invoke-Exporter $NoReplayTagAttempt $NoReplayTagOutput
	if ($NoReplayTag.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $NoReplayTagOutput)) {
		throw 'Missing replay-attribution sentinel did not fail before publication.'
	}

	$EmptyExportAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'EmptyExport') $Engine 'ReplayEmptyExport.utrace'
	$EmptyExportOutput = Join-Path $FixtureRoot 'EmptyExportOutput'
	$EmptyExport = Invoke-Exporter $EmptyExportAttempt $EmptyExportOutput
	if ($EmptyExport.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $EmptyExportOutput)) {
		throw 'Empty allocator evidence did not fail before publication.'
	}

	$TamperedAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'Tampered') $Engine 'ReplayTampered.utrace'
	$TamperedReceipt = Get-Content -Raw -LiteralPath $TamperedAttempt |
		ConvertFrom-Json
	$TamperedStream = [System.IO.File]::Open(
		[string]$TamperedReceipt.traceFile,
		[System.IO.FileMode]::Append,
		[System.IO.FileAccess]::Write,
		[System.IO.FileShare]::None)
	try {
		$TamperedStream.WriteByte(0)
	}
	finally {
		$TamperedStream.Dispose()
	}
	$TamperedOutput = Join-Path $FixtureRoot 'TamperedOutput'
	$Tampered = Invoke-Exporter `
		$TamperedAttempt $TamperedOutput
	if ($Tampered.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $TamperedOutput)) {
		throw 'Tampered trace did not fail before Insights execution.'
	}

	$ForeignIndexAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'ForeignIndex') $Engine 'ReplayForeignIndex.utrace'
	$ForeignIndexReceipt = Get-Content -Raw -LiteralPath $ForeignIndexAttempt |
		ConvertFrom-Json
	$ForeignIndexReceipt.testIndexFile = '..\Pass\index.json'
	Write-Json $ForeignIndexAttempt $ForeignIndexReceipt
	$ForeignIndexOutput = Join-Path $FixtureRoot 'ForeignIndexOutput'
	$ForeignIndex = Invoke-Exporter $ForeignIndexAttempt $ForeignIndexOutput
	if ($ForeignIndex.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $ForeignIndexOutput)) {
		throw 'Foreign index reference did not fail before publication.'
	}

	$WrongTestAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'WrongTest') $Engine 'ReplayWrongTest.utrace'
	$WrongTestIndex = Join-Path (Split-Path -Parent $WrongTestAttempt) 'index.json'
	$WrongTestIndexValue = Get-Content -Raw -LiteralPath $WrongTestIndex |
		ConvertFrom-Json
	$WrongTestIndexValue.tests[0].fullTestPath =
		'SeinARTS.Perf.Replay.OperationalSoak.Wrong'
	Write-Json $WrongTestIndex $WrongTestIndexValue
	Update-AttemptArtifactHash `
		$WrongTestAttempt 'testIndexSha256' $WrongTestIndex
	$WrongTestOutput = Join-Path $FixtureRoot 'WrongTestOutput'
	$WrongTest = Invoke-Exporter $WrongTestAttempt $WrongTestOutput
	if ($WrongTest.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $WrongTestOutput)) {
		throw 'Wrong operational-soak test identity did not fail.'
	}

	$MissingTimestampAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'MissingTimestamp') $Engine 'ReplayMissingTimestamp.utrace'
	$MissingTimestampProvenance = Join-Path `
		(Split-Path -Parent $MissingTimestampAttempt) 'build-provenance.json'
	$MissingTimestampValue = Get-Content -Raw `
		-LiteralPath $MissingTimestampProvenance | ConvertFrom-Json
	$MissingTimestampValue.PSObject.Properties.Remove('builtAtUtc')
	Write-Json $MissingTimestampProvenance $MissingTimestampValue
	Update-AttemptArtifactHash $MissingTimestampAttempt `
		'testBuildProvenanceSha256' $MissingTimestampProvenance
	$MissingTimestampOutput = Join-Path $FixtureRoot 'MissingTimestampOutput'
	$MissingTimestamp = Invoke-Exporter `
		$MissingTimestampAttempt $MissingTimestampOutput
	if ($MissingTimestamp.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $MissingTimestampOutput)) {
		throw 'Missing provenance timestamp did not fail.'
	}

	$MissingMapAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'MissingMap') $Engine 'ReplayMissingMap.utrace'
	$MissingMapProvenance = Join-Path `
		(Split-Path -Parent $MissingMapAttempt) 'build-provenance.json'
	$MissingMapValue = Get-Content -Raw -LiteralPath $MissingMapProvenance |
		ConvertFrom-Json
	$MissingMapValue.productionDllSha256.PSObject.Properties.Remove(
		$ExpectedProductionDlls[0])
	Write-Json $MissingMapProvenance $MissingMapValue
	Update-AttemptArtifactHash `
		$MissingMapAttempt 'testBuildProvenanceSha256' $MissingMapProvenance
	$MissingMapOutput = Join-Path $FixtureRoot 'MissingMapOutput'
	$MissingMap = Invoke-Exporter $MissingMapAttempt $MissingMapOutput
	if ($MissingMap.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $MissingMapOutput)) {
		throw 'Incomplete production provenance map did not fail.'
	}

	$StaleHashAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'StaleHash') $Engine 'ReplayStaleHash.utrace'
	$StaleHashProvenance = Join-Path `
		(Split-Path -Parent $StaleHashAttempt) 'build-provenance.json'
	$StaleHashValue = Get-Content -Raw -LiteralPath $StaleHashProvenance |
		ConvertFrom-Json
	$StaleHashValue.productionDllSha256.PSObject.Properties[
		$ExpectedProductionDlls[0]].Value = '0' * 64
	Write-Json $StaleHashProvenance $StaleHashValue
	Update-AttemptArtifactHash `
		$StaleHashAttempt 'testBuildProvenanceSha256' $StaleHashProvenance
	$StaleHashOutput = Join-Path $FixtureRoot 'StaleHashOutput'
	$StaleHash = Invoke-Exporter $StaleHashAttempt $StaleHashOutput
	if ($StaleHash.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $StaleHashOutput)) {
		throw 'Stale live artifact hash did not fail.'
	}

	$MissingFalseAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'MissingFalse') $Engine 'ReplayMissingFalse.utrace'
	$MissingFalseValue = Get-Content -Raw -LiteralPath $MissingFalseAttempt |
		ConvertFrom-Json
	$MissingFalseValue.PSObject.Properties.Remove('dirtyWorkingTree')
	Write-Json $MissingFalseAttempt $MissingFalseValue
	$MissingFalseOutput = Join-Path $FixtureRoot 'MissingFalseOutput'
	$MissingFalse = Invoke-Exporter $MissingFalseAttempt $MissingFalseOutput
	if ($MissingFalse.ExitCode -eq 0 -or
		(Test-Path -LiteralPath $MissingFalseOutput)) {
		throw 'Missing false-valued attempt property did not fail.'
	}

	$CollisionAttempt = New-AttemptFixture `
		(Join-Path $FixtureRoot 'Collision') $Engine 'ReplayCollision.utrace'
	$CollisionOutput = Join-Path $FixtureRoot 'CollisionOutput'
	New-Item -ItemType Directory -Path $CollisionOutput | Out-Null
	$Collision = Invoke-Exporter $CollisionAttempt $CollisionOutput
	if ($Collision.ExitCode -eq 0) {
		throw 'Preexisting publication destination did not fail.'
	}
	$StagingResidue = @(Get-ChildItem -LiteralPath $FixtureRoot `
		-Directory -Recurse -Filter '*.staging-*')
	if ($StagingResidue.Count -ne 0) {
		throw 'Replay-memory qualification left publication staging residue.'
	}

	Write-Host `
		'[ReplayMemoryInsightsSelfTest] Windows PowerShell 5.1 pass, analyzer trust, retention, callstack, replay-sentinel, empty-evidence, tamper, provenance, property-presence, and publication fixtures succeeded.' `
		-ForegroundColor Green
}
finally {
	if (-not $KeepFixtures -and (Test-Path -LiteralPath $FixtureRoot)) {
		$ResolvedFixtureRoot = [System.IO.Path]::GetFullPath($FixtureRoot)
		$ResolvedFixtureParent = [System.IO.Path]::GetFullPath(
			$FixtureParent).TrimEnd('\', '/')
		if (-not $ResolvedFixtureRoot.StartsWith(
			$ResolvedFixtureParent +
				[System.IO.Path]::DirectorySeparatorChar,
			[System.StringComparison]::OrdinalIgnoreCase)) {
			throw "Refusing to clean fixture outside '$ResolvedFixtureParent'."
		}
		Remove-Item -LiteralPath $ResolvedFixtureRoot -Recurse -Force
	}
}
