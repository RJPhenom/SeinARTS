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

	[ValidateRange(30, 7200)]
	[int] $TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$PluginRoot = $PSScriptRoot
$ProjectRoot = (Resolve-Path (Join-Path $PluginRoot '..\..')).Path
$Uproject = Join-Path $ProjectRoot 'SeinARTS.uproject'
$BuildScript = Join-Path $ProjectRoot 'Build.ps1'
$EditorCmd = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path -LiteralPath $EditorCmd)) {
	throw "UE 5.7 command-line editor was not found at '$EditorCmd'."
}

$ExtensionPlugins = @(
	'SeinARTSSquadExtension',
	'SeinARTSCoverExtension',
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
$TestReceiptEntry = $Receipt.Plugins | Where-Object { $_.Name -eq 'SeinARTSTestSuite' } | Select-Object -First 1
if ($TestReceiptEntry -and $TestReceiptEntry.Enabled) {
	throw "The shared editor receipt still enables the test suite. Run Build.ps1 once to restore normal startup, then retry."
}

$SafeSuite = $Suite -replace '[^A-Za-z0-9_.-]', '_'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$ReportPath = Join-Path $ProjectRoot "Saved\Automation\$SafeSuite-$Stamp"
$LogPath = Join-Path $ReportPath 'Automation.log'
$StdoutPath = Join-Path $ReportPath 'EditorStdout.log'
$StderrPath = Join-Path $ReportPath 'EditorStderr.log'
New-Item -ItemType Directory -Path $ReportPath -Force | Out-Null

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
$EditorProcess = Start-Process -FilePath $EditorCmd -ArgumentList $NativeArgs `
	-RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath `
	-WindowStyle Hidden -PassThru

if (-not $EditorProcess.WaitForExit($TimeoutSeconds * 1000)) {
	Stop-SeinProcessTree -ProcessId $EditorProcess.Id
	throw "Automation exceeded $TimeoutSeconds seconds. See '$LogPath' and '$StdoutPath'."
}

$EditorProcess.WaitForExit()
$EditorProcess.Refresh()
$EditorExitCode = $EditorProcess.ExitCode

$IndexPath = Join-Path $ReportPath 'index.json'
if (-not (Test-Path -LiteralPath $IndexPath)) {
	throw "Automation produced no index.json (editor exit $EditorExitCode). See '$LogPath', '$StdoutPath', and '$StderrPath'."
}

$Report = Get-Content -Raw -LiteralPath $IndexPath | ConvertFrom-Json
$Tests = @($Report.tests)
if ($Tests.Count -eq 0) {
	throw "Automation matched no tests for '$Suite'. See '$IndexPath'."
}

$Unsuccessful = @($Tests | Where-Object { $_.state -ne 'Success' })
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

Write-Host "[RunTests.ps1] Passed $($Tests.Count) test(s)." -ForegroundColor Green
Write-Host $IndexPath
