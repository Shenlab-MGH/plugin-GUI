[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)

if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell 7 session.'
}

& winget install `
    --source winget `
    --exact `
    --id Microsoft.VisualStudio.2022.BuildTools `
    --version 17.14.36 `
    --silent `
    --disable-interactivity `
    --accept-source-agreements `
    --accept-package-agreements `
    --override '--wait --quiet --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'

if ($LASTEXITCODE -notin 0, 3010) {
    throw "Visual Studio Build Tools installation failed with exit code $LASTEXITCODE"
}

if ($LASTEXITCODE -eq 3010) {
    Write-Warning 'Visual Studio Build Tools installed successfully; Windows restart is required.'
}
else {
    Write-Host 'Visual Studio Build Tools installed successfully.'
}
