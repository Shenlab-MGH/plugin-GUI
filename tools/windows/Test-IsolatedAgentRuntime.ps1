[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [int] $ProcessId,

    [Parameter(Mandatory)]
    [string] $StateDirectory,

    [switch] $ExpectInteractiveUia,

    [string] $ExpectedExecutable,

    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string] $ExpectedExecutableSha256,

    [ValidateRange(1024, 65535)]
    [int] $AgentPort = 38498
)

$ErrorActionPreference = 'Stop'
$process = Get-Process -Id $ProcessId -ErrorAction Stop
$expectedExecutablePath = $null
if ($ExpectedExecutable) {
    if (-not [System.IO.Path]::IsPathFullyQualified($ExpectedExecutable)) {
        throw 'ExpectedExecutable must be an absolute path.'
    }
    $expectedExecutablePath = [System.IO.Path]::GetFullPath(
        $ExpectedExecutable
    )
    if (-not (Test-Path -LiteralPath $expectedExecutablePath -PathType Leaf)) {
        throw "ExpectedExecutable does not exist: $expectedExecutablePath"
    }
}
elseif ($ExpectInteractiveUia) {
    throw 'Interactive UIA verification requires ExpectedExecutable.'
}
if ($expectedExecutablePath -and -not $ExpectedExecutableSha256) {
    throw 'ExpectedExecutable requires ExpectedExecutableSha256.'
}
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
$hasAgentUiaReadOnlyArgument = (
    $commandLine -match '(?:^|\s)--agent-uia-readonly(?:\s|$)'
)
$hasAgentUiaInteractiveArgument = (
    $commandLine -match '(?:^|\s)--agent-uia-interactive(?:\s|$)'
)
$processExecutablePath = [System.IO.Path]::GetFullPath($process.Path)
$expectedExecutableSha256 = if ($ExpectedExecutableSha256) {
    $ExpectedExecutableSha256.ToUpperInvariant()
} else { $null }
$processExecutableSha256 = (
    Get-FileHash -LiteralPath $processExecutablePath -Algorithm SHA256
).Hash

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
    uia_mode_is_exactly_one = (
        $hasAgentUiaReadOnlyArgument -xor
        $hasAgentUiaInteractiveArgument
    )
    agent_uia_mode_argument_matches = if ($ExpectInteractiveUia) {
        $hasAgentUiaInteractiveArgument -and
        -not $hasAgentUiaReadOnlyArgument
    }
    else {
        $hasAgentUiaReadOnlyArgument -and
        -not $hasAgentUiaInteractiveArgument
    }
    interactive_uia_mode_matches = if ($ExpectInteractiveUia) {
        $hasAgentUiaInteractiveArgument
    }
    else {
        $hasAgentUiaReadOnlyArgument
    }
    executable_path_matches = if ($expectedExecutablePath) {
        $processExecutablePath.Equals(
            $expectedExecutablePath,
            [System.StringComparison]::OrdinalIgnoreCase
        )
    }
    else {
        $true
    }
    executable_sha256_matches = if ($expectedExecutablePath) {
        $processExecutableSha256 -eq $expectedExecutableSha256
    }
    else {
        $true
    }
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
    expected_executable = $expectedExecutablePath
    process_executable_sha256 = $processExecutableSha256
    listeners = $listeners
    checks = $checks
}
$result | ConvertTo-Json -Depth 6
if (-not $result.pass) {
    throw 'Isolated runtime verification failed.'
}
