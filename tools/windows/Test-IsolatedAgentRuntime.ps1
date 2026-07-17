[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [int] $ProcessId,

    [Parameter(Mandatory)]
    [string] $StateDirectory,

    [ValidateRange(1024, 65535)]
    [int] $AgentPort = 38498
)

$ErrorActionPreference = 'Stop'
$process = Get-Process -Id $ProcessId -ErrorAction Stop
$commandLine = (
    Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId"
).CommandLine
$listeners = @(
    Get-NetTCPConnection -State Listen -OwningProcess $ProcessId `
        -ErrorAction SilentlyContinue |
        Select-Object LocalAddress, LocalPort, OwningProcess, State
)
$agentListeners = @(
    $listeners | Where-Object LocalPort -eq $AgentPort
)
$nativeListeners = @(
    $listeners | Where-Object LocalPort -eq 37497
)
$expectedState = [System.IO.Path]::GetFullPath($StateDirectory)

$checks = [ordered]@{
    process_is_open_ephys = ($process.ProcessName -eq 'open-ephys')
    has_state_directory_argument = (
        $commandLine -like "*--state-dir*$expectedState*"
    )
    native_http_locked_off_argument = (
        $commandLine -like '*--no-http*'
    )
    user_plugins_locked_off_argument = (
        $commandLine -like '*--no-user-plugins*'
    )
    agent_port_argument = (
        $commandLine -like "*--agent-port*$AgentPort*"
    )
    agent_listener_is_loopback = (
        $agentListeners.Count -eq 1 -and
        $agentListeners[0].LocalAddress -in @('127.0.0.1', '::1')
    )
    native_37497_not_owned = ($nativeListeners.Count -eq 0)
}

$result = [ordered]@{
    pass = -not ($checks.Values -contains $false)
    process_id = $ProcessId
    command_line = $commandLine
    listeners = $listeners
    checks = $checks
}
$result | ConvertTo-Json -Depth 6
if (-not $result.pass) {
    throw 'Isolated runtime verification failed.'
}
