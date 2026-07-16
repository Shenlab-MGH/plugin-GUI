[CmdletBinding()]
param(
    [string] $DownloadDirectory =
        'D:\cong\tool-cache\vs-buildtools-17.14.36'
)

$ErrorActionPreference = 'Stop'
$packageId = 'Microsoft.VisualStudio.2022.BuildTools'
$packageVersion = '17.14.36'
$installerName =
    'Visual Studio BuildTools 2022_17.14.36_Machine_X64_exe_en-US.exe'
$installerSha256 =
    '5AE95BB02BB3442441A8D891E5BB1D2975445E2E3EE16ADA5BC7BD17227F1DD7'

New-Item -ItemType Directory -Force -Path $DownloadDirectory |
    Out-Null

$installerPath = Join-Path $DownloadDirectory $installerName
$installer = Get-Item `
    -LiteralPath $installerPath `
    -ErrorAction SilentlyContinue

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

    $installer = Get-Item `
        -LiteralPath $installerPath `
        -ErrorAction SilentlyContinue
}

if (-not $installer) {
    throw "Build Tools installer was not found in $DownloadDirectory"
}

$signature = Get-AuthenticodeSignature -LiteralPath $installer.FullName
if ($signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
    throw 'Build Tools installer does not have a valid Microsoft signature.'
}
$observedInstallerSha256 = (
    Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256
).Hash
if ($observedInstallerSha256 -ne $installerSha256) {
    throw (
        'Build Tools installer SHA-256 does not match the pinned ' +
        "$packageVersion package."
    )
}
if (
    $installer.VersionInfo.ProductName -ne
        'Microsoft Visual Studio BuildTools' -or
    $installer.VersionInfo.FileVersion -ne '17.14.37502.11'
) {
    throw 'Build Tools installer product identity is not the pinned release.'
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

if ($isAdministrator) {
    $process = Start-Process `
        -FilePath $installer.FullName `
        -ArgumentList $installArguments `
        -Wait `
        -PassThru
}
else {
    Write-Host 'Requesting Build Tools installation through Windows UAC.'
    $process = Start-Process `
        -FilePath $installer.FullName `
        -Verb RunAs `
        -ArgumentList $installArguments `
        -Wait `
        -PassThru
}
$installerExitCode = $process.ExitCode

if ($installerExitCode -notin 0, 3010) {
    throw (
        'Visual Studio Build Tools bootstrapper failed with exit code ' +
        "$installerExitCode"
    )
}

$vswhere =
    'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$deadline = [DateTime]::UtcNow.AddMinutes(10)
$installationPath = ''
$installation = $null
do {
    if (Test-Path -LiteralPath $vswhere) {
        $vswhereOutput = @(
            & $vswhere `
                -latest `
                -products Microsoft.VisualStudio.Product.BuildTools `
                -version '[17.14,17.15)' `
                -requires `
                    Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                    Microsoft.VisualStudio.Component.Windows11SDK.26100 `
                -format json
        )
        if ($LASTEXITCODE -ne 0) {
            throw "vswhere failed with exit code $LASTEXITCODE"
        }
        $vswhereJson = ($vswhereOutput -join [Environment]::NewLine).Trim()
        if ($vswhereJson) {
            $matches = @($vswhereJson | ConvertFrom-Json)
            if ($matches.Count -gt 0) {
                $installation = $matches[0]
                $installationPath = [string] $installation.installationPath
            }
        }
    }

    if (
        $installationPath -and
        $installation.productId -eq
            'Microsoft.VisualStudio.Product.BuildTools' -and
        $installation.catalog.productDisplayVersion -eq $packageVersion -and
        $installation.isComplete -eq $true -and
        $installation.isLaunchable -eq $true
    ) {
        break
    }
    $installationPath = ''
    $installation = $null
    Start-Sleep -Seconds 5
}
while ([DateTime]::UtcNow -lt $deadline)

if (-not $installationPath) {
    throw (
        'The bootstrapper returned, but the Visual Studio C++ workload ' +
        'was not registered within 10 minutes. Inspect the latest ' +
        'dd_setup_*.log in the user TEMP directory.'
    )
}

$msbuild = Join-Path `
    $installationPath `
    'MSBuild\Current\Bin\MSBuild.exe'
$msvcRoot = Join-Path $installationPath 'VC\Tools\MSVC'
$windowsSdkRoot =
    'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0'

if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not installed: $msbuild"
}
if (-not (Test-Path -LiteralPath $msvcRoot)) {
    throw "MSVC tools were not installed: $msvcRoot"
}
$compiler = Get-ChildItem -LiteralPath $msvcRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object {
        Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe'
    } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $compiler) {
    throw "The x64 MSVC compiler was not installed under: $msvcRoot"
}
if (-not (Test-Path -LiteralPath $windowsSdkRoot)) {
    throw "Windows SDK was not installed: $windowsSdkRoot"
}

if ($installerExitCode -eq 3010) {
    Write-Warning (
        'Visual Studio Build Tools installed and verified; Windows ' +
        'restart is required.'
    )
}
else {
    Write-Host (
        'Visual Studio Build Tools C++ workload installed and verified: ' +
        $installationPath
    )
}
