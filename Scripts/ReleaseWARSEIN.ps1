#Requires -Version 7.0
<#
.SYNOPSIS
  Manually cut a WARSEIN update: package the production plugins FAB-style,
  publish them as a GitHub prerelease, and trigger WARSEIN's cloud install.

.DESCRIPTION
  The on-demand SeinARTS -> WARSEIN delivery (RJ ruling 2026-08-26; replaces
  the push-to-main hook sync):

    1. Scripts/PackagePlugins.ps1 -PackageOnly   (FAB-standard artifacts, .dist)
    2. gh release create v<Version> --prerelease (zips attached)
    3. gh workflow run seinarts-update.yml on RJPhenom/WARSEIN, which installs
       the release into WARSEIN's Plugins/ and commits — entirely in the cloud.

  Afterward, pull WARSEIN on whatever machine you're at and build (or run
  Scripts/SyncWARSEIN.ps1 for a fully local package+install+build instead).

  Prerelease marks these as iteration drops; qualified milestone releases
  still go through Scripts/Release/Invoke-ReleaseGate.ps1.

  HEAD must equal origin/main: packaging uses the local working tree, so a
  stale or diverged checkout would silently release the wrong content —
  `git pull` first if cloud sessions have advanced main.

.EXAMPLE
  .\Scripts\ReleaseWARSEIN.ps1
  # auto-versioned (0.0.<commit count>-<sha>) prerelease + WARSEIN install

.EXAMPLE
  .\Scripts\ReleaseWARSEIN.ps1 -Version 0.3.0
#>
param(
    [string] $Version,
    [switch] $SkipInstallTrigger   # publish only; don't kick WARSEIN's workflow
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $ProjectRoot '.dist'

git -C $ProjectRoot fetch origin --quiet
$Head = (git -C $ProjectRoot rev-parse HEAD).Trim()
$OriginMain = (git -C $ProjectRoot rev-parse refs/remotes/origin/main).Trim()
if ($Head -cne $OriginMain) {
    throw "HEAD ($Head) is not origin/main ($OriginMain). Pull (or push) main first so the release matches the repo's tip."
}
$Dirty = @(git -C $ProjectRoot status --porcelain=v1)
if ($Dirty.Count -ne 0) {
    throw 'Working tree is not clean; a release must package exactly the pushed commit.'
}

if (-not $Version) {
    $Short = (git -C $ProjectRoot rev-parse --short HEAD).Trim()
    $Version = "0.0.$((git -C $ProjectRoot rev-list --count HEAD).Trim())-$Short"
}

Write-Host "[ReleaseWARSEIN] packaging $Version from $Head" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'PackagePlugins.ps1') -Version $Version -PackageOnly
if ($LASTEXITCODE -ne 0) { throw "Packaging failed (exit $LASTEXITCODE)." }

$Zips = @(Get-ChildItem $Dist -Filter '*.zip' | ForEach-Object { $_.FullName })
if ($Zips.Count -eq 0) { throw "No packaged zips found in $Dist." }
$Repo = (git -C $ProjectRoot remote get-url origin).Trim()

Write-Host "[ReleaseWARSEIN] publishing prerelease v$Version ($($Zips.Count) packages)" -ForegroundColor Cyan
gh release create "v$Version" @Zips -R $Repo --target $Head --prerelease `
    --title "SeinARTS v$Version" `
    --notes "Iteration drop from ``$Head`` (UE 5.8, Win64, UAT BuildPlugin). Not gate-qualified; milestone releases go through Invoke-ReleaseGate.ps1."
if ($LASTEXITCODE -ne 0) { throw "gh release create failed (exit $LASTEXITCODE)." }

if ($SkipInstallTrigger) {
    Write-Host "[ReleaseWARSEIN] published v$Version (WARSEIN install trigger skipped)." -ForegroundColor Yellow
    exit 0
}
gh workflow run seinarts-update.yml -R RJPhenom/WARSEIN -f "tag=v$Version"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Release v$Version published, but triggering WARSEIN's workflow failed. Run it from WARSEIN's Actions tab with tag 'v$Version'."
    exit 1
}
Write-Host "[ReleaseWARSEIN] done - v$Version published; WARSEIN's repo will hold the install in ~1 min. Pull + build WARSEIN when ready." -ForegroundColor Green
