[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Pinned CMake executable not found: $cmake"
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio locator not found: $vswhere"
}

$sourceCommit = (git -C $repoRoot rev-parse HEAD).Trim()
$sourceDescribe = (git -C $repoRoot describe --tags --always --dirty).Trim()
$cmakeVersion = (& $cmake --version | Select-Object -First 1).Trim()
$visualStudioPath = (& $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath).Trim()

if (-not $visualStudioPath) {
    throw 'Visual Studio C++ Build Tools installation was not found.'
}

$msvcToolsRoot = Join-Path $visualStudioPath 'VC\Tools\MSVC'
$msvcVersion = Get-ChildItem -LiteralPath $msvcToolsRoot -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty Name

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
$sdkVersions = @()
if (Test-Path -LiteralPath $sdkRoot) {
    $sdkVersions = @(Get-ChildItem -LiteralPath $sdkRoot -Directory |
        Sort-Object Name |
        Select-Object -ExpandProperty Name)
}

[ordered]@{
    source_commit = $sourceCommit
    source_describe = $sourceDescribe
    cmake_version = $cmakeVersion
    generator = 'Visual Studio 17 2022'
    architecture = 'x64'
    visual_studio_installation = $visualStudioPath
    msvc_version = $msvcVersion
    windows_sdk_versions = $sdkVersions
    configuration = 'Release'
} | ConvertTo-Json -Depth 4
