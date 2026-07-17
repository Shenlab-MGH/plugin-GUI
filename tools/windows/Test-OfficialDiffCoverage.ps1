[CmdletBinding()]
param(
    [string] $Repository = (
        Resolve-Path (Join-Path $PSScriptRoot '..\..')
    ).Path
)

$ErrorActionPreference = 'Stop'
$officialCommit = 'c91afebcfb0678a667fb93f6312ed33c56ec640f'
$repo = (Resolve-Path -LiteralPath $Repository).Path
$registryPath = Join-Path $repo 'OFFICIAL-DIFF.md'
if (-not (Test-Path -LiteralPath $registryPath -PathType Leaf)) {
    throw 'OFFICIAL_DIFF_MISSING: OFFICIAL-DIFF.md is required.'
}

& git -C $repo cat-file -e "$officialCommit`^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "OFFICIAL_BASELINE_MISSING: $officialCommit"
}

$changedTracked = @(
    & git -C $repo diff --name-only $officialCommit -- `
        Source Plugins CMakeLists.txt JuceLibraryCode
)
if ($LASTEXITCODE -ne 0) {
    throw 'OFFICIAL_DIFF_FAILED: git diff failed.'
}
$untracked = @(
    & git -C $repo ls-files --others --exclude-standard -- `
        Source Plugins CMakeLists.txt JuceLibraryCode
)
if ($LASTEXITCODE -ne 0) {
    throw 'OFFICIAL_DIFF_FAILED: git ls-files failed.'
}
$changed = @($changedTracked + $untracked | Sort-Object -Unique)

$registered = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal
)
foreach ($line in Get-Content -LiteralPath $registryPath) {
    if ($line -match '^\|\s*([^|]+?)\s*\|\s*(ADDED|MODIFIED)\s*\|') {
        [void]$registered.Add($Matches[1].Trim().Replace('\', '/'))
    }
}

$undisclosed = @(
    $changed |
        Where-Object { -not $registered.Contains($_.Replace('\', '/')) } |
        Sort-Object
)
if ($undisclosed.Count -gt 0) {
    throw "UNDISCLOSED_CHANGE:$($undisclosed[0])"
}

[ordered]@{
    pass = $true
    official_commit = $officialCommit
    changed_production_files = $changed.Count
    registered_changed_files = $changed.Count
} | ConvertTo-Json
