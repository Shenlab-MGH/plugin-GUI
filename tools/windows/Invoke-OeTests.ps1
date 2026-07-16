[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = 'C:\Program Files\CMake\bin\ctest.exe'
$buildDirectory = Join-Path $repoRoot 'Build'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Pinned CTest executable not found: $cmake"
}

& $cmake --test-dir $buildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Open Ephys tests failed with exit code $LASTEXITCODE"
}
