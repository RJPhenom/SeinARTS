# The release line advances by an explicit product decision. Published tags reserve patches.
function Resolve-SeinNextReleaseVersion {
    param([string[]] $Tags, [string] $ReleaseLine = '0.3')
    if ($ReleaseLine -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
        throw "Invalid release line '$ReleaseLine'. Expected MAJOR.MINOR."
    }
    $Floor = [version] "$ReleaseLine.0"
    $Patch = -1
    foreach ($Tag in $Tags) {
        if ($Tag -notmatch '^v((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))(?:[-+].+)?$') { continue }
        $TaggedVersion = [version] $Matches[1]
        if ($TaggedVersion.Major -gt $Floor.Major -or
            ($TaggedVersion.Major -eq $Floor.Major -and $TaggedVersion.Minor -gt $Floor.Minor)) {
            throw "Tag '$Tag' is newer than release line $ReleaseLine. Select the intended release line explicitly."
        }
        if ($TaggedVersion.Major -eq $Floor.Major -and $TaggedVersion.Minor -eq $Floor.Minor) {
            $Patch = [Math]::Max($Patch, $TaggedVersion.Build)
        }
    }
    return "$ReleaseLine.$([long]$Patch + 1)"
}

function Get-SeinNextReleaseVersion {
    param([string] $RepositoryRoot)
    $Tags = @(git -C $RepositoryRoot tag --list)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read release tags.' }
    return Resolve-SeinNextReleaseVersion -Tags $Tags
}
