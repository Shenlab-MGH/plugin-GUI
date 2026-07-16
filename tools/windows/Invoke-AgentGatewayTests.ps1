[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:PYTHONPATH = Join-Path $repoRoot 'tools\agent_gateway'

& python -m compileall `
    -q `
    (Join-Path $repoRoot 'tools\agent_gateway')
if ($LASTEXITCODE -ne 0) {
    throw "Agent Gateway compile check failed with exit code $LASTEXITCODE"
}

& python -m unittest discover `
    -s (Join-Path $repoRoot 'Tests\AgentGateway') `
    -v

if ($LASTEXITCODE -ne 0) {
    throw "Agent Gateway tests failed with exit code $LASTEXITCODE"
}
