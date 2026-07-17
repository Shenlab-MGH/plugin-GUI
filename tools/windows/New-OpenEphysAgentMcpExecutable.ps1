[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputDirectory,

    [string] $UvExecutable
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$project = Join-Path $repoRoot 'integrations\mcp'
$lockFile = Join-Path $project 'uv.lock'
$launcher = Join-Path $project 'launcher.py'
if (-not (Test-Path -LiteralPath $lockFile -PathType Leaf)) {
    throw 'uv.lock is required for the MCP executable build.'
}
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw 'MCP executable launcher is missing.'
}

if (-not $UvExecutable) {
    $command = Get-Command uv -ErrorAction SilentlyContinue
    if ($command) {
        $UvExecutable = $command.Source
    }
}
if (-not $UvExecutable -or
    -not (Test-Path -LiteralPath $UvExecutable -PathType Leaf)) {
    throw 'uv executable was not found. Pass -UvExecutable explicitly.'
}

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$workRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'open-ephys-agent-mcp-build-' + [guid]::NewGuid().ToString('N')
)
New-Item -ItemType Directory -Path $workRoot | Out-Null

try {
    & $UvExecutable run --frozen --project $project pyinstaller `
        --noconfirm `
        --clean `
        --onefile `
        --name open-ephys-agent-mcp `
        --distpath $output `
        --workpath (Join-Path $workRoot 'work') `
        --specpath (Join-Path $workRoot 'spec') `
        $launcher
    if ($LASTEXITCODE -ne 0) {
        throw "pyinstaller failed with exit code $LASTEXITCODE"
    }
}
finally {
    $resolvedWork = [System.IO.Path]::GetFullPath($workRoot)
    $resolvedTemp = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath()
    )
    if ($resolvedWork.StartsWith(
        $resolvedTemp,
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}

$artifact = Join-Path $output 'open-ephys-agent-mcp.exe'
if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
    throw 'open-ephys-agent-mcp.exe was not produced.'
}

[ordered]@{
    executable = $artifact
    sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
    bytes = (Get-Item -LiteralPath $artifact).Length
    lockfile = $lockFile
} | ConvertTo-Json
