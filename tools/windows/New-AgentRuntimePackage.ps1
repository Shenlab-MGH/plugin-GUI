[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ReleaseDirectory,

    [Parameter(Mandatory)]
    [string] $Destination
)

$ErrorActionPreference = 'Stop'

$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$executable = Join-Path $release 'open-ephys.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Release directory has no open-ephys.exe: $release"
}

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
Copy-Item -Path (Join-Path $release '*') `
    -Destination $destinationPath `
    -Recurse `
    -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-IsolatedAgentRuntime.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Test-IsolatedAgentRuntime.ps1'
) -Destination $destinationPath -Force

$files = @(
    Get-ChildItem -LiteralPath $destinationPath -File -Recurse |
        Sort-Object FullName |
        ForEach-Object {
            [ordered]@{
                path = [System.IO.Path]::GetRelativePath(
                    $destinationPath,
                    $_.FullName
                )
                bytes = $_.Length
                sha256 = (
                    Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
                ).Hash
            }
        }
)

$manifest = [ordered]@{
    schema_version = 1
    product = 'open-ephys-agent'
    version = '0.0.1'
    created_at = (Get-Date).ToUniversalTime().ToString('o')
    source_commit = (git rev-parse HEAD).Trim()
    source_dirty = [bool](git status --porcelain)
    release_directory = $release
    destination = $destinationPath
    files = $files
}

$manifestPath = Join-Path $destinationPath 'RUN-MANIFEST.json'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

$manifest | ConvertTo-Json -Depth 6
