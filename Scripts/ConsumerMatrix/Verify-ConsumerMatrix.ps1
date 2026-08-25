#Requires -Version 5.1
<#
.SYNOPSIS
  Proves SeinARTS from a clean, generated downstream C++ project.

.DESCRIPTION
  Creates isolated projects under Saved/ConsumerMatrix and installs either
  selected production-plugin source from this checkout or exact packaged ZIPs
  supplied through -ArtifactDirectory. It builds the Editor and Shipping game
  targets, plus Client and Server when the engine distribution supports them.
  It then creates a consumer-owned map, generates the consumer-owned
  simulation-content manifest, loads the exact maps, cooks/packages them,
  smoke-loads the packaged game, and drives a real packaged
  listen-server/client/replay qualification for Framework and Movement+.

  No generated artifact is written to the repository's tracked Output or Docs
  trees. The generated projects are disposable evidence, not source fixtures.
#>
[CmdletBinding()]
param(
	[ValidateSet('Framework', 'Cover', 'Squad', 'MovementPlus', 'OnlineServices', 'Full', 'All')]
	[string] $Profile = 'All',

	[string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',

	# Stable identity supplied by the release orchestrator. Direct runs receive
	# a fresh identity so receipts from overlapping invocations cannot alias.
	[string] $QualificationRunId,

	# Directory containing <PluginName>.zip files produced by PackagePlugins.ps1.
	# Artifact mode validates and installs the exact ZIP contents instead of
	# copying source directly from this checkout.
	[string] $ArtifactDirectory,

	# Validate and extract the required ZIP set, then stop before generating or
	# building consumer projects. Useful as a fast packaging preflight.
	[switch] $ValidateArtifactsOnly,

	# Generate isolated non-unity Editor translation units for every Public
	# header in the selected production plugins.
	[switch] $AuditPublicHeaders,

	[switch] $SkipCook,

	[switch] $ReuseGenerated,

	# Diagnostic escape hatch. Release evidence must exercise the packaged
	# listen-server/client/resync/reconnect/replay flow.
	[switch] $SkipRuntimeQualification,

	# Baseline uses a direct loopback connection. Adverse routes the real
	# Shipping client/server traffic through the deterministic UDP fault proxy.
	[ValidateSet('Baseline', 'Adverse')]
	[string] $RuntimeNetworkProfile = 'Baseline',

	# Useful only for local diagnosis on launcher-engine installations that do
	# not contain Epic's Client/Server target support. Release CI must omit it.
	[switch] $SkipClientServer
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$EngineBuildVersionPath = Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $EngineBuildVersionPath -PathType Leaf)) {
	throw "UE build identity is missing: '$EngineBuildVersionPath'."
}
$EngineBuildVersion = Get-Content -Raw -LiteralPath $EngineBuildVersionPath |
	ConvertFrom-Json
$EngineBuildFingerprint = (Get-FileHash -LiteralPath $EngineBuildVersionPath `
	-Algorithm SHA256).Hash
if ([int]$EngineBuildVersion.MajorVersion -ne 5 -or
	[int]$EngineBuildVersion.MinorVersion -ne 8) {
	throw "SeinARTS consumer qualification requires UE 5.8; '$EngineRoot' reports $($EngineBuildVersion.MajorVersion).$($EngineBuildVersion.MinorVersion)."
}
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$RunUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$GeneratedRoot = Join-Path $RepoRoot 'Saved\ConsumerMatrix'
$PluginSourceRoot = Join-Path $RepoRoot 'Plugins'
$ConsumerGenerationSchemaVersion = 7
$ArtifactHashes = @{}
$ArtifactVersion = $null
$QualificationRunId = if ($QualificationRunId) {
	$QualificationRunId.ToLowerInvariant()
} else {
	[Guid]::NewGuid().ToString('N')
}
if ($QualificationRunId -notmatch '^[0-9a-f]{32}$') {
	throw "QualificationRunId '$QualificationRunId' must be 32 lowercase hexadecimal characters."
}
$PipelineMutex = [System.Threading.Mutex]::new(
	$false, 'Local\SeinARTS.ArtifactPipeline')
$PipelineMutexAcquired = $false
try {
	$PipelineMutexAcquired = $PipelineMutex.WaitOne(0)
}
catch [System.Threading.AbandonedMutexException] {
	$PipelineMutexAcquired = $true
}
if (-not $PipelineMutexAcquired) {
	$PipelineMutex.Dispose()
	throw 'Another SeinARTS release, packaging, or consumer qualification run owns the artifact pipeline.'
}

try {

if ($ArtifactDirectory -and $ReuseGenerated) {
	throw '-ArtifactDirectory cannot be combined with -ReuseGenerated; release artifacts require fresh consumers.'
}
if ($AuditPublicHeaders -and $ReuseGenerated) {
	throw '-AuditPublicHeaders cannot be combined with -ReuseGenerated; header evidence requires a fresh generated module set.'
}
if ($ValidateArtifactsOnly -and -not $ArtifactDirectory) {
	throw '-ValidateArtifactsOnly requires -ArtifactDirectory.'
}

foreach ($Required in @($BuildBat, $EditorCmd, $RunUat)) {
	if (-not (Test-Path -LiteralPath $Required)) {
		throw "Required UE 5.8 tool is missing: '$Required'."
	}
}

$Profiles = if ($Profile -eq 'All') {
	@('Framework', 'Cover', 'Squad', 'MovementPlus', 'OnlineServices', 'Full')
} else {
	@($Profile)
}

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

function Get-ChildRelativePath([string] $Root, [string] $Path)
{
	$ResolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	$ResolvedPath = [System.IO.Path]::GetFullPath($Path)
	$RequiredPrefix = $ResolvedRoot + [System.IO.Path]::DirectorySeparatorChar
	if (-not $ResolvedPath.StartsWith(
			$RequiredPrefix,
			[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Path '$ResolvedPath' is not a child of '$ResolvedRoot'."
	}
	return $ResolvedPath.Substring($RequiredPrefix.Length)
}

function Test-SeinSemVer([string] $Version)
{
	return $Version -match (
		'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)' +
		'(?:-(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)' +
		'(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*)?' +
		'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')
}

function Invoke-Checked(
	[string] $Description,
	[string] $Executable,
	[string[]] $Arguments)
{
	Write-Host "[ConsumerMatrix] $Description" -ForegroundColor Cyan
	& $Executable @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Description failed with exit code $LASTEXITCODE."
	}
}

function Assert-NoRunningConsumerEditor([string] $UprojectPath)
{
	$ResolvedUproject = [System.IO.Path]::GetFullPath($UprojectPath).Replace('/', '\')
	$UprojectLeaf = [System.IO.Path]::GetFileName($ResolvedUproject)
	try {
		$EditorProcesses = @(Get-CimInstance Win32_Process -Filter (
			"Name = 'UnrealEditor.exe' OR Name = 'UnrealEditor-Cmd.exe'"))
	}
	catch {
		throw "Cannot safely inspect running Unreal Editor processes before building '$ResolvedUproject': $($_.Exception.Message)"
	}

	foreach ($EditorProcess in $EditorProcesses) {
		if ([string]::IsNullOrWhiteSpace([string]$EditorProcess.CommandLine)) {
			throw "Cannot safely inspect Unreal Editor process $($EditorProcess.ProcessId) before building '$ResolvedUproject': its command line is unavailable."
		}
		$CommandLine = ([string]$EditorProcess.CommandLine).Replace('/', '\')
		if ($CommandLine.IndexOf(
			$ResolvedUproject,
			[System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
			$CommandLine.IndexOf(
				$UprojectLeaf,
				[System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
			throw "Generated consumer project '$ResolvedUproject' is open in Unreal Editor process $($EditorProcess.ProcessId). Close it before qualification."
		}
	}
}

function Get-RequiredArtifactPlugins
{
	$Required = [System.Collections.Generic.HashSet[string]]::new(
		[System.StringComparer]::OrdinalIgnoreCase)
	[void]$Required.Add('SeinARTSFramework')
	foreach ($ProfileName in $Profiles) {
		if ($ProfileName -in @('MovementPlus', 'Full')) {
			[void]$Required.Add('SeinARTSMovementPlusExtension')
		}
		if ($ProfileName -in @('Cover', 'Full')) {
			[void]$Required.Add('SeinARTSCoverExtension')
		}
		if ($ProfileName -in @('Squad', 'Full')) {
			[void]$Required.Add('SeinARTSSquadExtension')
		}
		if ($ProfileName -eq 'Full') {
			[void]$Required.Add('SeinARTSCoverSquadExtension')
		}
		if ($ProfileName -in @('OnlineServices', 'Full')) {
			[void]$Required.Add('SeinARTSOnlineServicesExtension')
		}
	}
	return @($Required | Sort-Object)
}

function Initialize-ArtifactPluginSource([string] $Directory)
{
	$ResolvedArtifacts = (Resolve-Path -LiteralPath $Directory).Path
	$StageRoot = Join-Path $GeneratedRoot '_ArtifactPlugins'
	$ResolvedGeneratedRoot = [System.IO.Path]::GetFullPath($GeneratedRoot)
	$ResolvedStageRoot = [System.IO.Path]::GetFullPath($StageRoot)
	if (-not $ResolvedStageRoot.StartsWith(
		$ResolvedGeneratedRoot + [System.IO.Path]::DirectorySeparatorChar,
		[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to stage release artifacts outside '$GeneratedRoot'."
	}
	if (Test-Path -LiteralPath $StageRoot) {
		Remove-Item -LiteralPath $StageRoot -Recurse -Force
	}
	New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null
	$ArchiveSnapshotRoot = Join-Path $StageRoot '_Archives'
	New-Item -ItemType Directory -Path $ArchiveSnapshotRoot -Force | Out-Null

	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$ExpectedVersion = $null
	$InvalidFileNameChars = [System.IO.Path]::GetInvalidFileNameChars()
	$ReservedDeviceName = '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'
	foreach ($PluginName in Get-RequiredArtifactPlugins) {
		$ZipPath = Join-Path $ResolvedArtifacts "$PluginName.zip"
		if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) {
			throw "Required packaged plugin is missing: '$ZipPath'."
		}

		$QualifiedZipPath = Join-Path $ArchiveSnapshotRoot "$PluginName.zip"
		$SourceStream = [System.IO.File]::Open(
			$ZipPath,
			[System.IO.FileMode]::Open,
			[System.IO.FileAccess]::Read,
			[System.IO.FileShare]::Read)
		try {
			$SnapshotStream = [System.IO.File]::Open(
				$QualifiedZipPath,
				[System.IO.FileMode]::Create,
				[System.IO.FileAccess]::Write,
				[System.IO.FileShare]::None)
			try {
				$SourceStream.CopyTo($SnapshotStream)
				$SnapshotStream.Flush()
			}
			finally {
				$SnapshotStream.Dispose()
			}
		}
		finally {
			$SourceStream.Dispose()
		}

		$QualifiedZipLock = [System.IO.File]::Open(
			$QualifiedZipPath,
			[System.IO.FileMode]::Open,
			[System.IO.FileAccess]::Read,
			[System.IO.FileShare]::Read)
		try {
		$Archive = [System.IO.Compression.ZipFile]::OpenRead($QualifiedZipPath)
		try {
			$DescriptorEntries = 0
			[uint64]$ExpandedBytes = 0
			$EntryCount = 0
			$SeenEntryPaths = [System.Collections.Generic.HashSet[string]]::new(
				[System.StringComparer]::OrdinalIgnoreCase)
			foreach ($Entry in $Archive.Entries) {
				$EntryPath = $Entry.FullName.Replace('\', '/')
				++$EntryCount
				if ($EntryCount -gt 100000) {
					throw "Archive '$ZipPath' exceeds the 100000-entry qualification bound."
				}
				$CanonicalEntryPath = $EntryPath.TrimEnd('/')
				if (-not $CanonicalEntryPath -or
					$CanonicalEntryPath.Contains('//') -or
					[System.IO.Path]::IsPathRooted($CanonicalEntryPath)) {
					throw "Archive '$ZipPath' contains an entry outside '$PluginName/': '$EntryPath'."
				}
				$Parts = @($CanonicalEntryPath.Split('/'))
				foreach ($Part in $Parts) {
					if (-not $Part -or
						$Part -in @('.', '..') -or
						$Part -cne $Part.TrimEnd(' ', '.') -or
						$Part.IndexOfAny($InvalidFileNameChars) -ge 0 -or
						$Part -match $ReservedDeviceName) {
						throw "Archive '$ZipPath' contains a Windows-unsafe path '$EntryPath'."
					}
				}
				if ($Parts[0] -cne $PluginName) {
					throw "Archive '$ZipPath' contains an entry outside '$PluginName/': '$EntryPath'."
				}
				$CanonicalEntryPath = $Parts -join '/'
				if (-not $SeenEntryPaths.Add($CanonicalEntryPath)) {
					throw "Archive '$ZipPath' contains a duplicate or case-colliding entry '$EntryPath'."
				}
				if ($CanonicalEntryPath -ceq "$PluginName/$PluginName.uplugin") {
					++$DescriptorEntries
				}
				if ($CanonicalEntryPath -match "^$([regex]::Escape($PluginName))/Intermediate(?:/|$)" -or
					$CanonicalEntryPath.EndsWith('.pdb',
						[System.StringComparison]::OrdinalIgnoreCase)) {
					throw "Release archive '$ZipPath' contains stripped build scratch '$EntryPath'."
				}
				if ([uint64]$Entry.Length -gt 2GB) {
					throw "Release archive '$ZipPath' contains an entry larger than 2 GiB: '$EntryPath'."
				}
				if ($Entry.Length -gt 64MB -and
					$Entry.CompressedLength -gt 0 -and
					([double]$Entry.Length / [double]$Entry.CompressedLength) -gt 1000.0) {
					throw "Release archive '$ZipPath' contains an excessive compression ratio: '$EntryPath'."
				}
				$ExpandedBytes += [uint64]$Entry.Length
				if ($ExpandedBytes -gt 8GB) {
					throw "Release archive '$ZipPath' expands beyond the 8 GiB qualification bound."
				}
			}
			if ($DescriptorEntries -ne 1) {
				throw "Release archive '$ZipPath' must contain exactly one root descriptor."
			}
		}
		finally {
			$Archive.Dispose()
		}

		Expand-Archive -LiteralPath $QualifiedZipPath -DestinationPath $StageRoot -Force
		$ExtractedRoot = Join-Path $StageRoot $PluginName
		$DescriptorPath = Join-Path $ExtractedRoot "$PluginName.uplugin"
		$SourcePath = Join-Path $ExtractedRoot 'Source'
		if (-not (Test-Path -LiteralPath $DescriptorPath -PathType Leaf) -or
			-not (Test-Path -LiteralPath $SourcePath -PathType Container)) {
			throw "Release archive '$ZipPath' lacks its root descriptor or Source tree."
		}
		$Descriptor = Get-Content -Raw -LiteralPath $DescriptorPath |
			ConvertFrom-Json
		if ($Descriptor.Installed -ne $true) {
			throw "Release descriptor '$DescriptorPath' is not stamped Installed:true."
		}
		$VersionName = [string]$Descriptor.VersionName
		if (-not (Test-SeinSemVer $VersionName)) {
			throw "Release descriptor '$DescriptorPath' has an invalid SemVer VersionName '$VersionName'."
		}
		if ($null -eq $ExpectedVersion) {
			$ExpectedVersion = $VersionName
		}
		elseif ($VersionName -cne $ExpectedVersion) {
			throw "Release archive versions disagree: expected '$ExpectedVersion', '$PluginName' is '$VersionName'."
		}
		if ($PluginName -ceq 'SeinARTSFramework') {
			$RequiredPublicFiles = @(
				'Tools\Diagnostics\Test-SeinARTSInstallation.ps1')
			foreach ($RelativePath in $RequiredPublicFiles) {
				$RequiredPath = Join-Path $ExtractedRoot $RelativePath
				if (-not (Test-Path -LiteralPath $RequiredPath -PathType Leaf)) {
					throw "Framework release archive lacks required public file '$RelativePath'."
				}
			}
			$DiagnosticPath = Join-Path $ExtractedRoot `
				'Tools\Diagnostics\Test-SeinARTSInstallation.ps1'
			$Tokens = $null
			$ParseErrors = $null
			[void][System.Management.Automation.Language.Parser]::ParseFile(
				$DiagnosticPath,
				[ref]$Tokens,
				[ref]$ParseErrors)
			if ($ParseErrors.Count -ne 0) {
				throw "Framework installation diagnostic does not parse: $($ParseErrors[0].Message)"
			}
		}
		$ArtifactHashes[$PluginName] =
			(Get-FileHash -LiteralPath $QualifiedZipPath -Algorithm SHA256).Hash
		}
		finally {
			$QualifiedZipLock.Dispose()
		}
	}

	$script:PluginSourceRoot = $StageRoot
	$script:ArtifactVersion = $ExpectedVersion
	Write-Host `
		"[ConsumerMatrix] qualifying exact plugin artifacts v$ExpectedVersion from '$ResolvedArtifacts'." `
		-ForegroundColor Green
}

function Invoke-ManifestBootstrap(
	[string] $ProfileName,
	[string] $Uproject,
	[string] $ScriptPath,
	[string] $ManifestPath,
	[string] $ProjectRoot)
{
	Write-Host `
		"[ConsumerMatrix] $ProfileName bootstrap manifest generation" `
		-ForegroundColor Cyan
	$BootstrapOutput = @(& $EditorCmd @(
		$Uproject,
		'-run=pythonscript',
		"-script=$ScriptPath",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout') 2>&1)
	$BootstrapExitCode = $LASTEXITCODE
	$BootstrapLines = @($BootstrapOutput | ForEach-Object { "$_" })
	$BootstrapLines | ForEach-Object { Write-Host $_ }
	$BootstrapLog = Join-Path `
		$ProjectRoot 'Saved\BootstrapManifest.log'
	Write-Utf8NoBom $BootstrapLog ($BootstrapLines -join "`r`n")

	if (-not (Test-Path -LiteralPath $ManifestPath)) {
		throw "$ProfileName bootstrap manifest generation produced no '$ManifestPath'."
	}
	if (-not ($BootstrapLines -match 'Generated simulation-content manifest')) {
		throw "$ProfileName bootstrap produced an asset but no generation-success record. See '$BootstrapLog'."
	}
	if ($BootstrapExitCode -eq 0) {
		return
	}

	# The bootstrap world necessarily starts before the newly generated profile
	# exists. It may therefore see either no configured manifest or a stale
	# contributor-set profile. Permit only those exact protocol errors; the next
	# editor invocation must start clean from the generated asset.
	$UnexpectedErrors = @($BootstrapLines | Where-Object {
		$_ -match 'Error:' -and
		$_ -notmatch 'Configured Simulation Content Manifest .* could not be loaded' -and
		$_ -notmatch 'Simulation-content container has no exact profile for the active contributor set' -and
		$_ -notmatch 'pool-object codec manifest could not freeze'
	})
	if ($BootstrapExitCode -ne 1 -or $UnexpectedErrors.Count -gt 0) {
		throw "$ProfileName bootstrap manifest process failed unexpectedly (exit $BootstrapExitCode). See '$BootstrapLog'."
	}
	Write-Host `
		"[ConsumerMatrix] $ProfileName accepted the one-time stale-manifest bootstrap diagnostics." `
		-ForegroundColor DarkYellow
}

function Copy-CleanPlugin([string] $PluginName, [string] $ProjectRoot)
{
	$Source = Join-Path $PluginSourceRoot $PluginName
	$Destination = Join-Path $ProjectRoot "Plugins\$PluginName"
	if (-not (Test-Path -LiteralPath $Source)) {
		throw "Production plugin '$PluginName' is missing."
	}
	if ($ArtifactDirectory) {
		$PluginParent = Split-Path -Parent $Destination
		New-Item -ItemType Directory -Path $PluginParent -Force | Out-Null
		Copy-Item -LiteralPath $Source -Destination $PluginParent -Recurse
		return
	}
	New-Item -ItemType Directory -Path $Destination -Force | Out-Null

	$Descriptor = Join-Path $Source "$PluginName.uplugin"
	Copy-Item -LiteralPath $Descriptor -Destination $Destination
	foreach ($DirectoryName in @('Source', 'Content', 'Config', 'Resources', 'Shaders')) {
		$Directory = Join-Path $Source $DirectoryName
		if (Test-Path -LiteralPath $Directory) {
			Copy-Item -LiteralPath $Directory -Destination $Destination -Recurse
		}
	}
}

function Refresh-ConsumerPlugins(
	[string[]] $PluginNames,
	[string] $ProjectRoot)
{
	$ResolvedGeneratedRoot = [System.IO.Path]::GetFullPath($GeneratedRoot)
	$ResolvedProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
	if (-not $ResolvedProjectRoot.StartsWith(
		$ResolvedGeneratedRoot + [System.IO.Path]::DirectorySeparatorChar,
		[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to refresh consumer plugins outside '$GeneratedRoot'."
	}
	foreach ($PluginName in $PluginNames) {
		$SourceRoot = Join-Path $PluginSourceRoot $PluginName
		$DestinationRoot = Join-Path $ProjectRoot "Plugins\$PluginName"
		New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null

		$SourceDescriptor = Join-Path $SourceRoot "$PluginName.uplugin"
		$DestinationDescriptor = Join-Path $DestinationRoot "$PluginName.uplugin"
		$SourceDescriptorInfo = Get-Item -LiteralPath $SourceDescriptor
		$DestinationDescriptorInfo = Get-Item `
			-LiteralPath $DestinationDescriptor -ErrorAction SilentlyContinue
		if (
			-not $DestinationDescriptorInfo -or
			$SourceDescriptorInfo.Length -ne $DestinationDescriptorInfo.Length -or
			$SourceDescriptorInfo.LastWriteTimeUtc -ne
				$DestinationDescriptorInfo.LastWriteTimeUtc
		) {
			Copy-Item -LiteralPath $SourceDescriptor `
				-Destination $DestinationDescriptor -Force
		}

		foreach ($DirectoryName in @(
			'Source', 'Content', 'Config', 'Resources', 'Shaders')) {
			$SourceDirectory = Join-Path $SourceRoot $DirectoryName
			$DestinationDirectory = Join-Path $DestinationRoot $DirectoryName
			if (-not (Test-Path -LiteralPath $SourceDirectory)) {
				if (Test-Path -LiteralPath $DestinationDirectory) {
					Remove-Item -LiteralPath $DestinationDirectory -Recurse -Force
				}
				continue
			}

			New-Item -ItemType Directory `
				-Path $DestinationDirectory -Force | Out-Null
			$SourceFiles = @{}
			foreach ($SourceFile in Get-ChildItem `
				-LiteralPath $SourceDirectory -Recurse -File) {
				$Relative = Get-ChildRelativePath `
					$SourceDirectory $SourceFile.FullName
				$SourceFiles[$Relative] = $SourceFile
			}
			foreach ($DestinationFile in Get-ChildItem `
				-LiteralPath $DestinationDirectory -Recurse -File) {
				$Relative = Get-ChildRelativePath `
					$DestinationDirectory $DestinationFile.FullName
				if (-not $SourceFiles.ContainsKey($Relative)) {
					Remove-Item -LiteralPath $DestinationFile.FullName -Force
				}
			}
			foreach ($Pair in $SourceFiles.GetEnumerator()) {
				$DestinationFile = Join-Path $DestinationDirectory $Pair.Key
				New-Item -ItemType Directory `
					-Path (Split-Path -Parent $DestinationFile) `
					-Force | Out-Null
				Copy-Item -LiteralPath $Pair.Value.FullName `
					-Destination $DestinationFile -Force
			}
		}
	}
}

function Assert-NoHostGameDependency([string] $ProjectRoot)
{
	# Byte-level scan for host example-content paths across every consumer
	# file, binary assets included — a self-contained equivalent of the
	# previous binary ripgrep check, so the gate needs no
	# external tool. Latin1 maps bytes 1:1 onto chars, making the ordinal
	# Contains() a raw byte search identical to ripgrep's binary mode.
	$Needle = '/Game/SeinARTSExamples'
	$ExcludedSegments = @('\Binaries\', '\Intermediate\', '\Saved\')
	$Forbidden = @(Get-ChildItem -LiteralPath $ProjectRoot -Recurse -File |
		Where-Object {
			$Relative = $_.FullName.Substring($ProjectRoot.Length)
			-not ($ExcludedSegments | Where-Object { $Relative.Contains($_) })
		} |
		Where-Object {
			[System.IO.File]::ReadAllText(
				$_.FullName, [System.Text.Encoding]::Latin1).Contains($Needle)
		} |
		ForEach-Object { $_.FullName })
	if ($Forbidden) {
		throw "Generated consumer contains forbidden host example-content references:`n$($Forbidden -join "`n")"
	}
}

function Install-ConsumerRuntimeQualificationTemplates(
	[string] $ProjectRoot,
	[string] $ProfileName)
{
	$TemplateNames = [System.Collections.Generic.List[string]]::new()
	foreach ($TemplateName in @(
		'SeinConsumerQualificationSubsystem.h',
		'SeinConsumerQualificationSubsystem.cpp')) {
		$TemplateNames.Add($TemplateName)
	}
	$MovementTemplateNames = @(
		'SeinConsumerMovementQualification.h',
		'SeinConsumerMovementQualification.cpp')
	if ($ProfileName -eq 'MovementPlus') {
		foreach ($TemplateName in $MovementTemplateNames) {
			$TemplateNames.Add($TemplateName)
		}
	} else {
		foreach ($TemplateName in $MovementTemplateNames) {
			$Destination = Join-Path $ProjectRoot "Source\SeinConsumer\$TemplateName"
			if (Test-Path -LiteralPath $Destination) {
				Remove-Item -LiteralPath $Destination -Force
			}
		}
	}
	foreach ($TemplateName in $TemplateNames) {
		$TemplatePath = Join-Path $PSScriptRoot "Templates\$TemplateName"
		if (-not (Test-Path -LiteralPath $TemplatePath)) {
			throw "Consumer runtime qualification template is missing: '$TemplatePath'."
		}
		Copy-Item -LiteralPath $TemplatePath `
			-Destination (Join-Path $ProjectRoot "Source\SeinConsumer\$TemplateName") `
			-Force
	}
}

function Install-PublicHeaderAuditModules(
	[string[]] $PluginNames,
	[string] $ProjectRoot)
{
	$ModuleEntries = [System.Collections.Generic.List[object]]::new()
	$ModuleNames = [System.Collections.Generic.List[string]]::new()
	$HeaderManifest = [ordered]@{}
	$HeaderCount = 0

	foreach ($PluginName in $PluginNames) {
		$PluginRoot = Join-Path $PluginSourceRoot $PluginName
		$DescriptorPath = Join-Path $PluginRoot "$PluginName.uplugin"
		$Descriptor = Get-Content -Raw -LiteralPath $DescriptorPath |
			ConvertFrom-Json
		foreach ($Module in @($Descriptor.Modules)) {
			$ProductionModuleName = [string]$Module.Name
			$PublicRoot = Join-Path $PluginRoot (
				"Source\$ProductionModuleName\Public")
			if (-not (Test-Path -LiteralPath $PublicRoot -PathType Container)) {
				continue
			}
			$Headers = @(Get-ChildItem -LiteralPath $PublicRoot -Recurse -File |
				Where-Object { $_.Extension -in @('.h', '.hpp', '.inl') } |
				Sort-Object FullName)
			if ($Headers.Count -eq 0) {
				continue
			}

			$AuditModuleName = (
				"SeinHeaderAudit_$ProductionModuleName" -replace '[^A-Za-z0-9_]', '_')
			$AuditRoot = Join-Path $ProjectRoot "Source\$AuditModuleName"
			New-Item -ItemType Directory -Path $AuditRoot -Force | Out-Null
			$BuildCs = @"
using UnrealBuildTool;

public class $AuditModuleName : ModuleRules
{
	public $AuditModuleName(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		PrivateDependencyModuleNames.AddRange(
			new string[] { "Core", "$ProductionModuleName" });
	}
}
"@
			Write-Utf8NoBom (
				Join-Path $AuditRoot "$AuditModuleName.Build.cs") $BuildCs
			Write-Utf8NoBom (
				Join-Path $AuditRoot "$AuditModuleName.cpp") @"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, $AuditModuleName)
"@

			$RelativeHeaders = [System.Collections.Generic.List[string]]::new()
			for ($HeaderIndex = 0;
				$HeaderIndex -lt $Headers.Count;
				++$HeaderIndex) {
				$RelativeHeader = (Get-ChildRelativePath `
					$PublicRoot $Headers[$HeaderIndex].FullName).Replace('\', '/')
				$RelativeHeaders.Add($RelativeHeader)
				$HeaderSource = ('#include "{0}"' -f $RelativeHeader) +
					[Environment]::NewLine
				Write-Utf8NoBom (
					Join-Path $AuditRoot (
						'Header{0:D4}.cpp' -f $HeaderIndex)) $HeaderSource
				++$HeaderCount
			}
			$HeaderManifest[$ProductionModuleName] = $RelativeHeaders
			$ModuleNames.Add($AuditModuleName)
			$ModuleEntries.Add([ordered]@{
				Name = $AuditModuleName
				Type = 'Editor'
				LoadingPhase = 'Default'
			})
		}
	}

	$ManifestPath = Join-Path $ProjectRoot 'Saved\PublicHeaderAudit.json'
	Write-Utf8NoBom $ManifestPath ([ordered]@{
		schemaVersion = 1
		headerCount = $HeaderCount
		modules = $HeaderManifest
	} | ConvertTo-Json -Depth 8)
	return [pscustomobject]@{
		ModuleEntries = @($ModuleEntries)
		ModuleNames = @($ModuleNames)
		HeaderCount = $HeaderCount
		ManifestPath = $ManifestPath
	}
}

function New-ConsumerProject([string] $ProfileName)
{
	$ProjectRoot = Join-Path $GeneratedRoot $ProfileName
	$ResolvedGeneratedRoot = [System.IO.Path]::GetFullPath($GeneratedRoot)
	$ResolvedProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
	if (-not $ResolvedProjectRoot.StartsWith(
			$ResolvedGeneratedRoot + [System.IO.Path]::DirectorySeparatorChar,
			[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to clean consumer path outside '$ResolvedGeneratedRoot'."
	}
	if (Test-Path -LiteralPath $ProjectRoot) {
		Remove-Item -LiteralPath $ProjectRoot -Recurse -Force
	}
	New-Item -ItemType Directory -Path $ProjectRoot -Force | Out-Null

	$Plugins = @('SeinARTSFramework')
	$ModuleDependencies = @(
		'Core',
		'CoreUObject',
		'Engine',
		'SeinARTSCore',
		'SeinARTSFramework',
		'SeinARTSCoreEntity',
		'SeinARTSNet')
	$Definitions = @()
	$ExtraIncludes = @()
	$HeaderProof = @()
	if ($ProfileName -in @('MovementPlus', 'Full')) {
		$Plugins += 'SeinARTSMovementPlusExtension'
		$ModuleDependencies += @(
			'GameplayTags',
			'SeinARTSMovement',
			'SeinARTSMovementPlus',
			'SeinARTSNavigation')
		$Definitions += 'SEIN_CONSUMER_WITH_MOVEMENT_PLUS=1'
		$ExtraIncludes += '#include "Movement/SeinWheeledVehicleMovement.h"'
		$HeaderProof += '(void)USeinWheeledVehicleMovement::StaticClass();'
	}
	if ($ProfileName -eq 'MovementPlus') {
		$Definitions += 'SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS=1'
	}
	if ($ProfileName -in @('Cover', 'Full')) {
		$Plugins += 'SeinARTSCoverExtension'
		$ModuleDependencies += 'SeinARTSCover'
		$Definitions += 'SEIN_CONSUMER_WITH_COVER=1'
		$ExtraIncludes += '#include "Components/SeinCoverComponent.h"'
		$HeaderProof += '(void)FSeinCoverComponent::StaticStruct();'
	}
	if ($ProfileName -in @('Squad', 'Full')) {
		$Plugins += 'SeinARTSSquadExtension'
		$ModuleDependencies += 'SeinARTSSquad'
		$Definitions += 'SEIN_CONSUMER_WITH_SQUAD=1'
		$ExtraIncludes += '#include "SeinSquadDispatchResolver.h"'
		$HeaderProof += '(void)USeinSquadDispatchResolver::StaticClass();'
	}
	if ($ProfileName -eq 'Full') {
		$Plugins += 'SeinARTSCoverSquadExtension'
		$ModuleDependencies += 'SeinARTSCoverSquad'
		$Definitions += 'SEIN_CONSUMER_WITH_FULL_EXTENSIONS=1'
		$ExtraIncludes += '#include "SeinCoverAwareSquadDispatchResolver.h"'
		$HeaderProof += '(void)USeinCoverAwareSquadDispatchResolver::StaticClass();'
	}
	if ($ProfileName -in @('OnlineServices', 'Full')) {
		$Plugins += 'SeinARTSOnlineServicesExtension'
		$ModuleDependencies += 'SeinARTSOnlineServices'
		$Definitions += 'SEIN_CONSUMER_WITH_ONLINE_SERVICES=1'
		$ExtraIncludes += '#include "Subsystem/SeinOnlineServicesSubsystem.h"'
		$HeaderProof += '(void)USeinOnlineServicesSubsystem::StaticClass();'
	}

	foreach ($PluginName in $Plugins) {
		Copy-CleanPlugin $PluginName $ProjectRoot
	}
	$HeaderAudit = if ($AuditPublicHeaders) {
		Install-PublicHeaderAuditModules $Plugins $ProjectRoot
	} else {
		[pscustomobject]@{
			ModuleEntries = @()
			ModuleNames = @()
			HeaderCount = 0
			ManifestPath = $null
		}
	}

	$PluginEntries = @(
		foreach ($PluginName in $Plugins) {
			[ordered]@{ Name = $PluginName; Enabled = $true }
		}
		[ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
		[ordered]@{ Name = 'EditorScriptingUtilities'; Enabled = $true }
		[ordered]@{ Name = 'OnlineSubsystemNull'; Enabled = $true }
	)
	$ProjectModules = @(
		[ordered]@{
			Name = 'SeinConsumer'
			Type = 'Runtime'
			LoadingPhase = 'Default'
		}
		$HeaderAudit.ModuleEntries
	)
	$Uproject = [ordered]@{
		FileVersion = 3
		EngineAssociation = '5.8'
		Category = 'SeinARTS Consumer Matrix'
		Description = "Generated clean $ProfileName consumer"
		Modules = $ProjectModules
		Plugins = $PluginEntries
	}
	$UprojectPath = Join-Path $ProjectRoot 'SeinConsumer.uproject'
	Write-Utf8NoBom $UprojectPath ($Uproject | ConvertTo-Json -Depth 8)

	$TargetCommon = @'
using UnrealBuildTool;
using System.Collections.Generic;

public class SeinConsumerTarget : TargetRules
{
	public SeinConsumerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SeinConsumer");
	}
}
'@
	$TargetEditor = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerEditorTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerEditorTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Editor')
	if ($HeaderAudit.ModuleNames.Count -gt 0) {
		$AuditTargetLines = ($HeaderAudit.ModuleNames | ForEach-Object {
			"`t`tExtraModuleNames.Add(`"$_`");"
		}) -join "`r`n"
		$TargetEditor = $TargetEditor.Replace(
			'ExtraModuleNames.Add("SeinConsumer");',
			"ExtraModuleNames.Add(`"SeinConsumer`");`r`n$AuditTargetLines")
	}
	$TargetClient = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerClientTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerClientTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Client')
	$TargetServer = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerServerTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerServerTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Server')
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer.Target.cs') $TargetCommon
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerEditor.Target.cs') $TargetEditor
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerClient.Target.cs') $TargetClient
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerServer.Target.cs') $TargetServer

	$QuotedDependencies = ($ModuleDependencies | Sort-Object -Unique |
		ForEach-Object { "`"$_`"" }) -join ', '
	$DefinitionLines = ($Definitions | ForEach-Object {
		"`t`tPublicDefinitions.Add(`"$_`");"
	}) -join "`r`n"
	$BuildCs = @"
using UnrealBuildTool;

public class SeinConsumer : ModuleRules
{
	public SeinConsumer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { $QuotedDependencies });
$DefinitionLines
	}
}
"@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer\SeinConsumer.Build.cs') $BuildCs

	$IncludeLines = $ExtraIncludes -join "`r`n"
	$ProofLines = ($HeaderProof | ForEach-Object { "`t$_" }) -join "`r`n"
	$ModuleCpp = @"
#include "Modules/ModuleManager.h"
#include "Abilities/SeinAbility.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Serialization/SeinSimulationContentRegistry.h"
$IncludeLines
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
#include "SeinConsumerMovementQualification.h"
#endif

namespace
{
	void CompileConsumerSurfaceProof()
	{
		(void)USeinAbility::StaticClass();
$ProofLines
	}
}

class FSeinConsumerModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath =
			USeinConsumerQualificationMoveAbility::StaticClass()->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;

		FSeinSimulationContentContributorDescriptor Descriptor;
		Descriptor.StableContributorId = TEXT("seinconsumer.qualification");
		Descriptor.ContributorRevision = 1;
		Descriptor.DiscoveryRoots.Add(MoveTemp(Root));
		FString Error;
		SimulationContentHandle =
			FSeinSimulationContentRegistry::RegisterContributor(
				Descriptor, &Error);
		if (!SimulationContentHandle.IsValid())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Consumer qualification content registration failed: %s"),
				*Error);
		}

		FSeinPoolObjectCodecDescriptor CodecDescriptor;
		CodecDescriptor.NativeAnchor =
			USeinConsumerQualificationMoveAbility::StaticClass();
		CodecDescriptor.Kind = ESeinPoolObjectKind::Ability;
		CodecDescriptor.StableProviderId =
			TEXT("seinconsumer.qualification.move.reflection");
		CodecDescriptor.StateSchemaVersion = 2;
		CodecDescriptor.BehaviorRevision = 1;
		CodecDescriptor.CodecRevision = 3;
		CodecDescriptor.MaxStateBytes =
			FSeinPoolObjectCodecRegistry::MaxStateBytes;
		PoolObjectCodecHandle = FSeinPoolObjectCodecRegistry::Register(
			FName(TEXT("seinconsumer")),
			CodecDescriptor,
			FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
			&Error);
		if (!PoolObjectCodecHandle.IsValid())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Consumer qualification ability codec registration failed: %s"),
				*Error);
		}
#endif
	}

	virtual void ShutdownModule() override
	{
		PoolObjectCodecHandle.Reset();
		SimulationContentHandle.Reset();
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	FSeinPoolObjectCodecRegistrationHandle PoolObjectCodecHandle;
	FSeinSimulationContentRegistrationHandle SimulationContentHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSeinConsumerModule, SeinConsumer, "SeinConsumer");
"@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer\SeinConsumer.cpp') $ModuleCpp
	$DefaultEngine = @'
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Game/Maps/ConsumerLobbyMap
EditorStartupMap=/Game/Maps/ConsumerLobbyMap
GlobalDefaultGameMode=/Script/SeinARTSFramework.SeinGameMode

[/Script/Engine.Engine]
WorldSettingsClassName=/Script/SeinARTSFramework.SeinWorldSettings

[/Script/Engine.RendererSettings]
r.AllowStaticLighting=False
r.GenerateMeshDistanceFields=False
r.DynamicGlobalIlluminationMethod=0
r.ReflectionMethod=0
r.RayTracing=False

[CoreRedirects]
+ClassRedirects=(OldName="/Script/SeinARTSEditor.SeinWidgetBlueprint",NewName="/Script/SeinARTSGraphNodes.SeinWidgetBlueprint")

[OnlineSubsystem]
DefaultPlatformService=Null

[OnlineSubsystemNull]
bAutoLoginAtStartup=True
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Config\DefaultEngine.ini') $DefaultEngine

	$NavigationClassPath = if ($ProfileName -eq 'MovementPlus') {
		'/Script/SeinConsumer.SeinConsumerQualificationNavigation'
	} else {
		'/Script/SeinARTSNavigation.SeinNavigationAStar'
	}
	$DefaultGame = @'
[/Script/EngineSettings.GeneralProjectSettings]
ProjectID=E42D638747C4108CCF59B1A7AB1A57D4

[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
SimulationContentManifest=/Game/SeinARTS/SeinSimulationContentManifest.SeinSimulationContentManifest
DefaultBrokerResolverClass=/Script/SeinARTSCoreEntity.SeinDefaultCommandBrokerResolver
NavigationClass=__SEIN_CONSUMER_NAVIGATION_CLASS__
LevelDataClass=/Script/SeinARTSLevelData.SeinLevelDataDefault
FogOfWarClass=/Script/SeinARTSFogOfWar.SeinFogOfWarDefault
AvoidanceClass=/Script/SeinARTSMovement.SeinAvoidanceDefault
CollisionResolverClass=/Script/SeinARTSCoreEntity.SeinCollisionResolverDefault
DefaultFormation=/Script/SeinARTSCoreEntity.SeinRingFormation
FormationPreviewActorClass=/Script/SeinARTSFramework.SeinFormationPreviewActor
bNetworkingEnabled=True
TurnRate=10
InputDelayTurns=2
MaxPlayers=2
RelayActorClass=/Script/SeinARTSNet.SeinNetRelay
bDeterminismChecksEnabled=True
bConfigParityCheckEnabled=True
DeterminismCheckIntervalTurns=5
ReplayCheckpointIntervalTurns=30
ReplayTurnBatchSize=4
ReplayMaxFileSizeMiB=512
DroppedToAITakeoverSeconds=30.0
DebugFixedSessionSeed=12345
LobbyReconnectGraceSeconds=60.0

[/Script/UnrealEd.ProjectPackagingSettings]
+MapsToCook=(FilePath="/Game/Maps/ConsumerLobbyMap")
+MapsToCook=(FilePath="/Game/Maps/ConsumerMap")
'@
	$DefaultGame = $DefaultGame.Replace(
		'__SEIN_CONSUMER_NAVIGATION_CLASS__', $NavigationClassPath)
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Config\DefaultGame.ini') $DefaultGame

	$MovementFixtureClassExpression = if ($ProfileName -eq 'MovementPlus') {
		'unreal.load_class(None, "/Script/SeinConsumer.SeinConsumerMovementUnit")'
	} else {
		'None'
	}
	$CreateMapPy = @'
import unreal

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

lobby_path = "/Game/Maps/ConsumerLobbyMap"
if not unreal.EditorAssetLibrary.does_asset_exist(lobby_path):
    if not level_editor.new_level(lobby_path):
        raise RuntimeError("Could not create consumer-owned lobby map " + lobby_path)
if not unreal.EditorAssetLibrary.save_asset(lobby_path, only_if_is_dirty=False):
    raise RuntimeError("Could not save consumer-owned lobby map " + lobby_path)

match_path = "/Game/Maps/ConsumerMap"
if not unreal.EditorAssetLibrary.does_asset_exist(match_path):
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.new_level(match_path):
        raise RuntimeError("Could not create consumer-owned match map " + match_path)
else:
    if not level_editor.load_level(match_path):
        raise RuntimeError("Could not load consumer-owned match map " + match_path)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("auto_start_sim", False)
starts = [
    actor for actor in actor_editor.get_all_level_actors()
    if isinstance(actor, unreal.SeinPlayerStart)
]
if not starts:
    for slot, x in ((1, -200.0), (2, 200.0)):
        start = actor_editor.spawn_actor_from_class(
            unreal.SeinPlayerStart,
            unreal.Vector(x, 0.0, 100.0),
            unreal.Rotator(0.0, 0.0, 0.0)
        )
        if start is None:
            raise RuntimeError("Could not spawn SeinPlayerStart for slot " + str(slot))
        start.set_editor_property("player_slot", slot)
        start.set_actor_label("ConsumerPlayerStart_" + str(slot))
        starts.append(start)
movement_unit_class = __SEIN_MOVEMENT_FIXTURE_CLASS__
for start in starts:
    slot = start.get_editor_property("player_slot")
    spawn_class = movement_unit_class if slot == 2 else None
    start.set_editor_property("spawn_entity", spawn_class)
if not unreal.EditorAssetLibrary.save_asset(match_path, only_if_is_dirty=False):
    raise RuntimeError("Could not save consumer-owned match map " + match_path)
'@
	$CreateMapPy = $CreateMapPy.Replace(
		'__SEIN_MOVEMENT_FIXTURE_CLASS__', $MovementFixtureClassExpression)
	Write-Utf8NoBom (Join-Path $ProjectRoot 'CreateConsumerMap.py') $CreateMapPy

	$GenerateManifestPy = @'
import unreal

manifest_path = "/Game/SeinARTS/SeinSimulationContentManifest"
unreal.SystemLibrary.execute_console_command(
    None, "Sein.SimulationContent.GenerateManifest"
)
if not unreal.EditorAssetLibrary.does_asset_exist(manifest_path):
    raise RuntimeError("Manifest generation produced no " + manifest_path)
unreal.log("Verified generated manifest " + manifest_path)
'@
	Write-Utf8NoBom `
		(Join-Path $ProjectRoot 'GenerateSimulationContentManifest.py') `
		$GenerateManifestPy

	$ExpectedMovementFixtureClassPath = if ($ProfileName -eq 'MovementPlus') {
		'/Script/SeinConsumer.SeinConsumerMovementUnit'
	} else {
		''
	}
	$VerifyMapPy = @'
import unreal

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for asset_path in ("/Game/Maps/ConsumerLobbyMap", "/Game/Maps/ConsumerMap"):
    if not level_editor.load_level(asset_path):
        raise RuntimeError("Could not load consumer-owned map " + asset_path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or not world.get_path_name().startswith(asset_path + "."):
        actual = "<none>" if world is None else world.get_path_name()
        raise RuntimeError(
            "Consumer map load resolved the wrong editor world: " + actual
        )
    if asset_path.endswith("ConsumerMap"):
        starts = [
            actor for actor in actor_editor.get_all_level_actors()
            if isinstance(actor, unreal.SeinPlayerStart)
        ]
        slots = sorted(actor.get_editor_property("player_slot") for actor in starts)
        if slots != [1, 2]:
            raise RuntimeError("Consumer match map has wrong player slots: " + str(slots))
        expected_fixture = "__SEIN_EXPECTED_MOVEMENT_FIXTURE_CLASS__"
        fixture_paths = {}
        for start in starts:
            spawn_class = start.get_editor_property("spawn_entity")
            fixture_paths[start.get_editor_property("player_slot")] = (
                "" if spawn_class is None else spawn_class.get_path_name()
            )
        expected_paths = {1: "", 2: expected_fixture}
        if fixture_paths != expected_paths:
            raise RuntimeError(
                "Consumer match map has wrong spawn fixtures: " + str(fixture_paths)
            )
    unreal.log("Verified loaded consumer world " + world.get_path_name())
'@
	$VerifyMapPy = $VerifyMapPy.Replace(
		'__SEIN_EXPECTED_MOVEMENT_FIXTURE_CLASS__',
		$ExpectedMovementFixtureClassPath)
	Write-Utf8NoBom (Join-Path $ProjectRoot 'VerifyConsumerMap.py') $VerifyMapPy
	Write-Utf8NoBom `
		(Join-Path $ProjectRoot '.consumer-matrix-schema') `
		("$ConsumerGenerationSchemaVersion`n")

	Assert-NoHostGameDependency $ProjectRoot
	return [pscustomobject]@{
		Name = $ProfileName
		Root = $ProjectRoot
		Uproject = $UprojectPath
		Plugins = $Plugins
		PublicHeaderCount = $HeaderAudit.HeaderCount
		PublicHeaderManifest = $HeaderAudit.ManifestPath
	}
}

function Enable-ConsumerRuntimeMapConfiguration([string] $ProjectRoot)
{
	$ConfigPath = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
	$ConfigText = Get-Content -Raw -LiteralPath $ConfigPath
	$Marker = '; ConsumerMatrix runtime-map configuration'
	if ($ConfigText.Contains($Marker)) {
		return
	}
	$RuntimeConfig = @'

; ConsumerMatrix runtime-map configuration
[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
+AvailableMaps=(Map="/Game/Maps/ConsumerMap.ConsumerMap",DisplayName=NSLOCTEXT("[SeinConsumer]", "ConsumerMap", "Consumer Match"),SlotCount=2,TeamCount=2)
DefaultGameplayMap=/Game/Maps/ConsumerMap.ConsumerMap

'@
	Write-Utf8NoBom $ConfigPath ($ConfigText.TrimEnd() + $RuntimeConfig)
}

function Invoke-ConsumerProfile([string] $ProfileName)
{
	$StartedAt = [DateTime]::UtcNow
	$RuntimeResult = $null
	$RuntimeReport = $null
	$InstallationDiagnosticReceipt = $null
	$ExistingRoot = Join-Path $GeneratedRoot $ProfileName
	$ExistingUproject = Join-Path $ExistingRoot 'SeinConsumer.uproject'
	$ExistingSchemaPath = Join-Path $ExistingRoot '.consumer-matrix-schema'
	$CanReuseGenerated = $ReuseGenerated `
		-and (Test-Path -LiteralPath $ExistingUproject) `
		-and (Test-Path -LiteralPath $ExistingSchemaPath) `
		-and (Get-Content -Raw -LiteralPath $ExistingSchemaPath).Trim() -eq
			[string]$ConsumerGenerationSchemaVersion
	if ($CanReuseGenerated) {
		$ExistingPlugins = @('SeinARTSFramework')
		if ($ProfileName -in @('MovementPlus', 'Full')) {
			$ExistingPlugins += 'SeinARTSMovementPlusExtension'
		}
		if ($ProfileName -in @('Cover', 'Full')) {
			$ExistingPlugins += 'SeinARTSCoverExtension'
		}
		if ($ProfileName -in @('Squad', 'Full')) {
			$ExistingPlugins += 'SeinARTSSquadExtension'
		}
		if ($ProfileName -eq 'Full') {
			$ExistingPlugins += 'SeinARTSCoverSquadExtension'
		}
		if ($ProfileName -in @('OnlineServices', 'Full')) {
			$ExistingPlugins += 'SeinARTSOnlineServicesExtension'
		}
		$Project = [pscustomobject]@{
			Name = $ProfileName
			Root = $ExistingRoot
			Uproject = $ExistingUproject
			Plugins = $ExistingPlugins
			PublicHeaderCount = 0
			PublicHeaderManifest = $null
		}
	} else {
		$Project = New-ConsumerProject $ProfileName
	}
	if ($ReuseGenerated) {
		# Reuse keeps expensive generated maps and build products, but source
		# evidence must always come from the current checkout.
		Refresh-ConsumerPlugins $Project.Plugins $Project.Root
	}
	Install-ConsumerRuntimeQualificationTemplates $Project.Root $ProfileName
	# Consumer outputs live beneath Saved/ConsumerMatrix and cannot overlap the
	# loaded host project's modules. UBT's Live Coding mutex keys off the shared
	# UnrealEditor executable path and mistakes the host editor for this generated
	# Editor target, so bypass that guard only after proving no consumer is open.
	Assert-NoRunningConsumerEditor $Project.Uproject
	$CommonBuildArgs = @(
		'Win64', 'Development', "-Project=$($Project.Uproject)",
		'-WaitMutex')
	Invoke-Checked "$ProfileName Editor build" $BuildBat (
		@('SeinConsumerEditor') + $CommonBuildArgs + '-NoHotReloadFromIDE')
	if (-not $SkipClientServer) {
		Invoke-Checked "$ProfileName Client build" $BuildBat (@('SeinConsumerClient') + $CommonBuildArgs)
		Invoke-Checked "$ProfileName Server build" $BuildBat (@('SeinConsumerServer') + $CommonBuildArgs)
	}

	# Bootstrap the configured manifest before constructing a UWorld. The
	# framework intentionally validates manifest ownership at world startup;
	# a generated consumer must satisfy that contract before its first map is
	# created, then regenerate after the map exists so authored world content is
	# part of the final profile.
	$ManifestScript = Join-Path `
		$Project.Root 'GenerateSimulationContentManifest.py'
	$ManifestPath = Join-Path `
		$Project.Root 'Content\SeinARTS\SeinSimulationContentManifest.uasset'
	Invoke-ManifestBootstrap `
		$ProfileName `
		$Project.Uproject `
		$ManifestScript `
		$ManifestPath `
		$Project.Root

	$MapPath = Join-Path $Project.Root 'Content\Maps\ConsumerMap.umap'
	$LobbyMapPath = Join-Path `
		$Project.Root 'Content\Maps\ConsumerLobbyMap.umap'
	if ($ProfileName -eq 'MovementPlus' -or -not ($ReuseGenerated -and
		(Test-Path -LiteralPath $MapPath) -and
		(Test-Path -LiteralPath $LobbyMapPath))) {
		Invoke-Checked "$ProfileName consumer map generation" $EditorCmd @(
			$Project.Uproject,
			'-run=pythonscript',
			"-script=$(Join-Path $Project.Root 'CreateConsumerMap.py')",
			'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')
	}
	if (-not (Test-Path -LiteralPath $MapPath) -or
		-not (Test-Path -LiteralPath $LobbyMapPath)) {
		throw "$ProfileName map generation did not produce both consumer maps."
	}
	Enable-ConsumerRuntimeMapConfiguration $Project.Root

	# Regenerate after the map exists. A builder failure is logged as Error and
	# makes the Python commandlet fail even though the bootstrap asset exists.
	Invoke-Checked "$ProfileName manifest generation" $EditorCmd @(
		$Project.Uproject,
		'-run=pythonscript',
		"-script=$ManifestScript",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')
	if (-not (Test-Path -LiteralPath $ManifestPath)) {
		throw "$ProfileName manifest generation produced no '$ManifestPath'."
	}
	$InstallationDiagnostic = if ($ArtifactDirectory) {
		Join-Path $Project.Root `
			'Plugins\SeinARTSFramework\Tools\Diagnostics\Test-SeinARTSInstallation.ps1'
	} else {
		Join-Path $RepoRoot `
			'Scripts\Diagnostics\Test-SeinARTSInstallation.ps1'
	}
	if (-not (Test-Path -LiteralPath $InstallationDiagnostic -PathType Leaf)) {
		throw "$ProfileName installation diagnostic is missing: '$InstallationDiagnostic'."
	}
	$WindowsPowerShell = Join-Path $env:SystemRoot `
		'System32\WindowsPowerShell\v1.0\powershell.exe'
	$DiagnosticJson = @(& $WindowsPowerShell `
		-NoProfile `
		-ExecutionPolicy Bypass `
		-File $InstallationDiagnostic `
		-Project $Project.Uproject `
		-EngineRoot $EngineRoot `
		-Json)
	if ($LASTEXITCODE -ne 0) {
		throw "$ProfileName installation diagnostic failed with exit code $LASTEXITCODE."
	}
	$DiagnosticReport = ($DiagnosticJson -join "`r`n") | ConvertFrom-Json
	$ExpectedDiagnosticMode = if ($ArtifactDirectory) { 'Release' } else { 'Source' }
	$ExpectedManifestObject =
		'/Game/SeinARTS/SeinSimulationContentManifest.SeinSimulationContentManifest'
	$DiagnosticPlugins = @($DiagnosticReport.enabledProductionPlugins | Sort-Object)
	if ([int]$DiagnosticReport.schemaVersion -ne 1 -or
		[string]$DiagnosticReport.result -cne 'Passed' -or
		[int]$DiagnosticReport.errorCount -ne 0 -or
		-not ([System.IO.Path]::GetFullPath([string]$DiagnosticReport.project).Equals(
			[System.IO.Path]::GetFullPath($Project.Uproject),
			[System.StringComparison]::OrdinalIgnoreCase)) -or
		-not ([System.IO.Path]::GetFullPath([string]$DiagnosticReport.engineRoot).Equals(
			[System.IO.Path]::GetFullPath($EngineRoot),
			[System.StringComparison]::OrdinalIgnoreCase)) -or
		[string]$DiagnosticReport.integrationMode -cne $ExpectedDiagnosticMode -or
		($ArtifactDirectory -and
			[string]$DiagnosticReport.cohortVersion -cne $ArtifactVersion) -or
		-not $DiagnosticReport.cohortVersion -or
		[string]$DiagnosticReport.simulationContentManifest -cne $ExpectedManifestObject -or
		$DiagnosticPlugins.Count -ne $Project.Plugins.Count -or
		(@(Compare-Object $DiagnosticPlugins @($Project.Plugins | Sort-Object))).Count -ne 0) {
		throw "$ProfileName installation diagnostic returned an invalid pass receipt."
	}
	$InstallationDiagnosticReceipt = Join-Path $Project.Root `
		'Saved\Qualification\installation-diagnostic.json'
	Write-Utf8NoBom $InstallationDiagnosticReceipt `
		($DiagnosticReport | ConvertTo-Json -Depth 8)

	Assert-NoHostGameDependency $Project.Root
	Invoke-Checked "$ProfileName uncooked map load" $EditorCmd @(
		$Project.Uproject,
		'-run=pythonscript',
		"-script=$(Join-Path $Project.Root 'VerifyConsumerMap.py')",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')

	# Build Shipping only after the generated maps and final map-selection
	# config exist, so this binary is the exact package/runtime candidate.
	Invoke-Checked "$ProfileName Shipping game build" $BuildBat @(
		'SeinConsumer', 'Win64', 'Shipping',
		"-Project=$($Project.Uproject)", '-WaitMutex')

	$ArchiveRoot = Join-Path $Project.Root 'Saved\Packaged'
	if (-not $SkipCook) {
		Invoke-Checked "$ProfileName clean cook/package" $RunUat @(
			'BuildCookRun',
			"-project=$($Project.Uproject)",
			'-noP4', '-unattended', '-utf8output',
			'-platform=Win64', '-clientconfig=Shipping',
			'-cook', '-stage', '-pak', '-archive',
			"-archivedirectory=$ArchiveRoot",
			'-map=/Game/Maps/ConsumerLobbyMap+/Game/Maps/ConsumerMap',
			'-skipbuild')

		$GameExe = Get-ChildItem -LiteralPath $ArchiveRoot -Recurse -File |
			Where-Object {
				$_.Name -eq 'SeinConsumer-Win64-Shipping.exe'
			} |
			Select-Object -First 1
		if (-not $GameExe) {
			throw "$ProfileName package contains no Shipping game binary."
		}
		$SmokeOut = Join-Path $Project.Root 'Saved\PackagedSmoke.stdout.log'
		$SmokeErr = Join-Path $Project.Root 'Saved\PackagedSmoke.stderr.log'
		$Process = Start-Process -FilePath $GameExe.FullName `
			-ArgumentList @('-unattended', '-nullrhi', '-nosound', '-nosplash') `
			-RedirectStandardOutput $SmokeOut -RedirectStandardError $SmokeErr `
			-WindowStyle Hidden -PassThru
		if (-not $Process.WaitForExit(10000)) {
			# Shipping strips the interactive quit console command. Launch the real
			# game binary (not the root bootstrap executable) so this Process object
			# owns the process that maps the IoStore files. Packaged Game targets boot
			# their configured default map rather than honoring a raw map argument;
			# surviving this window proves that the executable mounted its IoStore
			# payload and opened the cooked lobby without an immediate module/content
			# crash. The Framework runtime leg explicitly travels to the match map.
			Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
			$Process.WaitForExit()
			$Process.Refresh()
			Write-Host `
				"[ConsumerMatrix] $ProfileName packaged game survived the 10-second map startup window." `
				-ForegroundColor Green
		}
		else {
			$Process.Refresh()
			if ($Process.ExitCode -ne 0) {
				throw "$ProfileName packaged map smoke load failed with exit code $($Process.ExitCode)."
			}
		}

		if ($ProfileName -in @('Framework', 'MovementPlus') -and
			-not $SkipRuntimeQualification) {
			$RuntimeScript = Join-Path `
				$PSScriptRoot 'Invoke-PackagedRuntimeQualification.ps1'
			if (-not (Test-Path -LiteralPath $RuntimeScript)) {
				throw "Packaged runtime qualification script is missing: '$RuntimeScript'."
			}
			& $RuntimeScript `
				-GameExecutable $GameExe.FullName `
				-ProjectRoot $Project.Root `
				-Profile $ProfileName `
				-NetworkProfile $RuntimeNetworkProfile
			if ($LASTEXITCODE -ne 0) {
				throw "$ProfileName packaged runtime qualification failed with exit code $LASTEXITCODE."
			}
			$RuntimeResult = Join-Path `
				$Project.Root 'Saved\RuntimeQualification\runtime-result.json'
			if (-not (Test-Path -LiteralPath $RuntimeResult)) {
				throw "$ProfileName runtime qualification produced no formal result."
			}
			$RuntimeReport = Get-Content -Raw -LiteralPath $RuntimeResult |
				ConvertFrom-Json
			if ([int]$RuntimeReport.schemaVersion -ne 5 -or
				[string]$RuntimeReport.profile -cne $ProfileName -or
				[string]$RuntimeReport.networkProfile -cne
					$RuntimeNetworkProfile -or
				[string]$RuntimeReport.executableSha256 -cne
					(Get-FileHash -LiteralPath $GameExe.FullName `
						-Algorithm SHA256).Hash) {
				throw "$ProfileName runtime result disagreed with the requested network profile."
			}
			if ($RuntimeNetworkProfile -eq 'Adverse' -and
				([string]$RuntimeReport.networkFaultInjection -cne 'Passed' -or
				[string]::IsNullOrWhiteSpace(
					[string]$RuntimeReport.networkFaultProxyResultSha256))) {
				throw "$ProfileName adverse runtime result did not prove injected faults."
			}
			if ($RuntimeNetworkProfile -eq 'Adverse') {
				$ProxyResultPath = Join-Path $Project.Root `
					'Saved\RuntimeQualification\proxy-result.json'
				if (-not (Test-Path -LiteralPath $ProxyResultPath -PathType Leaf) -or
					[string]$RuntimeReport.networkFaultProxyResultSha256 -cne
						(Get-FileHash -LiteralPath $ProxyResultPath -Algorithm SHA256).Hash) {
					throw "$ProfileName adverse runtime result did not bind its proxy receipt."
				}
				$ProxyReport = Get-Content -Raw -LiteralPath $ProxyResultPath |
					ConvertFrom-Json
				if ([int]$ProxyReport.schemaVersion -ne 2 -or
					[string]$ProxyReport.status -cne 'Passed' -or
					[string]$ProxyReport.jitterSequence -cne 'PerDirection' -or
					[int]$ProxyReport.latencyMs -ne 60 -or
					[int]$ProxyReport.jitterMs -ne 20 -or
					[int]$ProxyReport.dropEvery -ne 43 -or
					[int]$ProxyReport.duplicateEvery -ne 59 -or
					[int]$ProxyReport.reorderEvery -ne 31 -or
					[int]$ProxyReport.reorderDelayMs -ne 90 -or
					[uint64]$ProxyReport.seed -ne 5368391 -or
					[int64]$ProxyReport.clientEndpointChanges -lt 1 -or
					[int]$ProxyReport.discardedAtShutdown -ne 0 -or
					@(@('clientToServer', 'serverToClient') | Where-Object {
						$Stats = $ProxyReport.$_
						[int64]$Stats.receivedDatagrams -lt 100 -or
						[int64]$Stats.forwardedDatagrams -lt 100 -or
						[int64]$Stats.droppedDatagrams -lt 1 -or
						[int64]$Stats.duplicatedDatagrams -lt 1 -or
						[int64]$Stats.reorderedDatagrams -lt 1 -or
						[int64]$Stats.forwardedDuplicateDatagrams -lt 1 -or
						[int64]$Stats.observedOrderInversions -lt 1 -or
						[double]$Stats.minimumForwardDelayMs -lt 35 -or
						[double]$Stats.maximumForwardDelayMs -lt 120 -or
						[int64]$Stats.unroutableDatagrams -ne 0
					}).Count -ne 0) {
					throw "$ProfileName adverse proxy receipt did not satisfy its fault contract."
				}
			}
			elseif ([string]$RuntimeReport.networkFaultInjection -cne
					'NotApplicable' -or
				-not [string]::IsNullOrWhiteSpace(
					[string]$RuntimeReport.networkFaultProxyResultSha256)) {
				throw "$ProfileName baseline runtime result contains adverse fault evidence."
			}
		}
	}

	$Result = [ordered]@{
		schemaVersion = 7
		qualificationRunId = $QualificationRunId
		profile = $ProfileName
		plugins = $Project.Plugins
		pluginSource = if ($ArtifactDirectory) {
			'PackagedArtifacts'
		} else {
			'RepositorySource'
		}
		artifactVersion = if ($ArtifactDirectory) {
			$ArtifactVersion
		} else {
			$null
		}
		artifactSha256 = if ($ArtifactDirectory) {
			$ProfileHashes = [ordered]@{}
			foreach ($PluginName in $Project.Plugins) {
				$ProfileHashes[$PluginName] = $ArtifactHashes[$PluginName]
			}
			$ProfileHashes
		} else {
			$null
		}
		engine = $EngineRoot
		engineBuildFingerprint = $EngineBuildFingerprint
		publicHeaderAudit = if ($AuditPublicHeaders) { 'Passed' } else { 'Skipped' }
		publicHeaderCount = $Project.PublicHeaderCount
		publicHeaderManifest = $Project.PublicHeaderManifest
		publicHeaderManifestSha256 = if ($Project.PublicHeaderManifest) {
			(Get-FileHash -LiteralPath $Project.PublicHeaderManifest `
				-Algorithm SHA256).Hash
		} else { $null }
		editorBuild = 'Passed'
		clientBuild = if ($SkipClientServer) { 'Skipped' } else { 'Passed' }
		serverBuild = if ($SkipClientServer) { 'Skipped' } else { 'Passed' }
		shippingGameBuild = 'Passed'
		consumerMaps = @(
			'/Game/Maps/ConsumerLobbyMap',
			'/Game/Maps/ConsumerMap')
		consumerManifest = '/Game/SeinARTS/SeinSimulationContentManifest'
		installationDiagnostic = 'Passed'
		installationDiagnosticReceipt = $InstallationDiagnosticReceipt
		installationDiagnosticReceiptSha256 =
			(Get-FileHash -LiteralPath $InstallationDiagnosticReceipt `
				-Algorithm SHA256).Hash
		installationDiagnosticIntegrationMode =
			[string]$DiagnosticReport.integrationMode
		installationDiagnosticCohortVersion =
			[string]$DiagnosticReport.cohortVersion
		installationDiagnosticSourceCohortIdentity =
			[string]$DiagnosticReport.sourceCohortIdentity
		installationDiagnosticManifest =
			[string]$DiagnosticReport.simulationContentManifest
		uncookedLoad = 'Passed'
		cookAndPackagedLoad = if ($SkipCook) { 'Skipped' } else { 'Passed' }
		packagedRuntimeQualification = if (
			$ProfileName -notin @('Framework', 'MovementPlus')) {
			'NotApplicable'
		} elseif ($SkipCook -or $SkipRuntimeQualification) {
			'Skipped'
		} else {
			'Passed'
		}
		runtimeNetworkProfile = if (
			$ProfileName -notin @('Framework', 'MovementPlus') -or
			$SkipCook -or $SkipRuntimeQualification) {
			'NotApplicable'
		} else { $RuntimeNetworkProfile }
		runtimeNetworkFaultInjection = if ($RuntimeReport) {
			[string]$RuntimeReport.networkFaultInjection
		} else { $null }
		runtimeResultSha256 = if ($RuntimeResult) {
			(Get-FileHash -LiteralPath $RuntimeResult -Algorithm SHA256).Hash
		} else { $null }
		startedAtUtc = $StartedAt.ToString('o')
		completedAtUtc = [DateTime]::UtcNow.ToString('o')
	}
	$ResultPath = Join-Path $Project.Root 'matrix-result.json'
	Write-Utf8NoBom $ResultPath ($Result | ConvertTo-Json -Depth 6)
	Write-Host "[ConsumerMatrix] $ProfileName passed: $ResultPath" -ForegroundColor Green
}

New-Item -ItemType Directory -Path $GeneratedRoot -Force | Out-Null
if ($ArtifactDirectory) {
	Initialize-ArtifactPluginSource $ArtifactDirectory
}
if ($ValidateArtifactsOnly) {
	$ValidationResult = [ordered]@{
		schemaVersion = 2
		qualificationRunId = $QualificationRunId
		artifactVersion = $ArtifactVersion
		artifactSha256 = [ordered]@{}
		engine = $EngineRoot
		engineBuildFingerprint = $EngineBuildFingerprint
		validatedAtUtc = [DateTime]::UtcNow.ToString('o')
	}
	foreach ($PluginName in Get-RequiredArtifactPlugins) {
		$ValidationResult.artifactSha256[$PluginName] =
			$ArtifactHashes[$PluginName]
	}
	$ValidationPath = Join-Path $GeneratedRoot 'artifact-validation.json'
	Write-Utf8NoBom $ValidationPath `
		($ValidationResult | ConvertTo-Json -Depth 6)
	Write-Host `
		"[ConsumerMatrix] artifact validation passed: $ValidationPath" `
		-ForegroundColor Green
	return
}
foreach ($ProfileName in $Profiles) {
	Invoke-ConsumerProfile $ProfileName
}
}
finally {
	$PipelineMutex.ReleaseMutex()
	$PipelineMutex.Dispose()
}
