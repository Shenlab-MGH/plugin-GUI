[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$packageRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
$llvmPackage = Get-ChildItem -LiteralPath $packageRoot -Directory |
    Where-Object { $_.Name -match '^MartinStorsjo\.LLVM-MinGW\.UCRT_' } |
    Select-Object -First 1

if (-not $llvmPackage) {
    throw 'Portable LLVM-MinGW UCRT is not installed.'
}

$compiler = Get-ChildItem -LiteralPath $llvmPackage.FullName -Recurse -File `
    -Filter 'x86_64-w64-mingw32-clang++.exe' |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $compiler) {
    throw 'LLVM-MinGW C++ compiler was not found.'
}

$outputDirectory = Join-Path $repoRoot 'Build-agent-core'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

function Invoke-CompileAndRun {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string[]] $Sources,

        [string[]] $Libraries = @()
    )

    $executable = Join-Path $outputDirectory "$Name.exe"
    $sourcePaths = @($Sources | ForEach-Object { Join-Path $repoRoot $_ })

    & $compiler -std=c++17 -Wall -Wextra -Werror -static `
        @sourcePaths -o $executable @Libraries
    if ($LASTEXITCODE -ne 0) {
        throw "$Name compilation failed with exit code $LASTEXITCODE"
    }

    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-CompileAndRun -Name 'AgentCommandTests' -Sources @(
    'Tests\AgentCore\AgentCommandTests.cpp'
)

Invoke-CompileAndRun -Name 'RuntimeOptionsTests' -Sources @(
    'Tests\AgentCore\RuntimeOptionsTests.cpp',
    'Source\Agent\RuntimeOptions.cpp'
)

Invoke-CompileAndRun -Name 'AgentStateTests' -Sources @(
    'Tests\AgentCore\AgentStateTests.cpp',
    'Source\Agent\AgentState.cpp'
)

Invoke-CompileAndRun -Name 'AgentAccessibilityStateTests' -Sources @(
    'Tests\AgentCore\AgentAccessibilityStateTests.cpp',
    'Source\Agent\AgentAccessibilityState.cpp'
)

Invoke-CompileAndRun -Name 'ControlPanelCommandRouterTests' -Sources @(
    'Tests\AgentCore\ControlPanelCommandRouterTests.cpp',
    'Source\Agent\ControlPanelCommandRouter.cpp'
)

Invoke-CompileAndRun -Name 'AgentTransportPlannerTests' -Sources @(
    'Tests\AgentCore\AgentTransportPlannerTests.cpp',
    'Source\Agent\AgentTransportPlanner.cpp'
)

Invoke-CompileAndRun -Name 'TransportAccessibilityRegistryTests' -Sources @(
    'Tests\AgentCore\TransportAccessibilityRegistryTests.cpp',
    'Source\Agent\TransportAccessibilityRegistry.cpp'
)

Invoke-CompileAndRun -Name 'AgentTransportCoordinatorTests' -Sources @(
    'Tests\AgentCore\AgentTransportCoordinatorTests.cpp',
    'Source\Agent\AgentTransportCoordinator.cpp',
    'Source\Agent\AgentTransportPlanner.cpp'
)

Invoke-CompileAndRun -Name 'AgentTransportMailboxTests' -Sources @(
    'Tests\AgentCore\AgentTransportMailboxTests.cpp',
    'Source\Agent\AgentTransportMailbox.cpp'
)

Invoke-CompileAndRun -Name 'AgentTransportEndpointTests' -Sources @(
    'Tests\AgentCore\AgentTransportEndpointTests.cpp',
    'Source\Agent\AgentTransportEndpoint.cpp',
    'Source\Agent\AgentTransportMailbox.cpp',
    'Source\Agent\AgentStateSnapshotCache.cpp'
)

Invoke-CompileAndRun -Name 'AgentControlProtocolTests' -Sources @(
    'Tests\AgentCore\AgentControlProtocolTests.cpp',
    'Source\Agent\AgentControlProtocol.cpp',
    'Source\Agent\AgentExperimentDirectoryEndpoint.cpp'
)

Invoke-CompileAndRun -Name 'AgentLoopbackServerTests' -Sources @(
    'Tests\AgentCore\AgentLoopbackServerTests.cpp',
    'Source\Agent\AgentLoopbackServer.cpp',
    'Source\Agent\AgentControlProtocol.cpp',
    'Source\Agent\AgentTransportEndpoint.cpp',
    'Source\Agent\AgentTransportMailbox.cpp',
    'Source\Agent\AgentStateSnapshotCache.cpp',
    'Source\Agent\AgentExperimentDirectoryEndpoint.cpp'
) -Libraries @(
    '-lws2_32'
)

Invoke-CompileAndRun -Name 'AgentStateSnapshotCacheTests' -Sources @(
    'Tests\AgentCore\AgentStateSnapshotCacheTests.cpp',
    'Source\Agent\AgentStateSnapshotCache.cpp'
)

Invoke-CompileAndRun -Name 'AgentExperimentDirectoryTests' -Sources @(
    'Tests\AgentCore\AgentExperimentDirectoryTests.cpp',
    'Source\Agent\AgentExperimentDirectory.cpp'
)

Invoke-CompileAndRun -Name 'AgentExperimentDirectoryEndpointTests' -Sources @(
    'Tests\AgentCore\AgentExperimentDirectoryEndpointTests.cpp',
    'Source\Agent\AgentExperimentDirectoryEndpoint.cpp'
)

Invoke-CompileAndRun -Name 'AgentRecordingSafetyTests' -Sources @(
    'Tests\AgentCore\AgentRecordingSafetyTests.cpp',
    'Source\Agent\AgentRecordingSafety.cpp'
)

Invoke-CompileAndRun -Name 'AgentRecordingWatchdogTests' -Sources @(
    'Tests\AgentCore\AgentRecordingWatchdogTests.cpp',
    'Source\Agent\AgentRecordingWatchdog.cpp'
)
