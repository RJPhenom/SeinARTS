#Requires -Version 7.0
<#
.SYNOPSIS
  Package and publish a testing prerelease with reviewed release notes.
.DESCRIPTION
  Uses the active release line and advances its update number, or its fourth
  hotfix digit with -Hotfix. Only RJ changes the first two version digits.
  Requires a clean pushed main commit. Production milestone qualification is
  owned by Invoke-ReleaseGate.ps1; this command always publishes a prerelease.
#>
[CmdletBinding()]
param(
    [string] $Version,
    [switch] $Hotfix,
    [Parameter(Mandatory)] [string] $NotesFile
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $PSScriptRoot 'SeinReleaseVersion.ps1')
if (-not (Test-Path -LiteralPath $NotesFile -PathType Leaf) -or
    [string]::IsNullOrWhiteSpace((Get-Content -LiteralPath $NotesFile -Raw))) {
    throw 'Provide a nonempty file with detailed release notes.'
}
git -C $RepoRoot fetch origin --tags --quiet
if ($LASTEXITCODE -ne 0) { throw 'Could not refresh main and release tags.' }
$Head = (git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not resolve HEAD.' }
$Main = (git -C $RepoRoot rev-parse refs/remotes/origin/main).Trim()
if ($LASTEXITCODE -ne 0 -or $Head -cne $Main) { throw 'HEAD must equal pushed origin/main.' }
$Dirty = @(git -C $RepoRoot status --porcelain=v1)
if ($LASTEXITCODE -ne 0 -or $Dirty.Count) { throw 'Publication requires a clean working tree.' }
$NextVersion = Get-SeinNextReleaseVersion $RepoRoot -Hotfix:$Hotfix
if (-not $Version) { $Version = $NextVersion }
if ($Version -cne $NextVersion) {
    throw "Expected next version $NextVersion. Only RJ changes the configured release line."
}
$Dist = Join-Path $RepoRoot '.dist'
& (Join-Path $RepoRoot 'Scripts/PackagePlugins.ps1') -Version $Version -PackageOnly
if ($LASTEXITCODE -ne 0) { throw 'Plugin packaging failed.' }
$Manifest = Get-Content -Raw -LiteralPath (Join-Path $Dist 'release-manifest.json') | ConvertFrom-Json
if ($Manifest.sourceDirty -or $Manifest.sourceCommit -cne $Head -or
    $Manifest.version -cne $Version -or $Manifest.artifacts.Count -ne 6) {
    throw 'Packaged release provenance does not match the clean commit.'
}
$Zips = @($Manifest.artifacts | ForEach-Object { Join-Path $Dist $_.file })
foreach ($Artifact in $Manifest.artifacts) {
    if ((Get-FileHash -LiteralPath (Join-Path $Dist $Artifact.file)).Hash -ine $Artifact.sha256) {
        throw "Packaged artifact changed: $($Artifact.file)"
    }
}
$Repository = (git -C $RepoRoot remote get-url origin).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not resolve publication repository.' }
gh release create "v$Version" @Zips -R $Repository --target $Head --prerelease --draft `
    --title "SeinARTS v$Version" --notes-file $NotesFile
if ($LASTEXITCODE -ne 0) { throw 'Draft release creation failed.' }
$Remote = gh release view "v$Version" -R $Repository --json assets,isDraft,isPrerelease,targetCommitish | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or -not $Remote.isDraft -or -not $Remote.isPrerelease -or
    $Remote.targetCommitish -cne $Head -or $Remote.assets.Count -ne 6) {
    throw 'Draft metadata mismatch; publication stopped.'
}
$VerificationRoot = Join-Path $RepoRoot ('Saved/ReleaseUploads/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $VerificationRoot | Out-Null
gh release download "v$Version" -R $Repository --dir $VerificationRoot --pattern '*.zip'
if ($LASTEXITCODE -ne 0) { throw 'Could not download draft artifacts for verification.' }
foreach ($Artifact in $Manifest.artifacts) {
    $Downloaded = Join-Path $VerificationRoot $Artifact.file
    if ((Get-FileHash -LiteralPath $Downloaded).Hash -ine $Artifact.sha256) {
        throw "Uploaded artifact mismatch: $($Artifact.file). Draft remains unpublished."
    }
}
gh release edit "v$Version" -R $Repository --draft=false --prerelease
if ($LASTEXITCODE -ne 0) { throw 'Could not publish verified prerelease.' }
Write-Host "[Publish-Iteration] Published v$Version from $Head."
