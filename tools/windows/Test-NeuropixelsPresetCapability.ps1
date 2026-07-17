[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [int] $ProcessId,

    [Parameter(Mandatory)]
    [string] $PluginPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $PluginPath -PathType Leaf)) {
    throw "Neuropixels plugin does not exist: $PluginPath"
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$process = Get-Process -Id $ProcessId -ErrorAction Stop
if ($process.ProcessName -ne 'open-ephys') {
    throw 'ProcessId must identify open-ephys.'
}
if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
    throw 'Open Ephys does not expose a main window.'
}

$root = [System.Windows.Automation.AutomationElement]::FromHandle(
    $process.MainWindowHandle
)
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
$stack = [System.Collections.Generic.Stack[object]]::new()
$stack.Push($root)
$nodes = [System.Collections.Generic.List[object]]::new()
$presetTexts = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal
)

while ($stack.Count -gt 0 -and $nodes.Count -lt 5000) {
    $element = $stack.Pop()
    $current = $element.Current
    $value = $null
    $pattern = $null
    if ($element.TryGetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern,
        [ref]$pattern
    )) {
        $value = $pattern.Current.Value
    }

    foreach ($text in @($current.Name, $value)) {
        if ($text -and $text -match '^All Shanks [0-9]+-[0-9]+$') {
            [void]$presetTexts.Add($text)
        }
    }

    $nodes.Add([ordered]@{
        name = $current.Name
        automation_id = $current.AutomationId
        control_type = $current.ControlType.ProgrammaticName
        value = $value
        is_enabled = $current.IsEnabled
    })

    $child = $walker.GetFirstChild($element)
    while ($null -ne $child) {
        $stack.Push($child)
        $child = $walker.GetNextSibling($child)
    }
}

[ordered]@{
    schema_version = 'oe-agent-preset-capability/v0.0.1'
    observed_at = (Get-Date).ToUniversalTime().ToString('o')
    process_id = $process.Id
    executable = $process.Path
    plugin_path = (Resolve-Path -LiteralPath $PluginPath).Path
    plugin_sha256 = (
        Get-FileHash -LiteralPath $PluginPath -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    exact_displayed_preset_texts = @($presetTexts)
    semantic_set_available = $false
    documented_semantic_readback = $false
    accessibility_readback_available = ($presetTexts.Count -gt 0)
    accessibility_set_verified = $false
    classification = 'UNVERIFIABLE'
    uia_node_count = $nodes.Count
    uia_nodes = @($nodes)
} | ConvertTo-Json -Depth 8
