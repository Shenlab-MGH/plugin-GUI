[CmdletBinding()]
param(
    [int] $Port = 37498,
    [switch] $ArmSim
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:PYTHONPATH = Join-Path $repoRoot 'tools\agent_gateway'
$arguments = @(
    '-m',
    'open_ephys_agent_gateway',
    '--port',
    $Port
)
if ($ArmSim) {
    $arguments += '--arm-sim'
}

& python @arguments
