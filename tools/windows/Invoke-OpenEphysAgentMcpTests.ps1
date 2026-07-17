[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$project = Join-Path $repoRoot 'integrations\mcp'
$skillTests = Join-Path $repoRoot (
    'integrations\skills\open-ephys-operator\tests'
)

& py -3.12 -m uv run --frozen --project $project pytest `
    (Join-Path $project 'tests') `
    $skillTests `
    -q
if ($LASTEXITCODE -ne 0) {
    throw "MCP and Skill tests failed with exit code $LASTEXITCODE"
}

Write-Host 'PASS Open Ephys MCP and Skill tests'
