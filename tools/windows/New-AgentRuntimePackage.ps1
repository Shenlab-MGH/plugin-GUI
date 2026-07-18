[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ReleaseDirectory,

    [Parameter(Mandatory)]
    [string] $Destination,

    [Parameter(Mandatory)]
    [string] $McpExecutable
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$dirty = @(& git -C $repoRoot status --porcelain=v1)
if ($dirty.Count -ne 0) {
    throw 'SOURCE_DIRTY: commit or remove every worktree change before packaging.'
}
& pwsh -NoProfile -File (
    Join-Path $PSScriptRoot 'Test-OfficialDiffCoverage.ps1'
) -Repository $repoRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Runtime packaging blocked by undisclosed official differences.'
}

$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$executable = Join-Path $release 'open-ephys.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Release directory has no open-ephys.exe: $release"
}

$cmakeCache = Join-Path (Split-Path -Parent $release) 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
    throw "Release directory has no adjacent CMakeCache.txt: $release"
}
$cacheText = Get-Content -LiteralPath $cmakeCache -Raw
if ($cacheText -notmatch '(?m)^BUILD_TESTS:(?:BOOL|UNINITIALIZED)=OFF\r?$') {
    throw 'Runtime packages must come from a BUILD_TESTS=OFF build.'
}
if (Get-ChildItem -LiteralPath $release -Filter 'gui_testable_source.*') {
    throw 'Runtime package contains test-only gui_testable_source artifacts.'
}

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$mcpExecutablePath = [System.IO.Path]::GetFullPath($McpExecutable)
if (-not (Test-Path -LiteralPath $mcpExecutablePath -PathType Leaf) -or
    [System.IO.Path]::GetFileName($mcpExecutablePath) -ne
        'open-ephys-agent-mcp.exe') {
    throw 'McpExecutable must identify open-ephys-agent-mcp.exe.'
}
$mcpLock = Join-Path $repoRoot 'integrations\mcp\uv.lock'
$mcpProject = Join-Path $repoRoot 'integrations\mcp\pyproject.toml'
$skillSource = Join-Path $repoRoot (
    'integrations\skills\open-ephys-operator'
)
if (-not (Test-Path -LiteralPath $mcpLock -PathType Leaf) -or
    -not (Test-Path -LiteralPath $skillSource -PathType Container)) {
    throw 'Canonical MCP lockfile or Open Ephys Skill is missing.'
}
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
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Test-AgentAccessibility.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'New-FileReaderSimulationRun.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Invoke-FileReaderGuiQualification.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-OpenEphysAgentMcp.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'New-OpenEphysAgentMcpConfig.ps1'
) -Destination $destinationPath -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'OFFICIAL-DIFF.md') `
    -Destination $destinationPath -Force

$mcpDestination = Join-Path $destinationPath 'mcp'
New-Item -ItemType Directory -Path $mcpDestination | Out-Null
Copy-Item -LiteralPath $mcpExecutablePath `
    -Destination (Join-Path $mcpDestination 'open-ephys-agent-mcp.exe')
Copy-Item -LiteralPath $mcpLock -Destination $mcpDestination
Copy-Item -LiteralPath $mcpProject -Destination $mcpDestination

$skillDestination = Join-Path $destinationPath 'skills\open-ephys-operator'
New-Item -ItemType Directory -Path $skillDestination | Out-Null
Copy-Item -LiteralPath (Join-Path $skillSource 'SKILL.md') `
    -Destination $skillDestination
foreach ($directory in @('agents', 'references', 'scripts')) {
    Copy-Item -LiteralPath (Join-Path $skillSource $directory) `
        -Destination $skillDestination `
        -Recurse
}

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
