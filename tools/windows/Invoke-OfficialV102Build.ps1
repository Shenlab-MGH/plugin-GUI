[CmdletBinding()]
param(
    [string] $SourceRoot =
        'D:\cong\05-open-ephys-official-v1.0.2',

    [string] $EvidenceDirectory =
        'D:\cong\artifacts\open-ephys-v1.0.2-official\local-build'
)

$ErrorActionPreference = 'Stop'
$expectedCommit =
    'c91afebcfb0678a667fb93f6312ed33c56ec640f'
$expectedTree =
    '7f2541f24394ffdb204dc4326a2a86f1204beb43'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$vswhere =
    'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$expectedPlugins = @(
    'ArduinoOutput.dll',
    'BandpassFilter.dll',
    'ChannelMap.dll',
    'CommonAvgRef.dll',
    'LfpViewer.dll',
    'PhaseDetector.dll',
    'RecordControl.dll',
    'SpikeDetector.dll',
    'SpikeViewer.dll'
)

function Invoke-LoggedProcess {
    param(
        [Parameter(Mandatory)]
        [string] $Executable,

        [Parameter(Mandatory)]
        [string[]] $Arguments,

        [Parameter(Mandatory)]
        [string] $LogPath,

        [Parameter(Mandatory)]
        [string] $FailureMessage
    )

    & $Executable @Arguments 2>&1 |
        Tee-Object -FilePath $LogPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$FailureMessage (exit code $exitCode). See $LogPath"
    }
}

function Get-ReleaseManifest {
    param([Parameter(Mandatory)][string] $Root)

    return @(
        Get-ChildItem -LiteralPath $Root -Recurse -File |
            ForEach-Object {
                [pscustomobject][ordered]@{
                    path = $_.FullName.Substring($Root.Length + 1).
                        Replace('\', '/')
                    bytes = $_.Length
                    sha256 = (
                        Get-FileHash `
                            -LiteralPath $_.FullName `
                            -Algorithm SHA256
                    ).Hash
                }
            } |
            Sort-Object path
    )
}

if (-not (Test-Path -LiteralPath $SourceRoot)) {
    throw "Official source root does not exist: $SourceRoot"
}
$sourceRootResolved = (Resolve-Path -LiteralPath $SourceRoot).Path
$buildDirectory = Join-Path $sourceRootResolved 'Build'
$releaseDirectory = Join-Path $buildDirectory 'Release'

$commit = (& git -C $sourceRootResolved rev-parse HEAD).Trim()
$tree = (& git -C $sourceRootResolved rev-parse 'HEAD^{tree}').Trim()
$tag = (& git -C $sourceRootResolved describe --tags --exact-match).Trim()
$status = @(
    & git -C $sourceRootResolved status --porcelain=v1
)
if (
    $commit -ne $expectedCommit -or
    $tree -ne $expectedTree -or
    $tag -ne 'v1.0.2' -or
    $status.Count -ne 0
) {
    throw (
        'Refusing to build: source is not the clean official v1.0.2 ' +
        "baseline. commit=$commit tree=$tree tag=$tag " +
        "dirty_entries=$($status.Count)"
    )
}

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Pinned CMake is unavailable: $cmake"
}
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw (
        'Visual Studio 2022 Build Tools are unavailable. Approve the ' +
        'Windows UAC installation first.'
    )
}

$visualStudioPath = (
    & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
).Trim()
if (-not $visualStudioPath) {
    throw 'The Visual Studio C++ x64 workload is not installed.'
}

$msbuild = Join-Path $visualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild is unavailable: $msbuild"
}

New-Item `
    -ItemType Directory `
    -Force `
    -Path $EvidenceDirectory |
    Out-Null
$evidenceResolved = (Resolve-Path -LiteralPath $EvidenceDirectory).Path
$configureLog = Join-Path $evidenceResolved 'configure.log'
$buildLog = Join-Path $evidenceResolved 'build.log'

$existingBuildEntries = @(
    Get-ChildItem -LiteralPath $buildDirectory -Force |
        Where-Object Name -ne '.gitignore'
)
if ($existingBuildEntries.Count -gt 0) {
    throw (
        'Official baseline Build must be pristine. Create a fresh official ' +
        'v1.0.2 worktree instead of reusing CMake cache or output files. ' +
        "Unexpected entries: $(
            ($existingBuildEntries.Name | Sort-Object) -join ', '
        )"
    )
}

$buildStartedUtc = [DateTime]::UtcNow.ToString('o')
Invoke-LoggedProcess `
    -Executable $cmake `
    -Arguments @(
        '-S', $sourceRootResolved,
        '-B', $buildDirectory,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64'
    ) `
    -LogPath $configureLog `
    -FailureMessage 'Official v1.0.2 configure failed'

if (-not (Test-Path (
    Join-Path $buildDirectory 'ALL_BUILD.vcxproj'))) {
    throw 'Configure succeeded but ALL_BUILD.vcxproj was not generated.'
}

$existingCache = Join-Path $buildDirectory 'CMakeCache.txt'
$cacheText = Get-Content -LiteralPath $existingCache -Raw
if ($cacheText -match 'BUILD_TESTS:BOOL=ON') {
    throw (
        'Official production baseline unexpectedly enabled BUILD_TESTS. ' +
        'Use a separate test build.'
    )
}

Invoke-LoggedProcess `
    -Executable $cmake `
    -Arguments @(
        '--build', $buildDirectory,
        '--config', 'Release',
        '--target', 'ALL_BUILD',
        '--parallel'
    ) `
    -LogPath $buildLog `
    -FailureMessage 'Official v1.0.2 Release build failed'

$executable = Join-Path $releaseDirectory 'open-ephys.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Release executable was not produced: $executable"
}
$observedPlugins = @(
    Get-ChildItem `
        -LiteralPath (Join-Path $releaseDirectory 'plugins') `
        -Filter '*.dll' `
        -File |
        Sort-Object Name |
        Select-Object -ExpandProperty Name
)
$pluginDelta = @(
    Compare-Object $expectedPlugins $observedPlugins
)
if ($pluginDelta.Count -ne 0) {
    throw (
        'The local official build does not contain the expected nine ' +
        'built-in plugin DLLs.'
    )
}

$result = [ordered]@{
    status = 'BUILT'
    source_commit = $commit
    source_tree = $tree
    source_tag = $tag
    source_root = $sourceRootResolved
    build_directory = $buildDirectory
    release_directory = $releaseDirectory
    generator = 'Visual Studio 17 2022'
    architecture = 'x64'
    configuration = 'Release'
    build_tests = 'OFF'
    build_started_utc = $buildStartedUtc
    build_finished_utc = [DateTime]::UtcNow.ToString('o')
    visual_studio = $visualStudioPath
    executable = $executable
    executable_sha256 = (
        Get-FileHash `
            -LiteralPath $executable `
            -Algorithm SHA256
    ).Hash
    plugins = $observedPlugins
    configure_log = $configureLog
    configure_log_sha256 = (
        Get-FileHash `
            -LiteralPath $configureLog `
            -Algorithm SHA256
    ).Hash
    build_log = $buildLog
    build_log_sha256 = (
        Get-FileHash `
            -LiteralPath $buildLog `
            -Algorithm SHA256
    ).Hash
    cmake_cache = $existingCache
    cmake_cache_sha256 = (
        Get-FileHash `
            -LiteralPath $existingCache `
            -Algorithm SHA256
    ).Hash
    release_manifest = Get-ReleaseManifest $releaseDirectory
}
$resultPath = Join-Path $evidenceResolved 'build-result.json'
$result |
    ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding utf8
$result | ConvertTo-Json -Depth 5
