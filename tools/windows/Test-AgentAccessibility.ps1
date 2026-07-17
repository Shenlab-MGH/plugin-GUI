[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [int] $ProcessId,

    [int] $AgentPort = 38498
)

$ErrorActionPreference = 'Stop'
$ReadIterations = 10000

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

function Get-ExactElements {
    param(
        [Parameter(Mandatory)]
        [int] $OwnerProcessId,

        [Parameter(Mandatory)]
        [string] $AutomationId
    )

    $processCondition = New-Object `
        System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
            $OwnerProcessId)
    $elements = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        $processCondition)

    @(
        $elements | Where-Object {
            $_.GetCurrentPropertyValue(
                [System.Windows.Automation.AutomationElement]::AutomationIdProperty
            ) -eq $AutomationId
        }
    )
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

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $token = [Environment]::GetEnvironmentVariable(
        'OE_AGENT_TOKEN',
        'Process')
    if (-not $token -or $token.Length -lt 32) {
        throw 'OE_AGENT_TOKEN must contain at least 32 characters.'
    }

    $process = Get-Process -Id $ProcessId -ErrorAction Stop |
        Select-Object Id, ProcessName, Path
    if ($process.ProcessName -ne 'open-ephys') {
        throw 'ProcessId does not identify open-ephys.'
    }

    $headers = @{ Authorization = "Bearer $token" }
    $preStatus = Get-NativeStatus -Port $AgentPort -Headers $headers

    $deadline = (Get-Date).AddSeconds(10)
    do {
        $rootMatches = Get-ExactElements `
            -OwnerProcessId $ProcessId `
            -AutomationId "oe.agent.root"
        $acquisitionMatches = Get-ExactElements `
            -OwnerProcessId $ProcessId `
            -AutomationId "oe.transport.acquisition"
        $recordingMatches = Get-ExactElements `
            -OwnerProcessId $ProcessId `
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

    $pass =
        $childrenMatch `
        -and $valuesReadOnly `
        -and $actionsAbsent `
        -and $stateUnchanged

    [ordered]@{
        pass = $pass
        process = $process
        agent_port = $AgentPort
        root_count = $rootMatches.Count
        root_children = $childIds
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
