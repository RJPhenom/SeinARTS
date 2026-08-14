#Requires -Version 5.1
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $GameExecutable,

	[Parameter(Mandatory = $true)]
	[string] $ProjectRoot,

	[ValidateSet('Framework', 'MovementPlus')]
	[string] $Profile = 'Framework',

	[ValidateSet('Baseline', 'Adverse')]
	[string] $NetworkProfile = 'Baseline',

	[int] $TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$GameExecutable = (Resolve-Path -LiteralPath $GameExecutable).Path
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ExecutableSha256 = (Get-FileHash -LiteralPath $GameExecutable -Algorithm SHA256).Hash
$ExpectedMovementClass = '/Script/SeinARTSMovementPlus.SeinWheeledVehicleMovement'
$ExpectedMovementDestination = '214748364800000:21474836480000:0'
$FaultProxyScript = Join-Path $PSScriptRoot 'Invoke-UdpFaultProxy.ps1'
$MarkerDirectory = Join-Path $ProjectRoot 'Saved\RuntimeQualification'
$ResolvedMarkerDirectory = [System.IO.Path]::GetFullPath($MarkerDirectory)
if (-not $ResolvedMarkerDirectory.StartsWith(
	$ProjectRoot + [System.IO.Path]::DirectorySeparatorChar,
	[System.StringComparison]::OrdinalIgnoreCase)) {
	throw "Refusing to clean runtime marker path outside '$ProjectRoot'."
}
if (Test-Path -LiteralPath $MarkerDirectory) {
	Remove-Item -LiteralPath $MarkerDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $MarkerDirectory -Force | Out-Null

function Get-FreeUdpPort
{
	$Probe = [System.Net.Sockets.UdpClient]::new(0)
	try {
		return ([System.Net.IPEndPoint]$Probe.Client.LocalEndPoint).Port
	}
	finally {
		$Probe.Dispose()
	}
}

$ServerPort = Get-FreeUdpPort
$ProxyPort = $null
if ($NetworkProfile -eq 'Adverse') {
	if (-not (Test-Path -LiteralPath $FaultProxyScript -PathType Leaf)) {
		throw "UDP fault proxy is missing: '$FaultProxyScript'."
	}
	do {
		$ProxyPort = Get-FreeUdpPort
	} while ($ProxyPort -eq $ServerPort)
}
$ServerAddress = if ($NetworkProfile -eq 'Adverse') {
	"127.0.0.1:$ProxyPort"
} else {
	"127.0.0.1:$ServerPort"
}
$Processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$ProxyProcess = $null
$ProxyResult = $null
$ReplayArtifactPath = $null
$ReplayArtifactRoot = [System.IO.Path]::GetFullPath(
	(Join-Path $env:LOCALAPPDATA 'SeinConsumer\Saved\Replays'))

function New-QuotedArgument([string] $Name, [string] $Value)
{
	$Escaped = $Value.Replace('"', '\"')
	return "-$Name=`"$Escaped`""
}

function Start-QualificationProcess(
	[string] $Role,
	[string[]] $Arguments)
{
	$Stdout = Join-Path $MarkerDirectory "$Role.stdout.log"
	$Stderr = Join-Path $MarkerDirectory "$Role.stderr.log"
	$Process = Start-Process `
		-FilePath $GameExecutable `
		-ArgumentList $Arguments `
		-WorkingDirectory (Split-Path -Parent $GameExecutable) `
		-RedirectStandardOutput $Stdout `
		-RedirectStandardError $Stderr `
		-WindowStyle Hidden `
		-PassThru
	$Processes.Add($Process)
	return $Process
}

function Start-UdpFaultProxy
{
	$PowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
	$Stdout = Join-Path $MarkerDirectory 'proxy.stdout.log'
	$Stderr = Join-Path $MarkerDirectory 'proxy.stderr.log'
	$Arguments = @(
		'-NoProfile',
		'-ExecutionPolicy', 'Bypass',
		'-File', ('"{0}"' -f $FaultProxyScript),
		'-ListenPort', $ProxyPort,
		'-UpstreamHost', '127.0.0.1',
		'-UpstreamPort', $ServerPort,
		'-ControlDirectory', ('"{0}"' -f $MarkerDirectory),
		'-LatencyMs', 60,
		'-JitterMs', 20,
		'-DropEvery', 43,
		'-DuplicateEvery', 59,
		'-ReorderEvery', 31,
		'-ReorderDelayMs', 90,
		'-Seed', 5368391)
	return Start-Process `
		-FilePath $PowerShell `
		-ArgumentList $Arguments `
		-WorkingDirectory $ProjectRoot `
		-RedirectStandardOutput $Stdout `
		-RedirectStandardError $Stderr `
		-WindowStyle Hidden `
		-PassThru
}

function Get-QualificationFailure
{
	$Failure = Get-ChildItem -LiteralPath $MarkerDirectory `
		-Filter '*-failed.marker' -File -ErrorAction SilentlyContinue |
		Select-Object -First 1
	if (-not $Failure) {
		return $null
	}
	return "$($Failure.Name): $(Get-Content -Raw -LiteralPath $Failure.FullName)"
}

function Wait-QualificationMarker(
	[string] $FileName,
	[System.Diagnostics.Process[]] $RequiredProcesses)
{
	$Path = Join-Path $MarkerDirectory $FileName
	$Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	while ([DateTime]::UtcNow -lt $Deadline) {
		if (Test-Path -LiteralPath $Path) {
			return $Path
		}
		$Failure = Get-QualificationFailure
		if ($Failure) {
			throw "Packaged runtime qualification failed: $Failure"
		}
		foreach ($RequiredProcess in $RequiredProcesses) {
			$RequiredProcess.Refresh()
			if ($RequiredProcess.HasExited) {
				throw "Packaged $($RequiredProcess.Id) process exited before '$FileName' (exit $($RequiredProcess.ExitCode))."
			}
		}
		Start-Sleep -Milliseconds 200
	}
	throw "Timed out waiting for packaged runtime marker '$FileName'."
}

function Read-KeyValueMarker([string] $Path)
{
	$Values = @{}
	foreach ($Line in Get-Content -LiteralPath $Path) {
		$Equals = $Line.IndexOf('=')
		if ($Equals -le 0) {
			continue
		}
		$Values[$Line.Substring(0, $Equals)] = $Line.Substring($Equals + 1)
	}
	return $Values
}

function Assert-MovementEvidence(
	[hashtable] $Values,
	[string] $Label)
{
	$Required = @(
		'MovementState', 'MovementClass', 'MovementDestination',
		'MovementTargetWitness', 'MovementTelemetry')
	foreach ($Key in $Required) {
		if (-not $Values.ContainsKey($Key) -or
			[string]::IsNullOrWhiteSpace([string]$Values[$Key])) {
			throw "$Label omitted Movement+ evidence '$Key'."
		}
	}
	if ([string]$Values.MovementClass -cne $ExpectedMovementClass -or
		[string]$Values.MovementDestination -cne $ExpectedMovementDestination -or
		[string]$Values.MovementTargetWitness -cne 'Passed') {
		throw "$Label did not prove the exact Movement+ class and destination."
	}
}

function Stop-AndValidateUdpFaultProxy
{
	if (-not $ProxyProcess) {
		throw 'Adverse qualification has no UDP fault proxy process.'
	}
	Start-Sleep -Milliseconds 500
	New-Item -ItemType File `
		-Path (Join-Path $MarkerDirectory 'proxy-stop.marker') `
		-Force | Out-Null
	$ProxyResultPath = Wait-QualificationMarker `
		'proxy-result.json' @($ProxyProcess)
	if (-not $ProxyProcess.WaitForExit(10000)) {
		throw 'UDP fault proxy did not exit after its stop marker.'
	}
	$ProxyProcess.Refresh()
	if ($ProxyProcess.ExitCode -ne 0) {
		throw "UDP fault proxy exited with code $($ProxyProcess.ExitCode)."
	}

	$Result = Get-Content -Raw -LiteralPath $ProxyResultPath |
		ConvertFrom-Json
	if ([int]$Result.schemaVersion -ne 2 -or
		[string]$Result.status -cne 'Passed' -or
		[string]$Result.jitterSequence -cne 'PerDirection' -or
		[int]$Result.listenPort -ne $ProxyPort -or
		[string]$Result.upstream -cne "127.0.0.1:$ServerPort" -or
		[string]::IsNullOrWhiteSpace([string]$Result.clientEndpoint) -or
		[int]$Result.latencyMs -ne 60 -or
		[int]$Result.jitterMs -ne 20 -or
		[int]$Result.dropEvery -ne 43 -or
		[int]$Result.duplicateEvery -ne 59 -or
		[int]$Result.reorderEvery -ne 31 -or
		[int]$Result.reorderDelayMs -ne 90 -or
		[uint64]$Result.seed -ne 5368391 -or
		[int64]$Result.clientEndpointChanges -lt 1) {
		throw 'UDP fault proxy result disagreed with the adverse profile contract.'
	}
	foreach ($Direction in @('clientToServer', 'serverToClient')) {
		$Stats = $Result.$Direction
		if ([int64]$Stats.receivedDatagrams -lt 100 -or
			[int64]$Stats.forwardedDatagrams -lt 100 -or
			[int64]$Stats.droppedDatagrams -lt 1 -or
			[int64]$Stats.duplicatedDatagrams -lt 1 -or
			[int64]$Stats.reorderedDatagrams -lt 1 -or
			[int64]$Stats.forwardedDuplicateDatagrams -lt 1 -or
			[int64]$Stats.observedOrderInversions -lt 1 -or
			[double]$Stats.minimumForwardDelayMs -lt 35 -or
			[double]$Stats.maximumForwardDelayMs -lt 120 -or
			[int64]$Stats.unroutableDatagrams -ne 0) {
			throw "$Direction did not prove every adverse-network fault: $($Stats | ConvertTo-Json -Compress)."
		}
	}
	if ([int]$Result.discardedAtShutdown -ne 0) {
		throw "UDP fault proxy discarded $($Result.discardedAtShutdown) delayed datagram(s) at shutdown."
	}
	return $Result
}

$CommonArguments = @(
	'-unattended',
	'-nullrhi',
	'-nosound',
	'-nosplash',
	'-stdout',
	'-FullStdOutLogOutput',
	(New-QuotedArgument 'SeinConsumerMarkerDir' $MarkerDirectory)
)

try {
	Write-Host `
		"[ConsumerMatrix] $Profile packaged listen-server qualification on $ServerAddress" `
		-ForegroundColor Cyan
	$Server = Start-QualificationProcess 'server' (@(
		"-port=$ServerPort",
		'-SeinConsumerQualificationRole=Server'
	) + $CommonArguments)
	Wait-QualificationMarker 'server-ready.marker' @($Server) | Out-Null
	if ($NetworkProfile -eq 'Adverse') {
		$ProxyProcess = Start-UdpFaultProxy
		Wait-QualificationMarker `
			'proxy-ready.marker' @($Server, $ProxyProcess) | Out-Null
	}

	$Client = Start-QualificationProcess 'client' (@(
		'-SeinConsumerQualificationRole=Client',
		(New-QuotedArgument 'SeinConsumerServerAddress' $ServerAddress)
	) + $CommonArguments)

	$RootGossipPath = Wait-QualificationMarker `
		'server-root-gossip-complete.marker' @($Server, $Client)
	$RootGossipResult = Read-KeyValueMarker $RootGossipPath
	if (-not $RootGossipResult.ContainsKey('Turn') -or
		-not $RootGossipResult.ContainsKey('Reporters') -or
		[int]$RootGossipResult.Reporters -lt 2) {
		throw 'Packaged qualification did not complete a two-peer world-root comparison.'
	}
	Wait-QualificationMarker `
		'server-pair-grant-observed.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'client-pair-grant-observed.marker' @($Server, $Client) | Out-Null
	if ($Profile -eq 'MovementPlus') {
		Wait-QualificationMarker `
			'client-movement-command-submitted.marker' @($Server, $Client) | Out-Null
		$ServerMovementPath = Wait-QualificationMarker `
			'server-movement-observed.marker' @($Server, $Client)
		$ServerMovementResult = Read-KeyValueMarker $ServerMovementPath
		Assert-MovementEvidence $ServerMovementResult 'Server movement marker'
		$ClientMovementPath = Wait-QualificationMarker `
			'client-movement-observed.marker' @($Server, $Client)
		$ClientMovementResult = Read-KeyValueMarker $ClientMovementPath
		Assert-MovementEvidence $ClientMovementResult 'Client movement marker'
	}
	Wait-QualificationMarker `
		'client-resync-complete.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'server-drop-observed.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'client-reconnect-complete.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'client-reconnect-capability-preserved.marker' @($Server, $Client) | Out-Null
	if ($Profile -eq 'MovementPlus') {
		$ReconnectMovementPath = Wait-QualificationMarker `
			'client-reconnect-movement-preserved.marker' @($Server, $Client)
		$ReconnectMovementResult = Read-KeyValueMarker $ReconnectMovementPath
		Assert-MovementEvidence `
			$ReconnectMovementResult 'Reconnect movement marker'
	}
	Wait-QualificationMarker `
		'server-reconnect-activated.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'server-pair-revoke-observed.marker' @($Server, $Client) | Out-Null
	Wait-QualificationMarker `
		'client-pair-revoke-observed.marker' @($Server, $Client) | Out-Null
	$ServerCompletePath = Wait-QualificationMarker `
		'server-complete.marker' @($Server, $Client)
	$ServerResult = Read-KeyValueMarker $ServerCompletePath
	$RequiredServerKeys = @('Path', 'EndTick', 'Root')
	if ($Profile -eq 'MovementPlus') {
		$RequiredServerKeys += @(
			'MovementState', 'MovementClass', 'MovementDestination',
			'MovementTargetWitness', 'MovementTelemetry')
	}
	if ($Profile -eq 'MovementPlus') {
		Assert-MovementEvidence $ServerResult 'Server completion marker'
	}
	foreach ($RequiredKey in $RequiredServerKeys) {
		if (-not $ServerResult.ContainsKey($RequiredKey) -or
			[string]::IsNullOrWhiteSpace($ServerResult[$RequiredKey])) {
			throw "Server completion marker omitted '$RequiredKey'."
		}
	}
	if (-not (Test-Path -LiteralPath $ServerResult.Path)) {
		throw "Server published no replay at '$($ServerResult.Path)'."
	}
	$ReplayArtifactPath = [System.IO.Path]::GetFullPath($ServerResult.Path)
	if (-not $ReplayArtifactPath.StartsWith(
			$ReplayArtifactRoot + [System.IO.Path]::DirectorySeparatorChar,
			[System.StringComparison]::OrdinalIgnoreCase) -or
		[System.IO.Path]::GetExtension($ReplayArtifactPath) -ne '.seinreplay') {
		throw "Refusing to consume or clean replay outside '$ReplayArtifactRoot'."
	}

	foreach ($LiveProcess in @($Client, $Server)) {
		$LiveProcess.Refresh()
		if (-not $LiveProcess.HasExited) {
			Stop-Process -Id $LiveProcess.Id -Force -ErrorAction SilentlyContinue
			$LiveProcess.WaitForExit()
		}
	}
	if ($NetworkProfile -eq 'Adverse') {
		$ProxyResult = Stop-AndValidateUdpFaultProxy
	}

	Write-Host `
		'[ConsumerMatrix] packaged replay checkpoint-seek qualification' `
		-ForegroundColor Cyan
	$ReplayArguments = @(
		'-SeinConsumerQualificationRole=Replay',
		(New-QuotedArgument 'SeinConsumerReplayPath' $ReplayArtifactPath),
		(New-QuotedArgument 'SeinConsumerExpectedRoot' $ServerResult.Root),
		"-SeinConsumerExpectedEndTick=$($ServerResult.EndTick)"
	)
	if ($Profile -eq 'MovementPlus') {
		$ReplayArguments += (New-QuotedArgument `
			'SeinConsumerExpectedMovementState' $ServerResult.MovementState)
	}
	$Replay = Start-QualificationProcess 'replay' (
		$ReplayArguments + $CommonArguments)
	$ReplayCompletePath = Wait-QualificationMarker `
		'replay-complete.marker' @($Replay)
	$ReplayResult = Read-KeyValueMarker $ReplayCompletePath
	if ($ReplayResult.EndTick -ne $ServerResult.EndTick -or
		$ReplayResult.Root -ne $ServerResult.Root -or
		$ReplayResult.PairGrantWitness -ne 'Passed' -or
		$ReplayResult.PairRevokeWitness -ne 'Passed') {
		throw 'Replay completion marker disagreed with the authoritative server frontier.'
	}
	if ($Profile -eq 'MovementPlus' -and
		($ReplayResult.MovementCommandWitness -ne 'Passed' -or
		$ReplayResult.MovementStateWitness -ne 'Passed' -or
		$ReplayResult.MovementFinalStateWitness -ne 'Passed')) {
		throw 'Replay completion marker omitted the Movement+ command/state witnesses.'
	}
	if ($Profile -eq 'MovementPlus') {
		Assert-MovementEvidence $ReplayResult 'Replay completion marker'
	}
	$Replay.Refresh()
	if (-not $Replay.HasExited) {
		Stop-Process -Id $Replay.Id -Force -ErrorAction SilentlyContinue
		$Replay.WaitForExit()
	}
	Remove-Item -LiteralPath $ReplayArtifactPath -Force
	if (Test-Path -LiteralPath $ReplayArtifactPath) {
		throw "Verified replay artifact could not be removed: '$ReplayArtifactPath'."
	}

	$Result = [ordered]@{
		schemaVersion = 5
		profile = $Profile
		networkProfile = $NetworkProfile
		networkFaultInjection = if ($NetworkProfile -eq 'Adverse') {
			'Passed'
		} else { 'NotApplicable' }
		networkFaultProxyResultSha256 = if ($NetworkProfile -eq 'Adverse') {
			(Get-FileHash -LiteralPath (
				Join-Path $MarkerDirectory 'proxy-result.json') `
				-Algorithm SHA256).Hash
		} else { $null }
		networkFaultStatistics = if ($NetworkProfile -eq 'Adverse') {
			[ordered]@{
				clientToServer = $ProxyResult.clientToServer
				serverToClient = $ProxyResult.serverToClient
				queuePeak = [int]$ProxyResult.queuePeak
				clientEndpointChanges =
					[int64]$ProxyResult.clientEndpointChanges
			}
		} else { $null }
		executableSha256 = $ExecutableSha256
		listenServer = 'Passed'
		twoPlayerLobbyTravel = 'Passed'
		lockstepCommandFlow = 'Passed'
		determinismWorldRootGossip = 'Passed'
		checkpointTailResync = 'Passed'
		disconnectReconnect = 'Passed'
		replayCheckpointSeek = 'Passed'
		pairCapabilityCommandFlow = 'Passed'
		pairCapabilityReconnectPersistence = 'Passed'
		pairCapabilityReplayWitness = 'Passed'
		movementPlusCommandFlow = if ($Profile -eq 'MovementPlus') {
			'Passed'
		} else { 'NotApplicable' }
		movementPlusReconnectPersistence = if ($Profile -eq 'MovementPlus') {
			'Passed'
		} else { 'NotApplicable' }
		movementPlusReplayWitness = if ($Profile -eq 'MovementPlus') {
			'Passed'
		} else { 'NotApplicable' }
		movementClass = if ($Profile -eq 'MovementPlus') {
			$ServerResult.MovementClass
		} else { 'NotApplicable' }
		movementDestination = if ($Profile -eq 'MovementPlus') {
			$ServerResult.MovementDestination
		} else { 'NotApplicable' }
		serverMovementState = if ($Profile -eq 'MovementPlus') {
			$ServerResult.MovementState
		} else { 'NotApplicable' }
		clientMovementState = if ($Profile -eq 'MovementPlus') {
			$ClientMovementResult.MovementState
		} else { 'NotApplicable' }
		reconnectMovementState = if ($Profile -eq 'MovementPlus') {
			$ReconnectMovementResult.MovementState
		} else { 'NotApplicable' }
		replayMovementState = if ($Profile -eq 'MovementPlus') {
			$ReplayResult.MovementState
		} else { 'NotApplicable' }
		serverMovementTelemetry = if ($Profile -eq 'MovementPlus') {
			$ServerResult.MovementTelemetry
		} else { 'NotApplicable' }
		clientMovementTelemetry = if ($Profile -eq 'MovementPlus') {
			$ClientMovementResult.MovementTelemetry
		} else { 'NotApplicable' }
		reconnectMovementTelemetry = if ($Profile -eq 'MovementPlus') {
			$ReconnectMovementResult.MovementTelemetry
		} else { 'NotApplicable' }
		replayMovementTelemetry = if ($Profile -eq 'MovementPlus') {
			$ReplayResult.MovementTelemetry
		} else { 'NotApplicable' }
		endTick = [int]$ServerResult.EndTick
		canonicalRoot = $ServerResult.Root
		rootGossipTurn = [int]$RootGossipResult.Turn
		rootGossipReporters = [int]$RootGossipResult.Reporters
		replayFile = [System.IO.Path]::GetFileName($ReplayArtifactPath)
		replayArtifact = 'RemovedAfterVerification'
		serverPort = $ServerPort
		proxyPort = if ($NetworkProfile -eq 'Adverse') {
			$ProxyPort
		} else { $null }
	}
	$ResultPath = Join-Path $MarkerDirectory 'runtime-result.json'
	[System.IO.File]::WriteAllText(
		$ResultPath,
		($Result | ConvertTo-Json -Depth 4),
		[System.Text.UTF8Encoding]::new($false))
	Write-Host `
		"[ConsumerMatrix] $Profile packaged multiplayer/replay qualification passed: $ResultPath" `
		-ForegroundColor Green
}
finally {
	if ($ProxyProcess) {
		$ProxyProcess.Refresh()
		if (-not $ProxyProcess.HasExited) {
			New-Item -ItemType File `
				-Path (Join-Path $MarkerDirectory 'proxy-stop.marker') `
				-Force | Out-Null
			if (-not $ProxyProcess.WaitForExit(3000)) {
				Stop-Process -Id $ProxyProcess.Id -Force `
					-ErrorAction SilentlyContinue
				$ProxyProcess.WaitForExit()
			}
		}
	}
	foreach ($Process in $Processes) {
		$Process.Refresh()
		if (-not $Process.HasExited) {
			Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
			$Process.WaitForExit()
		}
	}
	if ($ReplayArtifactPath -and
		(Test-Path -LiteralPath $ReplayArtifactPath)) {
		Remove-Item -LiteralPath $ReplayArtifactPath -Force `
			-ErrorAction SilentlyContinue
	}
}
