#Requires -Version 5.1
<#
.SYNOPSIS
  Validate a SeinARTS project installation without modifying the project.

.EXAMPLE
  .\Test-SeinARTSInstallation.ps1 -Project C:\Projects\Game\Game.uproject

.EXAMPLE
  .\Test-SeinARTSInstallation.ps1 -Project C:\Projects\Game -Json
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Project,

	[string] $EngineRoot,

	[switch] $Json
)

$ErrorActionPreference = 'Stop'
$Findings = [System.Collections.Generic.List[object]]::new()

function Add-SeinFinding(
	[string] $Severity,
	[string] $Code,
	[string] $Message,
	[string] $Remediation = '')
{
	$Findings.Add([pscustomobject][ordered]@{
		severity = $Severity
		code = $Code
		message = $Message
		remediation = $Remediation
	})
}

function Resolve-SeinProject([string] $InputPath)
{
	if (-not (Test-Path -LiteralPath $InputPath)) {
		return $null
	}
	$Resolved = (Resolve-Path -LiteralPath $InputPath).Path
	if (Test-Path -LiteralPath $Resolved -PathType Leaf) {
		if ([System.IO.Path]::GetExtension($Resolved) -ieq '.uproject') {
			return $Resolved
		}
		return $null
	}
	$Projects = @(Get-ChildItem -LiteralPath $Resolved -Filter '*.uproject' -File)
	if ($Projects.Count -eq 1) {
		return $Projects[0].FullName
	}
	return $null
}

function Resolve-SeinEngine(
	[string] $RequestedRoot,
	[string] $Association)
{
	$Candidates = [System.Collections.Generic.List[string]]::new()
	if ($RequestedRoot) {
		$Candidates.Add($RequestedRoot)
	}
	else {
		if ($Association) {
			$Candidates.Add("C:\Program Files\Epic Games\UE_$Association")
		}
		$Candidates.Add('C:\Program Files\Epic Games\UE_5.8')
		foreach ($RegistryRoot in @(
			'HKLM:\SOFTWARE\EpicGames\Unreal Engine',
			'HKLM:\SOFTWARE\Epic Games\Unreal Engine')) {
			if (-not $Association) {
				continue
			}
			try {
				$Installed = (Get-ItemProperty `
					-Path "$RegistryRoot\$Association" `
					-ErrorAction Stop).InstalledDirectory
				if ($Installed) {
					$Candidates.Add([string]$Installed)
				}
			}
			catch {}
		}
	}
	foreach ($Candidate in $Candidates) {
		if (-not $Candidate -or -not (Test-Path -LiteralPath $Candidate)) {
			continue
		}
		$Resolved = (Resolve-Path -LiteralPath $Candidate).Path
		if (Test-Path -LiteralPath `
			(Join-Path $Resolved 'Engine\Build\Build.version') -PathType Leaf) {
			return $Resolved
		}
	}
	return $null
}

function Get-SeinPluginCandidates(
	[string] $PluginName,
	[string[]] $SearchRoots)
{
	$Seen = [System.Collections.Generic.HashSet[string]]::new(
		[System.StringComparer]::OrdinalIgnoreCase)
	$Candidates = [System.Collections.Generic.List[string]]::new()
	foreach ($Root in $SearchRoots) {
		if (-not $Root -or -not (Test-Path -LiteralPath $Root -PathType Container)) {
			continue
		}
		foreach ($Path in Get-ChildItem -LiteralPath $Root -Recurse -File `
				-Filter "$PluginName.uplugin" -ErrorAction SilentlyContinue) {
			$Path = $Path.FullName
			$Resolved = (Resolve-Path -LiteralPath $Path).Path
			if ($Seen.Add($Resolved)) {
				$Candidates.Add($Resolved)
			}
		}
	}
	return @($Candidates)
}

function Get-SeinIniValue(
	[string] $Path,
	[string] $Section,
	[string] $Key)
{
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		return $null
	}
	$CurrentSection = ''
	$Result = $null
	foreach ($RawLine in Get-Content -LiteralPath $Path) {
		$Line = $RawLine.Trim()
		if ($Line -match '^\[(.+)\]$') {
			$CurrentSection = $Matches[1]
			continue
		}
		if ($CurrentSection -ceq $Section -and `
			$Line -match ('^' + [regex]::Escape($Key) + '\s*=\s*(.*)$')) {
			$Result = $Matches[1].Trim().Trim('"')
		}
	}
	return $Result
}

function Test-SeinSemVer([string] $Value)
{
	return $Value -match (
		'^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)' +
		'(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)' +
		'(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?' +
		'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')
}

$ProjectPath = Resolve-SeinProject $Project
$ProjectRoot = $null
$ProjectJson = $null
$ResolvedEngineRoot = $null
$EnabledProductionPlugins = [System.Collections.Generic.List[string]]::new()
$IntegrationMode = $null
$CohortVersion = $null
$SourceCohortIdentity = $null
$ManifestPath = $null

if (-not $ProjectPath) {
	Add-SeinFinding 'Error' 'SEIN001' `
		"Project '$Project' does not resolve to exactly one .uproject file." `
		'Pass a .uproject path or a directory containing exactly one .uproject.'
}
else {
	$ProjectRoot = Split-Path -Parent $ProjectPath
	try {
		$ProjectJson = Get-Content -Raw -LiteralPath $ProjectPath |
			ConvertFrom-Json
		Add-SeinFinding 'Pass' 'SEIN002' "Loaded project '$ProjectPath'."
	}
	catch {
		Add-SeinFinding 'Error' 'SEIN003' `
			"Project descriptor is not valid JSON: $($_.Exception.Message)" `
			'Repair the .uproject JSON before loading it in Unreal.'
	}
}

if ($ProjectJson) {
	$Association = [string]$ProjectJson.EngineAssociation
	$ResolvedEngineRoot = Resolve-SeinEngine $EngineRoot $Association
	if (-not $ResolvedEngineRoot) {
		Add-SeinFinding 'Error' 'SEIN010' `
			'Could not resolve the Unreal Engine installation.' `
			'Install UE 5.8 or pass -EngineRoot with a UE 5.8 root.'
	}
	else {
		try {
			$BuildVersionPath = Join-Path $ResolvedEngineRoot `
				'Engine\Build\Build.version'
			$BuildVersion = Get-Content -Raw -LiteralPath $BuildVersionPath |
				ConvertFrom-Json
			if ([int]$BuildVersion.MajorVersion -ne 5 -or `
				[int]$BuildVersion.MinorVersion -ne 8) {
				Add-SeinFinding 'Error' 'SEIN011' `
					"Resolved engine is $($BuildVersion.MajorVersion).$($BuildVersion.MinorVersion), not the qualified UE 5.8 baseline." `
					'Use UE 5.8 for the qualified Win64 integration.'
			}
			else {
				Add-SeinFinding 'Pass' 'SEIN012' `
					"Resolved qualified UE 5.8 engine '$ResolvedEngineRoot'."
			}
		}
		catch {
			Add-SeinFinding 'Error' 'SEIN013' `
				"Could not read the engine build identity: $($_.Exception.Message)" `
				'Repair the engine installation or pass a different -EngineRoot.'
		}
	}
}

if ($ProjectJson -and $ProjectRoot) {
	$ProductionDependencies = [ordered]@{
		SeinARTSFramework = @()
		SeinARTSSquadExtension = @('SeinARTSFramework')
		SeinARTSCoverExtension = @('SeinARTSFramework')
		SeinARTSMovementPlusExtension = @('SeinARTSFramework')
		SeinARTSCoverSquadExtension = @(
			'SeinARTSFramework',
			'SeinARTSCoverExtension',
			'SeinARTSSquadExtension')
	}
	$TestPlugins = @('SeinARTSTestSuite', 'SeinARTSExtensionTestSuite')
	$ProjectPluginEntries = @($ProjectJson.Plugins)
	foreach ($TestPlugin in $TestPlugins) {
		$EnabledTestEntry = @($ProjectPluginEntries | Where-Object {
			[string]$_.Name -ceq $TestPlugin -and [bool]$_.Enabled
		})
		if ($EnabledTestEntry.Count -gt 0) {
			Add-SeinFinding 'Error' 'SEIN020' `
				"Development-only plugin '$TestPlugin' is enabled in the project descriptor." `
				'Disable test-suite plugins outside explicit development test runs.'
		}
	}
	if (-not ($Findings | Where-Object { $_.code -eq 'SEIN020' })) {
		Add-SeinFinding 'Pass' 'SEIN021' `
			'Development-only SeinARTS test plugins are not enabled.'
	}

	$ExplicitRoots = @($ProjectPluginEntries | Where-Object {
		[bool]$_.Enabled -and $ProductionDependencies.Contains([string]$_.Name)
	} | ForEach-Object { [string]$_.Name })
	if ($ExplicitRoots.Count -eq 0) {
		Add-SeinFinding 'Error' 'SEIN022' `
			'No SeinARTS production plugin is explicitly enabled in the project.' `
			'Enable SeinARTSFramework or one of its optional extensions in the .uproject.'
	}

	$SearchRoots = [System.Collections.Generic.List[string]]::new()
	$SearchRoots.Add((Join-Path $ProjectRoot 'Plugins'))
	foreach ($AdditionalRoot in @($ProjectJson.AdditionalPluginDirectories)) {
		if (-not $AdditionalRoot) {
			continue
		}
		$CandidateRoot = [string]$AdditionalRoot
		if (-not [System.IO.Path]::IsPathRooted($CandidateRoot)) {
			$CandidateRoot = Join-Path $ProjectRoot $CandidateRoot
		}
		$SearchRoots.Add($CandidateRoot)
	}
	if ($ResolvedEngineRoot) {
		$SearchRoots.Add((Join-Path $ResolvedEngineRoot 'Engine\Plugins\Marketplace'))
		$SearchRoots.Add((Join-Path $ResolvedEngineRoot 'Engine\Plugins'))
	}
	$CanonicalSearchRoots = [System.Collections.Generic.List[string]]::new()
	foreach ($SearchRoot in @($SearchRoots | Where-Object {
			Test-Path -LiteralPath $_ -PathType Container
		} | ForEach-Object {
			(Resolve-Path -LiteralPath $_).Path.TrimEnd('\', '/')
		} | Sort-Object Length)) {
		$Covered = $false
		foreach ($CanonicalRoot in $CanonicalSearchRoots) {
			if ($SearchRoot -ieq $CanonicalRoot -or
				$SearchRoot.StartsWith(
					$CanonicalRoot + [System.IO.Path]::DirectorySeparatorChar,
					[System.StringComparison]::OrdinalIgnoreCase)) {
				$Covered = $true
				break
			}
		}
		if (-not $Covered) {
			$CanonicalSearchRoots.Add($SearchRoot)
		}
	}
	$SearchRoots = $CanonicalSearchRoots

	$Queue = [System.Collections.Generic.Queue[string]]::new()
	foreach ($RootPlugin in $ExplicitRoots) {
		$Queue.Enqueue($RootPlugin)
	}
	$Visited = [System.Collections.Generic.HashSet[string]]::new(
		[System.StringComparer]::Ordinal)
	$Descriptors = @{}
	while ($Queue.Count -gt 0) {
		$PluginName = $Queue.Dequeue()
		if (-not $Visited.Add($PluginName)) {
			continue
		}
		$EnabledProductionPlugins.Add($PluginName)
		$Candidates = @(Get-SeinPluginCandidates $PluginName @($SearchRoots))
		if ($Candidates.Count -eq 0) {
			Add-SeinFinding 'Error' 'SEIN030' `
				"Enabled plugin '$PluginName' has no discoverable descriptor." `
				'Install the matching plugin under Project/Plugins or Engine/Plugins/Marketplace.'
		}
		elseif ($Candidates.Count -gt 1) {
			Add-SeinFinding 'Error' 'SEIN031' `
				"Plugin '$PluginName' has duplicate installations: $($Candidates -join '; ')." `
				'Keep exactly one project or engine installation of each enabled SeinARTS plugin.'
		}
		else {
			try {
				$Descriptor = Get-Content -Raw -LiteralPath $Candidates[0] |
					ConvertFrom-Json
				$Descriptors[$PluginName] = [pscustomobject]@{
					Path = $Candidates[0]
					Json = $Descriptor
				}
				Add-SeinFinding 'Pass' 'SEIN032' `
					"Resolved '$PluginName' to '$($Candidates[0])'."
			}
			catch {
				Add-SeinFinding 'Error' 'SEIN033' `
					"Plugin descriptor '$($Candidates[0])' is invalid JSON: $($_.Exception.Message)" `
					'Reinstall the plugin from a qualified source or release archive.'
			}
		}
		foreach ($Dependency in @($ProductionDependencies[$PluginName])) {
			$Queue.Enqueue($Dependency)
		}
	}

	foreach ($PluginName in @($Visited)) {
		if (-not $Descriptors.ContainsKey($PluginName)) {
			continue
		}
		$Declared = @($Descriptors[$PluginName].Json.Plugins)
		$ExpectedSeinDependencies = @($ProductionDependencies[$PluginName])
		foreach ($Dependency in $ExpectedSeinDependencies) {
			$Matching = @($Declared | Where-Object {
				[string]$_.Name -ceq $Dependency -and [bool]$_.Enabled
			})
			if ($Matching.Count -ne 1) {
				Add-SeinFinding 'Error' 'SEIN034' `
					"Plugin '$PluginName' does not declare required dependency '$Dependency' exactly once and enabled." `
					'Reinstall a qualified SeinARTS plugin cohort.'
			}
		}
		$UnexpectedSeinDependencies = @($Declared | Where-Object {
			[string]$_.Name -like 'SeinARTS*' -and
			[string]$_.Name -cnotin $ExpectedSeinDependencies
		})
		if ($UnexpectedSeinDependencies.Count -gt 0) {
			Add-SeinFinding 'Error' 'SEIN036' `
				"Plugin '$PluginName' declares unexpected SeinARTS dependencies: $(@($UnexpectedSeinDependencies | ForEach-Object { [string]$_.Name }) -join ', ')." `
				'Reinstall a qualified SeinARTS plugin cohort.'
		}
	}
	if (-not ($Findings | Where-Object {
		$_.code -in @('SEIN030', 'SEIN031', 'SEIN033', 'SEIN034', 'SEIN036')
	})) {
		Add-SeinFinding 'Pass' 'SEIN035' `
			'Enabled SeinARTS plugin dependency closure is complete and unambiguous.'
	}

	if ($Descriptors.Count -gt 0) {
		$Versions = @($Descriptors.Values | ForEach-Object {
			[string]$_.Json.VersionName
		} | Sort-Object -Unique)
		$InstalledModes = @($Descriptors.Values | ForEach-Object {
			[bool]$_.Json.Installed
		} | Sort-Object -Unique)
		if ($Versions.Count -ne 1 -or -not $Versions[0]) {
			Add-SeinFinding 'Error' 'SEIN040' `
				"Enabled production plugin versions disagree or are missing: $($Versions -join ', ')." `
				'Replace the complete enabled plugin cohort from one release or one pinned source commit.'
		}
		else {
			$CohortVersion = $Versions[0]
		}
		if ($InstalledModes.Count -ne 1) {
			Add-SeinFinding 'Error' 'SEIN041' `
				'Enabled production plugins mix release-installed and source-integration descriptors.' `
				'Use one complete release cohort or one commit-pinned source cohort.'
		}
		else {
			$IntegrationMode = if ($InstalledModes[0]) { 'Release' } else { 'Source' }
			if ($IntegrationMode -eq 'Release' -and $CohortVersion -and `
				-not (Test-SeinSemVer $CohortVersion)) {
				Add-SeinFinding 'Error' 'SEIN042' `
					"Release cohort version '$CohortVersion' is not SemVer 2.0." `
					'Reinstall artifacts produced by the SeinARTS release gate.'
			}
		}
		if (-not ($Findings | Where-Object {
			$_.code -in @('SEIN040', 'SEIN041', 'SEIN042')
		})) {
			Add-SeinFinding 'Pass' 'SEIN043' `
				"Enabled plugins form one $IntegrationMode cohort at version '$CohortVersion'."
		}
		if ($IntegrationMode -eq 'Source') {
			$Git = Get-Command git -ErrorAction SilentlyContinue
			$GitRoots = [System.Collections.Generic.List[string]]::new()
			$SourcePathsByRoot = @{}
			$AllDescriptorsTracked = $true
			if ($Git) {
				foreach ($DescriptorRecord in $Descriptors.Values) {
					$PluginDirectory = Split-Path -Parent $DescriptorRecord.Path
					$RootOutput = @(& $Git.Source -C $PluginDirectory `
						rev-parse --show-toplevel 2>$null)
					if ($LASTEXITCODE -ne 0 -or $RootOutput.Count -ne 1) {
						$GitRoots.Clear()
						break
					}
					$GitRoot = [System.IO.Path]::GetFullPath([string]$RootOutput[0]).TrimEnd('\', '/')
					if (-not $SourcePathsByRoot.ContainsKey($GitRoot)) {
						$GitRoots.Add($GitRoot)
						$SourcePathsByRoot[$GitRoot] = [System.Collections.Generic.List[string]]::new()
					}
					$RelativePluginPath = $PluginDirectory.Substring(
						$GitRoot.Length).TrimStart('\', '/')
					$SourcePathsByRoot[$GitRoot].Add($RelativePluginPath)
					$RelativeDescriptorPath = $DescriptorRecord.Path.Substring(
						$GitRoot.Length).TrimStart('\', '/')
					$TrackedOutput = @(& $Git.Source -C $GitRoot ls-files `
						-- $RelativeDescriptorPath 2>$null)
					if ($LASTEXITCODE -ne 0 -or $TrackedOutput.Count -ne 1) {
						$AllDescriptorsTracked = $false
					}
				}
			}
			if ($GitRoots.Count -gt 1) {
				Add-SeinFinding 'Error' 'SEIN044' `
					"Source plugins span multiple Git repositories: $($GitRoots -join '; ')." `
					'Install every enabled source plugin from one pinned repository checkout.'
			}
			elseif ($GitRoots.Count -eq 1 -and $AllDescriptorsTracked) {
				$GitRoot = $GitRoots[0]
				$CommitOutput = @(& $Git.Source -C $GitRoot rev-parse HEAD 2>$null)
				$StatusArguments = @(
					'-C', $GitRoot, 'status', '--porcelain',
					'--untracked-files=all', '--') + @($SourcePathsByRoot[$GitRoot])
				$DirtyPaths = @(& $Git.Source @StatusArguments 2>$null)
				if ($LASTEXITCODE -eq 0 -and $CommitOutput.Count -eq 1) {
					$SourceCohortIdentity = 'git:{0}{1}' -f `
						([string]$CommitOutput[0]).Trim().ToLowerInvariant(), `
						$(if ($DirtyPaths.Count -gt 0) { '+dirty' } else { '' })
					if ($DirtyPaths.Count -gt 0) {
						Add-SeinFinding 'Warning' 'SEIN045' `
							"Source cohort is based on '$SourceCohortIdentity'; plugin paths contain local changes." `
							'Use a clean pinned commit for distributable source integration.'
					}
					else {
						Add-SeinFinding 'Pass' 'SEIN046' `
							"Source plugins resolve to pinned cohort '$SourceCohortIdentity'."
					}
				}
			}
			if (-not $SourceCohortIdentity -and
				-not ($Findings | Where-Object { $_.code -eq 'SEIN044' })) {
				Add-SeinFinding 'Warning' 'SEIN047' `
					'Source cohort commit identity could not be verified.' `
					'Install from one clean pinned Git commit or use qualified release artifacts.'
			}
		}
	}

	$DefaultGameIni = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
	$ManifestPath = Get-SeinIniValue `
		$DefaultGameIni `
		'/Script/SeinARTSCoreEntity.SeinARTSCoreSettings' `
		'SimulationContentManifest'
	if (-not $ManifestPath) {
		Add-SeinFinding 'Error' 'SEIN050' `
			'Config/DefaultGame.ini does not configure SimulationContentManifest.' `
			'Create a project-owned manifest asset and assign it in SeinARTS Core project settings.'
	}
	elseif (-not $ManifestPath.StartsWith('/Game/', [System.StringComparison]::Ordinal)) {
		Add-SeinFinding 'Error' 'SEIN051' `
			"SimulationContentManifest '$ManifestPath' is not project-owned under /Game/." `
			'Point the setting at a manifest asset in the consuming project Content directory.'
	}
	else {
		$PackagePath = ($ManifestPath -split '\.', 2)[0]
		$RelativePackagePath = $PackagePath.Substring(6)
		$Segments = @($RelativePackagePath.Split('/'))
		$ContentRoot = [System.IO.Path]::GetFullPath(
			(Join-Path $ProjectRoot 'Content')).TrimEnd('\', '/')
		$ManifestFile = $null
		if (-not $RelativePackagePath -or
			$ManifestPath.Contains('\') -or
			@($Segments | Where-Object { -not $_ -or $_ -in @('.', '..') }).Count -gt 0) {
			Add-SeinFinding 'Error' 'SEIN054' `
				"SimulationContentManifest '$ManifestPath' is not a canonical /Game/ asset path." `
				'Choose a project content asset without traversal or empty path segments.'
		}
		else {
			$RelativeAssetPath = $RelativePackagePath.Replace(
				'/', [string][System.IO.Path]::DirectorySeparatorChar) + '.uasset'
			$ManifestFile = [System.IO.Path]::GetFullPath(
				(Join-Path $ContentRoot $RelativeAssetPath))
			if (-not $ManifestFile.StartsWith(
					$ContentRoot + [System.IO.Path]::DirectorySeparatorChar,
					[System.StringComparison]::OrdinalIgnoreCase)) {
				Add-SeinFinding 'Error' 'SEIN054' `
					"SimulationContentManifest '$ManifestPath' escapes the project Content directory." `
					'Choose a project-owned asset beneath /Game/.'
				$ManifestFile = $null
			}
		}
		if ($ManifestFile -and -not (Test-Path -LiteralPath $ManifestFile -PathType Leaf)) {
			Add-SeinFinding 'Error' 'SEIN052' `
				"Configured simulation-content manifest is missing on disk: '$ManifestFile'." `
				'Run Sein.SimulationContent.GenerateManifest in the Unreal Output Log, then save the asset.'
		}
		elseif ($ManifestFile) {
			Add-SeinFinding 'Pass' 'SEIN053' `
				"Configured project-owned simulation-content manifest exists at '$ManifestFile'."
		}
	}
}

$Errors = @($Findings | Where-Object { $_.severity -eq 'Error' })
$Warnings = @($Findings | Where-Object { $_.severity -eq 'Warning' })
$Report = [pscustomobject][ordered]@{
	schemaVersion = 1
	result = if ($Errors.Count -eq 0) { 'Passed' } else { 'Failed' }
	project = $ProjectPath
	engineRoot = $ResolvedEngineRoot
	integrationMode = $IntegrationMode
	cohortVersion = $CohortVersion
	sourceCohortIdentity = $SourceCohortIdentity
	enabledProductionPlugins = @($EnabledProductionPlugins | Sort-Object)
	simulationContentManifest = $ManifestPath
	errorCount = $Errors.Count
	warningCount = $Warnings.Count
	findings = @($Findings)
}

if ($Json) {
	$Report | ConvertTo-Json -Depth 8
}
else {
	foreach ($Finding in $Findings) {
		$Prefix = "[$($Finding.severity.ToUpperInvariant())][$($Finding.code)]"
		Write-Output "$Prefix $($Finding.message)"
		if ($Finding.remediation) {
			Write-Output "  Fix: $($Finding.remediation)"
		}
	}
	Write-Output "[SeinARTS] $($Report.result): $($Errors.Count) error(s), $($Warnings.Count) warning(s)."
}

if ($Errors.Count -gt 0) {
	exit 1
}
