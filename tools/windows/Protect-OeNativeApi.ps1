[CmdletBinding(SupportsShouldProcess)]
param(
    [switch] $Apply,
    [string] $Program =
        'C:\Program Files\Open Ephys\open-ephys.exe',
    [int] $Port = 37497
)

$ErrorActionPreference = 'Stop'
$ruleName = 'Open Ephys native API block inbound'

$listeners = @(
    Get-NetTCPConnection `
        -State Listen `
        -LocalPort $Port `
        -ErrorAction SilentlyContinue |
        Select-Object LocalAddress, LocalPort, OwningProcess
)
$applicationFilters = @(
    Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
        Where-Object { $_.Program -ieq $Program }
)
$allowRules = @(
    foreach ($filter in $applicationFilters) {
        Get-NetFirewallRule `
            -AssociatedNetFirewallApplicationFilter $filter `
            -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Enabled -eq 'True' -and
                $_.Direction -eq 'Inbound' -and
                $_.Action -eq 'Allow'
            }
    }
)
$blockRule = Get-NetFirewallRule `
    -DisplayName $ruleName `
    -ErrorAction SilentlyContinue

[ordered]@{
    program = $Program
    port = $Port
    listeners = $listeners
    inbound_allow_rules = @(
        $allowRules |
            Select-Object DisplayName, Enabled, Profile, Direction, Action
    )
    blocking_rule_present = [bool] $blockRule
    apply_requested = [bool] $Apply
} | ConvertTo-Json -Depth 6

if (-not $Apply) {
    Write-Host 'Inspection only. No firewall state was changed.'
    return
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )) {
    throw 'Run this script from an elevated PowerShell session.'
}

if (-not (Test-Path -LiteralPath $Program)) {
    throw "Open Ephys executable not found: $Program"
}

if (-not $blockRule -and $PSCmdlet.ShouldProcess(
        "$Program TCP/$Port",
        'Block inbound native Open Ephys API access on all profiles'
    )) {
    New-NetFirewallRule `
        -DisplayName $ruleName `
        -Direction Inbound `
        -Action Block `
        -Protocol TCP `
        -LocalPort $Port `
        -Program $Program `
        -Profile Any |
        Out-Null
}

$verified = Get-NetFirewallRule `
    -DisplayName $ruleName `
    -ErrorAction Stop
if ($verified.Enabled -ne 'True' -or
    $verified.Direction -ne 'Inbound' -or
    $verified.Action -ne 'Block') {
    throw 'Firewall block rule verification failed.'
}

Write-Host 'Verified inbound block rule for the native Open Ephys API.'
