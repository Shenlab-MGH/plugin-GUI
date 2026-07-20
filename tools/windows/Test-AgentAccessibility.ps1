[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [int] $ProcessId,

    [int] $AgentPort = 38498,

    [switch] $ExpectInteractive,

    [switch] $ExerciseTransport,

    [ValidateSet('FILE_READER_GUI_SMOKE')]
    [string] $ApprovedSimulationProfile,

    [string] $ExpectedConfiguration,

    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string] $ExpectedConfigurationSha256,

    [string] $ExpectedStateDirectory
)

$ErrorActionPreference = 'Stop'
$ReadIterations = 10000
$script:UiaProviderReadEvidence = @()

. (Join-Path $PSScriptRoot 'UiAutomationProviderRetry.ps1')

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class AgentNativeCommandLine
{
    [DllImport("shell32.dll", SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(
        [MarshalAs(UnmanagedType.LPWStr)] string commandLine,
        out int argc);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    public static string[] Split(string commandLine)
    {
        int argc;
        var argv = CommandLineToArgvW(commandLine, out argc);
        if (argv == IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception();
        try
        {
            var result = new string[argc];
            for (var index = 0; index < argc; ++index)
            {
                var value = Marshal.ReadIntPtr(
                    argv, index * IntPtr.Size);
                result[index] = Marshal.PtrToStringUni(value);
            }
            return result;
        }
        finally
        {
            LocalFree(argv);
        }
    }
}
'@

function Get-NativeStatus {
    param(
        [Parameter(Mandatory)]
        [int] $Port,

        [Parameter(Mandatory)]
        [hashtable] $Headers
    )

    Invoke-RestMethod `
        -Method Get `
        -Uri "http://127.0.0.1:$Port/v1/status" `
        -Headers $Headers `
        -TimeoutSec 5
}

function Wait-NativeMode {
    param(
        [Parameter(Mandatory)] [int] $Port,
        [Parameter(Mandatory)] [hashtable] $Headers,
        [Parameter(Mandatory)] [string] $Mode
    )

    $deadline = (Get-Date).AddSeconds(10)
    do {
        $status = Get-NativeStatus -Port $Port -Headers $Headers
        if ($status.mode -eq $Mode) { return $status }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for Open Ephys mode $Mode."
}

function Wait-TransportTerminal {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.ValuePattern] $ValuePattern
    )

    $deadline = (Get-Date).AddSeconds(10)
    do {
        $value = $ValuePattern.Current.Value
        $request = [regex]::Match($value, '\|REQUEST=([^|]+)')
        $state = [regex]::Match($value, '\|STATE=([^|]+)')
        $outcome = [regex]::Match($value, '\|OUTCOME=([^|]+)')
        if ($request.Success -and $state.Success `
            -and $state.Groups[1].Value -in @(
                'COMPLETED', 'CANCELLED', 'EXPIRED')) {
            return [pscustomobject]@{
                request_id = $request.Groups[1].Value
                state = $state.Groups[1].Value
                outcome = if ($outcome.Success) {
                    $outcome.Groups[1].Value
                } else { $null }
            }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw 'RECOVERY_REQUIRED: UIA request did not reach terminal state.'
}

function Get-ProcessWindow {
    param(
        [Parameter(Mandatory)]
        [int] $OwnerProcessId
    )

    $processCondition = New-Object `
        System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
            $OwnerProcessId)
    $read = Invoke-UiaProviderRead `
        -OperationName "Find process $OwnerProcessId top-level window" `
        -Operation {
            [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
                [System.Windows.Automation.TreeScope]::Children,
                $processCondition)
        }
    $script:UiaProviderReadEvidence += $read | Select-Object `
        attempts, transient_failures, elapsed_milliseconds
    $matches = @($read.value | ForEach-Object { $_ })
    if ($matches.Count -ne 1) {
        throw "Expected one top-level process window; observed $($matches.Count)."
    }
    $matches[0]
}

function Get-ExactElements {
    param(
        [Parameter(Mandatory)]
        [object[]] $Elements,

        [Parameter(Mandatory)]
        [string] $AutomationId
    )

    @(
        $Elements | Where-Object {
            $_.GetCurrentPropertyValue(
                [System.Windows.Automation.AutomationElement]::AutomationIdProperty
            ) -eq $AutomationId
        }
    )
}

function Get-ProcessElements {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement] $ProcessWindow
    )

    $read = Invoke-UiaProviderRead `
        -OperationName 'Audit target process-window descendants' `
        -Operation {
            $ProcessWindow.FindAll(
                [System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition)
        }
    $script:UiaProviderReadEvidence += $read | Select-Object `
        attempts, transient_failures, elapsed_milliseconds
    @($ProcessWindow) + @($read.value | ForEach-Object { $_ })
}

function Get-Pattern {
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement] $Element,

        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationPattern] $Pattern
    )

    $value = $null
    $available = $Element.TryGetCurrentPattern($Pattern, [ref] $value)
    [pscustomobject]@{
        available = $available
        value = $value
    }
}

function Assert-FileReaderConfig {
    param([Parameter(Mandatory)] [string] $Path)

    [xml]$xml = Get-Content -LiteralPath $Path -Raw
    $processors = @($xml.SETTINGS.SIGNALCHAIN.PROCESSOR)
    $names = @($processors | ForEach-Object { [string]$_.name } | Sort-Object)
    $expectedNames = @('File Reader', 'LFP Viewer', 'Record Node') | Sort-Object
    if ($names.Count -ne 3 `
        -or ($names -join '|') -ne ($expectedNames -join '|')) {
        throw 'Simulation graph must contain only File Reader, Record Node, and LFP Viewer.'
    }
    $expectedProcessors = @{
        'File Reader' = @{
            plugin = 'File Reader'; type = '0'; processorType = '2'; nodeId = '100'
        }
        'Record Node' = @{
            plugin = 'Record Node'; type = '0'; processorType = '8'; nodeId = '101'
        }
        'LFP Viewer' = @{
            plugin = 'LFP Viewer'; type = '1'; processorType = '3'; nodeId = '102'
        }
    }
    foreach ($name in $expectedProcessors.Keys) {
        $matches = @($processors | Where-Object name -eq $name)
        $expected = $expectedProcessors[$name]
        if ($matches.Count -ne 1 `
            -or [string]$matches[0].pluginName -ne $expected.plugin `
            -or [string]$matches[0].type -ne $expected.type `
            -or [string]$matches[0].processorType -ne $expected.processorType `
            -or [string]$matches[0].nodeId -ne $expected.nodeId) {
            throw "Processor schema mismatch: $name."
        }
    }
    $fileReader = @($processors | Where-Object name -eq 'File Reader')
    if ([int]$fileReader[0].STREAM.channel_count -ne 16 `
        -or [double]$fileReader[0].STREAM.sample_rate -ne 40000.0) {
        throw 'File Reader source must be exactly 16 channels at 40 kHz.'
    }
    if ((Get-Content -LiteralPath $Path -Raw) `
        -match '(?i)Neuropix|OneBox|PXI') {
        throw 'Hardware-source identifiers are prohibited in File Reader smoke.'
    }
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    if ($ExerciseTransport -and -not $ExpectInteractive) {
        throw 'ExerciseTransport requires ExpectInteractive.'
    }
    if ($ExerciseTransport `
        -and $ApprovedSimulationProfile -ne 'FILE_READER_GUI_SMOKE') {
        throw 'ExerciseTransport requires FILE_READER_GUI_SMOKE approval.'
    }
    if ($ExerciseTransport -and (
        -not $ExpectedConfiguration `
        -or -not $ExpectedConfigurationSha256 `
        -or -not $ExpectedStateDirectory)) {
        throw 'ExerciseTransport requires config path, config SHA-256, and state directory.'
    }

    $token = [Environment]::GetEnvironmentVariable(
        'OE_AGENT_TOKEN',
        'Process')
    if (-not $token -or $token.Length -lt 32) {
        throw 'OE_AGENT_TOKEN must contain at least 32 characters.'
    }

    $process = Get-Process -Id $ProcessId -ErrorAction Stop |
        Select-Object Id, ProcessName, Path, StartTime
    if ($process.ProcessName -ne 'open-ephys') {
        throw 'ProcessId does not identify open-ephys.'
    }

    $simulationVerified = $false
    if ($ExerciseTransport) {
        $expectedConfigPath = (
            Resolve-Path -LiteralPath $ExpectedConfiguration
        ).Path
        $expectedStatePath = (
            Resolve-Path -LiteralPath $ExpectedStateDirectory
        ).Path
        $configHash = (
            Get-FileHash -LiteralPath $expectedConfigPath -Algorithm SHA256
        ).Hash
        if ($configHash -ne $ExpectedConfigurationSha256.ToUpperInvariant()) {
            throw 'Expected simulation configuration SHA-256 mismatch.'
        }
        $commandLine = (
            Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId"
        ).CommandLine
        [array]$runtimeArguments = [AgentNativeCommandLine]::Split(
            $commandLine)
        $configMatches = @(
            $runtimeArguments | Where-Object {
                [System.IO.Path]::IsPathFullyQualified($_) `
                -and [System.IO.Path]::GetFullPath($_).Equals(
                    $expectedConfigPath,
                    [System.StringComparison]::OrdinalIgnoreCase)
            }
        )
        $stateIndexes = @(
            for ($index = 0; $index -lt $runtimeArguments.Count; ++$index) {
                if ($runtimeArguments[$index] -eq '--state-dir') { $index }
            }
        )
        $noUserPluginCount = @(
            $runtimeArguments | Where-Object { $_ -eq '--no-user-plugins' }
        ).Count
        $stateArgumentMatches =
            $stateIndexes.Count -eq 1 `
            -and $stateIndexes[0] + 1 -lt $runtimeArguments.Count `
            -and [System.IO.Path]::IsPathFullyQualified(
                $runtimeArguments[$stateIndexes[0] + 1]) `
            -and [System.IO.Path]::GetFullPath(
                $runtimeArguments[$stateIndexes[0] + 1]).Equals(
                    $expectedStatePath,
                    [System.StringComparison]::OrdinalIgnoreCase)
        if ($configMatches.Count -ne 1 `
            -or -not $stateArgumentMatches `
            -or $noUserPluginCount -ne 1) {
            throw 'Runtime argv is not exactly bound to the approved config and state directory.'
        }
        Assert-FileReaderConfig -Path $expectedConfigPath
        $recoveryConfig = Join-Path $expectedStatePath 'recoveryConfig.xml'
        $deadline = (Get-Date).AddSeconds(10)
        $processStartUtc = $process.StartTime.ToUniversalTime()
        while ((-not (Test-Path -LiteralPath $recoveryConfig -PathType Leaf) `
            -or (Get-Item -LiteralPath $recoveryConfig -ErrorAction SilentlyContinue).LastWriteTimeUtc `
                -lt $processStartUtc) `
            -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if (-not (Test-Path -LiteralPath $recoveryConfig -PathType Leaf) `
            -or (Get-Item -LiteralPath $recoveryConfig).LastWriteTimeUtc `
                -lt $processStartUtc) {
            throw 'Runtime recoveryConfig.xml was not published by this process.'
        }
        Assert-FileReaderConfig -Path $recoveryConfig
        $simulationVerified = $true
    }

    $headers = @{ Authorization = "Bearer $token" }
    $preStatus = Get-NativeStatus -Port $AgentPort -Headers $headers

    $processWindow = Get-ProcessWindow -OwnerProcessId $ProcessId
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $processElements = Get-ProcessElements `
            -ProcessWindow $processWindow
        $rootMatches = Get-ExactElements `
            -Elements $processElements `
            -AutomationId "oe.agent.root"
        $acquisitionMatches = Get-ExactElements `
            -Elements $processElements `
            -AutomationId "oe.transport.acquisition"
        $recordingMatches = Get-ExactElements `
            -Elements $processElements `
            -AutomationId "oe.transport.recording"
        if ($rootMatches.Count -eq 1 `
            -and $acquisitionMatches.Count -eq 1 `
            -and $recordingMatches.Count -eq 1) {
            break
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)

    if ($rootMatches.Count -ne 1) {
        throw "Expected one Agent root; observed $($rootMatches.Count)."
    }
    if ($acquisitionMatches.Count -ne 1) {
        throw "Expected one acquisition node; observed $($acquisitionMatches.Count)."
    }
    if ($recordingMatches.Count -ne 1) {
        throw "Expected one recording node; observed $($recordingMatches.Count)."
    }

    $root = $rootMatches[0]
    $acquisition = $acquisitionMatches[0]
    $recording = $recordingMatches[0]
    $children = $root.FindAll(
        [System.Windows.Automation.TreeScope]::Children,
        [System.Windows.Automation.Condition]::TrueCondition)
    $childIds = @(
        $children | ForEach-Object {
            $_.GetCurrentPropertyValue(
                [System.Windows.Automation.AutomationElement]::AutomationIdProperty)
        } | Sort-Object
    )

    $acquisitionValue = Get-Pattern `
        -Element $acquisition `
        -Pattern ([System.Windows.Automation.ValuePattern]::Pattern)
    $recordingValue = Get-Pattern `
        -Element $recording `
        -Pattern ([System.Windows.Automation.ValuePattern]::Pattern)
    $acquisitionInvoke = Get-Pattern `
        -Element $acquisition `
        -Pattern ([System.Windows.Automation.InvokePattern]::Pattern)
    $recordingInvoke = Get-Pattern `
        -Element $recording `
        -Pattern ([System.Windows.Automation.InvokePattern]::Pattern)
    $acquisitionToggle = Get-Pattern `
        -Element $acquisition `
        -Pattern ([System.Windows.Automation.TogglePattern]::Pattern)
    $recordingToggle = Get-Pattern `
        -Element $recording `
        -Pattern ([System.Windows.Automation.TogglePattern]::Pattern)

    $actionableElements = @(
        foreach ($element in $processElements) {
            try {
                $invoke = Get-Pattern -Element $element `
                    -Pattern ([System.Windows.Automation.InvokePattern]::Pattern)
                $toggle = Get-Pattern -Element $element `
                    -Pattern ([System.Windows.Automation.TogglePattern]::Pattern)
                $window = Get-Pattern -Element $element `
                    -Pattern ([System.Windows.Automation.WindowPattern]::Pattern)
                $transform = Get-Pattern -Element $element `
                    -Pattern ([System.Windows.Automation.TransformPattern]::Pattern)
                if ($invoke.available -or $toggle.available `
                    -or $window.available -or $transform.available) {
                    [pscustomobject]@{
                        automation_id = $element.GetCurrentPropertyValue(
                            [System.Windows.Automation.AutomationElement]::AutomationIdProperty)
                        name = $element.GetCurrentPropertyValue(
                            [System.Windows.Automation.AutomationElement]::NameProperty)
                        invoke = $invoke.available
                        toggle = $toggle.available
                        window = $window.available
                        transform = $transform.available
                    }
                }
            }
            catch {
                throw 'Failed to audit the complete process UIA action tree.'
            }
        }
    )

    if (-not $acquisitionValue.available `
        -or -not $recordingValue.available) {
        throw 'Both transport nodes must expose ValuePattern.'
    }

    for ($index = 0; $index -lt $ReadIterations; ++$index) {
        $null = $acquisitionValue.value.Current.Value
        $null = $recordingValue.value.Current.Value
    }

    $postStatus = Get-NativeStatus -Port $AgentPort -Headers $headers
    $expectedChildren = @(
        'oe.transport.acquisition',
        'oe.transport.recording'
    )
    $childrenMatch =
        $childIds.Count -eq 2 `
        -and ($childIds -join '|') -eq ($expectedChildren -join '|')
    $valuesReadOnly =
        $acquisitionValue.value.Current.IsReadOnly `
        -and $recordingValue.value.Current.IsReadOnly
    $actionsAbsent =
        -not $acquisitionInvoke.available `
        -and -not $recordingInvoke.available `
        -and -not $acquisitionToggle.available `
        -and -not $recordingToggle.available
    $stateUnchanged =
        $preStatus.mode -eq $postStatus.mode `
        -and $preStatus.revision -eq $postStatus.revision

    $interactivePatternsMatch =
        $acquisitionInvoke.available `
        -and $recordingInvoke.available `
        -and -not $acquisitionToggle.available `
        -and -not $recordingToggle.available
    $patternsMatch = if ($ExpectInteractive) {
        $interactivePatternsMatch
    } else {
        $actionsAbsent
    }
    $windowSignature =
        'oe.agent.window|I=False|T=False|W=True|X=True'
    $expectedActionSignatures = @($windowSignature)
    if ($ExpectInteractive) {
        $expectedActionSignatures += @(
            'oe.transport.acquisition|I=True|T=False|W=False|X=False',
            'oe.transport.recording|I=True|T=False|W=False|X=False'
        )
    }
    $observedActionSignatures = @(
        $actionableElements | ForEach-Object {
            '{0}|I={1}|T={2}|W={3}|X={4}' -f `
                $_.automation_id, $_.invoke, $_.toggle, `
                $_.window, $_.transform
        } | Sort-Object
    )
    $actionAllowlistMatches =
        $observedActionSignatures.Count -eq `
            $expectedActionSignatures.Count `
        -and ($observedActionSignatures -join ';') -eq `
            (($expectedActionSignatures | Sort-Object) -join ';')

    $exercisePassed = $true
    $exerciseModes = @()
    $exerciseRequests = @()
    $exerciseRecovery = 'not-needed'
    if ($ExerciseTransport) {
        if ($postStatus.mode -ne 'IDLE') {
            throw 'Interactive UIA exercise requires an initial IDLE state.'
        }
        try {
            $acquisitionInvoke.value.Invoke()
            $firstTerminal = Wait-TransportTerminal `
                -ValuePattern $acquisitionValue.value
            $exerciseRequests += $firstTerminal
            if ($firstTerminal.outcome -ne 'COMPLETED') {
                throw "Acquisition request failed: $($firstTerminal.outcome)."
            }
            $acquiring = Wait-NativeMode `
                -Port $AgentPort -Headers $headers -Mode 'ACQUIRE'
            $exerciseModes += $acquiring.mode
            $acquisitionInvoke.value.Invoke()
            $secondTerminal = Wait-TransportTerminal `
                -ValuePattern $acquisitionValue.value
            $exerciseRequests += $secondTerminal
            if ($secondTerminal.outcome -ne 'COMPLETED') {
                throw "Idle request failed: $($secondTerminal.outcome)."
            }
            $restored = Wait-NativeMode `
                -Port $AgentPort -Headers $headers -Mode 'IDLE'
            $exerciseModes += $restored.mode
        }
        finally {
            try {
                if ($acquisitionValue.value.Current.Value `
                    -match '\|REQUEST=') {
                    $null = Wait-TransportTerminal `
                        -ValuePattern $acquisitionValue.value
                }
                $recoveryStatus = Get-NativeStatus `
                    -Port $AgentPort -Headers $headers
                if ($recoveryStatus.mode -eq 'ACQUIRE') {
                    $acquisitionInvoke.value.Invoke()
                    $recoveryTerminal = Wait-TransportTerminal `
                        -ValuePattern $acquisitionValue.value
                    $exerciseRequests += $recoveryTerminal
                    if ($recoveryTerminal.outcome -ne 'COMPLETED') {
                        throw "Recovery request failed: $($recoveryTerminal.outcome)."
                    }
                    $null = Wait-NativeMode `
                        -Port $AgentPort -Headers $headers -Mode 'IDLE'
                    $exerciseRecovery = 'restored-idle'
                }
                elseif ($recoveryStatus.mode -eq 'IDLE') {
                    $exerciseRecovery = 'confirmed-idle'
                }
                else {
                    throw "Unsafe recovery state: $($recoveryStatus.mode)."
                }
            }
            catch {
                throw "RECOVERY_REQUIRED: $($_.Exception.Message)"
            }
        }
        $exercisePassed =
            $exerciseModes.Count -eq 2 `
            -and $exerciseModes[0] -eq 'ACQUIRE' `
            -and $exerciseModes[1] -eq 'IDLE'
    }

    $pass =
        $childrenMatch `
        -and $valuesReadOnly `
        -and $patternsMatch `
        -and $actionAllowlistMatches `
        -and $stateUnchanged `
        -and $exercisePassed

    [ordered]@{
        pass = $pass
        process = $process
        agent_port = $AgentPort
        root_count = $rootMatches.Count
        root_children = $childIds
        actionable_elements = $actionableElements
        observed_action_signatures = $observedActionSignatures
        action_allowlist_matches = $actionAllowlistMatches
        os_window_patterns_acknowledged = $true
        acquisition = [ordered]@{
            value = $acquisitionValue.value.Current.Value
            value_read_only = $acquisitionValue.value.Current.IsReadOnly
            invoke_available = $acquisitionInvoke.available
            toggle_available = $acquisitionToggle.available
        }
        recording = [ordered]@{
            value = $recordingValue.value.Current.Value
            value_read_only = $recordingValue.value.Current.IsReadOnly
            invoke_available = $recordingInvoke.available
            toggle_available = $recordingToggle.available
        }
        read_iterations = $ReadIterations
        uia_provider_reads = $script:UiaProviderReadEvidence
        expected_mode = if ($ExpectInteractive) { 'interactive' } else { 'read-only' }
        exercise_transport = [bool]$ExerciseTransport
        approved_simulation_profile = $ApprovedSimulationProfile
        simulation_verified = $simulationVerified
        expected_configuration = $expectedConfigPath
        expected_configuration_sha256 = $ExpectedConfigurationSha256
        expected_state_directory = $expectedStatePath
        exercise_modes = $exerciseModes
        exercise_requests = $exerciseRequests
        exercise_recovery = $exerciseRecovery
        pre_mode = $preStatus.mode
        post_mode = $postStatus.mode
        pre_revision = $preStatus.revision
        post_revision = $postStatus.revision
    } | ConvertTo-Json -Depth 5

    if (-not $pass) {
        exit 1
    }
}
catch {
    [ordered]@{
        pass = $false
        process_id = $ProcessId
        agent_port = $AgentPort
        error = $_.Exception.Message
    } | ConvertTo-Json -Depth 3
    exit 1
}
