#Requires -Version 5.1
<#
.SYNOPSIS
  Runs a deterministic local UDP fault proxy for packaged network qualification.

.DESCRIPTION
  Forwards one Unreal client endpoint to one upstream listen server while
  injecting bounded latency, jitter, loss, duplication, and reordering. Fault
  cadence and jitter are deterministic for a supplied seed. The controller
  stops the proxy by creating proxy-stop.marker in -ControlDirectory.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateRange(1, 65535)]
	[int] $ListenPort,

	[Parameter(Mandatory = $true)]
	[string] $UpstreamHost,

	[Parameter(Mandatory = $true)]
	[ValidateRange(1, 65535)]
	[int] $UpstreamPort,

	[Parameter(Mandatory = $true)]
	[string] $ControlDirectory,

	[ValidateRange(0, 5000)]
	[int] $LatencyMs = 60,

	[ValidateRange(0, 1000)]
	[int] $JitterMs = 20,

	[ValidateRange(0, 1000000)]
	[int] $DropEvery = 43,

	[ValidateRange(0, 1000000)]
	[int] $DuplicateEvery = 59,

	[ValidateRange(0, 1000000)]
	[int] $ReorderEvery = 31,

	[ValidateRange(0, 5000)]
	[int] $ReorderDelayMs = 90,

	[ValidateRange(1, [uint32]::MaxValue)]
	[uint32] $Seed = 0x51EA47
)

$ErrorActionPreference = 'Stop'
$ControlDirectory = [System.IO.Path]::GetFullPath($ControlDirectory)
New-Item -ItemType Directory -Path $ControlDirectory -Force | Out-Null
$ReadyPath = Join-Path $ControlDirectory 'proxy-ready.marker'
$StopPath = Join-Path $ControlDirectory 'proxy-stop.marker'
$ResultPath = Join-Path $ControlDirectory 'proxy-result.json'
$FailurePath = Join-Path $ControlDirectory 'proxy-failed.marker'
foreach ($Path in @($ReadyPath, $StopPath, $ResultPath, $FailurePath)) {
	Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
}

$ClientToServerJitterSeed = [uint32]$Seed
$ServerToClientJitterSeed = [uint32](
	$Seed -bxor [uint32]2654435769)
if ($ServerToClientJitterSeed -eq 0) {
	$ServerToClientJitterSeed = [uint32]1
}
$script:RandomState = @{
	clientToServer = $ClientToServerJitterSeed
	serverToClient = $ServerToClientJitterSeed
}
function Get-NextFaultRandom([string] $Direction)
{
	$Value = [uint32]$script:RandomState[$Direction]
	$Value = [uint32]($Value -bxor [uint32]($Value -shl 13))
	$Value = [uint32]($Value -bxor [uint32]($Value -shr 17))
	$Value = [uint32]($Value -bxor [uint32]($Value -shl 5))
	$script:RandomState[$Direction] = $Value
	return $Value
}

function Get-FaultJitterMilliseconds([string] $Direction)
{
	if ($JitterMs -eq 0) {
		return 0
	}
	$Range = [uint32](2 * $JitterMs + 1)
	return [int]((Get-NextFaultRandom $Direction) % $Range) - $JitterMs
}

function New-DirectionStatistics
{
	return [ordered]@{
		receivedDatagrams = 0L
		receivedBytes = 0L
		forwardedDatagrams = 0L
		forwardedBytes = 0L
		droppedDatagrams = 0L
		duplicatedDatagrams = 0L
		reorderedDatagrams = 0L
		forwardedDuplicateDatagrams = 0L
		observedOrderInversions = 0L
		minimumForwardDelayMs = $null
		maximumForwardDelayMs = 0.0
		unroutableDatagrams = 0L
	}
}

function Test-IsCadenceHit([int64] $Ordinal, [int] $Every)
{
	return $Every -gt 0 -and ($Ordinal % $Every) -eq 0
}

function Add-ScheduledDatagram(
	[byte[]] $Data,
	[System.Net.IPEndPoint] $Destination,
	[string] $Direction,
	[int64] $Ordinal,
	[int] $DelayMs,
	[bool] $IsDuplicate)
{
	$script:ScheduleSequence++
	$ScheduledAtTicks = [DateTime]::UtcNow.Ticks
	$DueTicks = $ScheduledAtTicks +
		([int64][Math]::Max(0, $DelayMs) * [TimeSpan]::TicksPerMillisecond)
	$script:Schedule.Add([pscustomobject]@{
		DueTicks = $DueTicks
		Sequence = $script:ScheduleSequence
		Data = $Data
		Destination = $Destination
		Direction = $Direction
		Ordinal = $Ordinal
		ScheduledAtTicks = $ScheduledAtTicks
		IsDuplicate = $IsDuplicate
	})
	if ($script:Schedule.Count -gt $script:QueuePeak) {
		$script:QueuePeak = $script:Schedule.Count
	}
}

$Socket = $null
$StartedAt = [DateTime]::UtcNow
$script:Schedule = [System.Collections.Generic.List[object]]::new()
$script:ScheduleSequence = 0L
$script:QueuePeak = 0
$ClientEndpoint = $null
$ClientEndpointChanges = 0L
$ClientToServer = New-DirectionStatistics
$ServerToClient = New-DirectionStatistics
$LastForwardedOrdinal = @{
	clientToServer = 0L
	serverToClient = 0L
}

try {
	$UpstreamAddress = @(
		[System.Net.Dns]::GetHostAddresses($UpstreamHost) |
		Where-Object AddressFamily -eq ([System.Net.Sockets.AddressFamily]::InterNetwork) |
		Select-Object -First 1)
	if ($UpstreamAddress.Count -ne 1) {
		throw "Could not resolve an IPv4 address for upstream host '$UpstreamHost'."
	}
	$UpstreamEndpoint = [System.Net.IPEndPoint]::new(
		$UpstreamAddress[0], $UpstreamPort)
	$Socket = [System.Net.Sockets.UdpClient]::new($ListenPort)
	$Socket.Client.ReceiveBufferSize = 4MB
	$Socket.Client.SendBufferSize = 4MB
	if ($env:OS -eq 'Windows_NT') {
		# Windows reports ICMP port-unreachable responses as fatal UDP receive
		# resets by default. A reconnect proxy must survive the interval after an
		# endpoint closes and before its replacement socket is bound.
		$null = $Socket.Client.IOControl(
			-1744830452, [byte[]](0, 0, 0, 0), $null)
	}

	$Ready = [ordered]@{
		schemaVersion = 2
		listenPort = $ListenPort
		upstream = $UpstreamEndpoint.ToString()
		seed = [uint64]$Seed
		jitterSequence = 'PerDirection'
		latencyMs = $LatencyMs
		jitterMs = $JitterMs
		dropEvery = $DropEvery
		duplicateEvery = $DuplicateEvery
		reorderEvery = $ReorderEvery
		reorderDelayMs = $ReorderDelayMs
		startedAtUtc = $StartedAt.ToString('o')
	}
	[System.IO.File]::WriteAllText(
		$ReadyPath,
		($Ready | ConvertTo-Json -Depth 4),
		[System.Text.UTF8Encoding]::new($false))

	while (-not (Test-Path -LiteralPath $StopPath)) {
		$ReceivedAny = $false
		$ReceivedBatch = 0
		while ($Socket.Client.Poll(
			0, [System.Net.Sockets.SelectMode]::SelectRead) -and
			$ReceivedBatch -lt 64) {
			$ReceivedAny = $true
			$ReceivedBatch++
			$RemoteEndpoint = [System.Net.IPEndPoint]::new(
				[System.Net.IPAddress]::Any, 0)
			[byte[]]$Data = $Socket.Receive([ref]$RemoteEndpoint)
			$FromUpstream = $RemoteEndpoint.Port -eq $UpstreamEndpoint.Port -and
				$RemoteEndpoint.Address.Equals($UpstreamEndpoint.Address)

			if ($FromUpstream) {
				$Direction = 'serverToClient'
				$Statistics = $ServerToClient
				$Destination = $ClientEndpoint
			}
			else {
				$Direction = 'clientToServer'
				$Statistics = $ClientToServer
				$Destination = $UpstreamEndpoint
				if ($ClientEndpoint -and
					-not $RemoteEndpoint.Equals($ClientEndpoint)) {
					$ClientEndpointChanges++
				}
				$ClientEndpoint = $RemoteEndpoint
			}

			$Statistics.receivedDatagrams++
			$Statistics.receivedBytes += $Data.Length
			$Ordinal = [int64]$Statistics.receivedDatagrams
			if (-not $Destination) {
				$Statistics.unroutableDatagrams++
				continue
			}
			if (Test-IsCadenceHit $Ordinal $DropEvery) {
				$Statistics.droppedDatagrams++
				continue
			}

			$DelayMs = $LatencyMs + (Get-FaultJitterMilliseconds $Direction)
			if (Test-IsCadenceHit $Ordinal $ReorderEvery) {
				$DelayMs += $ReorderDelayMs
				$Statistics.reorderedDatagrams++
			}
			Add-ScheduledDatagram `
				$Data $Destination $Direction $Ordinal $DelayMs $false
			if (Test-IsCadenceHit $Ordinal $DuplicateEvery) {
				$Statistics.duplicatedDatagrams++
				Add-ScheduledDatagram `
					$Data $Destination $Direction $Ordinal ($DelayMs + 1) $true
			}
		}

		while ($script:Schedule.Count -gt 0) {
			$NowTicks = [DateTime]::UtcNow.Ticks
			$ReadyIndex = -1
			$ReadyDueTicks = [int64]::MaxValue
			$ReadySequence = [int64]::MaxValue
			for ($Index = 0; $Index -lt $script:Schedule.Count; $Index++) {
				$Candidate = $script:Schedule[$Index]
				if ($Candidate.DueTicks -le $NowTicks -and
					($Candidate.DueTicks -lt $ReadyDueTicks -or
						($Candidate.DueTicks -eq $ReadyDueTicks -and
							$Candidate.Sequence -lt $ReadySequence))) {
					$ReadyIndex = $Index
					$ReadyDueTicks = $Candidate.DueTicks
					$ReadySequence = $Candidate.Sequence
				}
			}
			if ($ReadyIndex -lt 0) {
				break
			}

			$Scheduled = $script:Schedule[$ReadyIndex]
			$script:Schedule.RemoveAt($ReadyIndex)
			$Sent = $Socket.Send(
				$Scheduled.Data,
				$Scheduled.Data.Length,
				$Scheduled.Destination)
			$Statistics = if ($Scheduled.Direction -eq 'clientToServer') {
				$ClientToServer
			} else {
				$ServerToClient
			}
			$Statistics.forwardedDatagrams++
			$Statistics.forwardedBytes += $Sent
			if ($Scheduled.IsDuplicate) {
				$Statistics.forwardedDuplicateDatagrams++
			}
			if ([int64]$Scheduled.Ordinal -lt
				[int64]$LastForwardedOrdinal[$Scheduled.Direction]) {
				$Statistics.observedOrderInversions++
			}
			if ([int64]$Scheduled.Ordinal -gt
				[int64]$LastForwardedOrdinal[$Scheduled.Direction]) {
				$LastForwardedOrdinal[$Scheduled.Direction] =
					[int64]$Scheduled.Ordinal
			}
			$ObservedDelayMs = ([DateTime]::UtcNow.Ticks -
				[int64]$Scheduled.ScheduledAtTicks) /
				[double][TimeSpan]::TicksPerMillisecond
			if ($null -eq $Statistics.minimumForwardDelayMs -or
				$ObservedDelayMs -lt
					[double]$Statistics.minimumForwardDelayMs) {
				$Statistics.minimumForwardDelayMs = $ObservedDelayMs
			}
			if ($ObservedDelayMs -gt
				[double]$Statistics.maximumForwardDelayMs) {
				$Statistics.maximumForwardDelayMs = $ObservedDelayMs
			}
		}

		if (-not $ReceivedAny) {
			Start-Sleep -Milliseconds 1
		}
	}

	$Result = [ordered]@{
		schemaVersion = 2
		status = 'Passed'
		listenPort = $ListenPort
		upstream = $UpstreamEndpoint.ToString()
		clientEndpoint = if ($ClientEndpoint) {
			$ClientEndpoint.ToString()
		} else { $null }
		clientEndpointChanges = $ClientEndpointChanges
		seed = [uint64]$Seed
		jitterSequence = 'PerDirection'
		latencyMs = $LatencyMs
		jitterMs = $JitterMs
		dropEvery = $DropEvery
		duplicateEvery = $DuplicateEvery
		reorderEvery = $ReorderEvery
		reorderDelayMs = $ReorderDelayMs
		queuePeak = $script:QueuePeak
		discardedAtShutdown = $script:Schedule.Count
		clientToServer = $ClientToServer
		serverToClient = $ServerToClient
		startedAtUtc = $StartedAt.ToString('o')
		completedAtUtc = [DateTime]::UtcNow.ToString('o')
	}
	[System.IO.File]::WriteAllText(
		$ResultPath,
		($Result | ConvertTo-Json -Depth 6),
		[System.Text.UTF8Encoding]::new($false))
}
catch {
	[System.IO.File]::WriteAllText(
		$FailurePath,
		$_.Exception.ToString(),
		[System.Text.UTF8Encoding]::new($false))
	throw
}
finally {
	if ($Socket) {
		$Socket.Dispose()
	}
}
