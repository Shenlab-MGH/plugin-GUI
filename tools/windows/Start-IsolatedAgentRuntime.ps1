[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Executable,

    [Parameter(Mandatory)]
    [string] $StateDirectory,

    [string] $ConfigurationFile,

    [ValidateRange(1024, 65535)]
    [int] $AgentPort = 38498,

    [switch] $Launch,

    [switch] $MaintenanceWindowApproved
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string] $Path) {
    [System.IO.Path]::GetFullPath($Path)
}

function Test-IsWithin([string] $Candidate, [string] $Root) {
    $rootWithSeparator = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    return $Candidate.Equals(
        $Root,
        [System.StringComparison]::OrdinalIgnoreCase
    ) -or $Candidate.StartsWith(
        $rootWithSeparator,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function ConvertTo-ProcessArgument([string] $Value) {
    if ($Value.Contains('"')) {
        throw 'Runtime arguments must not contain quote characters.'
    }
    if ($Value -match '\s') {
        return '"' + $Value + '"'
    }
    return $Value
}

if (-not [System.IO.Path]::IsPathFullyQualified($Executable)) {
    throw 'Executable must be an absolute path.'
}
$executablePath = Get-FullPath $Executable
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Executable does not exist: $executablePath"
}

if (-not [System.IO.Path]::IsPathFullyQualified($StateDirectory)) {
    throw 'StateDirectory must be an absolute path.'
}
$statePath = Get-FullPath $StateDirectory
$stateRoot = [System.IO.Path]::GetPathRoot($statePath)
if ($statePath.TrimEnd('\') -eq $stateRoot.TrimEnd('\')) {
    throw 'StateDirectory must not be a filesystem root.'
}

$officialStateRoot = Get-FullPath (
    Join-Path $env:LOCALAPPDATA 'Open Ephys'
)
if (Test-IsWithin $statePath $officialStateRoot) {
    throw 'StateDirectory must be outside the official Open Ephys state root.'
}

$configPath = $null
if ($ConfigurationFile) {
    if (-not [System.IO.Path]::IsPathFullyQualified($ConfigurationFile)) {
        throw 'ConfigurationFile must be an absolute path.'
    }
    $configPath = Get-FullPath $ConfigurationFile
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "Configuration file does not exist: $configPath"
    }
}

if ($AgentPort -eq 37497) {
    throw 'AgentPort must not use the native Open Ephys port 37497.'
}

$tokenPresent = (
    $env:OE_AGENT_TOKEN -and
    $env:OE_AGENT_TOKEN.Length -ge 32
)
$running = @(
    Get-Process -Name 'open-ephys' -ErrorAction SilentlyContinue |
        Select-Object Id, Path, StartTime
)
$portInUse = [bool](
    Get-NetTCPConnection `
        -State Listen `
        -LocalPort $AgentPort `
        -ErrorAction SilentlyContinue
)
$nativePortInUse = [bool](
    Get-NetTCPConnection `
        -State Listen `
        -LocalPort 37497 `
        -ErrorAction SilentlyContinue
)

$arguments = @(
    '--state-dir', $statePath,
    '--no-http',
    '--agent-port', [string]$AgentPort,
    '--no-user-plugins',
    '--agent-uia-readonly'
)
if ($configPath) {
    $arguments += $configPath
}

$blockingReasons = [System.Collections.Generic.List[string]]::new()
if ($running.Count -gt 0) {
    $blockingReasons.Add('An Open Ephys process is already running.')
}
if (-not $tokenPresent) {
    $blockingReasons.Add(
        'OE_AGENT_TOKEN must contain at least 32 characters.'
    )
}
if ($portInUse) {
    $blockingReasons.Add("Agent port $AgentPort is already in use.")
}
if ($nativePortInUse) {
    $blockingReasons.Add(
        'Native Open Ephys port 37497 already has a listener.'
    )
}
if ($Launch -and -not $MaintenanceWindowApproved) {
    $blockingReasons.Add(
        'Launch requires -MaintenanceWindowApproved.'
    )
}

$result = [ordered]@{
    mode = if ($Launch) { 'launch' } else { 'inspect-only' }
    can_launch = ($blockingReasons.Count -eq 0)
    executable = $executablePath
    executable_sha256 = (
        Get-FileHash -LiteralPath $executablePath -Algorithm SHA256
    ).Hash
    state_directory = $statePath
    configuration_file = $configPath
    agent_port = $AgentPort
    native_http_locked_off = $true
    user_plugins_locked_off = $true
    agent_uia_readonly = $true
    token_present = $tokenPresent
    running_open_ephys = $running
    blocking_reasons = @($blockingReasons)
    arguments = $arguments
}

if (-not $Launch) {
    $result | ConvertTo-Json -Depth 6
    return
}

if ($blockingReasons.Count -gt 0) {
    $result | ConvertTo-Json -Depth 6
    throw 'Isolated runtime launch blocked by preflight.'
}

$launcherMutex = [System.Threading.Mutex]::new(
    $false,
    'Global\OpenEphysAgentV001Launcher'
)
if (-not $launcherMutex.WaitOne(0)) {
    throw 'Another Open Ephys Agent launcher is active.'
}

try {
$secondProcessCheck = @(
    Get-Process -Name 'open-ephys' -ErrorAction SilentlyContinue
)
if ($secondProcessCheck.Count -gt 0) {
    throw 'Open Ephys appeared after preflight; launch blocked.'
}

New-Item -ItemType Directory -Force -Path $statePath | Out-Null
$argumentLine = (
    $arguments |
        ForEach-Object { ConvertTo-ProcessArgument $_ }
) -join ' '
$process = Start-Process `
    -FilePath $executablePath `
    -ArgumentList $argumentLine `
    -PassThru

$result['process_id'] = $process.Id
$result['started_at'] = (Get-Date).ToString('o')
$result | ConvertTo-Json -Depth 6
}
finally {
    $launcherMutex.ReleaseMutex()
    $launcherMutex.Dispose()
}
