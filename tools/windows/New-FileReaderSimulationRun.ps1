[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $PackageRoot,
    [Parameter(Mandatory)] [string] $RunRoot,
    [ValidateRange(1024, 65535)] [int] $AgentPort = 38511,
    [ValidateRange(8, 8)] [int] $BlockCount = 8,
    [ValidateRange(10.0, 10.0)] [double] $TargetSeconds = 10.0,
    [ValidateRange(10.0, 10.0)] [double] $MinimumSeconds = 10.0,
    [ValidateRange(12.0, 12.0)] [double] $MaximumSeconds = 12.0,
    [ValidateRange(120, 120)] [int] $SourceDurationSeconds = 120
)

$ErrorActionPreference = 'Stop'

function Get-CanonicalPath([string] $Path) {
    [System.IO.Path]::GetFullPath($Path)
}

$package = Get-CanonicalPath $PackageRoot
$run = Get-CanonicalPath $RunRoot
$approvedRoot = Get-CanonicalPath 'C:\OE-Agent-Simulation'

if (-not $run.StartsWith(
        $approvedRoot.TrimEnd('\') + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "RunRoot must be a new child of $approvedRoot"
}
if (Test-Path -LiteralPath $run) {
    throw "RunRoot already exists: $run"
}
if ($AgentPort -eq 37497) {
    throw 'AgentPort must not be the native HTTP port 37497.'
}

$packageManifestPath = Join-Path $package 'RUN-MANIFEST.json'
$sourceConfig = Join-Path $package 'configs\file_reader_config.xml'
foreach ($required in @(
    $packageManifestPath,
    $sourceConfig,
    (Join-Path $package 'open-ephys.exe'),
    (Join-Path $package 'Start-IsolatedAgentRuntime.ps1'),
    (Join-Path $package 'Test-IsolatedAgentRuntime.ps1'),
    (Join-Path $package 'Test-AgentAccessibility.ps1')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required package file is missing: $required"
    }
}

$packageManifest = Get-Content -Raw -LiteralPath $packageManifestPath |
    ConvertFrom-Json
$hashFailures = @()
foreach ($entry in $packageManifest.files) {
    $candidate = Join-Path $package $entry.path
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        $hashFailures += "missing:$($entry.path)"
        continue
    }
    $actual = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash
    if ($actual -ne $entry.sha256) {
        $hashFailures += "hash:$($entry.path)"
    }
}
if ($hashFailures.Count -ne 0) {
    throw "Package verification failed: $($hashFailures -join ', ')"
}

$state = Join-Path $run 'state'
$recordings = Join-Path $run 'recordings'
$evidence = Join-Path $run 'evidence'
$derivedSource = Join-Path $run 'file-reader-source'
New-Item -ItemType Directory -Path $state, $recordings, $evidence, $derivedSource |
    Out-Null

$sourceStructurePath = Join-Path $package 'resources\structure.oebin'
$sourceContinuousPath = Join-Path $package `
    'resources\continuous\example_data\continuous.dat'
if (-not (Test-Path -LiteralPath $sourceStructurePath -PathType Leaf) `
    -or -not (Test-Path -LiteralPath $sourceContinuousPath -PathType Leaf)) {
    throw 'Bundled File Reader source is incomplete.'
}
$derivedStructurePath = Join-Path $derivedSource 'structure.oebin'
$structure = Get-Content -Raw -LiteralPath $sourceStructurePath | ConvertFrom-Json
$sourceStreams = @($structure.continuous)
if ($sourceStreams.Count -ne 1 `
    -or [int]$sourceStreams[0].num_channels -ne 16 `
    -or [double]$sourceStreams[0].sample_rate -ne 40000.0) {
    throw 'Bundled File Reader source contract changed.'
}
$structure.events = @()
$structure | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $derivedStructurePath -Encoding utf8NoBOM
$streamFolder = Join-Path $derivedSource (
    'continuous\' + [string]$sourceStreams[0].folder_name
)
New-Item -ItemType Directory -Path $streamFolder | Out-Null
$derivedContinuousPath = Join-Path $streamFolder 'continuous.dat'
$targetSourceBytes = [int64]$SourceDurationSeconds * 40000 * 16 * 2
$inputStream = [IO.File]::OpenRead($sourceContinuousPath)
$outputStream = [IO.File]::Open(
    $derivedContinuousPath,
    [IO.FileMode]::CreateNew,
    [IO.FileAccess]::Write,
    [IO.FileShare]::None
)
try {
    $buffer = [byte[]]::new(1024 * 1024)
    $remaining = $targetSourceBytes
    while ($remaining -gt 0) {
        $read = $inputStream.Read($buffer, 0, $buffer.Length)
        if ($read -eq 0) {
            $inputStream.Position = 0
            continue
        }
        $write = [int][Math]::Min([int64]$read, $remaining)
        $outputStream.Write($buffer, 0, $write)
        $remaining -= $write
    }
}
finally {
    $outputStream.Dispose()
    $inputStream.Dispose()
}
if ((Get-Item -LiteralPath $derivedContinuousPath).Length -ne $targetSourceBytes) {
    throw 'Derived File Reader source length mismatch.'
}

[xml] $configuration = Get-Content -Raw -LiteralPath $sourceConfig
$processors = @($configuration.SETTINGS.SIGNALCHAIN.PROCESSOR)
foreach ($processor in $processors) {
    if ([string] $processor.nodeId -notin @('100', '101', '102')) {
        [void] $configuration.SETTINGS.SIGNALCHAIN.RemoveChild($processor)
    }
}
$recordNodes = @(
    $configuration.SETTINGS.SIGNALCHAIN.PROCESSOR |
        Where-Object { $_.name -eq 'Record Node' }
)
if ($recordNodes.Count -ne 1 -or [string] $recordNodes[0].nodeId -ne '101') {
    throw 'Derived configuration must contain exactly Record Node 101.'
}
$fileReaders = @(
    $configuration.SETTINGS.SIGNALCHAIN.PROCESSOR |
        Where-Object { $_.name -eq 'File Reader' }
)
if ($fileReaders.Count -ne 1 -or [string]$fileReaders[0].nodeId -ne '100') {
    throw 'Derived configuration must contain exactly File Reader 100.'
}
$fileReaders[0].PROCESSOR_PARAMETERS.SetAttribute(
    'selected_file', $derivedStructurePath
)
$fileReaders[0].PROCESSOR_PARAMETERS.SetAttribute('start_time', '00:00:00.000')
$fileReaders[0].PROCESSOR_PARAMETERS.SetAttribute('end_time', '00:01:59.999')
$recordNodes[0].PROCESSOR_PARAMETERS.SetAttribute('directory', $recordings)
$configuration.SETTINGS.CONTROLPANEL.SetAttribute('recordPath', $recordings)
$configuration.SETTINGS.CONTROLPANEL.SetAttribute('forceNewDirectory', '1')
$configuration.SETTINGS.FILENAMECONFIG.PREPEND.SetAttribute('state', '0')
$configuration.SETTINGS.FILENAMECONFIG.PREPEND.SetAttribute('value', '')
$configuration.SETTINGS.FILENAMECONFIG.MAIN.SetAttribute('state', '2')
$configuration.SETTINGS.FILENAMECONFIG.MAIN.SetAttribute('value', 'SIM_FILE_READER')
$configuration.SETTINGS.FILENAMECONFIG.APPEND.SetAttribute('state', '0')
$configuration.SETTINGS.FILENAMECONFIG.APPEND.SetAttribute('value', '')
if ($configuration.OuterXml -match 'directory="default"' -or
    $configuration.OuterXml -match 'recordPath="default"') {
    throw 'Derived configuration still contains a default recording path.'
}

$derivedConfig = Join-Path $run 'file-reader-simulation.xml'
$configuration.Save($derivedConfig)
$blockPlan = @(
    1..$BlockCount | ForEach-Object {
        [ordered]@{
            index = $_
            directory_name = 'BLOCK_{0:d2}' -f $_
            simulation_label = 'SYNTHETIC_FILE_READER_BLOCK_{0:d2}' -f $_
            target_seconds = $TargetSeconds
            minimum_seconds = $MinimumSeconds
            maximum_seconds = $MaximumSeconds
            neuropixels_preset = $null
        }
    }
)

$runManifest = [ordered]@{
    schema_version = 'oe-agent-file-reader-eight-block-qualification/v1'
    run_id = Split-Path -Leaf $run
    qualification_name = 'FILE_READER_EIGHT_BLOCK_QUALIFICATION'
    mode = 'SIMULATION_NO_SUBJECT'
    source_type = 'Open Ephys File Reader bundled example data'
    scientific_claim = 'technical-integrity-only'
    gui_version = '1.0.2-agent-v0.0.1'
    package_source_commit = $packageManifest.source_commit
    package_manifest_sha256 = (
        Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256
    ).Hash
    executable_sha256 = (
        Get-FileHash -LiteralPath (Join-Path $package 'open-ephys.exe') `
            -Algorithm SHA256
    ).Hash
    source_config_sha256 = (
        Get-FileHash -LiteralPath $sourceConfig -Algorithm SHA256
    ).Hash
    derived_config_sha256 = (
        Get-FileHash -LiteralPath $derivedConfig -Algorithm SHA256
    ).Hash
    derived_source_structure = $derivedStructurePath
    derived_source_continuous = $derivedContinuousPath
    derived_source_duration_seconds = $SourceDurationSeconds
    derived_source_continuous_bytes = $targetSourceBytes
    derived_source_continuous_sha256 = (
        Get-FileHash -LiteralPath $derivedContinuousPath -Algorithm SHA256
    ).Hash
    state_directory = $state
    recording_root = $recordings
    evidence_directory = $evidence
    agent_port = $AgentPort
    approved_block_count = $BlockCount
    blocks = $blockPlan
    expected_stream = [ordered]@{
        name = 'example_data'
        channels = 16
        sample_rate_hz = 40000
        bytes_per_frame = 32
    }
    qualification_labels = [ordered]@{
        FILE_READER_EIGHT_BLOCK_QUALIFICATION = 'PENDING'
        NEUROPIXELS_SIM_CAPABILITY = 'UNVERIFIED'
        EIGHT_PRESET_SIM_ACCEPTANCE = 'BLOCKED'
        EIGHT_SHANK_CLAIM = 'PROHIBITED'
        SCIENTIFIC_SIGNAL_QC = 'NEEDS_REVIEW'
    }
    limitations = @(
        'Eight File Reader blocks are synthetic workflow repetitions, not shanks.',
        'No Neuropixels runtime, preset, probe, OneBox, or synchronization was tested.',
        'Scientific signal quality requires scientist-supplied criteria and hardware data.'
    )
    created_at = (Get-Date).ToUniversalTime().ToString('o')
}
$runManifestPath = Join-Path $run 'RUN-MANIFEST.json'
$runManifest | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $runManifestPath -Encoding utf8NoBOM

[pscustomobject]@{
    run_root = $run
    state_directory = $state
    recording_root = $recordings
    evidence_directory = $evidence
    configuration = $derivedConfig
    run_manifest = $runManifestPath
    package_files_verified = $packageManifest.files.Count
    block_count = $BlockCount
    can_launch = $true
} | ConvertTo-Json -Depth 5
