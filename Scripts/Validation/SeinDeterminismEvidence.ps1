#Requires -Version 5.1
# Read-only verifier shared by the A/B runner and release evidence capture.
function Assert-SeinDeterminismEvidence([string] $ResultFile)
{
    $Result = Get-Content -Raw -LiteralPath $ResultFile | ConvertFrom-Json
    $Root = Split-Path -Parent $ResultFile
    if ($Result.schemaVersion -ne 1 -or $Result.status -cne 'Passed' -or
        $Result.expectedFrames -ne 120 -or @($Result.attempts).Count -ne 2) {
        throw 'Incomplete determinism A/B result.'
    }
    $Traces = @{}; $Attempts = @{}
    foreach ($Role in @('serial', 'parallel')) {
        $Records = @($Result.attempts | Where-Object { $_.role -ceq $Role })
        if ($Records.Count -ne 1) { throw "Missing or duplicate $Role evidence." }
        $Record = $Records[0]
        foreach ($Name in @('attempt.json', 'index.json', 'build-provenance.json', 'Automation.log')) {
            $Files = @($Record.files | Where-Object { $_.file -ceq "$Role/$Name" })
            if ($Files.Count -ne 1) { throw "Missing or duplicate $Role/$Name binding." }
            if ($Files[0].sha256 -cne (Get-FileHash -LiteralPath (Join-Path $Root "$Role/$Name") -Algorithm SHA256).Hash) {
                throw "Changed determinism evidence: $Role/$Name."
            }
        }
        $Attempt = Get-Content -Raw -LiteralPath (Join-Path $Root "$Role/attempt.json") | ConvertFrom-Json
        $Index = Get-Content -Raw -LiteralPath (Join-Path $Root "$Role/index.json") | ConvertFrom-Json
        $Build = Get-Content -Raw -LiteralPath (Join-Path $Root "$Role/build-provenance.json") | ConvertFrom-Json
        $Suite = if ($Role -eq 'serial') { 'SeinARTS.Determinism.Process.SerialCollisionTrace' }
            else { 'SeinARTS.Determinism.Process.ParallelCollisionTrace' }
        if ($Attempt.schemaVersion -ne 4 -or $Attempt.status -cne 'Passed' -or
            $Attempt.profile -cne 'Framework' -or $Attempt.suite -cne $Suite -or
            $Attempt.attemptId -cne $Record.attemptId -or [string]::IsNullOrWhiteSpace($Attempt.attemptId) -or
            $null -eq $Attempt.editorExitCode -or $Attempt.editorExitCode -ne 0 -or
            $Attempt.editorProcessId -le 0 -or $Attempt.expectedMinimumCount -lt 1 -or
            $Attempt.discoveredTestCount -lt $Attempt.expectedMinimumCount -or
            $null -eq $Attempt.unsuccessfulTestCount -or $Attempt.unsuccessfulTestCount -ne 0 -or
            @($Index.tests).Count -ne $Attempt.discoveredTestCount -or
            @($Index.tests | Where-Object { $_.state -cne 'Success' }).Count -ne 0 -or
            $Index.failed -ne 0 -or $Index.notRun -ne 0 -or $Index.inProcess -ne 0 -or
            $Attempt.testIndexFile -cne 'index.json' -or
            $Attempt.testIndexSha256 -cne (Get-FileHash -LiteralPath (Join-Path $Root "$Role/index.json")).Hash -or
            $Attempt.testBuildProvenanceFile -cne 'build-provenance.json' -or
            $Attempt.testBuildProvenanceSha256 -cne (Get-FileHash -LiteralPath (Join-Path $Root "$Role/build-provenance.json")).Hash -or
            $Build.schemaVersion -ne 4 -or $Build.profile -cne 'Framework') {
            throw "Invalid $Role test receipt or report."
        }
        foreach ($Field in @('commit', 'engineRoot', 'engineBuildFingerprint', 'compileSourceFingerprint')) {
            if ([string]::IsNullOrWhiteSpace([string]$Result.$Field) -or
                $Attempt.$Field -cne $Result.$Field -or $Build.$Field -cne $Result.$Field) {
                throw "Mismatched $Role $Field."
            }
        }
        if ($null -eq $Result.dirtyWorkingTree -or $null -eq $Attempt.dirtyWorkingTree -or
            $null -eq $Build.dirtyWorkingTree -or
            $Attempt.dirtyWorkingTree -ne $Result.dirtyWorkingTree -or
            $Build.dirtyWorkingTree -ne $Result.dirtyWorkingTree -or
            $Attempt.allowKnownStartupErrors -ne $Result.allowKnownStartupErrors) {
            throw "Mismatched $Role working-tree or startup-error policy."
        }
        foreach ($Field in @('dllSha256', 'productionDllSha256', 'metadataSha256')) {
            if ($null -eq $Build.$Field -or @($Build.$Field.PSObject.Properties).Count -eq 0) {
                throw "Missing $Role binary provenance."
            }
        }
        if ([DateTime]$Attempt.completedAtUtc -lt [DateTime]$Attempt.startedAtUtc -or
            [DateTime]$Attempt.startedAtUtc -lt ([DateTime]$Result.startedAtUtc).AddSeconds(-120) -or
            [DateTime]$Attempt.completedAtUtc -gt ([DateTime]$Result.completedAtUtc).AddSeconds(120)) {
            throw "Stale $Role attempt."
        }
        $Attempts[$Role] = $Attempt
        $Traces[$Role] = @(Select-String -LiteralPath (Join-Path $Root "$Role/Automation.log") `
            -Pattern '\[SeinDeterminismTrace\]\s+(tick=.*)$' | ForEach-Object {
                $_.Matches[0].Groups[1].Value.Trim()
            })
        if ($Traces[$Role].Count -ne 120) { throw "Expected 120 $Role trace frames." }
    }
    if ($Attempts.serial.attemptId -ceq $Attempts.parallel.attemptId -or
        $Attempts.serial.editorProcessId -eq $Attempts.parallel.editorProcessId -or
        $Attempts.serial.testBuildProvenanceSha256 -cne $Attempts.parallel.testBuildProvenanceSha256) {
        throw 'A/B requires distinct process attempts using the same qualified build.'
    }
    for ($Index = 0; $Index -lt 120; ++$Index) {
        foreach ($Role in @('serial', 'parallel')) {
            $Match = [regex]::Match($Traces[$Role][$Index], '^tick=(\d+) root=([0-9A-Fa-f]{32}) pose=(0x[0-9A-Fa-f]{16})$')
            if (-not $Match.Success -or [int]$Match.Groups[1].Value -ne ($Index + 1)) {
                throw "Malformed or out-of-order $Role trace at frame $($Index + 1)."
            }
        }
        if ($Traces.serial[$Index] -cne $Traces.parallel[$Index]) {
            throw "Serial/parallel canonical-root or raw-pose divergence at tick $($Index + 1)."
        }
    }
    return $Result
}
