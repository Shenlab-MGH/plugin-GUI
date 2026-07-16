[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

& (Join-Path $PSScriptRoot 'Invoke-AgentCoreTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Agent core tests failed with exit code $LASTEXITCODE"
}

$contracts = @(
    'Tests\AgentContracts\test_transport_accessibility_source.py',
    'Tests\AgentContracts\test_transport_command_routing_source.py',
    'Tests\AgentContracts\test_transport_coordinator_integration_source.py',
    'Tests\AgentContracts\test_transport_mailbox_integration_source.py'
)

foreach ($contract in $contracts) {
    & python (Join-Path $repoRoot $contract)
    if ($LASTEXITCODE -ne 0) {
        throw "$contract failed with exit code $LASTEXITCODE"
    }
}

& git -C $repoRoot diff --check
if ($LASTEXITCODE -ne 0) {
    throw "git diff --check failed with exit code $LASTEXITCODE"
}

Write-Host 'PASS agent fork local checks'
