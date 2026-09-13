# Only RJ changes the first two digits. Updates advance the third; bug hotfixes advance the fourth.
function Resolve-SeinNextReleaseVersion {
    param([string[]] $Tags, [string] $ReleaseLine = '0.2', [switch] $Hotfix)
    if ($ReleaseLine -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
        throw "Invalid release line '$ReleaseLine'. Expected MAJOR.MINOR."
    }
    $Floor = [version] "$ReleaseLine.0"
    $Patch = -1
    $Revision = 0
    foreach ($Tag in $Tags) {
        if ($Tag -notmatch '^v((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:\.(0|[1-9]\d*))?)(?:[-+].+)?$') { continue }
        $TaggedVersion = [version] $Matches[1]
        if ($TaggedVersion.Major -gt $Floor.Major -or
            ($TaggedVersion.Major -eq $Floor.Major -and $TaggedVersion.Minor -gt $Floor.Minor)) {
            throw "Tag '$Tag' is newer than release line $ReleaseLine. Select the intended release line explicitly."
        }
        if ($TaggedVersion.Major -eq $Floor.Major -and $TaggedVersion.Minor -eq $Floor.Minor) {
            if ($TaggedVersion.Build -gt $Patch) {
                $Patch = $TaggedVersion.Build
                $Revision = [Math]::Max(0, $TaggedVersion.Revision)
            }
            elseif ($TaggedVersion.Build -eq $Patch) {
                $Revision = [Math]::Max($Revision, $TaggedVersion.Revision)
            }
        }
    }
    if ($Hotfix) {
        if ($Patch -lt 0) { throw 'A crash or bug hotfix requires an existing release in this line.' }
        return "$ReleaseLine.$Patch.$([long]$Revision + 1)"
    }
    return "$ReleaseLine.$([long]$Patch + 1)"
}

function Get-SeinNextReleaseVersion {
    param([string] $RepositoryRoot, [switch] $Hotfix)
    $Tags = @(git -C $RepositoryRoot tag --list)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read release tags.' }
    return Resolve-SeinNextReleaseVersion -Tags $Tags -Hotfix:$Hotfix
}
