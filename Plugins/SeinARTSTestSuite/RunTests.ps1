#Requires -Version 5.1
<#
.SYNOPSIS
  Build and run a selected SeinARTS Automation suite.

.EXAMPLE
  .\RunTests.ps1
  # Builds the disabled test plugin, then runs SeinARTS.Unit headlessly.

.EXAMPLE
  .\RunTests.ps1 -Suite SeinARTS.Determinism -SkipBuild
#>
[CmdletBinding()]
param(
	[ValidateNotNullOrEmpty()]
	[string] $Suite = 'SeinARTS.Unit',

	[ValidateSet('Framework', 'All')]
	[string] $Profile = 'All',

	[switch] $SkipBuild,

	[switch] $KeepRendering,

	[switch] $AllowKnownStartupErrors,

	[ValidateRange(0, 100000)]
	[int] $MinimumExpectedTests = 0,

	[ValidateRange(30, 7200)]
	[int] $TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$PluginRoot = $PSScriptRoot
$ProjectRoot = (Resolve-Path (Join-Path $PluginRoot '..\..')).Path
$Uproject = Join-Path $ProjectRoot 'SeinARTS.uproject'
$BuildScript = Join-Path $ProjectRoot 'Build.ps1'
$EditorCmd = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$SafeSuite = $Suite -replace '[^A-Za-z0-9_.-]', '_'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$AttemptId = [Guid]::NewGuid().ToString('N')
$ReportPath = Join-Path $ProjectRoot "Saved\Automation\$SafeSuite-$Stamp-$($AttemptId.Substring(0, 8))"
$LogPath = Join-Path $ReportPath 'Automation.log'
$StdoutPath = Join-Path $ReportPath 'EditorStdout.log'
$StderrPath = Join-Path $ReportPath 'EditorStderr.log'
$AttemptPath = Join-Path $ReportPath 'attempt.json'
$ExpectedCountsPath = Join-Path $PluginRoot 'ExpectedTestCounts.json'
New-Item -ItemType Directory -Path $ReportPath -Force | Out-Null

$GitCommit = 'unavailable'
$GitDirty = $null
$GitCommitOutput = & git -C $ProjectRoot rev-parse HEAD 2>$null
if ($LASTEXITCODE -eq 0 -and $GitCommitOutput) {
	$GitCommit = ($GitCommitOutput | Select-Object -First 1).Trim()
	$GitStatus = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=normal 2>$null)
	if ($LASTEXITCODE -eq 0) {
		$GitDirty = $GitStatus.Count -gt 0
	}
}

$Attempt = [pscustomobject][ordered]@{
	schemaVersion = 1
	attemptId = $AttemptId
	suite = $Suite
	profile = $Profile
	commit = $GitCommit
	dirtyWorkingTree = $GitDirty
	startedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
	completedAtUtc = $null
	status = 'Started'
	timeoutSeconds = $TimeoutSeconds
	skipBuild = [bool]$SkipBuild
	keepRendering = [bool]$KeepRendering
	allowKnownStartupErrors = [bool]$AllowKnownStartupErrors
	expectedMinimumCount = $null
	expectedCountSource = $null
	expectedBaselineCommit = $null
	editorProcessId = $null
	editorExitCode = $null
	discoveredTestCount = $null
	unsuccessfulTestCount = $null
	failure = $null
	reportPath = $ReportPath
}

function Write-SeinAttemptManifest
{
	$Attempt | ConvertTo-Json -Depth 6 |
		Set-Content -LiteralPath $AttemptPath -Encoding UTF8
}

Write-SeinAttemptManifest

try {

if (-not (Test-Path -LiteralPath $EditorCmd)) {
	throw "UE 5.7 command-line editor was not found at '$EditorCmd'."
}

$ExtensionPlugins = @(
	'SeinARTSSquadExtension',
	'SeinARTSCoverExtension',
	'SeinARTSCoverSquadExtension',
	'SeinARTSMovementPlusExtension'
)
$EnabledTestPlugins = @('SeinARTSTestSuite')
$BuildArgs = @('-EnablePlugin=SeinARTSTestSuite')

if ($Profile -eq 'All') {
	$EnabledTestPlugins += 'SeinARTSExtensionTestSuite'
	$BuildArgs = @('-EnablePlugin=SeinARTSTestSuite+SeinARTSExtensionTestSuite')
}
else {
	$BuildArgs += '-DisablePlugin=' + (($ExtensionPlugins + 'SeinARTSExtensionTestSuite') -join '+')
}

if (-not $SkipBuild) {
	$TestBuildExitCode = $null
	$ReceiptRestoreExitCode = $null
	try {
		& $BuildScript -ExtraArgs $BuildArgs
		$TestBuildExitCode = $LASTEXITCODE
	}
	finally {
		# UBT writes profile plugin states into the shared editor receipt. Restore the ordinary
		# project receipt before launching Automation so normal Editor startup is never contaminated.
		& $BuildScript
		$ReceiptRestoreExitCode = $LASTEXITCODE
	}

	if ($TestBuildExitCode -ne 0) {
		throw "Test-enabled editor build failed with exit code $TestBuildExitCode."
	}
	if ($ReceiptRestoreExitCode -ne 0) {
		throw "Tests compiled, but restoring the ordinary editor receipt failed with exit code $ReceiptRestoreExitCode."
	}
}

$RequiredTestDlls = @(
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSTestSupport.dll',
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSFrameworkTests.dll',
	'Plugins\SeinARTSTestSuite\Binaries\Win64\UnrealEditor-SeinARTSEditorTests.dll'
)
if ($Profile -eq 'All') {
	$RequiredTestDlls += @(
		'Plugins\SeinARTSExtensionTestSuite\Binaries\Win64\UnrealEditor-SeinARTSExtensionTests.dll',
		'Plugins\SeinARTSExtensionTestSuite\Binaries\Win64\UnrealEditor-SeinARTSExtensionEditorTests.dll'
	)
}
foreach ($RelativeDll in $RequiredTestDlls) {
	if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot $RelativeDll))) {
		throw "Missing test module '$RelativeDll'. Run again without -SkipBuild."
	}
}

$ReceiptPath = Join-Path $ProjectRoot 'Binaries\Win64\SeinARTSEditor.target'
if (-not (Test-Path -LiteralPath $ReceiptPath)) {
	throw "No ordinary editor target receipt exists. Run again without -SkipBuild."
}
$Receipt = Get-Content -Raw -LiteralPath $ReceiptPath | ConvertFrom-Json
if ($Receipt.PSObject.Properties.Name -notcontains 'BuildPlugins') {
	throw "The editor target receipt has no BuildPlugins field; cannot verify normal-startup plugin isolation."
}
$UnexpectedTestPlugins = @(
	'SeinARTSTestSuite',
	'SeinARTSExtensionTestSuite'
) | Where-Object { $Receipt.BuildPlugins -contains $_ }
if ($UnexpectedTestPlugins.Count -gt 0) {
	throw "The shared editor receipt still includes test plugin(s): $($UnexpectedTestPlugins -join ', '). Run Build.ps1 once to restore normal startup, then retry."
}

$ExpectedFloor = $null
if ($MinimumExpectedTests -gt 0) {
	$ExpectedFloor = $MinimumExpectedTests
	$Attempt.expectedCountSource = 'CommandLine'
}
else {
	if (-not (Test-Path -LiteralPath $ExpectedCountsPath)) {
		throw "Missing checked-in expected-count baseline '$ExpectedCountsPath'."
	}
	$ExpectedCounts = Get-Content -Raw -LiteralPath $ExpectedCountsPath |
		ConvertFrom-Json
	if ($ExpectedCounts.schemaVersion -ne 1) {
		throw "Unsupported expected-count schema in '$ExpectedCountsPath'."
	}
	$MatchingBaselines = @($ExpectedCounts.baselines | Where-Object {
		$_.suite -ieq $Suite -and $_.profile -ieq $Profile
	})
	if ($MatchingBaselines.Count -gt 1) {
		throw "Expected-count baseline contains duplicate '$Profile' / '$Suite' entries."
	}
	if ($MatchingBaselines.Count -eq 1) {
		$SelectedBaseline = $MatchingBaselines[0]
		$ExpectedFloor = [int]$SelectedBaseline.minimumCount
		if ($ExpectedFloor -le 0) {
			throw "Expected-count baseline for '$Profile' / '$Suite' must be positive."
		}
		$BaselineCommit = [string]$SelectedBaseline.establishedAtCommit
		if ($BaselineCommit -notmatch '^[0-9a-fA-F]{40}$') {
			throw "Expected-count baseline for '$Profile' / '$Suite' has an invalid establishedAtCommit."
		}
		if ($GitCommit -eq 'unavailable') {
			throw "Cannot validate expected-count baseline ancestry because the current git commit is unavailable."
		}
		& git -C $ProjectRoot merge-base --is-ancestor $BaselineCommit $GitCommit
		if ($LASTEXITCODE -ne 0) {
			throw "Expected-count baseline commit '$BaselineCommit' is not an ancestor of current commit '$GitCommit'."
		}
		$Attempt.expectedCountSource = 'CheckedInBaseline'
		$Attempt.expectedBaselineCommit = $BaselineCommit
	}
	else {
		$CanonicalBroadSuites = @(
			'SeinARTS.Unit',
			'SeinARTS.Integration',
			'SeinARTS.Determinism',
			'SeinARTS.Editor'
		)
		if ($CanonicalBroadSuites | Where-Object { $_ -ieq $Suite }) {
			throw "Missing expected-count baseline for canonical suite '$Profile' / '$Suite'."
		}
		$Attempt.expectedCountSource = 'None'
		Write-Warning "No checked-in expected-count floor applies to '$Profile' / '$Suite'. The attempt receipt will record discovery, but only the nonzero guard applies."
	}
}
$Attempt.expectedMinimumCount = $ExpectedFloor
Write-SeinAttemptManifest

$EditorArgs = @(
	$Uproject,
	('-EnablePlugins=' + ($EnabledTestPlugins -join ',')),
	'-unattended',
	'-nop4',
	'-nosplash',
	'-stdout',
	'-FullStdOutLogOutput',
	"-ExecCmds=Automation RunTests $Suite;Quit",
	'-TestExit=Automation Test Queue Empty',
	"-ReportExportPath=$ReportPath",
	"-abslog=$LogPath"
)

if ($Profile -eq 'Framework') {
	$EditorArgs += ('-DisablePlugins=' + (($ExtensionPlugins + 'SeinARTSExtensionTestSuite') -join ','))
}

if (-not $KeepRendering) {
	$EditorArgs += '-NullRHI'
}

Write-Host "[RunTests.ps1] suite:  $Suite" -ForegroundColor Cyan
Write-Host "[RunTests.ps1] report: $ReportPath" -ForegroundColor DarkGray

function Stop-SeinProcessTree([int] $ProcessId)
{
	$Children = Get-CimInstance Win32_Process -Filter "ParentProcessId = $ProcessId" -ErrorAction SilentlyContinue
	foreach ($Child in $Children) {
		Stop-SeinProcessTree -ProcessId $Child.ProcessId
	}
	Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

# Start-Process accepts one native argument string. None of these arguments contains an embedded
# quote, so quoting every argument is sufficient to preserve spaces and the ExecCmds semicolon.
$NativeArgs = ($EditorArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
$Attempt.status = 'Running'
Write-SeinAttemptManifest
$EditorProcess = Start-Process -FilePath $EditorCmd -ArgumentList $NativeArgs `
	-RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath `
	-WindowStyle Hidden -PassThru
$Attempt.editorProcessId = $EditorProcess.Id
Write-SeinAttemptManifest

if (-not $EditorProcess.WaitForExit($TimeoutSeconds * 1000)) {
	Stop-SeinProcessTree -ProcessId $EditorProcess.Id
	throw "Automation exceeded $TimeoutSeconds seconds. See '$LogPath' and '$StdoutPath'."
}

$EditorProcess.WaitForExit()
$EditorProcess.Refresh()
$EditorExitCode = $EditorProcess.ExitCode

# Some PowerShell/Process combinations lose ExitCode after a redirected native process has
# already closed, even though WaitForExit completed and Unreal emitted its terminal status.
# Fall back only to Automation's exact final marker; absence or malformed output remains fatal.
if ($null -eq $EditorExitCode) {
	$ExitMarker = Select-String -Path $StdoutPath -Pattern '\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*' |
		Select-Object -Last 1
	if (-not $ExitMarker -or $ExitMarker.Matches.Count -ne 1) {
		throw "Automation process exposed no exit code or terminal marker. See '$StdoutPath'."
	}
	$EditorExitCode = [int]$ExitMarker.Matches[0].Groups[1].Value
	Write-Verbose "Recovered editor exit code $EditorExitCode from Unreal's terminal marker."
}
$Attempt.editorExitCode = $EditorExitCode
Write-SeinAttemptManifest

$IndexPath = Join-Path $ReportPath 'index.json'
if (-not (Test-Path -LiteralPath $IndexPath)) {
	throw "Automation produced no index.json (editor exit $EditorExitCode). See '$LogPath', '$StdoutPath', and '$StderrPath'."
}

$Report = Get-Content -Raw -LiteralPath $IndexPath | ConvertFrom-Json
$Tests = @($Report.tests)
$Attempt.discoveredTestCount = $Tests.Count
Write-SeinAttemptManifest
if ($Tests.Count -eq 0) {
	throw "Automation matched no tests for '$Suite'. See '$IndexPath'."
}
if ($null -ne $ExpectedFloor -and $Tests.Count -lt $ExpectedFloor) {
	throw "Automation discovered $($Tests.Count) test(s) for '$Profile' / '$Suite', below the expected minimum $ExpectedFloor. See '$ExpectedCountsPath' and '$IndexPath'."
}

$Unsuccessful = @($Tests | Where-Object { $_.state -ne 'Success' })
$Attempt.unsuccessfulTestCount = $Unsuccessful.Count
Write-SeinAttemptManifest
$FirstTestLine = Select-String -Path $LogPath -SimpleMatch `
	'LogAutomationController: Display: Test Started.' | Select-Object -First 1
$StartupErrors = @()
if ($FirstTestLine) {
	$StartupErrors = @(Get-Content -LiteralPath $LogPath -TotalCount ($FirstTestLine.LineNumber - 1) |
		Select-String -Pattern ':\s+Error:')
}

if ($StartupErrors.Count -gt 0 -and $AllowKnownStartupErrors) {
	$BaselinePath = Join-Path $PluginRoot 'KnownStartupErrors.txt'
	$ExpectedErrors = @(Get-Content -LiteralPath $BaselinePath |
		Where-Object { $_.Trim() -and -not $_.TrimStart().StartsWith('#') })
	$ActualErrors = @($StartupErrors | ForEach-Object {
		$_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', ''
	})

	$ExpectedSignature = (@($ExpectedErrors | Sort-Object) -join "`n")
	$ActualSignature = (@($ActualErrors | Sort-Object) -join "`n")
	if ($ExpectedSignature -eq $ActualSignature) {
		Write-Warning "Accepted $($StartupErrors.Count) exactly matched startup error(s) from KnownStartupErrors.txt."
		$StartupErrors = @()
	}
	else {
		Write-Host 'Startup errors do not exactly match KnownStartupErrors.txt.' -ForegroundColor Red
	}
}

if ($EditorExitCode -ne 0 -or $Unsuccessful.Count -gt 0 -or $StartupErrors.Count -gt 0) {
	$States = ($Unsuccessful | ForEach-Object { "$($_.testDisplayName): $($_.state)" }) -join '; '
	if ($StartupErrors.Count -gt 0) {
		Write-Host "Startup emitted $($StartupErrors.Count) error(s):" -ForegroundColor Red
		$StartupErrors | ForEach-Object { Write-Host $_.Line }
	}
	if ($EditorExitCode -ne 0 -or $Unsuccessful.Count -gt 0) {
		Get-Content -Tail 80 -LiteralPath $StdoutPath -ErrorAction SilentlyContinue
		Get-Content -Tail 40 -LiteralPath $StderrPath -ErrorAction SilentlyContinue
	}
	throw "Automation failed (editor exit $EditorExitCode). $States See '$IndexPath'. -AllowKnownStartupErrors accepts only the exact checked-in signature multiset."
}

$Attempt.status = 'Passed'
$Attempt.completedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
Write-SeinAttemptManifest
Write-Host "[RunTests.ps1] Passed $($Tests.Count) test(s)." -ForegroundColor Green
Write-Host $IndexPath
}
catch {
	$Attempt.status = 'Failed'
	$Attempt.completedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
	$Attempt.failure = $_.Exception.Message
	try {
		Write-SeinAttemptManifest
	}
	catch {
		Write-Warning "Could not finalize attempt manifest '$AttemptPath': $($_.Exception.Message)"
	}
	throw
}
