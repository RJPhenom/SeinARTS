#Requires -Version 5.1
[CmdletBinding()]
param(
	[switch] $KeepFixtures
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Diagnostic = Join-Path $PSScriptRoot 'Test-SeinARTSInstallation.ps1'
$WindowsPowerShell = Join-Path $env:SystemRoot `
	'System32\WindowsPowerShell\v1.0\powershell.exe'
$FixtureParent = Join-Path $RepoRoot 'Saved\DiagnosticsSelfTest'
$FixtureRoot = Join-Path $FixtureParent ([Guid]::NewGuid().ToString('N'))

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

function New-FakeEngine([string] $Root)
{
	Write-Json (Join-Path $Root 'Engine\Build\Build.version') ([ordered]@{
		MajorVersion = 5
		MinorVersion = 8
	})
}

function Invoke-Diagnostic([string] $ProjectPath, [string] $EnginePath)
{
	$Output = @(& $WindowsPowerShell `
		-NoProfile `
		-ExecutionPolicy Bypass `
		-File $Diagnostic `
		-Project $ProjectPath `
		-EngineRoot $EnginePath `
		-Json)
	$ExitCode = $LASTEXITCODE
	[pscustomobject]@{
		ExitCode = $ExitCode
		Report = (($Output -join "`r`n") | ConvertFrom-Json)
	}
}

if (-not (Test-Path -LiteralPath $Diagnostic -PathType Leaf) -or
	-not (Test-Path -LiteralPath $WindowsPowerShell -PathType Leaf)) {
	throw 'Installation diagnostic self-test requires the repository diagnostic and Windows PowerShell 5.1.'
}

New-Item -ItemType Directory -Path $FixtureRoot -Force | Out-Null
try {
	$PassRoot = Join-Path $FixtureRoot 'Pass'
	$PassProject = Join-Path $PassRoot 'Game'
	$PassEngine = Join-Path $PassRoot 'FakeUE58'
	New-FakeEngine $PassEngine
	Write-Json (Join-Path $PassProject 'Game.uproject') ([ordered]@{
		FileVersion = 3
		EngineAssociation = '5.8'
		Plugins = @([ordered]@{
			Name = 'SeinARTSFramework'
			Enabled = $true
		})
	})
	Write-Json (Join-Path $PassProject `
		'Plugins\Vendor\SeinARTSFramework\SeinARTSFramework.uplugin') ([ordered]@{
		FileVersion = 3
		Version = 1
		VersionName = '1.2.3-alpha.1+build.5'
		Installed = $true
		Modules = @()
		Plugins = @()
	})
	Write-Utf8NoBom (Join-Path $PassProject 'Config\DefaultGame.ini') @'
[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
SimulationContentManifest=/Game/Generated/Manifest.Manifest
'@
	$PassManifest = Join-Path $PassProject 'Content\Generated\Manifest.uasset'
	New-Item -ItemType Directory -Path (Split-Path -Parent $PassManifest) `
		-Force | Out-Null
	[System.IO.File]::WriteAllBytes($PassManifest, [byte[]]@(0))
	$Pass = Invoke-Diagnostic `
		(Join-Path $PassProject 'Game.uproject') $PassEngine
	if ($Pass.ExitCode -ne 0 -or
		[string]$Pass.Report.result -cne 'Passed' -or
		[int]$Pass.Report.errorCount -ne 0 -or
		[string]$Pass.Report.integrationMode -cne 'Release' -or
		[string]$Pass.Report.cohortVersion -cne '1.2.3-alpha.1+build.5') {
		throw 'Valid nested release fixture did not produce the expected pass receipt.'
	}

	$FailRoot = Join-Path $FixtureRoot 'Fail'
	$FailProject = Join-Path $FailRoot 'Game'
	$FailEngine = Join-Path $FailRoot 'FakeUE58'
	New-FakeEngine $FailEngine
	Write-Json (Join-Path $FailProject 'Game.uproject') ([ordered]@{
		FileVersion = 3
		EngineAssociation = '5.8'
		AdditionalPluginDirectories = @('MorePlugins')
		Plugins = @(
			[ordered]@{ Name = 'SeinARTSFramework'; Enabled = $true },
			[ordered]@{ Name = 'SeinARTSMovementPlusExtension'; Enabled = $true })
	})
	Write-Json (Join-Path $FailProject `
		'Plugins\SeinARTSFramework\SeinARTSFramework.uplugin') ([ordered]@{
		FileVersion = 3
		Version = 1
		VersionName = '1.0.0-01'
		Installed = $true
		Modules = @()
		Plugins = @([ordered]@{
			Name = 'SeinARTSTestSuite'
			Enabled = $true
		})
	})
	$MovementDescriptor = [ordered]@{
		FileVersion = 3
		Version = 1
		VersionName = '1.0.0-01'
		Installed = $true
		Modules = @()
		Plugins = @([ordered]@{
			Name = 'SeinARTSFramework'
			Enabled = $true
		})
	}
	foreach ($RelativePath in @(
		'Plugins\VendorA\SeinARTSMovementPlusExtension\SeinARTSMovementPlusExtension.uplugin',
		'MorePlugins\VendorB\SeinARTSMovementPlusExtension\SeinARTSMovementPlusExtension.uplugin')) {
		Write-Json (Join-Path $FailProject $RelativePath) $MovementDescriptor
	}
	Write-Utf8NoBom (Join-Path $FailProject 'Config\DefaultGame.ini') @'
[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
SimulationContentManifest=/Game/../Config/Foo.Foo
'@
	$Fail = Invoke-Diagnostic `
		(Join-Path $FailProject 'Game.uproject') $FailEngine
	$ActualCodes = @($Fail.Report.findings | Where-Object {
		[string]$_.severity -ceq 'Error'
	} | ForEach-Object { [string]$_.code } | Sort-Object)
	$ExpectedCodes = @('SEIN031', 'SEIN036', 'SEIN042', 'SEIN054')
	if ($Fail.ExitCode -ne 1 -or
		[string]$Fail.Report.result -cne 'Failed' -or
		(@(Compare-Object $ActualCodes $ExpectedCodes)).Count -ne 0) {
		throw "Adversarial fixture returned unexpected errors: $($ActualCodes -join ', ')."
	}

	Write-Host `
		'[DiagnosticsSelfTest] Windows PowerShell 5.1 pass and adversarial fixtures succeeded.' `
		-ForegroundColor Green
}
finally {
	if (-not $KeepFixtures -and (Test-Path -LiteralPath $FixtureRoot)) {
		$ResolvedFixtureRoot = [System.IO.Path]::GetFullPath($FixtureRoot)
		$ResolvedFixtureParent = [System.IO.Path]::GetFullPath(
			$FixtureParent).TrimEnd('\', '/')
		if (-not $ResolvedFixtureRoot.StartsWith(
				$ResolvedFixtureParent + [System.IO.Path]::DirectorySeparatorChar,
				[System.StringComparison]::OrdinalIgnoreCase)) {
			throw "Refusing to clean fixture outside '$ResolvedFixtureParent'."
		}
		Remove-Item -LiteralPath $ResolvedFixtureRoot -Recurse -Force
	}
}
