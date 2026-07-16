[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$buildDirectory = Join-Path $repoRoot 'Build'

if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt'))) {
    throw 'CMake is not configured. Run Invoke-OeConfigure.ps1 first.'
}

& $cmake --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Open Ephys Release build failed with exit code $LASTEXITCODE"
}
