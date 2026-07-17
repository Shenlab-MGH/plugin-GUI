[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

& pwsh -NoProfile -File (
    Join-Path $PSScriptRoot 'Test-OfficialDiffCoverage.ps1'
) -Repository $repoRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Official difference coverage check failed.'
}

& (Join-Path $PSScriptRoot 'Invoke-AgentCoreTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Agent core tests failed with exit code $LASTEXITCODE"
}

& (Join-Path $PSScriptRoot 'Invoke-AgentGatewayTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Agent Gateway tests failed with exit code $LASTEXITCODE"
}

& (Join-Path $PSScriptRoot 'Invoke-OpenEphysAgentMcpTests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Open Ephys MCP tests failed with exit code $LASTEXITCODE"
}

$contracts = @(
    'Tests\AgentContracts\test_transport_accessibility_source.py',
    'Tests\AgentContracts\test_windows_automation_id_source.py',
    'Tests\AgentContracts\test_windows_uia_verifier_source.py',
    'Tests\AgentContracts\test_transport_command_routing_source.py',
    'Tests\AgentContracts\test_transport_coordinator_integration_source.py',
    'Tests\AgentContracts\test_transport_mailbox_integration_source.py',
    'Tests\AgentContracts\test_agent_loopback_server_source.py',
    'Tests\AgentContracts\test_official_baseline_verifier_source.py',
    'Tests\AgentContracts\test_official_build_wrapper_source.py',
    'Tests\AgentContracts\test_build_tools_installer_source.py',
    'Tests\AgentContracts\test_runtime_isolation_wiring_source.py',
    'Tests\AgentContracts\test_isolated_runtime_launcher_source.py',
    'Tests\AgentContracts\test_mcp_skill_packaging_source.py',
    'Tests\AgentContracts\test_control_panel_experiment_adapter_source.py'
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
