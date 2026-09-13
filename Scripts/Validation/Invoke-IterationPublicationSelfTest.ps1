#Requires -Version 7.0
# Exercise publication decisions with fixture Git, packaging, and hosting commands only.
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Root = Join-Path $RepoRoot ('Saved/IterationPublicationSelfTest/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $Root 'Scripts/Release') -Force | Out-Null
foreach ($Name in @('Publish-Iteration.ps1','SeinReleaseVersion.ps1')) {
    Copy-Item (Join-Path $RepoRoot "Scripts/Release/$Name") (Join-Path $Root "Scripts/Release/$Name")
}
'Fixture release notes.' | Set-Content (Join-Path $Root 'notes.md')
@'
param([string] $Version, [switch] $PackageOnly)
$Root = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $Root '.dist'
New-Item -ItemType Directory -Path $Dist -Force | Out-Null
$Artifacts = @(foreach ($Index in 1..6) {
    $Name="Plugin$Index.zip"
    'fixture bytes' | Set-Content (Join-Path $Dist $Name)
    @{file=$Name;sha256=(Get-FileHash (Join-Path $Dist $Name)).Hash}
})
@{sourceDirty=$false;sourceCommit=('a'*40);version=$Version;artifacts=$Artifacts} |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Dist 'release-manifest.json')
$global:LASTEXITCODE=0
'@ | Set-Content (Join-Path $Root 'Scripts/PackagePlugins.ps1')
@'
param([string] $Mode)
$ErrorActionPreference='Stop'
$global:SeinPublicationFixtureRoot=$PSScriptRoot
$global:SeinPublicationFixtureMode=$Mode
function git {
    $global:LASTEXITCODE=0
    switch ($args[2]) {
        'rev-parse' { 'a'*40 }
        'tag' { 'v0.2.0' }
        'remote' { 'https://example.invalid/fixture.git' }
        'status' { if ($global:SeinPublicationFixtureMode -eq 'dirty') { ' M owned.cpp' } }
    }
}
function gh {
    $global:LASTEXITCODE=0
    Add-Content (Join-Path $global:SeinPublicationFixtureRoot "$global:SeinPublicationFixtureMode.calls") ($args -join ' ')
    switch ($args[1]) {
        'create' { if ($global:SeinPublicationFixtureMode -eq 'upload-failure') { $global:LASTEXITCODE=9 } }
        'view' {
            @{isDraft=$true;isPrerelease=($global:SeinPublicationFixtureMode -ne 'wrong-classification');targetCommitish=('a'*40);assets=@(1..6)} | ConvertTo-Json
        }
        'download' {
            $Destination=$args[([array]::IndexOf($args,'--dir')+1)]
            Copy-Item (Join-Path $global:SeinPublicationFixtureRoot '.dist/*.zip') $Destination
            if ($global:SeinPublicationFixtureMode -eq 'hash-mismatch') { 'changed bytes' | Set-Content (Join-Path $Destination 'Plugin1.zip') }
        }
    }
}
$Parameters=@{NotesFile=(Join-Path $PSScriptRoot 'notes.md')}
if ($Mode -eq 'wrong-line') { $Parameters.Version='0.3.0' }
& (Join-Path $PSScriptRoot 'Scripts/Release/Publish-Iteration.ps1') @Parameters
'@ | Set-Content (Join-Path $Root 'fixture.ps1')
$Results=@()
foreach ($Mode in @('pass','dirty','wrong-line','upload-failure','wrong-classification','hash-mismatch')) {
    $Log=Join-Path $Root "$Mode.log"
    & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $Root 'fixture.ps1') -Mode $Mode *> $Log
    $Code=$LASTEXITCODE
    $CallsPath=Join-Path $Root "$Mode.calls"
    $Calls=if (Test-Path $CallsPath) { @(Get-Content $CallsPath) } else { @() }
    $Published=@($Calls | Where-Object { $_ -like 'release edit *' }).Count -eq 1
    if (($Code -eq 0) -ne ($Mode -eq 'pass') -or $Published -ne ($Mode -eq 'pass')) {
        throw "Unexpected publication outcome for $Mode. Log: $Log"
    }
    if ($Mode -eq 'pass' -and -not ($Calls -match '--draft=false --prerelease')) {
        throw 'Successful publication must remain a prerelease.'
    }
    $Results+=@{mode=$Mode;exitCode=$Code;published=$Published}
}
@{status='Passed';cases=$Results} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Root 'result.json')
Write-Host "[IterationPublicationSelfTest] Passed six publication scenarios. Receipt: $Root/result.json"
exit 0
