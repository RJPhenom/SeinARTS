#Requires -Version 5.1
<#
.SYNOPSIS
  Exercises the deterministic UDP fault proxy against a local echo endpoint.
#>
[CmdletBinding()]
param(
	[ValidateRange(5, 120)]
	[int] $TimeoutSeconds = 30,

	[string] $ResultDirectory
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$ProxyScript = Join-Path $PSScriptRoot 'Invoke-UdpFaultProxy.ps1'
if (-not (Test-Path -LiteralPath $ProxyScript -PathType Leaf)) {
	throw "UDP fault proxy is missing: '$ProxyScript'."
}

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

function Wait-Path(
	[string] $Path,
	[System.Diagnostics.Process] $Process,
	[int] $Seconds)
{
	$Deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	while ([DateTime]::UtcNow -lt $Deadline) {
		if (Test-Path -LiteralPath $Path) {
			return
		}
		$Process.Refresh()
		if ($Process.HasExited) {
			throw "UDP fault proxy exited early with code $($Process.ExitCode)."
		}
		Start-Sleep -Milliseconds 50
	}
	throw "Timed out waiting for '$Path'."
}

$AttemptId = [Guid]::NewGuid().ToString('N')
$AttemptRoot = if ($ResultDirectory) {
	[System.IO.Path]::GetFullPath($ResultDirectory)
} else {
	Join-Path $RepoRoot (
		'Saved\Automation\UdpFaultProxySelfTest-{0}-{1}' -f
		(Get-Date -Format 'yyyyMMdd-HHmmss'), $AttemptId.Substring(0, 8))
}
if (Test-Path -LiteralPath $AttemptRoot) {
	throw "UDP fault proxy self-test result directory already exists: '$AttemptRoot'."
}
New-Item -ItemType Directory -Path $AttemptRoot -Force | Out-Null
$ReadyPath = Join-Path $AttemptRoot 'proxy-ready.marker'
$StopPath = Join-Path $AttemptRoot 'proxy-stop.marker'
$ResultPath = Join-Path $AttemptRoot 'proxy-result.json'
$FailurePath = Join-Path $AttemptRoot 'proxy-failed.marker'
$ProxyStdout = Join-Path $AttemptRoot 'proxy.stdout.log'
$ProxyStderr = Join-Path $AttemptRoot 'proxy.stderr.log'

$UpstreamPort = Get-FreeUdpPort
do {
	$ProxyPort = Get-FreeUdpPort
} while ($ProxyPort -eq $UpstreamPort)

$PowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
$Arguments = @(
	'-NoProfile',
	'-ExecutionPolicy', 'Bypass',
	'-File', ('"{0}"' -f $ProxyScript),
	'-ListenPort', $ProxyPort,
	'-UpstreamHost', '127.0.0.1',
	'-UpstreamPort', $UpstreamPort,
	'-ControlDirectory', ('"{0}"' -f $AttemptRoot),
	'-LatencyMs', 20,
	'-JitterMs', 5,
	'-DropEvery', 43,
	'-DuplicateEvery', 59,
	'-ReorderEvery', 31,
	'-ReorderDelayMs', 35,
	'-Seed', 5368391)

$ProxyProcess = $null
$Upstream = $null
$Client = $null
$Succeeded = $false
try {
	$ProxyProcess = Start-Process `
		-FilePath $PowerShell `
		-ArgumentList $Arguments `
		-WorkingDirectory $RepoRoot `
		-RedirectStandardOutput $ProxyStdout `
		-RedirectStandardError $ProxyStderr `
		-WindowStyle Hidden `
		-PassThru
	Wait-Path $ReadyPath $ProxyProcess 10

	$Upstream = [System.Net.Sockets.UdpClient]::new($UpstreamPort)
	$Client = [System.Net.Sockets.UdpClient]::new(0)
	$ProxyEndpoint = [System.Net.IPEndPoint]::new(
		[System.Net.IPAddress]::Loopback, $ProxyPort)
	$ReturnedSequences = [System.Collections.Generic.HashSet[int]]::new()
	$ReturnedDatagrams = 0
	$ReturnedOrderInversions = 0
	$LastReturnedSequence = -1
	$EchoedDatagrams = 0
	$InvalidPayloads = 0
	$PacketCount = 240
	for ($Sequence = 0; $Sequence -lt $PacketCount; $Sequence++) {
		[byte[]]$Payload = [BitConverter]::GetBytes([int]$Sequence)
		$null = $Client.Send($Payload, $Payload.Length, $ProxyEndpoint)
	}

	$Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$LastActivity = [DateTime]::UtcNow
	while ([DateTime]::UtcNow -lt $Deadline) {
		$Activity = $false
		while ($Upstream.Client.Poll(
			0, [System.Net.Sockets.SelectMode]::SelectRead)) {
			$Remote = [System.Net.IPEndPoint]::new(
				[System.Net.IPAddress]::Any, 0)
			[byte[]]$Payload = $Upstream.Receive([ref]$Remote)
			$null = $Upstream.Send($Payload, $Payload.Length, $Remote)
			$EchoedDatagrams++
			$Activity = $true
		}
		while ($Client.Client.Poll(
			0, [System.Net.Sockets.SelectMode]::SelectRead)) {
			$Remote = [System.Net.IPEndPoint]::new(
				[System.Net.IPAddress]::Any, 0)
			[byte[]]$Payload = $Client.Receive([ref]$Remote)
			if ($Payload.Length -ne 4) {
				$InvalidPayloads++
			}
			else {
				$Sequence = [BitConverter]::ToInt32($Payload, 0)
				if ($Sequence -lt 0 -or $Sequence -ge $PacketCount) {
					$InvalidPayloads++
				}
				else {
					if ($LastReturnedSequence -ge 0 -and
						$Sequence -lt $LastReturnedSequence) {
						$ReturnedOrderInversions++
					}
					$LastReturnedSequence = $Sequence
					$null = $ReturnedSequences.Add($Sequence)
				}
			}
			$ReturnedDatagrams++
			$Activity = $true
		}
		if ($Activity) {
			$LastActivity = [DateTime]::UtcNow
		}
		elseif ($EchoedDatagrams -gt 100 -and
			$ReturnedSequences.Count -gt 100 -and
			([DateTime]::UtcNow - $LastActivity).TotalSeconds -ge 2) {
			break
		}
		Start-Sleep -Milliseconds 1
	}

	New-Item -ItemType File -Path $StopPath -Force | Out-Null
	Wait-Path $ResultPath $ProxyProcess 10
	if (-not $ProxyProcess.WaitForExit(10000)) {
		throw 'UDP fault proxy did not exit after its stop marker.'
	}
	$ProxyProcess.Refresh()
	if ($ProxyProcess.ExitCode -ne 0) {
		throw "UDP fault proxy exited with code $($ProxyProcess.ExitCode)."
	}
	if (Test-Path -LiteralPath $FailurePath) {
		throw "UDP fault proxy reported: $(Get-Content -Raw -LiteralPath $FailurePath)"
	}

	$ProxyResult = Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
	if ([int]$ProxyResult.schemaVersion -ne 2 -or
		[string]$ProxyResult.status -cne 'Passed' -or
		[string]$ProxyResult.jitterSequence -cne 'PerDirection') {
		throw 'UDP fault proxy self-test received an invalid proxy receipt.'
	}
	$Expected = [ordered]@{
		clientToServer = [ordered]@{
			received = 240L
			forwarded = 239L
			dropped = 5L
			duplicated = 4L
			reordered = 7L
		}
		serverToClient = [ordered]@{
			received = 239L
			forwarded = 238L
			dropped = 5L
			duplicated = 4L
			reordered = 7L
		}
	}
	foreach ($Direction in @('clientToServer', 'serverToClient')) {
		$Stats = $ProxyResult.$Direction
		$DirectionExpected = $Expected[$Direction]
		if ([int64]$Stats.receivedDatagrams -ne $DirectionExpected.received -or
			[int64]$Stats.forwardedDatagrams -ne $DirectionExpected.forwarded -or
			[int64]$Stats.droppedDatagrams -ne $DirectionExpected.dropped -or
			[int64]$Stats.duplicatedDatagrams -ne $DirectionExpected.duplicated -or
			[int64]$Stats.reorderedDatagrams -ne $DirectionExpected.reordered -or
			[int64]$Stats.forwardedDuplicateDatagrams -lt 1 -or
			[int64]$Stats.observedOrderInversions -lt 1 -or
			[double]$Stats.minimumForwardDelayMs -lt 10 -or
			[double]$Stats.maximumForwardDelayMs -lt 45) {
			throw "$Direction fault counts were not deterministic: $($Stats | ConvertTo-Json -Compress)."
		}
	}
	if ($InvalidPayloads -ne 0 -or $ReturnedSequences.Count -lt 180 -or
		$ReturnedDatagrams -le $ReturnedSequences.Count -or
		$ReturnedOrderInversions -lt 1) {
		throw "Echo integrity/fault observation failed: datagrams=$ReturnedDatagrams, unique=$($ReturnedSequences.Count), inversions=$ReturnedOrderInversions, invalid=$InvalidPayloads."
	}
	if ([int]$ProxyResult.discardedAtShutdown -ne 0) {
		throw "Proxy stopped with $($ProxyResult.discardedAtShutdown) scheduled datagram(s)."
	}

	$SelfTestResult = [ordered]@{
		schemaVersion = 2
		status = 'Passed'
		attemptId = $AttemptId
		packetCount = $PacketCount
		echoedDatagrams = $EchoedDatagrams
		uniqueReturnedSequences = $ReturnedSequences.Count
		returnedDatagrams = $ReturnedDatagrams
		returnedOrderInversions = $ReturnedOrderInversions
		invalidPayloads = $InvalidPayloads
		proxyResult = [System.IO.Path]::GetFileName($ResultPath)
		proxyResultSha256 =
			(Get-FileHash -LiteralPath $ResultPath -Algorithm SHA256).Hash
		completedAtUtc = [DateTime]::UtcNow.ToString('o')
	}
	$SelfTestPath = Join-Path $AttemptRoot 'self-test-result.json'
	[System.IO.File]::WriteAllText(
		$SelfTestPath,
		($SelfTestResult | ConvertTo-Json -Depth 4),
		[System.Text.UTF8Encoding]::new($false))
	$Succeeded = $true
	Write-Host `
		"[ConsumerMatrix] UDP fault proxy self-test passed: $SelfTestPath" `
		-ForegroundColor Green
}
finally {
	if ($Client) { $Client.Dispose() }
	if ($Upstream) { $Upstream.Dispose() }
	if ($ProxyProcess) {
		$ProxyProcess.Refresh()
		if (-not $ProxyProcess.HasExited) {
			New-Item -ItemType File -Path $StopPath -Force | Out-Null
			if (-not $ProxyProcess.WaitForExit(3000)) {
				Stop-Process -Id $ProxyProcess.Id -Force -ErrorAction SilentlyContinue
				$ProxyProcess.WaitForExit()
			}
		}
	}
	if (-not $Succeeded) {
		Write-Warning "UDP fault proxy self-test evidence retained at '$AttemptRoot'."
	}
}
