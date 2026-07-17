[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $PackageDirectory,

    [Parameter(Mandatory)]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$package = [System.IO.Path]::GetFullPath($PackageDirectory)
$launcher = Join-Path $package 'Start-OpenEphysAgentMcp.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "MCP launcher is missing: $launcher"
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null

$tomlLauncher = $launcher.Replace('\', '\\').Replace('"', '\"')
$tomlPackage = $package.Replace('\', '\\').Replace('"', '\"')
$codex = @"
[mcp_servers.open-ephys-agent]
command = "pwsh"
args = ["-NoProfile", "-File", "$tomlLauncher", "-PackageDirectory", "$tomlPackage"]
env_vars = ["OE_AGENT_TOKEN", "OE_AGENT_BASE_URL"]
required = true
enabled_tools = ["oe_get_identity", "oe_get_capabilities", "oe_get_runtime_status", "oe_run_readonly_preflight"]
default_tools_approval_mode = "auto"
"@

$jsonLauncher = ConvertTo-Json $launcher -Compress
$jsonPackage = ConvertTo-Json $package -Compress
$claude = @"
{
  "mcpServers": {
    "open-ephys-agent": {
      "type": "stdio",
      "command": "pwsh",
      "args": ["-NoProfile", "-File", $jsonLauncher, "-PackageDirectory", $jsonPackage]
    }
  },
  "enabledTools": ["oe_get_identity", "oe_get_capabilities", "oe_get_runtime_status", "oe_run_readonly_preflight"]
}
"@

$codexPath = Join-Path $output 'codex-open-ephys-agent.toml'
$claudePath = Join-Path $output 'claude-open-ephys-agent.json'
Set-Content -LiteralPath $codexPath -Value $codex -Encoding utf8
Set-Content -LiteralPath $claudePath -Value $claude -Encoding utf8

[ordered]@{
    codex = $codexPath
    claude = $claudePath
    global_configuration_modified = $false
    token_persisted = $false
} | ConvertTo-Json
