[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $PackageDirectory
)

$ErrorActionPreference = 'Stop'
$token = [Environment]::GetEnvironmentVariable(
    'OE_AGENT_TOKEN',
    [EnvironmentVariableTarget]::Process
)
if (-not $token -or $token.Length -lt 32) {
    throw 'TOKEN_TOO_SHORT: OE_AGENT_TOKEN must contain at least 32 characters.'
}

$baseUrl = [Environment]::GetEnvironmentVariable(
    'OE_AGENT_BASE_URL',
    [EnvironmentVariableTarget]::Process
)
if (-not $baseUrl) {
    $baseUrl = 'http://127.0.0.1:38498'
    $env:OE_AGENT_BASE_URL = $baseUrl
}
try {
    $uri = [uri]$baseUrl
}
catch {
    throw 'BASE_URL_INVALID: OE_AGENT_BASE_URL is invalid.'
}
if ($uri.Scheme -ne 'http' -or
    $uri.Host -notin @('127.0.0.1', '::1') -or
    $uri.Port -lt 1 -or
    $uri.AbsolutePath -ne '/') {
    throw 'BASE_URL_NOT_LITERAL_LOOPBACK: use loopback HTTP with an explicit port.'
}
if ($uri.Port -eq 37497) {
    throw 'NATIVE_HTTP_FORBIDDEN: legacy Open Ephys HTTP is forbidden.'
}

$package = [System.IO.Path]::GetFullPath($PackageDirectory)
$executablePath = Join-Path $package 'mcp\open-ephys-agent-mcp.exe'
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Packaged MCP executable is missing: $executablePath"
}

& $executablePath
exit $LASTEXITCODE
