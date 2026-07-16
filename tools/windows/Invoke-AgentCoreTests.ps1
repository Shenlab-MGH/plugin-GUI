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
        [string[]] $Sources
    )

    $executable = Join-Path $outputDirectory "$Name.exe"
    $sourcePaths = @($Sources | ForEach-Object { Join-Path $repoRoot $_ })

    & $compiler -std=c++17 -Wall -Wextra -Werror -static `
        @sourcePaths -o $executable
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

Invoke-CompileAndRun -Name 'AgentStateTests' -Sources @(
    'Tests\AgentCore\AgentStateTests.cpp',
    'Source\Agent\AgentState.cpp'
)

Invoke-CompileAndRun -Name 'ControlPanelCommandRouterTests' -Sources @(
    'Tests\AgentCore\ControlPanelCommandRouterTests.cpp',
    'Source\Agent\ControlPanelCommandRouter.cpp'
)
