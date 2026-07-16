[CmdletBinding()]
param(
    [string] $DownloadDirectory =
        'D:\cong\tool-cache\vs-buildtools-17.14.36'
)

$ErrorActionPreference = 'Stop'
$packageId = 'Microsoft.VisualStudio.2022.BuildTools'
$packageVersion = '17.14.36'

New-Item -ItemType Directory -Force -Path $DownloadDirectory |
    Out-Null

$installer = Get-ChildItem -LiteralPath $DownloadDirectory `
    -Filter '*.exe' `
    -File `
    -ErrorAction SilentlyContinue |
    Where-Object Name -Like 'Visual Studio BuildTools*' |
    Select-Object -First 1

if (-not $installer) {
    & winget download `
        --source winget `
        --exact `
        --id $packageId `
        --version $packageVersion `
        --download-directory $DownloadDirectory `
        --accept-source-agreements `
        --accept-package-agreements

    if ($LASTEXITCODE -ne 0) {
        throw "Build Tools download failed with exit code $LASTEXITCODE"
    }

    $installer = Get-ChildItem -LiteralPath $DownloadDirectory `
        -Filter '*.exe' `
        -File |
        Where-Object Name -Like 'Visual Studio BuildTools*' |
        Select-Object -First 1
}

if (-not $installer) {
    throw "Build Tools installer was not found in $DownloadDirectory"
}

$signature = Get-AuthenticodeSignature -LiteralPath $installer.FullName
if ($signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
    throw 'Build Tools installer does not have a valid Microsoft signature.'
}

$installArguments = @(
    '--wait',
    '--quiet',
    '--norestart',
    '--nocache',
    '--add',
    'Microsoft.VisualStudio.Workload.VCTools',
    '--includeRecommended'
)

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)
$isAdministrator = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

if (-not $isAdministrator) {
    Start-Process `
        -FilePath $installer.FullName `
        -Verb RunAs `
        -ArgumentList $installArguments
    Write-Host 'Build Tools installation requested through Windows UAC.'
    return
}

& $installer.FullName @installArguments

if ($LASTEXITCODE -notin 0, 3010) {
    throw "Visual Studio Build Tools installation failed with exit code $LASTEXITCODE"
}

if ($LASTEXITCODE -eq 3010) {
    Write-Warning 'Visual Studio Build Tools installed successfully; Windows restart is required.'
}
else {
    Write-Host 'Visual Studio Build Tools installed successfully.'
}
