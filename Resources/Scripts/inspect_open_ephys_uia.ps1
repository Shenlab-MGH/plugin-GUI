[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int] $TargetProcessId,

    [string] $WindowTitle = 'Open Ephys GUI',

    [string] $ExpectedMainWindowAutomationId,

    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$processCondition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
    $TargetProcessId)
$nameCondition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::NameProperty,
    $WindowTitle)
$windowCondition = [System.Windows.Automation.AndCondition]::new(
    $processCondition,
    $nameCondition)

$window = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
    [System.Windows.Automation.TreeScope]::Children,
    $windowCondition)

if ($null -eq $window) {
    throw "Open Ephys window '$WindowTitle' was not found for process $TargetProcessId."
}

$elements = @($window)
$elements += @($window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition))

$records = foreach ($element in $elements) {
    try {
        $controlType = $element.Current.ControlType.ProgrammaticName -replace '^ControlType\.', ''
        $patterns = @($element.GetSupportedPatterns() | ForEach-Object {
                $_.ProgrammaticName -replace '^Pattern\.', ''
            })

        [ordered]@{
            name          = $element.Current.Name
            automation_id = $element.Current.AutomationId
            control_type  = $controlType
            enabled       = $element.Current.IsEnabled
            offscreen     = $element.Current.IsOffscreen
            focusable     = $element.Current.IsKeyboardFocusable
            focused       = $element.Current.HasKeyboardFocus
            patterns      = $patterns
        }
    }
    catch [System.Windows.Automation.ElementNotAvailableException] {
        continue
    }
}

if ($records.Count -eq 0) {
    throw 'The Open Ephys UIA tree was empty.'
}

if ($ExpectedMainWindowAutomationId) {
    $actualId = $records[0].automation_id
    if ($actualId -ne $ExpectedMainWindowAutomationId) {
        throw "Expected main-window AutomationId '$ExpectedMainWindowAutomationId', found '$actualId'."
    }
}

$json = $records | ConvertTo-Json -Depth 5

if ($OutputPath) {
    $resolvedOutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    $parentDirectory = Split-Path -Parent $resolvedOutputPath

    if ($parentDirectory -and -not (Test-Path -LiteralPath $parentDirectory)) {
        New-Item -ItemType Directory -Path $parentDirectory | Out-Null
    }

    Set-Content -LiteralPath $resolvedOutputPath -Value $json -Encoding utf8
}

$json
