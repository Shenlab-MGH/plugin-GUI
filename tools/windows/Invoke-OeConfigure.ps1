[CmdletBinding()]
param(
    [switch] $AgentNativeFeatures
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Pinned CMake executable not found: $cmake"
}

$arguments = @(
    '-S', $repoRoot,
    '-B', (Join-Path $repoRoot 'Build'),
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    '-DBUILD_TESTS=ON'
)

if ($AgentNativeFeatures) {
    $arguments += '-DOE_AGENT_NATIVE_FEATURES=ON'
}

& $cmake @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Open Ephys CMake configure failed with exit code $LASTEXITCODE"
}
