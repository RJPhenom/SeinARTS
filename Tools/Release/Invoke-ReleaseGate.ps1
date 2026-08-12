#Requires -Version 7.0
<#
.SYNOPSIS
  Run the complete SeinARTS build, automation, package, artifact-consumer, and
  optional publication gate.

.DESCRIPTION
  Publication mode is intentionally strict: every gate runs and
  PackagePlugins.ps1 publishes only after exact-ZIP consumer qualification.
  -PackageOnly is the local diagnostic mode and may use explicit skip switches.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)]
	[string] $Version,

	[string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',

	[switch] $PackageOnly,

	[switch] $SkipClientServer,

	[switch] $SkipCook,

	[switch] $SkipRuntimeQualification
)

$ErrorActionPreference = 'Stop'
function Test-SeinSemVer([string] $Candidate)
{
	return $Candidate -match (
		'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)' +
		'(?:-(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)' +
		'(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*)?' +
		'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')
}
if (-not (Test-SeinSemVer $Version)) {
	throw "Version '$Version' is not a valid SemVer 2.0 version."
}
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
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
$LockRoot = Join-Path $RepoRoot 'Saved\Locks'
$LockPath = Join-Path $LockRoot 'release-gate.lock'
try {
	New-Item -ItemType Directory -Path $LockRoot -Force | Out-Null
	$ReleaseGateLock = [System.IO.File]::Open(
		$LockPath,
		[System.IO.FileMode]::OpenOrCreate,
		[System.IO.FileAccess]::ReadWrite,
		[System.IO.FileShare]::None)
}
catch {
	$PipelineMutex.ReleaseMutex()
	$PipelineMutex.Dispose()
	throw "Another SeinARTS release gate owns '$LockPath'. Wait for it to finish before retrying."
}

try {
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
	throw "SeinARTS release gates require UE 5.8; '$EngineRoot' reports $($EngineBuildVersion.MajorVersion).$($EngineBuildVersion.MinorVersion)."
}
$BuildScript = Join-Path $RepoRoot 'Scripts\Build.ps1'
$TestScript = Join-Path $RepoRoot 'Plugins\SeinARTSTestSuite\RunTests.ps1'
$PackageScript = Join-Path $RepoRoot 'Scripts\PackagePlugins.ps1'
$ConsumerScript = Join-Path $RepoRoot 'Tools\ConsumerMatrix\Verify-ConsumerMatrix.ps1'
$DiagnosticScript = Join-Path $RepoRoot `
	'Tools\Diagnostics\Test-SeinARTSInstallation.ps1'
$DiagnosticSelfTestScript = Join-Path $RepoRoot `
	'Tools\Diagnostics\Invoke-InstallationDiagnosticSelfTest.ps1'
$Dist = Join-Path $RepoRoot '.dist'
$ReceiptRoot = Join-Path $RepoRoot 'Saved\ReleaseGate'
$ProductionPlugins = @(
	'SeinARTSFramework',
	'SeinARTSSquadExtension',
	'SeinARTSCoverExtension',
	'SeinARTSMovementPlusExtension',
	'SeinARTSCoverSquadExtension')
$ExpectedTestSuites = @(
	'SeinARTS.Unit',
	'SeinARTS.Integration',
	'SeinARTS.Determinism',
	'SeinARTS.Editor',
	'SeinARTS.Sim',
	'SeinARTS.Perf')
$ExpectedTestProfiles = @('All', 'Framework')
$ExpectedMatrixProfiles = @(
	'Framework', 'Cover', 'Squad', 'MovementPlus', 'Full')
$ReceiptPath = Join-Path $ReceiptRoot (
	'release-gate-{0}.json' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

if (-not $PackageOnly -and
	($SkipClientServer -or $SkipCook -or $SkipRuntimeQualification)) {
	throw 'Publication mode accepts no skip switches. Use -PackageOnly for diagnostic partial gates.'
}

foreach ($RequiredScript in @(
	$BuildScript, $TestScript, $PackageScript, $ConsumerScript,
	$DiagnosticScript, $DiagnosticSelfTestScript)) {
	if (-not (Test-Path -LiteralPath $RequiredScript -PathType Leaf)) {
		throw "Release-gate dependency is missing: '$RequiredScript'."
	}
}

$Commit = (git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $Commit) {
	throw 'Could not resolve release-gate source commit.'
}
$InitialStatus = @(git -C $RepoRoot status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0) {
	throw 'Could not capture release-gate source status.'
}
if (-not $PackageOnly -and $InitialStatus.Count -ne 0) {
	throw 'Publication release gate requires a clean working tree.'
}
if (-not $PackageOnly -and
	-not (Get-Command gh -ErrorAction SilentlyContinue)) {
	throw 'Publication release gate requires the GitHub CLI (gh).'
}
if (-not $PackageOnly) {
	git -C $RepoRoot fetch origin --quiet
	if ($LASTEXITCODE -ne 0) {
		throw 'Could not refresh origin for release-source verification.'
	}
	$OriginMain = (git -C $RepoRoot rev-parse refs/remotes/origin/main).Trim()
	if ($LASTEXITCODE -ne 0 -or -not $OriginMain -or
		$Commit -cne $OriginMain) {
		throw "Publication requires HEAD ($Commit) to be the exact origin/main commit ($OriginMain)."
	}
}

$Steps = [System.Collections.Generic.List[object]]::new()
$Receipt = [ordered]@{
	schemaVersion = 2
	version = $Version
	commit = $Commit
	dirtyWorkingTree = $InitialStatus.Count -ne 0
	engineRoot = $EngineRoot
	engineBuildFingerprint = $EngineBuildFingerprint
	mode = if ($PackageOnly) { 'PackageOnly' } else { 'Publish' }
	startedAtUtc = [DateTime]::UtcNow.ToString('o')
	completedAtUtc = $null
	status = 'Running'
	steps = $Steps
	releaseManifest = $null
	testAttempts = @()
	matrixReceipts = @()
	matrixRunId = $null
	evidenceArchive = $null
	publicationAssets = @()
	postPublicationWarnings = @()
	failure = $null
}
$QualifiedTestAttemptPaths =
	[System.Collections.Generic.List[string]]::new()
$QualifiedMatrixReceiptPaths =
	[System.Collections.Generic.List[string]]::new()

function Assert-ReleaseSourceUnchanged
{
	$CurrentCommit = (git -C $RepoRoot rev-parse HEAD).Trim()
	$CurrentStatus = @(git -C $RepoRoot status --porcelain=v1 --untracked-files=all)
	if ($LASTEXITCODE -ne 0 -or $CurrentCommit -cne $Commit -or
		$CurrentStatus.Count -ne 0) {
		throw 'Release source changed during qualification. Re-run from the intended clean commit.'
	}
}

function Get-FileRecord([string] $Path)
{
	$Item = Get-Item -LiteralPath $Path -ErrorAction Stop
	return [ordered]@{
		file = $Item.Name
		bytes = $Item.Length
		sha256 = (Get-FileHash -LiteralPath $Item.FullName -Algorithm SHA256).Hash
	}
}

function Get-VerifiedReleaseAssets([switch] $RequireQualification)
{
	$ManifestPath = Join-Path $Dist 'release-manifest.json'
	if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
		throw "Release manifest is missing: '$ManifestPath'."
	}
	$Manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
	if ($Manifest.schemaVersion -ne 2 -or
		[string]$Manifest.version -cne $Version -or
		[string]$Manifest.sourceCommit -cne $Commit -or
		$Manifest.sourceDirty -ne $false -or
		[string]$Manifest.engineRoot -cne $EngineRoot -or
		[string]$Manifest.engineBuildFingerprint -cne $EngineBuildFingerprint -or
		[string]$Manifest.targetPlatform -cne 'Win64' -or
		[string]$Manifest.engineVersion -notmatch '^5\.8(?:\.|$)') {
		throw 'Release manifest does not match the qualified clean source, version, engine, or platform.'
	}
	$Records = @($Manifest.artifacts)
	if ($Records.Count -ne $ProductionPlugins.Count) {
		throw 'Release manifest does not contain the exact production-plugin cohort.'
	}
	$Assets = [System.Collections.Generic.List[string]]::new()
	foreach ($PluginName in $ProductionPlugins) {
		$Matching = @($Records | Where-Object {
			[string]$_.plugin -ceq $PluginName })
		if ($Matching.Count -ne 1) {
			throw "Release manifest must contain exactly one '$PluginName' record."
		}
		$Record = $Matching[0]
		$ExpectedFile = "$PluginName.zip"
		$AssetPath = Join-Path $Dist $ExpectedFile
		if ([string]$Record.file -cne $ExpectedFile -or
			[string]$Record.versionName -cne $Version -or
			-not (Test-Path -LiteralPath $AssetPath -PathType Leaf) -or
			[int64]$Record.bytes -ne (Get-Item -LiteralPath $AssetPath).Length -or
			[string]$Record.sha256 -cne
				(Get-FileHash -LiteralPath $AssetPath -Algorithm SHA256).Hash) {
			throw "Qualified artifact bytes changed or provenance is invalid for '$PluginName'."
		}
		$Assets.Add($AssetPath)
	}
	$Assets.Add($ManifestPath)
	if ($RequireQualification) {
		$EvidenceArchive = Join-Path $Dist 'SeinARTS-release-evidence.zip'
		$StableReceipt = Join-Path $Dist 'SeinARTS-release-gate.json'
		if ([string]$Manifest.qualification.evidenceFile -cne
				[System.IO.Path]::GetFileName($EvidenceArchive) -or
			[string]$Manifest.qualification.releaseGateReceipt -cne
				[System.IO.Path]::GetFileName($StableReceipt) -or
			-not (Test-Path -LiteralPath $EvidenceArchive -PathType Leaf) -or
			-not (Test-Path -LiteralPath $StableReceipt -PathType Leaf) -or
			[int64]$Manifest.qualification.evidenceBytes -ne
				(Get-Item -LiteralPath $EvidenceArchive).Length -or
			[string]$Manifest.qualification.evidenceSha256 -cne
				(Get-FileHash -LiteralPath $EvidenceArchive -Algorithm SHA256).Hash) {
			throw 'Release qualification evidence or stable receipt does not match the manifest.'
		}
		$StableReceiptJson = Get-Content -Raw -LiteralPath $StableReceipt |
			ConvertFrom-Json
		if ($StableReceiptJson.schemaVersion -ne 2 -or
			[string]$StableReceiptJson.status -cne 'Qualified' -or
			[string]$StableReceiptJson.version -cne $Version -or
			[string]$StableReceiptJson.commit -cne $Commit -or
			$StableReceiptJson.dirtyWorkingTree -ne $false) {
			throw 'Stable release-gate receipt is not the qualified clean source receipt.'
		}
		$Assets.Add($EvidenceArchive)
		$Assets.Add($StableReceipt)
	}
	return @($Assets)
}

function Get-QualifiedTestAttemptPath(
	[string] $Suite,
	[string] $Profile,
	[DateTime] $InvocationStarted)
{
	$Candidates = @(
		Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'Saved\Automation') `
			-Recurse -File -Filter 'attempt.json' -ErrorAction SilentlyContinue |
			ForEach-Object {
				try {
					$Attempt = Get-Content -Raw -LiteralPath $_.FullName |
						ConvertFrom-Json
					$Started = [DateTime]::Parse(
						[string]$Attempt.startedAtUtc,
						[System.Globalization.CultureInfo]::InvariantCulture,
						[System.Globalization.DateTimeStyles]::RoundtripKind)
					if ([string]$Attempt.suite -ceq $Suite -and
						[string]$Attempt.profile -ceq $Profile -and
						$Started -ge $InvocationStarted) {
						[pscustomobject]@{ Path = $_.FullName; Attempt = $Attempt }
					}
				}
				catch {}
			})
	if ($Candidates.Count -ne 1) {
		throw "Expected exactly one '$Profile' / '$Suite' attempt from this invocation, found $($Candidates.Count)."
	}
	$Candidate = $Candidates[0]
	$Attempt = $Candidate.Attempt
	$ProvenancePath = Join-Path (Split-Path -Parent $Candidate.Path) `
		([string]$Attempt.testBuildProvenanceFile)
	$Provenance = if (Test-Path -LiteralPath $ProvenancePath -PathType Leaf) {
		Get-Content -Raw -LiteralPath $ProvenancePath | ConvertFrom-Json
	} else { $null }
	$IndexPath = Join-Path (Split-Path -Parent $Candidate.Path) `
		([string]$Attempt.testIndexFile)
	$Index = if (Test-Path -LiteralPath $IndexPath -PathType Leaf) {
		Get-Content -Raw -LiteralPath $IndexPath | ConvertFrom-Json
	} else { $null }
	$AttemptStarted = [DateTime]::Parse(
		[string]$Attempt.startedAtUtc,
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind)
	$AttemptCompleted = [DateTime]::Parse(
		[string]$Attempt.completedAtUtc,
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind)
	if ($Attempt.schemaVersion -ne 4 -or
		[string]$Attempt.status -cne 'Passed' -or
		[string]$Attempt.commit -cne $Commit -or
		[bool]$Attempt.dirtyWorkingTree -ne [bool]$Receipt.dirtyWorkingTree -or
		[string]$Attempt.engineRoot -cne $EngineRoot -or
		[string]$Attempt.engineBuildFingerprint -cne $EngineBuildFingerprint -or
		[int]$Attempt.expectedMinimumCount -le 0 -or
		[int]$Attempt.discoveredTestCount -lt [int]$Attempt.expectedMinimumCount -or
		[int]$Attempt.unsuccessfulTestCount -ne 0 -or
		[int]$Attempt.editorExitCode -ne 0 -or
		$AttemptCompleted -lt $AttemptStarted -or
		$null -ne $Attempt.failure -or
		[string]$Attempt.reportPath -cne (Split-Path -Parent $Candidate.Path) -or
		[string]$Attempt.testBuildProvenanceFile -cne 'build-provenance.json' -or
		$null -eq $Provenance -or
		[string]$Attempt.testBuildProvenanceSha256 -cne
			(Get-FileHash -LiteralPath $ProvenancePath -Algorithm SHA256).Hash -or
		$Provenance.schemaVersion -ne 4 -or
		[string]$Provenance.profile -cne $Profile -or
		[string]$Provenance.commit -cne $Commit -or
		[bool]$Provenance.dirtyWorkingTree -ne [bool]$Receipt.dirtyWorkingTree -or
		[string]$Provenance.engineRoot -cne $EngineRoot -or
		[string]$Provenance.engineBuildFingerprint -cne $EngineBuildFingerprint -or
		[string]$Provenance.compileSourceFingerprint -cne
			[string]$Attempt.compileSourceFingerprint -or
		@($Provenance.dllSha256.PSObject.Properties).Count -le 0 -or
		@($Provenance.productionDllSha256.PSObject.Properties).Count -le 0 -or
		@($Provenance.metadataSha256.PSObject.Properties).Count -le 0 -or
		[string]$Attempt.testIndexFile -cne 'index.json' -or
		$null -eq $Index -or
		[string]$Attempt.testIndexSha256 -cne
			(Get-FileHash -LiteralPath $IndexPath -Algorithm SHA256).Hash -or
		@($Index.tests).Count -ne [int]$Attempt.discoveredTestCount -or
		@($Index.tests | Where-Object { [string]$_.state -cne 'Success' }).Count -ne 0 -or
		[int]$Index.failed -ne 0 -or [int]$Index.notRun -ne 0 -or
		[int]$Index.inProcess -ne 0) {
		throw "Test attempt '$($Candidate.Path)' is not a complete, current qualification result."
	}
	return [string]$Candidate.Path
}

function Get-QualifiedMatrixReceiptPath(
	[string] $Profile,
	[DateTime] $InvocationStarted)
{
	$Path = Join-Path $RepoRoot "Saved\ConsumerMatrix\$Profile\matrix-result.json"
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Consumer matrix receipt is missing: '$Path'."
	}
	$Matrix = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
	$Started = [DateTime]::Parse(
		[string]$Matrix.startedAtUtc,
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind)
	$Completed = [DateTime]::Parse(
		[string]$Matrix.completedAtUtc,
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind)
	$ExpectedPlugins = switch ($Profile) {
		'Framework' { @('SeinARTSFramework') }
		'Cover' { @('SeinARTSFramework', 'SeinARTSCoverExtension') }
		'Squad' { @('SeinARTSFramework', 'SeinARTSSquadExtension') }
		'MovementPlus' { @('SeinARTSFramework', 'SeinARTSMovementPlusExtension') }
		'Full' { $ProductionPlugins }
		default { throw "Unknown matrix profile '$Profile'." }
	}
	$ActualPlugins = @($Matrix.plugins | Sort-Object)
	$InstallationDiagnosticReceipt = [string]$Matrix.installationDiagnosticReceipt
	$ExpectedInstallationDiagnosticReceipt = Join-Path $RepoRoot `
		"Saved\ConsumerMatrix\$Profile\Saved\Qualification\installation-diagnostic.json"
	if ($Matrix.schemaVersion -ne 5 -or
		[string]$Matrix.profile -cne $Profile -or
		[string]$Matrix.qualificationRunId -cne $MatrixRunId -or
		[string]$Matrix.pluginSource -cne 'PackagedArtifacts' -or
		[string]$Matrix.artifactVersion -cne $Version -or
		[string]$Matrix.engine -cne $EngineRoot -or
		[string]$Matrix.engineBuildFingerprint -cne $EngineBuildFingerprint -or
		$Started -lt $InvocationStarted -or $Completed -lt $Started -or
		[string]$Matrix.publicHeaderAudit -cne 'Passed' -or
		[int]$Matrix.publicHeaderCount -le 0 -or
		[string]$Matrix.installationDiagnostic -cne 'Passed' -or
		[string]$InstallationDiagnosticReceipt -cne
			$ExpectedInstallationDiagnosticReceipt -or
		-not (Test-Path -LiteralPath $InstallationDiagnosticReceipt `
			-PathType Leaf) -or
		[string]$Matrix.installationDiagnosticReceiptSha256 -cne
			(Get-FileHash -LiteralPath $InstallationDiagnosticReceipt `
				-Algorithm SHA256).Hash -or
		[string]$Matrix.editorBuild -cne 'Passed' -or
		[string]$Matrix.shippingGameBuild -cne 'Passed' -or
		[string]$Matrix.uncookedLoad -cne 'Passed' -or
		$ActualPlugins.Count -ne $ExpectedPlugins.Count -or
		(@(Compare-Object $ActualPlugins @($ExpectedPlugins | Sort-Object))).Count -ne 0) {
		throw "Consumer matrix receipt '$Path' is incomplete or does not bind this release invocation."
	}
	$InstallationDiagnostic = Get-Content -Raw `
		-LiteralPath $InstallationDiagnosticReceipt | ConvertFrom-Json
	$DiagnosticPlugins = @(
		$InstallationDiagnostic.enabledProductionPlugins | Sort-Object)
	$ExpectedDiagnosticProject = Join-Path $RepoRoot `
		"Saved\ConsumerMatrix\$Profile\SeinConsumer.uproject"
	$ExpectedManifestObject =
		'/Game/Generated/SeinSimulationContentManifest.SeinSimulationContentManifest'
	if ([int]$InstallationDiagnostic.schemaVersion -ne 1 -or
		[string]$InstallationDiagnostic.result -cne 'Passed' -or
		[int]$InstallationDiagnostic.errorCount -ne 0 -or
		-not ([System.IO.Path]::GetFullPath([string]$InstallationDiagnostic.project).Equals(
			[System.IO.Path]::GetFullPath($ExpectedDiagnosticProject),
			[System.StringComparison]::OrdinalIgnoreCase)) -or
		-not ([System.IO.Path]::GetFullPath([string]$InstallationDiagnostic.engineRoot).Equals(
			[System.IO.Path]::GetFullPath($EngineRoot),
			[System.StringComparison]::OrdinalIgnoreCase)) -or
		[string]$InstallationDiagnostic.integrationMode -cne 'Release' -or
		[string]$InstallationDiagnostic.cohortVersion -cne $Version -or
		[string]$InstallationDiagnostic.simulationContentManifest -cne
			$ExpectedManifestObject -or
		$DiagnosticPlugins.Count -ne $ExpectedPlugins.Count -or
		(@(Compare-Object $DiagnosticPlugins @($ExpectedPlugins | Sort-Object))).Count -ne 0 -or
		[string]$Matrix.installationDiagnosticIntegrationMode -cne
			[string]$InstallationDiagnostic.integrationMode -or
		[string]$Matrix.installationDiagnosticCohortVersion -cne
			[string]$InstallationDiagnostic.cohortVersion -or
		[string]$Matrix.installationDiagnosticManifest -cne
			[string]$InstallationDiagnostic.simulationContentManifest) {
		throw "Installation diagnostic for '$Profile' does not bind the qualified release profile."
	}
	if (-not $PackageOnly -and
		([string]$Matrix.clientBuild -cne 'Passed' -or
		[string]$Matrix.serverBuild -cne 'Passed' -or
		[string]$Matrix.cookAndPackagedLoad -cne 'Passed' -or
		($Profile -eq 'Framework' -and
			[string]$Matrix.packagedRuntimeQualification -cne 'Passed') -or
		($Profile -ne 'Framework' -and
			[string]$Matrix.packagedRuntimeQualification -cne 'NotApplicable'))) {
		throw "Consumer matrix receipt '$Path' used a release-prohibited skip or failed a required gate."
	}
	$Manifest = Get-Content -Raw -LiteralPath (Join-Path $Dist 'release-manifest.json') |
		ConvertFrom-Json
	foreach ($PluginName in $ExpectedPlugins) {
		$ArtifactRecord = @($Manifest.artifacts | Where-Object {
			[string]$_.plugin -ceq $PluginName })
		if ($ArtifactRecord.Count -ne 1 -or
			[string]$Matrix.artifactSha256.$PluginName -cne
				[string]$ArtifactRecord[0].sha256) {
			throw "Matrix receipt '$Path' does not bind the packaged '$PluginName' bytes."
		}
	}
	$HeaderPath = Join-Path $RepoRoot `
		"Saved\ConsumerMatrix\$Profile\Saved\PublicHeaderAudit.json"
	$Header = Get-Content -Raw -LiteralPath $HeaderPath | ConvertFrom-Json
	if ($Header.schemaVersion -ne 1 -or
		[int]$Header.headerCount -ne [int]$Matrix.publicHeaderCount -or
		[string]$Matrix.publicHeaderManifest -cne $HeaderPath -or
		[string]$Matrix.publicHeaderManifestSha256 -cne
			(Get-FileHash -LiteralPath $HeaderPath -Algorithm SHA256).Hash) {
		throw "Public-header evidence for '$Profile' is missing or does not match its matrix receipt."
	}
	if ($Profile -eq 'Framework' -and -not $PackageOnly) {
		$RuntimePath = Join-Path $RepoRoot `
			'Saved\ConsumerMatrix\Framework\Saved\RuntimeQualification\runtime-result.json'
		$Runtime = Get-Content -Raw -LiteralPath $RuntimePath | ConvertFrom-Json
		$RequiredRuntimeFields = @(
			'listenServer', 'twoPlayerLobbyTravel', 'lockstepCommandFlow',
			'determinismWorldRootGossip', 'checkpointTailResync',
			'disconnectReconnect', 'replayCheckpointSeek',
			'pairCapabilityCommandFlow',
			'pairCapabilityReconnectPersistence',
			'pairCapabilityReplayWitness')
		if ($Runtime.schemaVersion -ne 2 -or
			@($RequiredRuntimeFields | Where-Object {
				[string]$Runtime.$_ -cne 'Passed' }).Count -ne 0 -or
			[string]$Runtime.replayArtifact -cne 'RemovedAfterVerification' -or
			[string]$Matrix.runtimeResultSha256 -cne
				(Get-FileHash -LiteralPath $RuntimePath -Algorithm SHA256).Hash) {
			throw 'Framework runtime evidence did not pass the complete network, reconnect, and replay contract.'
		}
	}
	return $Path
}

function Write-ReleaseGateReceipt
{
	New-Item -ItemType Directory -Path $ReceiptRoot -Force | Out-Null
	[System.IO.File]::WriteAllText(
		$ReceiptPath,
		($Receipt | ConvertTo-Json -Depth 10),
		[System.Text.UTF8Encoding]::new($false))
}

function Get-VerifiedRemoteRelease(
	[string] $Repository,
	[object[]] $ExpectedRecords)
{
	$ViewOutput = & gh release view "v$Version" -R $Repository `
		--json 'tagName,targetCommitish,isDraft,isPrerelease,assets' 2>$null
	$ViewExitCode = $LASTEXITCODE
	$global:LASTEXITCODE = 0
	if ($ViewExitCode -ne 0) {
		return $null
	}
	$Remote = ($ViewOutput -join [Environment]::NewLine) | ConvertFrom-Json
	$ExpectedPrerelease = $Version.Split('+')[0].Contains('-')
	if ([string]$Remote.tagName -cne "v$Version" -or
		[string]$Remote.targetCommitish -cne $Commit -or
		[bool]$Remote.isPrerelease -ne $ExpectedPrerelease) {
		throw "Existing GitHub release v$Version does not match this commit or release classification."
	}
	$RemoteAssets = @($Remote.assets)
	if ($RemoteAssets.Count -ne $ExpectedRecords.Count) {
		throw "Existing GitHub release v$Version has $($RemoteAssets.Count) assets; expected $($ExpectedRecords.Count)."
	}
	foreach ($Expected in $ExpectedRecords) {
		$Matches = @($RemoteAssets | Where-Object {
			[string]$_.name -ceq [string]$Expected.file })
		if ($Matches.Count -ne 1 -or
			[int64]$Matches[0].size -ne [int64]$Expected.bytes) {
			throw "Existing GitHub asset '$($Expected.file)' is missing, duplicated, or has the wrong size."
		}
	}
	$DownloadRoot = Join-Path $ReceiptRoot (
		'remote-release-{0}' -f [System.IO.Path]::GetFileNameWithoutExtension($ReceiptPath))
	if (Test-Path -LiteralPath $DownloadRoot) {
		Remove-Item -LiteralPath $DownloadRoot -Recurse -Force
	}
	New-Item -ItemType Directory -Path $DownloadRoot -Force | Out-Null
	try {
		& gh release download "v$Version" -R $Repository `
			--dir $DownloadRoot --clobber
		if ($LASTEXITCODE -ne 0) {
			throw "Could not download GitHub release v$Version for exact-byte verification."
		}
		$Downloaded = @(Get-ChildItem -LiteralPath $DownloadRoot -File)
		if ($Downloaded.Count -ne $ExpectedRecords.Count) {
			throw "Downloaded GitHub release v$Version has an unexpected asset set."
		}
		foreach ($Expected in $ExpectedRecords) {
			$DownloadedPath = Join-Path $DownloadRoot ([string]$Expected.file)
			if (-not (Test-Path -LiteralPath $DownloadedPath -PathType Leaf) -or
				(Get-Item -LiteralPath $DownloadedPath).Length -ne [int64]$Expected.bytes -or
				(Get-FileHash -LiteralPath $DownloadedPath -Algorithm SHA256).Hash -cne
					[string]$Expected.sha256) {
				throw "GitHub release asset '$($Expected.file)' does not match the qualified local bytes."
			}
		}
	}
	finally {
		Remove-Item -LiteralPath $DownloadRoot -Recurse -Force `
			-ErrorAction SilentlyContinue
	}
	$global:LASTEXITCODE = 0
	return $Remote
}

function New-ReleaseEvidenceArchive
{
	$EvidenceRoot = Join-Path $ReceiptRoot (
		'evidence-{0}-{1}' -f $Version, [System.IO.Path]::GetFileNameWithoutExtension($ReceiptPath))
	if (Test-Path -LiteralPath $EvidenceRoot) {
		Remove-Item -LiteralPath $EvidenceRoot -Recurse -Force
	}
	New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
	$EvidenceSourcePaths = [System.Collections.Generic.List[string]]::new()
	foreach ($AttemptPath in $QualifiedTestAttemptPaths) {
		$AttemptRoot = Split-Path -Parent $AttemptPath
		$EvidenceSourcePaths.Add($AttemptPath)
		$EvidenceSourcePaths.Add((Join-Path $AttemptRoot 'build-provenance.json'))
		$EvidenceSourcePaths.Add((Join-Path $AttemptRoot 'index.json'))
	}
	foreach ($MatrixPath in $QualifiedMatrixReceiptPaths) {
		$ProfileName = Split-Path -Leaf (Split-Path -Parent $MatrixPath)
		$EvidenceSourcePaths.Add($MatrixPath)
		$EvidenceSourcePaths.Add((Join-Path $RepoRoot (
			'Saved\ConsumerMatrix\{0}\Saved\PublicHeaderAudit.json' -f $ProfileName)))
		$EvidenceSourcePaths.Add((Join-Path $RepoRoot (
			'Saved\ConsumerMatrix\{0}\Saved\Qualification\installation-diagnostic.json' -f $ProfileName)))
	}
	$RuntimeSource = Join-Path $RepoRoot `
		'Saved\ConsumerMatrix\Framework\Saved\RuntimeQualification\runtime-result.json'
	$EvidenceSourcePaths.Add($RuntimeSource)
	foreach ($Document in @('COMPATIBILITY.md', 'UPGRADING.md')) {
		$EvidenceSourcePaths.Add((Join-Path $RepoRoot "Docs\$Document"))
	}
	$EvidenceSourceLocks =
		[System.Collections.Generic.List[System.IO.FileStream]]::new()
	try {
		foreach ($SourcePath in @($EvidenceSourcePaths | Sort-Object -Unique)) {
			$EvidenceSourceLocks.Add([System.IO.File]::Open(
				$SourcePath,
				[System.IO.FileMode]::Open,
				[System.IO.FileAccess]::Read,
				[System.IO.FileShare]::Read))
		}
		$GateStarted = [DateTime]::Parse(
			[string]$Receipt.startedAtUtc,
			[System.Globalization.CultureInfo]::InvariantCulture,
			[System.Globalization.DateTimeStyles]::RoundtripKind)
	$Receipt.testAttempts = @($QualifiedTestAttemptPaths | ForEach-Object {
		$Attempt = Get-Content -Raw -LiteralPath $_ | ConvertFrom-Json
		$ValidatedPath = Get-QualifiedTestAttemptPath `
			-Suite ([string]$Attempt.suite) `
			-Profile ([string]$Attempt.profile) `
			-InvocationStarted $GateStarted
		if ([string]$ValidatedPath -cne [string]$_) {
			throw "Test attempt path changed before evidence capture: '$($_)'."
		}
		$Destination = Join-Path $EvidenceRoot (
			'test-{0}.json' -f [string]$Attempt.attemptId)
		Copy-Item -LiteralPath $_ -Destination $Destination -Force
		$ProvenanceSource = Join-Path (Split-Path -Parent $_) `
			'build-provenance.json'
		$ProvenanceDestination = Join-Path $EvidenceRoot (
			'test-build-{0}.json' -f [string]$Attempt.attemptId)
		Copy-Item -LiteralPath $ProvenanceSource `
			-Destination $ProvenanceDestination -Force
		$IndexSource = Join-Path (Split-Path -Parent $_) 'index.json'
		$IndexDestination = Join-Path $EvidenceRoot (
			'test-index-{0}.json' -f [string]$Attempt.attemptId)
		Copy-Item -LiteralPath $IndexSource `
			-Destination $IndexDestination -Force
		[ordered]@{
			suite = [string]$Attempt.suite
			profile = [string]$Attempt.profile
			attemptId = [string]$Attempt.attemptId
			file = [System.IO.Path]::GetFileName($Destination)
			sha256 = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
			buildProvenanceFile = [System.IO.Path]::GetFileName(
				$ProvenanceDestination)
			buildProvenanceSha256 = (Get-FileHash `
				-LiteralPath $ProvenanceDestination -Algorithm SHA256).Hash
			testIndexFile = [System.IO.Path]::GetFileName($IndexDestination)
			testIndexSha256 = (Get-FileHash -LiteralPath $IndexDestination `
				-Algorithm SHA256).Hash
		}
	})
	if ($Receipt.testAttempts.Count -ne 12) {
		throw "Release evidence expected 12 exact test attempts, found $($Receipt.testAttempts.Count)."
	}

	$Receipt.matrixReceipts = @($QualifiedMatrixReceiptPaths | ForEach-Object {
		$Matrix = Get-Content -Raw -LiteralPath $_ | ConvertFrom-Json
		$ValidatedPath = Get-QualifiedMatrixReceiptPath `
			-Profile ([string]$Matrix.profile) `
			-InvocationStarted $MatrixInvocationStarted
		if ([string]$ValidatedPath -cne [string]$_) {
			throw "Matrix receipt path changed before evidence capture: '$($_)'."
		}
		$Destination = Join-Path $EvidenceRoot (
			'matrix-{0}.json' -f [string]$Matrix.profile)
		Copy-Item -LiteralPath $_ -Destination $Destination -Force
		$HeaderPath = Join-Path $RepoRoot (
			'Saved\ConsumerMatrix\{0}\Saved\PublicHeaderAudit.json' -f
				[string]$Matrix.profile)
		$HeaderDestination = Join-Path $EvidenceRoot (
			'headers-{0}.json' -f [string]$Matrix.profile)
		Copy-Item -LiteralPath $HeaderPath -Destination $HeaderDestination -Force
		$DiagnosticPath = [string]$Matrix.installationDiagnosticReceipt
		$DiagnosticDestination = Join-Path $EvidenceRoot (
			'installation-{0}.json' -f [string]$Matrix.profile)
		Copy-Item -LiteralPath $DiagnosticPath `
			-Destination $DiagnosticDestination -Force
		[ordered]@{
			profile = [string]$Matrix.profile
			file = [System.IO.Path]::GetFileName($Destination)
			sha256 = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
			headerFile = [System.IO.Path]::GetFileName($HeaderDestination)
			headerSha256 = (Get-FileHash -LiteralPath $HeaderDestination -Algorithm SHA256).Hash
			installationFile = [System.IO.Path]::GetFileName($DiagnosticDestination)
			installationSha256 = (Get-FileHash -LiteralPath $DiagnosticDestination `
				-Algorithm SHA256).Hash
		}
	})
	if ($Receipt.matrixReceipts.Count -ne 5) {
		throw "Release evidence expected 5 exact consumer receipts, found $($Receipt.matrixReceipts.Count)."
	}

	$RuntimeDestination = Join-Path $EvidenceRoot 'framework-runtime-result.json'
	Copy-Item -LiteralPath $RuntimeSource `
		-Destination $RuntimeDestination -Force
	foreach ($Document in @('COMPATIBILITY.md', 'UPGRADING.md')) {
		Copy-Item -LiteralPath (Join-Path $RepoRoot "Docs\$Document") `
			-Destination (Join-Path $EvidenceRoot $Document) -Force
	}
	}
	finally {
		foreach ($Stream in $EvidenceSourceLocks) {
			$Stream.Dispose()
		}
	}

	$EvidenceFiles = [ordered]@{}
	foreach ($File in Get-ChildItem -LiteralPath $EvidenceRoot -File |
			Sort-Object Name) {
		$EvidenceFiles[$File.Name] = [ordered]@{
			bytes = $File.Length
			sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
		}
	}
	[ordered]@{
		schemaVersion = 1
		version = $Version
		commit = $Commit
		generatedAtUtc = [DateTime]::UtcNow.ToString('o')
		files = $EvidenceFiles
	} | ConvertTo-Json -Depth 8 | Set-Content `
		-LiteralPath (Join-Path $EvidenceRoot 'evidence-index.json') -Encoding utf8

	$EvidenceArchive = Join-Path $Dist 'SeinARTS-release-evidence.zip'
	if (Test-Path -LiteralPath $EvidenceArchive) {
		Remove-Item -LiteralPath $EvidenceArchive -Force
	}
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	[System.IO.Compression.ZipFile]::CreateFromDirectory(
		$EvidenceRoot, $EvidenceArchive, 'Optimal', $false)
	return $EvidenceArchive
}

function Invoke-ReleaseGateStep(
	[string] $Name,
	[scriptblock] $Action)
{
	$Started = [DateTime]::UtcNow
	Write-Host "[ReleaseGate] $Name" -ForegroundColor Cyan
	try {
		$global:LASTEXITCODE = 0
		& $Action
		$StepExitCode = $global:LASTEXITCODE
		if ($StepExitCode -ne 0) {
			throw "$Name returned exit code $StepExitCode."
		}
		$Steps.Add([ordered]@{
			name = $Name
			status = 'Passed'
			startedAtUtc = $Started.ToString('o')
			completedAtUtc = [DateTime]::UtcNow.ToString('o')
		})
		Write-ReleaseGateReceipt
	}
	catch {
		$Steps.Add([ordered]@{
			name = $Name
			status = 'Failed'
			startedAtUtc = $Started.ToString('o')
			completedAtUtc = [DateTime]::UtcNow.ToString('o')
			failure = $_.Exception.Message
		})
		$Receipt.status = 'Failed'
		$Receipt.failure = $_.Exception.Message
		$Receipt.completedAtUtc = [DateTime]::UtcNow.ToString('o')
		Write-ReleaseGateReceipt
		throw
	}
}

Write-ReleaseGateReceipt

Invoke-ReleaseGateStep 'Installation diagnostic self-test' {
	& $DiagnosticSelfTestScript
}
Invoke-ReleaseGateStep 'Host installation diagnostic' {
	& $DiagnosticScript `
		-Project (Join-Path $RepoRoot 'SeinARTS.uproject') `
		-EngineRoot $EngineRoot
}
Invoke-ReleaseGateStep 'Development Editor build' {
	& $BuildScript -Target SeinARTSEditor -Platform Win64 -Config Development -EngineRoot $EngineRoot
}
Invoke-ReleaseGateStep 'Shipping Game build' {
	& $BuildScript -Target SeinARTS -Platform Win64 -Config Shipping -EngineRoot $EngineRoot
}

foreach ($TestProfile in $ExpectedTestProfiles) {
	$FirstSuite = $true
	foreach ($Suite in $ExpectedTestSuites) {
		$CurrentSuite = $Suite
		$CurrentProfile = $TestProfile
		$SkipBuildForSuite = -not $FirstSuite
		$TestInvocationStarted = [DateTime]::UtcNow
		Invoke-ReleaseGateStep "$CurrentProfile $CurrentSuite" {
			$Arguments = @{
				Profile = $CurrentProfile
				Suite = $CurrentSuite
				TimeoutSeconds = 7200
				EngineRoot = $EngineRoot
			}
			if ($SkipBuildForSuite) {
				$Arguments.SkipBuild = $true
			}
			& $TestScript @Arguments
			if ($LASTEXITCODE -eq 0) {
				$QualifiedTestAttemptPaths.Add((Get-QualifiedTestAttemptPath `
					-Suite $CurrentSuite -Profile $CurrentProfile `
					-InvocationStarted $TestInvocationStarted))
			}
		}
		$FirstSuite = $false
	}
}

Invoke-ReleaseGateStep 'Standalone plugin packaging' {
	& $PackageScript -Version $Version -PackageOnly -EngineRoot $EngineRoot
}
$MatrixRunId = [Guid]::NewGuid().ToString('N')
$Receipt.matrixRunId = $MatrixRunId
$MatrixInvocationStarted = [DateTime]::UtcNow
Invoke-ReleaseGateStep 'Exact artifact consumer matrix' {
	$Arguments = @{
		Profile = 'All'
		EngineRoot = $EngineRoot
		ArtifactDirectory = $Dist
		AuditPublicHeaders = $true
		QualificationRunId = $MatrixRunId
	}
	if ($PackageOnly) {
		if ($SkipClientServer) { $Arguments.SkipClientServer = $true }
		if ($SkipCook) { $Arguments.SkipCook = $true }
		if ($SkipRuntimeQualification) {
			$Arguments.SkipRuntimeQualification = $true
		}
	}
	& $ConsumerScript @Arguments
	if ($LASTEXITCODE -eq 0) {
		foreach ($MatrixProfile in $ExpectedMatrixProfiles) {
			$QualifiedMatrixReceiptPaths.Add((Get-QualifiedMatrixReceiptPath `
				-Profile $MatrixProfile `
				-InvocationStarted $MatrixInvocationStarted))
		}
	}
}

$ManifestPath = Join-Path $Dist 'release-manifest.json'
$Receipt.releaseManifest = $ManifestPath

if (-not $PackageOnly) {
	$ReleaseAssets = $null
	$ExpectedPublicationRecords = $null
	Invoke-ReleaseGateStep 'Verify immutable release inputs' {
		Assert-ReleaseSourceUnchanged
		$ReleaseAssets = @(Get-VerifiedReleaseAssets)
	}

	Invoke-ReleaseGateStep 'Assemble qualified release evidence' {
		$EvidenceArchive = New-ReleaseEvidenceArchive
		$Receipt.evidenceArchive = Get-FileRecord $EvidenceArchive

		$Manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
		$Manifest | Add-Member -NotePropertyName qualification -Force `
			-NotePropertyValue ([ordered]@{
				releaseGateReceipt = 'SeinARTS-release-gate.json'
				evidenceFile = [System.IO.Path]::GetFileName($EvidenceArchive)
				evidenceBytes = (Get-Item -LiteralPath $EvidenceArchive).Length
				evidenceSha256 = (Get-FileHash -LiteralPath $EvidenceArchive `
					-Algorithm SHA256).Hash
			})
		$Manifest | ConvertTo-Json -Depth 12 | Set-Content `
			-LiteralPath $ManifestPath -Encoding utf8

		$QualifiedAssetPaths = @(
			@(Get-VerifiedReleaseAssets) + @($EvidenceArchive))
		$Receipt.publicationAssets = @($QualifiedAssetPaths | ForEach-Object {
			Get-FileRecord $_
		})
		$Receipt.publicationAssets += [ordered]@{
			file = 'SeinARTS-release-gate.json'
			bytes = $null
			sha256 = $null
			role = 'QualifiedReceiptSelf'
		}
		$Receipt.status = 'Qualified'
		Write-ReleaseGateReceipt
		$StableReceiptPath = Join-Path $Dist 'SeinARTS-release-gate.json'
		Copy-Item -LiteralPath $ReceiptPath -Destination $StableReceiptPath -Force
		$script:ReleaseAssets = @(
			Get-VerifiedReleaseAssets -RequireQualification)
		$script:ExpectedPublicationRecords = @($script:ReleaseAssets | ForEach-Object {
			Get-FileRecord $_
		})
	}

	Invoke-ReleaseGateStep 'Publish verified release assets' {
		Assert-ReleaseSourceUnchanged
		$PublicationLocks = [System.Collections.Generic.List[System.IO.FileStream]]::new()
		try {
			foreach ($AssetPath in $ReleaseAssets) {
				$PublicationLocks.Add([System.IO.File]::Open(
					$AssetPath,
					[System.IO.FileMode]::Open,
					[System.IO.FileAccess]::Read,
					[System.IO.FileShare]::Read))
			}
			foreach ($ExpectedRecord in $ExpectedPublicationRecords) {
				$AssetPath = Join-Path $Dist ([string]$ExpectedRecord.file)
				$CurrentRecord = Get-FileRecord $AssetPath
				if ([int64]$CurrentRecord.bytes -ne [int64]$ExpectedRecord.bytes -or
					[string]$CurrentRecord.sha256 -cne [string]$ExpectedRecord.sha256) {
					throw "Publication asset '$AssetPath' changed after qualification."
				}
			}
		git -C $RepoRoot fetch origin --quiet
		if ($LASTEXITCODE -ne 0) {
			throw 'Could not refresh origin before release publication.'
		}
		$CurrentOriginMain = (git -C $RepoRoot rev-parse refs/remotes/origin/main).Trim()
		if ($LASTEXITCODE -ne 0 -or
			[string]$CurrentOriginMain -cne $Commit) {
			throw "origin/main moved after qualification; expected exact release commit $Commit."
		}
		$Repository = (git -C $RepoRoot remote get-url origin).Trim()
		$Notes = @"
Packaged from ``$Commit`` (UE 5.8, Win64, UAT BuildPlugin -Rocket).

Plugins: $($ProductionPlugins -join ', ').

The exact attached ZIP bytes passed the complete release gate. SHA-256 artifact
provenance is in ``release-manifest.json``; test, consumer, installation, and public-header
receipts are in ``SeinARTS-release-evidence.zip``.
"@
		$GhArguments = @(
			'release', 'create', "v$Version") + @($ReleaseAssets) + @(
			'-R', $Repository,
			'--target', $Commit,
			'--title', "SeinARTS v$Version",
			'--notes', $Notes,
			'--draft')
		if ($Version.Split('+')[0].Contains('-')) {
			$GhArguments += '--prerelease'
		}
		$RemoteRelease = Get-VerifiedRemoteRelease `
			-Repository $Repository `
			-ExpectedRecords $ExpectedPublicationRecords
		if ($null -eq $RemoteRelease) {
			& gh @GhArguments
			$CreateExitCode = $LASTEXITCODE
			$global:LASTEXITCODE = 0
			$RemoteRelease = Get-VerifiedRemoteRelease `
				-Repository $Repository `
				-ExpectedRecords $ExpectedPublicationRecords
			if ($null -eq $RemoteRelease) {
				throw "gh release create returned exit code $CreateExitCode and no exact resumable release exists."
			}
		}
		if ([bool]$RemoteRelease.isDraft) {
			& gh release edit "v$Version" -R $Repository --draft=false
			$PublishExitCode = $LASTEXITCODE
			$global:LASTEXITCODE = 0
			$RemoteRelease = Get-VerifiedRemoteRelease `
				-Repository $Repository `
				-ExpectedRecords $ExpectedPublicationRecords
			if ($null -eq $RemoteRelease -or [bool]$RemoteRelease.isDraft) {
				throw "Publishing the exact draft release returned exit code $PublishExitCode and did not produce a verified public release."
			}
		}
		gh workflow run seinarts-update.yml -R RJPhenom/WARSEIN -f "tag=v$Version"
		$DispatchExitCode = $LASTEXITCODE
		$global:LASTEXITCODE = 0
		if ($DispatchExitCode -ne 0) {
			$Warning = "Release v$Version published, but the WARSEIN update dispatch failed with exit code $DispatchExitCode."
			$Receipt.postPublicationWarnings += $Warning
			Write-ReleaseGateReceipt
			Write-Warning $Warning
		}
		}
		finally {
			foreach ($Stream in $PublicationLocks) {
				$Stream.Dispose()
			}
		}
	}
}

$Receipt.status = 'Passed'
$Receipt.completedAtUtc = [DateTime]::UtcNow.ToString('o')
Write-ReleaseGateReceipt
Write-Host "[ReleaseGate] passed: $ReceiptPath" -ForegroundColor Green
}
catch {
	if ($null -ne $Receipt -and
		[string]$Receipt.status -notin @('Failed', 'Passed')) {
		$Receipt.status = 'Failed'
		$Receipt.failure = $_.Exception.Message
		$Receipt.completedAtUtc = [DateTime]::UtcNow.ToString('o')
		try { Write-ReleaseGateReceipt } catch {}
	}
	throw
}
finally {
	$ReleaseGateLock.Dispose()
	$PipelineMutex.ReleaseMutex()
	$PipelineMutex.Dispose()
}
