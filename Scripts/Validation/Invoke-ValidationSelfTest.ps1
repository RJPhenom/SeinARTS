#Requires -Version 5.1
<#
.SYNOPSIS
  Exercise validation wrappers and evidence rejection without Unreal, publishing, or production edits.
.DESCRIPTION
  Runs copied wrappers in an isolated fixture tree under Saved/ValidationSelfTest. Native build and
  Automation producers are deterministic fixtures; real engine coverage is a separate smoke check.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Root = Join-Path $RepoRoot ('Saved/ValidationSelfTest/' + [Guid]::NewGuid().ToString('N'))
$Fixture = Join-Path $Root 'Fixture Project'
New-Item -ItemType Directory -Path $Fixture -Force | Out-Null
$Shell = (Get-Process -Id $PID).Path
$Checks = [System.Collections.Generic.List[string]]::new()
$OldMode = $env:SEIN_VALIDATION_FIXTURE_MODE
function Check([bool] $Condition, [string] $Name) {
    if (-not $Condition) { throw "Self-test failed: $Name" }
    $Checks.Add($Name)
}
function Invoke-Fixture([string] $Script, [string[]] $Arguments, [bool] $ShouldPass) {
    $Log = Join-Path $Root ([Guid]::NewGuid().ToString('N') + '.log')
    $PreviousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue' # PS5.1 wraps native stderr in error records.
        & $Shell -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments *> $Log
        $Code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $PreviousPreference }
    Check (($Code -eq 0) -eq $ShouldPass) "$(Split-Path -Leaf $Script): mode=$env:SEIN_VALIDATION_FIXTURE_MODE exit=$Code expectedPass=$ShouldPass (log $Log)"
    return $Log
}
try {
    foreach ($Path in @('Scripts/Build.ps1', 'Scripts/Validate.ps1',
        'Scripts/Validation/SeinDeterminismEvidence.ps1',
        'Plugins/SeinARTSTestSuite/RunDeterminismAB.ps1')) {
        $Destination = Join-Path $Fixture $Path
        New-Item -ItemType Directory -Path (Split-Path $Destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $RepoRoot $Path) -Destination $Destination
    }
    $Engine = Join-Path $Fixture 'Fake Engine'
    $BuildRoot = Join-Path $Engine 'Engine/Build/BatchFiles'
    New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
    '{"MajorVersion":5,"MinorVersion":8}' | Set-Content (Join-Path $Engine 'Engine/Build/Build.version')
    '{}' | Set-Content (Join-Path $Fixture 'SeinARTS.uproject')
    $BuildBat = Join-Path $BuildRoot 'Build.bat'
    @'
@echo off
echo Compile [x64] Fixture.cpp
echo Link [x64] UnrealEditor-Fixture.dll
for /L %%i in (1,1,200) do echo Ordinary build output %%i
if "%SEIN_VALIDATION_FIXTURE_MODE%"=="build-failure" exit /b 7
exit /b 0
'@ | Set-Content -LiteralPath $BuildBat -Encoding ASCII
    # A fake producer lets us verify orchestration and admission independently of UE availability.
    $Stub = @'
param([string]$Suite,[string]$Profile,[switch]$SkipBuild,[switch]$QuietBuild,
    [string]$ResultFile,[string]$EngineRoot,[int]$TimeoutSeconds,[switch]$AllowKnownStartupErrors)
$ErrorActionPreference='Stop'
$Mode=$env:SEIN_VALIDATION_FIXTURE_MODE
if($Mode -eq 'runner-failure'){exit 9}
if($Mode -eq 'missing-result'){exit 0}
$Project=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Report=Join-Path $Project ('Saved/Automation/'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Report -Force | Out-Null
$Role=if($Suite -like '*ParallelCollisionTrace'){'parallel'}else{'serial'}
$Build=[ordered]@{schemaVersion=4;profile=$Profile;commit='fixture-commit';dirtyWorkingTree=$false
    engineRoot=$EngineRoot;engineBuildFingerprint='fixture-engine';compileSourceFingerprint='fixture-source'
    dllSha256=@{test='A'};productionDllSha256=@{production='B'};metadataSha256=@{metadata='C'}}
if($Mode -eq 'missing-provenance'){$Build.Remove('dllSha256')}
if($Mode -eq 'null-provenance'){$Build.productionDllSha256=$null}
$Build | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Report 'build-provenance.json')
$Index=[ordered]@{tests=@(@{state='Success'});failed=0;notRun=0;inProcess=0}
$Index | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Report 'index.json')
$Lines=@(foreach($Tick in 1..120){
    $Root=('a'*32);$Pose='0x0123456789abcdef'
    if($Role -eq 'parallel' -and $Tick -eq 63){
        if($Mode -eq 'root-divergence'){$Root=('b'*32)}
        if($Mode -eq 'pose-divergence'){$Pose='0x1123456789abcdef'}
        if($Mode -eq 'missing-frame'){continue}
        if($Mode -eq 'malformed'){ '[SeinDeterminismTrace] tick=BAD';continue }
    }
    "[SeinDeterminismTrace] tick=$Tick root=$Root pose=$Pose"
})
$Lines | Set-Content (Join-Path $Report 'Automation.log')
$Attempt=[ordered]@{schemaVersion=4;status='Passed';suite=$Suite;profile=$Profile
    attemptId=[Guid]::NewGuid().ToString('N');commit=$Build.commit;dirtyWorkingTree=$false
    startedAtUtc=[DateTime]::UtcNow.ToString('o');completedAtUtc=[DateTime]::UtcNow.ToString('o')
    engineRoot=$EngineRoot;engineBuildFingerprint=$Build.engineBuildFingerprint
    compileSourceFingerprint=$Build.compileSourceFingerprint;allowKnownStartupErrors=[bool]$AllowKnownStartupErrors
    editorProcessId=if($Role -eq 'serial'){101}else{102};editorExitCode=0
    expectedMinimumCount=1;discoveredTestCount=1;unsuccessfulTestCount=0
    testIndexFile='index.json';testIndexSha256=(Get-FileHash (Join-Path $Report 'index.json')).Hash
    testBuildProvenanceFile='build-provenance.json'
    testBuildProvenanceSha256=(Get-FileHash (Join-Path $Report 'build-provenance.json')).Hash
    reportPath=$Report;skipBuild=[bool]$SkipBuild;timeoutSeconds=$TimeoutSeconds}
if($Mode -eq 'wrong-profile'){$Attempt.profile='Wrong'}
if($Mode -eq 'same-process'){$Attempt.editorProcessId=101}
if($Mode -eq 'zero-tests'){$Attempt.discoveredTestCount=0}
if($Mode -eq 'failed-receipt'){$Attempt.status='Failed'}
$Attempt | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Report 'attempt.json')
Copy-Item -LiteralPath (Join-Path $Report 'attempt.json') -Destination $ResultFile
exit 0
'@
    $Stub | Set-Content -LiteralPath (Join-Path $Fixture 'Plugins/SeinARTSTestSuite/RunTests.ps1')
    # Validate.ps1 uses git for source identity and whitespace only; use a real isolated fixture repo.
    & git -C $Fixture init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Fixture git init failed.' }
    & git -C $Fixture -c core.autocrlf=false add .
    & git -C $Fixture -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m fixture
    if ($LASTEXITCODE -ne 0) { throw 'Fixture commit failed.' }

    $env:SEIN_VALIDATION_FIXTURE_MODE='pass'
    $BuildResult=Join-Path $Root 'build-pass.json'
    $Log=Invoke-Fixture (Join-Path $Fixture 'Scripts/Build.ps1') @('-Quiet','-EngineRoot',$Engine,'-ResultFile',$BuildResult) $true
    $Build=Get-Content -Raw $BuildResult | ConvertFrom-Json
    Check ($Build.status -eq 'Passed' -and $Build.buildActions.Count -eq 2) 'Quiet build retains compilation and link evidence'
    Check (@(Get-Content $Log).Count -lt 20 -and @(Get-Content $Build.logPath).Count -gt 200) 'Quiet build keeps bulk logs out of console'
    $Log=Invoke-Fixture (Join-Path $Fixture 'Scripts/Build.ps1') @('-EngineRoot',$Engine) $true
    Check (@(Get-Content $Log).Count -gt 200) 'Existing interactive build keeps streaming output'
    $env:SEIN_VALIDATION_FIXTURE_MODE='build-failure'
    $BuildResult=Join-Path $Root 'build-fail.json'
    $null=Invoke-Fixture (Join-Path $Fixture 'Scripts/Build.ps1') @('-Quiet','-EngineRoot',$Engine,'-ResultFile',$BuildResult) $false
    $Build=Get-Content -Raw $BuildResult | ConvertFrom-Json
    Check ($Build.status -eq 'Failed' -and $Build.exitCode -eq 7) 'Native failure remains exit 7'

    $ABScript=Join-Path $Fixture 'Plugins/SeinARTSTestSuite/RunDeterminismAB.ps1'
    foreach($Mode in @('pass','root-divergence','pose-divergence','missing-frame','malformed',
        'runner-failure','missing-result','wrong-profile','same-process','zero-tests','failed-receipt',
        'missing-provenance','null-provenance')) {
        $env:SEIN_VALIDATION_FIXTURE_MODE=$Mode
        $ABRoot=Join-Path $Root $Mode
        $null=Invoke-Fixture $ABScript @('-EngineRoot',$Engine,'-ResultDirectory',$ABRoot,'-TimeoutSeconds','91') ($Mode -eq 'pass')
        $AB=Get-Content -Raw (Join-Path $ABRoot 'ab-result.json') | ConvertFrom-Json
        Check (($AB.status -eq 'Passed') -eq ($Mode -eq 'pass')) "A/B receipt status: $Mode"
    }
    . (Join-Path $RepoRoot 'Scripts/Validation/SeinDeterminismEvidence.ps1')
    $GoodFile=Join-Path $Root 'pass/ab-result.json'
    $Good=Assert-SeinDeterminismEvidence $GoodFile
    $Serial=Get-Content -Raw (Join-Path $Root 'pass/serial/attempt.json') | ConvertFrom-Json
    $Parallel=Get-Content -Raw (Join-Path $Root 'pass/parallel/attempt.json') | ConvertFrom-Json
    Check ($Serial.engineRoot -eq $Engine -and $Serial.timeoutSeconds -eq 91) 'Engine path and timeout forwarded'
    Check (-not $Serial.skipBuild -and $Parallel.skipBuild) 'A/B builds once then reuses qualified profile'
    # The release guard is tested from its actual AST, without executing release orchestration.
    $Tokens=$null;$Errors=$null
    $ReleaseAst=[System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $RepoRoot 'Scripts/Release/Invoke-ReleaseGate.ps1'),[ref]$Tokens,[ref]$Errors)
    $Guard=$ReleaseAst.Find({param($Node) $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $Node.Name -eq 'Assert-ReleaseDeterminismEvidence'},$true)
    . ([scriptblock]::Create($Guard.Extent.Text))
    $DeterminismResultFile=$GoodFile;$Commit=$Good.commit;$EngineRoot=$Good.engineRoot
    $EngineBuildFingerprint=$Good.engineBuildFingerprint
    $Receipt=@{dirtyWorkingTree=$Good.dirtyWorkingTree;determinismAB=@{sha256=(Get-FileHash $GoodFile).Hash}}
    Assert-ReleaseDeterminismEvidence
    $AttemptGuard=$ReleaseAst.Find({param($Node) $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $Node.Name -eq 'Get-QualifiedTestAttemptPath'},$true)
    . ([scriptblock]::Create($AttemptGuard.Extent.Text))
    $ExactAttemptPath=Join-Path $Serial.reportPath 'attempt.json'
    $Qualified=Get-QualifiedTestAttemptPath -Suite $Serial.suite -Profile Framework `
        -InvocationStarted ([DateTime]$Good.startedAtUtc) -ExactAttemptPath $ExactAttemptPath
    Check ($Qualified -ceq $ExactAttemptPath) 'Release consumes exact invocation receipt without historical discovery'
    $Rejected=$false
    try { $null=Get-QualifiedTestAttemptPath -Suite 'SeinARTS.Unit' -Profile Framework `
        -InvocationStarted ([DateTime]$Good.startedAtUtc) -ExactAttemptPath $ExactAttemptPath } catch { $Rejected=$true }
    Check $Rejected 'Release rejects exact receipt from wrong suite'
    $Rejected=$false
    try { $null=Get-QualifiedTestAttemptPath -Suite $Serial.suite -Profile Framework `
        -InvocationStarted ([DateTime]$Good.startedAtUtc).AddHours(1) -ExactAttemptPath $ExactAttemptPath } catch { $Rejected=$true }
    Check $Rejected 'Release rejects old exact receipt'
    $Commit='foreign-commit';$Rejected=$false
    try { Assert-ReleaseDeterminismEvidence } catch { $Rejected=$true }
    Check $Rejected 'Release rejects evidence from another commit'
    $Commit=$Good.commit;$Receipt.determinismAB.sha256='tampered';$Rejected=$false
    try { Assert-ReleaseDeterminismEvidence } catch { $Rejected=$true }
    Check $Rejected 'Release rejects changed A/B receipt'
    Add-Content -LiteralPath (Join-Path $Root 'pass/parallel/Automation.log') -Value 'tamper'
    $Rejected=$false;try { $null=Assert-SeinDeterminismEvidence $GoodFile } catch { $Rejected=$true }
    Check $Rejected 'Evidence rejects modified retained log'

    foreach ($FunctionName in @('Write-ReleaseGateReceipt', 'Invoke-ReleaseGateStep')) {
        $Function=$ReleaseAst.Find({param($Node) $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $Node.Name -eq $FunctionName},$true)
        . ([scriptblock]::Create($Function.Extent.Text))
    }
    $ReceiptRoot=$Root;$ReceiptPath=Join-Path $Root 'fixture-release.json'
    $Steps=[System.Collections.Generic.List[object]]::new()
    $Receipt=@{status='Running';steps=$Steps}
    Invoke-ReleaseGateStep 'fixture success' { 1..200 | ForEach-Object { "fixture output $_" } }
    Check ($Steps[0].status -eq 'Passed' -and @(Get-Content $Steps[0].logPath).Count -eq 200) 'Release stage retains full output in log'
    $Rejected=$false
    try { Invoke-ReleaseGateStep 'fixture failure' { throw 'expected fixture failure' } } catch { $Rejected=$true }
    Check ($Rejected -and $Steps[1].status -eq 'Failed' -and $Receipt.status -eq 'Failed') 'Quiet release stage propagates failure'

    $Validate=Join-Path $Fixture 'Scripts/Validate.ps1'
    $env:SEIN_VALIDATION_FIXTURE_MODE='pass'
    $null=Invoke-Fixture $Validate @('-Preset','Documentation') $true
    $null=Invoke-Fixture $Validate @('-Preset','Focused') $false
    $null=Invoke-Fixture $Validate @('-Preset','Full','-Profile','All') $false
    $null=Invoke-Fixture $Validate @('-Preset','Focused','-Suite','SeinARTS.Unit.Core','-EngineRoot',$Engine) $true
    foreach($Mode in @('runner-failure','missing-result','failed-receipt')) {
        $env:SEIN_VALIDATION_FIXTURE_MODE=$Mode
        $null=Invoke-Fixture $Validate @('-Preset','Focused','-Suite','SeinARTS.Unit.Core','-EngineRoot',$Engine) $false
    }
    $env:SEIN_VALIDATION_FIXTURE_MODE='pass'
    $null=Invoke-Fixture $Validate @('-Preset','Simulation','-Profile','Framework','-EngineRoot',$Engine) $true
    $Simulation=@(Get-ChildItem (Join-Path $Fixture 'Saved/Validation') -Filter validation-result.json -Recurse |
        ForEach-Object { Get-Content -Raw $_.FullName | ConvertFrom-Json } |
        Where-Object { $_.preset -eq 'Simulation' -and $_.status -eq 'Passed' })
    $SimulationAB=Get-Content -Raw $Simulation[0].steps[-1].evidence | ConvertFrom-Json
    $SimulationABRoot=Split-Path $Simulation[0].steps[-1].evidence
    $SimulationSerial=Get-Content -Raw (Join-Path $SimulationABRoot 'serial/attempt.json') | ConvertFrom-Json
    Check $SimulationSerial.skipBuild 'Framework simulation reuses its profile for A/B'
    $null=Invoke-Fixture $Validate @('-Preset','Full','-EngineRoot',$Engine) $true
    $Full=@(Get-ChildItem (Join-Path $Fixture 'Saved/Validation') -Filter validation-result.json -Recurse |
        ForEach-Object { Get-Content -Raw $_.FullName | ConvertFrom-Json } |
        Where-Object { $_.preset -eq 'Full' -and $_.status -eq 'Passed' })
    Check ($Full.Count -eq 1 -and $Full[0].steps.Count -eq 15) 'Full preserves 12 suite runs, A/B, Shipping, whitespace'
    $Receipts=@($Full[0].steps | Where-Object { $_.name -match '^(All|Framework) SeinARTS\.' } |
        ForEach-Object { Get-Content -Raw $_.evidence | ConvertFrom-Json })
    Check (@($Receipts | Where-Object { -not $_.skipBuild }).Count -eq 2) 'Full builds once per profile'
    [ordered]@{status='Passed';shell=$Shell;checks=@($Checks);fixtureRoot=$Fixture} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'self-test-result.json')
    Write-Host "[ValidationSelfTest] Passed $($Checks.Count) checks. Receipt: $Root/self-test-result.json"
}
finally { $env:SEIN_VALIDATION_FIXTURE_MODE=$OldMode }
