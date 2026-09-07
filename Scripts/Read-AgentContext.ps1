#Requires -Version 5.1
<#
.SYNOPSIS
  Read a bounded part of repository agent context without loading unrelated history.
.EXAMPLE
  .\Scripts\Read-AgentContext.ps1 -File OPEN_RISKS.md
.EXAMPLE
  .\Scripts\Read-AgentContext.ps1 -File OPEN_RISKS.md -Section 'Explicit product decisions still required'
.EXAMPLE
  .\Scripts\Read-AgentContext.ps1 -File history/2026-09-07-project-state.md -IncludeHistory -Find 'reconnect'
#>
[CmdletBinding()]
param(
    [string] $File = 'README.md',
    [string] $Section,
    [string] $Find,
    [ValidateRange(0, 1000000)] [int] $StartLine = 0,
    [ValidateRange(1, 120)] [int] $MaxLines = 60,
    [switch] $IncludeHistory
)
$ErrorActionPreference = 'Stop'
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../.agents'))
$Path = [System.IO.Path]::GetFullPath((Join-Path $Root $File))
if (-not $Path.StartsWith($Root + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase) -or
    [System.IO.Path]::GetExtension($Path) -ine '.md') {
    throw '-File must name a Markdown file within the repository .agents directory.'
}
$Relative = $Path.Substring($Root.Length + 1).Replace('\', '/')
if ($Relative.StartsWith('history/', [System.StringComparison]::OrdinalIgnoreCase) -and -not $IncludeHistory) {
    throw 'Historical evidence requires -IncludeHistory and should answer a specific question.'
}
if (([int][bool]$Section + [int][bool]$Find + [int]($StartLine -gt 0)) -gt 1) {
    throw 'Choose one of -Section, -Find, or -StartLine.'
}
$Lines = @(Get-Content -LiteralPath $Path -Encoding UTF8)
$Headings = @(for ($i = 0; $i -lt $Lines.Count; ++$i) {
    if ($Lines[$i] -match '^(#{1,6})\s+(.+?)\s*#*\s*$') {
        [pscustomobject]@{ Index = $i; Depth = $Matches[1].Length; Title = $Matches[2] }
    }
})
$Indexes = @()
if ($Section) {
    $Selected = @($Headings | Where-Object { $_.Title -ieq $Section })
    if ($Selected.Count -ne 1) { throw "Expected one heading named '$Section'; found $($Selected.Count). List headings or use -StartLine." }
    $Heading = $Selected[0]
    $Next = $Headings | Where-Object { $_.Index -gt $Heading.Index -and $_.Depth -le $Heading.Depth } |
        Select-Object -First 1
    $End = if ($null -ne $Next) { $Next.Index } else { $Lines.Count }
    $Indexes = @($Heading.Index..($End - 1))
}
elseif ($Find) {
    $Indexes = @(for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i].IndexOf($Find, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { $i }
    })
}
elseif ($StartLine -gt 0) {
    if ($StartLine -gt $Lines.Count) { throw "Start line exceeds file length ($($Lines.Count))." }
    $Indexes = @(($StartLine - 1)..($Lines.Count - 1))
}
elseif ($Relative -ieq 'README.md') { $Indexes = @(0..($Lines.Count - 1)) }
else { $Indexes = @($Headings | ForEach-Object { $_.Index }) }
Write-Output ".agents/$Relative ($($Lines.Count) lines; $($Indexes.Count) selected)"
foreach ($Index in @($Indexes | Select-Object -First $MaxLines)) {
    $Text = $Lines[$Index]
    if ($Text.Length -gt 500) { $Text = $Text.Substring(0, 500) + ' [line clipped]' }
    Write-Output ('{0}: {1}' -f ($Index + 1), $Text)
}
if ($Indexes.Count -gt $MaxLines) {
    Write-Output "[Excerpt limited to $MaxLines lines; narrow the section/search or use -StartLine.]"
}
