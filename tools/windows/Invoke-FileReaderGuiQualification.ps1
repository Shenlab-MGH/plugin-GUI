[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $PackageRoot,
    [Parameter(Mandatory)] [string] $RunRoot,
    [ValidateRange(1024, 65535)] [int] $AgentPort = 38511,
    [ValidateRange(10.0, 10.0)] [double] $MinimumSeconds = 10.0,
    [ValidateRange(10.0, 10.0)] [double] $TargetSeconds = 10.0,
    [ValidateRange(12.0, 12.0)] [double] $MaximumSeconds = 12.0
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$StabilityObservationCount = 3
$StabilityObservationSpanSeconds = 10

function Get-FullPath([string] $Path) {
    [System.IO.Path]::GetFullPath($Path)
}

function Test-PathEqual([string] $Left, [string] $Right) {
    (Get-FullPath $Left).Equals(
        (Get-FullPath $Right),
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-NativeStatus {
    Invoke-RestMethod -Method Get -Uri "$script:Base/v1/status" `
        -Headers $script:Headers -TimeoutSec 5
}

function Invoke-HttpProbe(
    [string] $Method,
    [string] $Uri,
    [hashtable] $Headers = @{},
    [string] $Body = '') {
    $parameters = @{
        Method = $Method
        Uri = $Uri
        Headers = $Headers
        TimeoutSec = 5
        SkipHttpErrorCheck = $true
    }
    if ($Body.Length -ne 0) {
        $parameters['Body'] = $Body
        $parameters['ContentType'] = 'application/json'
    }
    $response = Invoke-WebRequest @parameters
    [pscustomobject]@{
        status_code = [int]$response.StatusCode
        body = if ($response.Content.Length) {
            $response.Content | ConvertFrom-Json
        } else { $null }
    }
}

function Test-UnauthorizedRequest {
    $missing = Invoke-HttpProbe -Method Get -Uri "$script:Base/v1/status"
    $wrong = Invoke-HttpProbe -Method Get -Uri "$script:Base/v1/status" `
        -Headers @{ Authorization = 'Bearer deliberately-wrong-token' }
    if ($missing.status_code -ne 401 -or $wrong.status_code -ne 401) {
        throw 'Agent endpoint did not reject missing and wrong bearer tokens.'
    }
    [pscustomobject]@{
        missing_token_status = $missing.status_code
        wrong_token_status = $wrong.status_code
        pass = $true
    }
}

function Wait-DirectoryRequest([string] $CommandId, [int] $Seconds = 10) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $query = Invoke-RestMethod -Method Get `
            -Uri "$script:Base/v1/experiment/directory/requests/$CommandId" `
            -Headers $script:Headers -TimeoutSec 5
        if ($query.state -in @('COMPLETED', 'CANCELLED', 'EXPIRED')) {
            return $query
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Directory request did not reach a terminal state: $CommandId"
}

function Test-StaleRevisionRejection {
    $status = Get-NativeStatus
    $commandId = 'negative-stale-' + [guid]::NewGuid().ToString('N')
    $request = [ordered]@{
        run_id = $script:RunId
        command_id = $commandId
        expected_session_id = $status.session_id
        approved_root = $script:RecordingRoot
        directory_name = 'REJECT_STALE_REVISION'
        expected_revision = [uint64]$status.revision + 1
    }
    $body = $request | ConvertTo-Json -Compress
    $receipt = Invoke-HttpProbe -Method Put `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -Body $body
    if ($receipt.status_code -ne 202 -or $receipt.body.state -ne 'PENDING') {
        throw 'Stale-revision negative control was not accepted for evaluation.'
    }
    $terminal = Wait-DirectoryRequest -CommandId $commandId
    if ($terminal.state -ne 'COMPLETED' `
        -or $terminal.outcome -ne 'REVISION_CONFLICT' `
        -or $terminal.prepared) {
        throw 'Stale revision did not fail closed.'
    }
    [pscustomobject]@{
        command_id = $commandId
        expected_revision = $request.expected_revision
        actual_revision = $status.revision
        terminal = $terminal
        request_body = $body
        pass = $true
    }
}

function Test-RequestIdSemantics([pscustomobject] $StaleControl) {
    $duplicate = Invoke-HttpProbe -Method Put `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -Body $StaleControl.request_body
    $changed = $StaleControl.request_body | ConvertFrom-Json
    $changed.directory_name = 'REJECT_CHANGED_PAYLOAD'
    $conflict = Invoke-HttpProbe -Method Put `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers `
        -Body ($changed | ConvertTo-Json -Compress)
    if ($duplicate.status_code -ne 200 `
        -or $duplicate.body.state -ne 'DUPLICATE' `
        -or $conflict.status_code -ne 409 `
        -or $conflict.body.reason -ne 'ID_CONFLICT') {
        throw 'Directory request-id idempotency/conflict semantics failed.'
    }
    [pscustomobject]@{
        duplicate_status = $duplicate.status_code
        duplicate_state = $duplicate.body.state
        conflict_status = $conflict.status_code
        conflict_reason = $conflict.body.reason
        pass = $true
    }
}

function Test-DirectoryCollisionRejection([string] $DirectoryName) {
    $status = Get-NativeStatus
    $target = Join-Path $script:RecordingRoot $DirectoryName
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        throw 'Collision control requires an existing closed block directory.'
    }
    $commandId = 'negative-collision-' + [guid]::NewGuid().ToString('N')
    $body = [ordered]@{
        run_id = $script:RunId
        command_id = $commandId
        expected_session_id = $status.session_id
        approved_root = $script:RecordingRoot
        directory_name = $DirectoryName
        expected_revision = [uint64]$status.revision
    } | ConvertTo-Json -Compress
    $receipt = Invoke-HttpProbe -Method Put `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -Body $body
    if ($receipt.status_code -ne 202 -or $receipt.body.state -ne 'PENDING') {
        throw 'Collision negative control was not accepted for evaluation.'
    }
    $terminal = Wait-DirectoryRequest -CommandId $commandId
    if ($terminal.state -ne 'COMPLETED' `
        -or $terminal.outcome -ne 'DIRECTORY_COLLISION' `
        -or $terminal.prepared) {
        throw 'Existing directory collision did not fail closed.'
    }
    [pscustomobject]@{
        command_id = $commandId
        target_path = $target
        terminal = $terminal
        pass = $true
    }
}

function Wait-NativeMode([string] $Mode, [int] $Seconds = 10) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $status = Get-NativeStatus
        if ($status.mode -eq $Mode) { return $status }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "RECOVERY_REQUIRED: timed out waiting for mode $Mode."
}

function Get-AgentElement([int] $OwnerProcessId, [string] $AutomationId) {
    $processCondition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
        $OwnerProcessId)
    $all = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        $processCondition)
    $matches = @($all | Where-Object {
        $_.GetCurrentPropertyValue(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty
        ) -eq $AutomationId
    })
    if ($matches.Count -ne 1) {
        throw "Expected one UIA element $AutomationId; observed $($matches.Count)."
    }
    $matches[0]
}

function Get-RequestId([string] $Value) {
    $match = [regex]::Match($Value, '\|REQUEST=([^|]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    $null
}

function Invoke-AgentTransport([string] $AutomationId, [string] $ExpectedMode) {
    $element = Get-AgentElement -OwnerProcessId $script:ProcessId `
        -AutomationId $AutomationId
    $valueObject = $null
    if (-not $element.TryGetCurrentPattern(
            [System.Windows.Automation.ValuePattern]::Pattern,
            [ref] $valueObject)) {
        throw "$AutomationId does not expose ValuePattern."
    }
    $invokeObject = $null
    if (-not $element.TryGetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern,
            [ref] $invokeObject)) {
        throw "$AutomationId does not expose InvokePattern."
    }
    $beforeRequest = Get-RequestId ([System.Windows.Automation.ValuePattern]$valueObject).Current.Value
    ([System.Windows.Automation.InvokePattern]$invokeObject).Invoke()
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $value = ([System.Windows.Automation.ValuePattern]$valueObject).Current.Value
        $request = [regex]::Match($value, '\|REQUEST=([^|]+)')
        $state = [regex]::Match($value, '\|STATE=([^|]+)')
        $outcome = [regex]::Match($value, '\|OUTCOME=([^|]+)')
        if ($request.Success -and $request.Groups[1].Value -ne $beforeRequest `
            -and $state.Success -and $state.Groups[1].Value -in @(
                'COMPLETED', 'CANCELLED', 'EXPIRED')) {
            $terminal = [ordered]@{
                automation_id = $AutomationId
                request_id = $request.Groups[1].Value
                state = $state.Groups[1].Value
                outcome = if ($outcome.Success) { $outcome.Groups[1].Value } else { $null }
                value = $value
            }
            if ($terminal.state -ne 'COMPLETED' -or $terminal.outcome -ne 'COMPLETED') {
                throw "UIA request failed: $($terminal | ConvertTo-Json -Compress)."
            }
            $readback = Wait-NativeMode -Mode $ExpectedMode
            $terminal['readback_mode'] = $readback.mode
            $terminal['readback_revision'] = $readback.revision
            return [pscustomobject]$terminal
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "RECOVERY_REQUIRED: UIA request for $AutomationId did not reach a new terminal state."
}

function Prepare-RecordingDirectory([string] $DirectoryName) {
    if ($DirectoryName.Length -gt 120 -or $DirectoryName -notmatch '^[A-Za-z0-9 _-]+$' `
        -or $DirectoryName -match '\s$' -or $DirectoryName -match ' \([1-9][0-9]*\)$') {
        throw 'Unsafe recording directory name.'
    }
    $reserved = @('CON', 'PRN', 'AUX', 'NUL') `
        + @(1..9 | ForEach-Object { "COM$_" }) `
        + @(1..9 | ForEach-Object { "LPT$_" })
    if ($reserved -contains $DirectoryName.ToUpperInvariant()) {
        throw 'Windows reserved recording directory name.'
    }
    $pre = Get-NativeStatus
    if ($pre.schema_version -ne 'oe-agent-control-preview/v0.0.1' `
        -or -not $pre.online -or $pre.phase -ne 'READY' `
        -or -not $pre.mutation_allowed -or $pre.mode -ne 'IDLE') {
        throw 'Directory preparation requires READY, armed, IDLE runtime.'
    }
    $directory = Invoke-RestMethod -Method Get `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -TimeoutSec 5
    if ($directory.session_id -ne $pre.session_id -or $directory.mode -ne 'IDLE' `
        -or [uint64]$directory.revision -ne [uint64]$pre.revision) {
        throw 'Directory snapshot/session/revision mismatch.'
    }
    $target = Join-Path $script:RecordingRoot $DirectoryName
    if (Test-Path -LiteralPath $target) {
        throw "Recording target already exists: $target"
    }
    if (@(Get-ChildItem -LiteralPath $script:RecordingRoot -Directory |
            Where-Object { $_.Name -ieq $DirectoryName }).Count -ne 0) {
        throw 'Case-insensitive recording target collision.'
    }
    $commandId = 'dir-' + [guid]::NewGuid().ToString('N')
    $body = [ordered]@{
        run_id = $script:RunId
        command_id = $commandId
        expected_session_id = $pre.session_id
        approved_root = $script:RecordingRoot
        directory_name = $DirectoryName
        expected_revision = [uint64]$directory.revision
    } | ConvertTo-Json -Compress
    $response = Invoke-WebRequest -Method Put `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -ContentType 'application/json' `
        -Body $body -TimeoutSec 5
    $receipt = $response.Content | ConvertFrom-Json
    if ($response.StatusCode -ne 202 -or $receipt.command_id -ne $commandId `
        -or $receipt.state -ne 'PENDING' -or $receipt.reason -ne 'NONE') {
        throw 'Directory request was not accepted.'
    }
    $deadline = (Get-Date).AddSeconds(10)
    $terminal = $null
    do {
        $query = Invoke-RestMethod -Method Get `
            -Uri "$script:Base/v1/experiment/directory/requests/$commandId" `
            -Headers $script:Headers -TimeoutSec 5
        if ($query.state -in @('COMPLETED', 'CANCELLED', 'EXPIRED')) {
            $terminal = $query
            break
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    if ($null -eq $terminal -or $terminal.state -ne 'COMPLETED' `
        -or $terminal.outcome -ne 'READY' -or -not $terminal.prepared `
        -or [uint64]$terminal.final_revision -ne [uint64]$directory.revision) {
        throw "Directory preparation failed: $($terminal | ConvertTo-Json -Compress)"
    }
    $readback = Invoke-RestMethod -Method Get `
        -Uri "$script:Base/v1/experiment/directory" `
        -Headers $script:Headers -TimeoutSec 5
    if (-not $readback.prepared -or $readback.target_exists `
        -or $readback.directory_name -ne $DirectoryName `
        -or -not (Test-PathEqual $readback.target_path $target) `
        -or -not (Test-PathEqual $readback.approved_root $script:RecordingRoot)) {
        throw 'Authoritative directory readback failed.'
    }
    [pscustomobject]@{
        command_id = $commandId
        target_path = $target
        receipt = $receipt
        terminal = $terminal
        readback = $readback
    }
}

function Invoke-RecordingSegment([int] $Number) {
    $name = 'BLOCK_{0:d2}' -f $Number
    $prepared = Prepare-RecordingDirectory -DirectoryName $name
    $events = @()
    $events += Invoke-AgentTransport -AutomationId 'oe.transport.acquisition' `
        -ExpectedMode 'ACQUIRE'
    $events += Invoke-AgentTransport -AutomationId 'oe.transport.recording' `
        -ExpectedMode 'RECORD'
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $dataFile = $null
    $minimumBytes = [int64](40000 * 16 * 2 * $MinimumSeconds)
    $deadline = (Get-Date).AddSeconds($MaximumSeconds + 5)
    do {
        $candidates = @(Get-ChildItem -LiteralPath $prepared.target_path `
            -Recurse -Filter continuous.dat -File -ErrorAction SilentlyContinue)
        if ($candidates.Count -eq 1) {
            # FileInfo.Length is cached. Re-resolve the file on each poll so
            # the stop decision follows bytes actually persisted by Open Ephys.
            $dataFile = Get-Item -LiteralPath $candidates[0].FullName
            if ($clock.Elapsed.TotalSeconds -ge $TargetSeconds `
                -and $dataFile.Length -ge $minimumBytes) { break }
        }
        elseif ($candidates.Count -gt 1) {
            throw 'Expected exactly one continuous.dat in File Reader smoke.'
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    $recordWallSeconds = $clock.Elapsed.TotalSeconds
    $events += Invoke-AgentTransport -AutomationId 'oe.transport.recording' `
        -ExpectedMode 'ACQUIRE'
    $events += Invoke-AgentTransport -AutomationId 'oe.transport.acquisition' `
        -ExpectedMode 'IDLE'
    if ($null -eq $dataFile -or -not (Test-Path -LiteralPath $dataFile.FullName)) {
        throw 'continuous.dat was not created.'
    }
    $stability = @()
    $stabilityInterval = $StabilityObservationSpanSeconds / `
        ($StabilityObservationCount - 1)
    foreach ($observation in 1..$StabilityObservationCount) {
        $stableFile = Get-Item -LiteralPath $dataFile.FullName
        $stability += [pscustomobject]@{
            observation = $observation
            observed_at = (Get-Date).ToUniversalTime().ToString('o')
            bytes = $stableFile.Length
            last_write_time_utc = $stableFile.LastWriteTimeUtc.ToString('o')
        }
        if ($observation -lt $StabilityObservationCount) {
            Start-Sleep -Seconds $stabilityInterval
        }
    }
    if (($stability.bytes | Sort-Object -Unique).Count -ne 1) {
        throw 'Closed recording file changed during the ten-second stability gate.'
    }
    $length2 = [int64]$stability[-1].bytes
    if (($length2 % 32) -ne 0) { throw 'continuous.dat is not frame aligned.' }
    $frames = [int64]($length2 / 32)
    $duration = $frames / 40000.0
    if ($duration -lt $MinimumSeconds) {
        throw "Persisted recording duration is short: $duration seconds."
    }
    if ($duration -gt $MaximumSeconds) {
        throw "Persisted recording duration exceeded timing tolerance: $duration seconds."
    }
    $structures = @(Get-ChildItem -LiteralPath $prepared.target_path `
        -Recurse -Filter structure.oebin -File)
    if ($structures.Count -ne 1) { throw 'Expected exactly one structure.oebin.' }
    $structure = Get-Content -Raw -LiteralPath $structures[0].FullName | ConvertFrom-Json
    $continuous = @($structure.continuous)
    if ($continuous.Count -ne 1 -or [int]$continuous[0].num_channels -ne 16 `
        -or [double]$continuous[0].sample_rate -ne 40000.0) {
        throw 'Recorded structure does not match 16 channels at 40 kHz.'
    }
    $settings = @(Get-ChildItem -LiteralPath $prepared.target_path `
        -Recurse -Filter settings.xml -File)
    if ($settings.Count -lt 1) { throw 'Recording settings.xml is missing.' }
    [pscustomobject]@{
        segment = $Number
        directory_name = $name
        target_path = $prepared.target_path
        directory_command_id = $prepared.command_id
        wall_seconds = [math]::Round($recordWallSeconds, 6)
        continuous_file = $dataFile.FullName
        bytes = $length2
        frames = $frames
        persisted_duration_seconds = [math]::Round($duration, 6)
        continuous_sha256 = (
            Get-FileHash -LiteralPath $dataFile.FullName -Algorithm SHA256
        ).Hash
        stability_observations = $stability
        structure_file = $structures[0].FullName
        settings_files = @($settings.FullName)
        transport_events = $events
    }
}

$package = Get-FullPath $PackageRoot
$run = Get-FullPath $RunRoot
$runManifestPath = Join-Path $run 'RUN-MANIFEST.json'
if (-not (Test-Path -LiteralPath $runManifestPath -PathType Leaf)) {
    throw 'Run manifest does not exist.'
}
$runManifest = Get-Content -Raw -LiteralPath $runManifestPath | ConvertFrom-Json
$script:RunId = [string]$runManifest.run_id
if ($runManifest.qualification_name -ne 'FILE_READER_EIGHT_BLOCK_QUALIFICATION' `
    -or [int]$runManifest.approved_block_count -ne 8 `
    -or @($runManifest.blocks).Count -ne 8) {
    throw 'Run manifest is not an eight-block File Reader qualification.'
}
foreach ($block in $runManifest.blocks) {
    if ([double]$block.target_seconds -ne $TargetSeconds `
        -or [double]$block.minimum_seconds -ne $MinimumSeconds `
        -or [double]$block.maximum_seconds -ne $MaximumSeconds `
        -or $null -ne $block.neuropixels_preset) {
        throw 'Run manifest timing or synthetic-block boundary mismatch.'
    }
}
$configuration = Get-FullPath (Join-Path $run 'file-reader-simulation.xml')
$stateDirectory = Get-FullPath $runManifest.state_directory
$script:RecordingRoot = Get-FullPath $runManifest.recording_root
$evidenceDirectory = Get-FullPath $runManifest.evidence_directory
$configurationHash = (Get-FileHash -LiteralPath $configuration -Algorithm SHA256).Hash
if ($configurationHash -ne $runManifest.derived_config_sha256) {
    throw 'Derived configuration hash does not match run manifest.'
}
$derivedSource = Get-FullPath $runManifest.derived_source_continuous
$runPrefix = $run.TrimEnd('\') + '\'
if (-not $derivedSource.StartsWith(
        $runPrefix,
        [System.StringComparison]::OrdinalIgnoreCase) `
    -or -not (Test-Path -LiteralPath $derivedSource -PathType Leaf)) {
    throw 'Derived File Reader source is outside the isolated run or missing.'
}
$derivedSourceFile = Get-Item -LiteralPath $derivedSource
$derivedSourceHash = (
    Get-FileHash -LiteralPath $derivedSource -Algorithm SHA256
).Hash
if ($derivedSourceFile.Length -ne [int64]$runManifest.derived_source_continuous_bytes `
    -or $derivedSourceHash -ne $runManifest.derived_source_continuous_sha256 `
    -or [int]$runManifest.derived_source_duration_seconds -ne 120) {
    throw 'Derived File Reader source manifest verification failed.'
}
$executable = Join-Path $package 'open-ephys.exe'
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash

$bytes = [byte[]]::new(48)
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$env:OE_AGENT_TOKEN = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
$approvalId = 'smoke-' + [guid]::NewGuid().ToString('N')
$script:Base = "http://127.0.0.1:$AgentPort"
$script:Headers = @{ Authorization = "Bearer $env:OE_AGENT_TOKEN" }
$script:ProcessId = $null
$result = [ordered]@{
    schema_version = 'oe-agent-file-reader-eight-block-qualification/v1'
    pass = $false
    run_id = $script:RunId
    run_root = $run
    started_at = (Get-Date).ToUniversalTime().ToString('o')
    qualification_name = 'FILE_READER_EIGHT_BLOCK_QUALIFICATION'
    scientific_claim = 'technical-integrity-only'
    limitations = @(
        'No Neuropixels runtime was used.',
        'No preset or physical shank selection was tested.',
        'Eight synthetic blocks are workflow repetitions, not shanks.',
        'Eight-shank claims remain prohibited.'
    )
    source_verification = [ordered]@{
        path = $derivedSource
        bytes = $derivedSourceFile.Length
        sha256 = $derivedSourceHash
        duration_seconds = $runManifest.derived_source_duration_seconds
        pass = $true
    }
}

try {
    $launchJson = & (Join-Path $package 'Start-IsolatedAgentRuntime.ps1') `
        -Executable $executable -StateDirectory $stateDirectory `
        -ConfigurationFile $configuration -AgentPort $AgentPort `
        -Launch -MaintenanceWindowApproved -EnableInteractiveUia `
        -EnableAgentMutation -ApprovalId $approvalId
    $launch = $launchJson | ConvertFrom-Json
    $script:ProcessId = [int]$launch.process_id
    $result['process_id'] = $script:ProcessId
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $listener = @(Get-NetTCPConnection -State Listen -LocalPort $AgentPort `
            -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $script:ProcessId)
        if ($listener.Count -eq 1) {
            try {
                $ready = Get-NativeStatus
                if ($ready.phase -eq 'READY' -and $ready.mode -eq 'IDLE') { break }
            } catch { }
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    if ($null -eq $ready -or $ready.phase -ne 'READY' -or $ready.mode -ne 'IDLE') {
        throw 'Open Ephys did not reach READY/IDLE in time.'
    }
    $runtimeJson = & (Join-Path $package 'Test-IsolatedAgentRuntime.ps1') `
        -ProcessId $script:ProcessId -StateDirectory $stateDirectory `
        -ExpectInteractiveUia -ExpectedExecutable $executable `
        -ExpectedExecutableSha256 $executableHash -AgentPort $AgentPort
    $runtime = $runtimeJson | ConvertFrom-Json
    $result['runtime_verification'] = $runtime
    if (-not $runtime.pass) { throw 'Runtime isolation verifier failed.' }
    $accessibilityJson = & (Join-Path $package 'Test-AgentAccessibility.ps1') `
        -ProcessId $script:ProcessId -AgentPort $AgentPort `
        -ExpectInteractive -ExerciseTransport `
        -ApprovedSimulationProfile FILE_READER_GUI_SMOKE `
        -ExpectedConfiguration $configuration `
        -ExpectedConfigurationSha256 $configurationHash `
        -ExpectedStateDirectory $stateDirectory
    $accessibility = $accessibilityJson | ConvertFrom-Json
    $result['accessibility_verification'] = $accessibility
    if (-not $accessibility.pass -or -not $accessibility.simulation_verified `
        -or $accessibility.post_mode -ne 'IDLE') {
        throw (
            'Interactive accessibility verifier failed: pass=' `
            + $accessibility.pass `
            + ', simulation_verified=' + $accessibility.simulation_verified `
            + ', post_mode=' + $accessibility.post_mode)
    }
    $result['launch'] = $launch
    $negativeControls = [ordered]@{}
    $negativeControls['unauthorized_requests'] = Test-UnauthorizedRequest
    $staleControl = Test-StaleRevisionRejection
    $negativeControls['stale_revision'] = $staleControl
    $negativeControls['request_id_semantics'] = `
        Test-RequestIdSemantics -StaleControl $staleControl
    $segments = @()
    foreach ($block in $runManifest.blocks) {
        $segments += Invoke-RecordingSegment -Number ([int]$block.index)
        if ([int]$block.index -eq 1) {
            $negativeControls['directory_collision'] = `
                Test-DirectoryCollisionRejection `
                    -DirectoryName ([string]$block.directory_name)
        }
    }
    $final = Get-NativeStatus
    if ($final.mode -ne 'IDLE') { throw 'Final runtime mode is not IDLE.' }
    if (($segments.target_path | Sort-Object -Unique).Count -ne 8) {
        throw 'Recording segment directories are not independent.'
    }
    $nativeDirectories = @(
        Get-ChildItem -LiteralPath $script:RecordingRoot -Directory
    )
    if ($nativeDirectories.Count -ne 8 `
        -or (Compare-Object `
            -ReferenceObject @($runManifest.blocks.directory_name | Sort-Object) `
            -DifferenceObject @($nativeDirectories.Name | Sort-Object)).Count -ne 0) {
        throw 'Recording root does not contain exactly the eight planned blocks.'
    }
    $result['negative_controls'] = $negativeControls
    $result['segments'] = $segments
    $result['final_status'] = $final
    $result['FILE_READER_EIGHT_BLOCK_QUALIFICATION'] = 'PASS'
    $result['NEUROPIXELS_SIM_CAPABILITY'] = 'UNVERIFIED'
    $result['EIGHT_PRESET_SIM_ACCEPTANCE'] = 'BLOCKED'
    $result['EIGHT_SHANK_CLAIM'] = 'PROHIBITED'
    $result['SCIENTIFIC_SIGNAL_QC'] = 'NEEDS_REVIEW'
    $result['pass'] = $true
}
catch {
    $result['error'] = $_.Exception.Message
    if ($script:ProcessId) {
        try {
            $status = Get-NativeStatus
            if ($status.mode -eq 'RECORD') {
                $result['recovery_recording'] = Invoke-AgentTransport `
                    -AutomationId 'oe.transport.recording' -ExpectedMode 'ACQUIRE'
                $status = Get-NativeStatus
            }
            if ($status.mode -eq 'ACQUIRE') {
                $result['recovery_acquisition'] = Invoke-AgentTransport `
                    -AutomationId 'oe.transport.acquisition' -ExpectedMode 'IDLE'
            }
            $result['recovery_final_status'] = Get-NativeStatus
        }
        catch {
            $result['recovery_error'] = $_.Exception.Message
        }
    }
}
finally {
    $result['finished_at'] = (Get-Date).ToUniversalTime().ToString('o')
    $evidencePath = Join-Path $evidenceDirectory `
        'file-reader-gui-qualification-result.json'
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $evidencePath -Encoding utf8NoBOM
    Remove-Item Env:OE_AGENT_TOKEN -ErrorAction SilentlyContinue
}

$result | ConvertTo-Json -Depth 12
if (-not $result.pass) { exit 1 }
