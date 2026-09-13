# Shared documentation selection for packages and release evidence.
function Get-SeinReleaseDocumentationFiles([string] $RepositoryRoot)
{
    $RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $DocsRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'Docs'))
    $Prefix = $DocsRoot + [IO.Path]::DirectorySeparatorChar
    # The working directory may contain Astro output and installed npm packages.
    # Only version-controlled documentation belongs to the source release.
    $Tracked = @(git -c core.quotepath=false -C $RepositoryRoot ls-files -- Docs)
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate tracked release documentation.' }
    $Files = @($Tracked | ForEach-Object {
        $Path = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $_))
        if (-not $Path.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Tracked documentation path escapes Docs: '$_'."
        }
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Tracked documentation file is missing: '$Path'."
        }
        Get-Item -LiteralPath $Path
    })
    return $Files
}

function Copy-SeinReleaseDocumentation([string] $RepositoryRoot, [string] $Destination)
{
    $DocsRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'Docs'))
    $Files = @(Get-SeinReleaseDocumentationFiles $RepositoryRoot)
    foreach ($File in $Files) {
        $RelativePath = $File.FullName.Substring($DocsRoot.Length + 1)
        $Target = Join-Path $Destination $RelativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
        Copy-Item -LiteralPath $File.FullName -Destination $Target -Force
    }
}
