[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $SourceRoot,

    [Parameter(Mandatory)]
    [string] $OfficialZip,

    [Parameter(Mandatory)]
    [string] $OfficialReleaseRoot,

    [string] $LocalReleaseRoot,

    [string] $LocalBuildEvidencePath,

    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
$expectedCommit =
    'c91afebcfb0678a667fb93f6312ed33c56ec640f'
$expectedTree =
    '7f2541f24394ffdb204dc4326a2a86f1204beb43'
$expectedZipSha256 =
    '5A61946F051E88947C2D08A5E0AA75A8457EDE1DC3F54238C569D23A6C55E429'
$expectedPlugins = @(
    'ArduinoOutput.dll',
    'BandpassFilter.dll',
    'ChannelMap.dll',
    'CommonAvgRef.dll',
    'LfpViewer.dll',
    'PhaseDetector.dll',
    'RecordControl.dll',
    'SpikeDetector.dll',
    'SpikeViewer.dll'
)
$officialPackagingExtras = @(
    'FrontPanelUSB-DriverOnly-4.5.5.exe',
    'FTD3XXDriver_WHQLCertified_1.3.0.10_Installer.exe',
    'LICENSE',
    'msvcp140.dll',
    'vcruntime140.dll',
    'vcruntime140_1.dll'
)

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory)]
        [string] $Path,

        [Parameter(Mandatory)]
        [string] $Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-StreamSha256 {
    param([Parameter(Mandatory)][IO.Stream] $Stream)

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString(
            $sha256.ComputeHash($Stream)
        ).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
        $Stream.Dispose()
    }
}

function New-Gate {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [bool] $Passed,

        [Parameter(Mandatory)]
        [object] $Evidence
    )

    return [pscustomobject][ordered]@{
        name = $Name
        status = if ($Passed) { 'PASS' } else { 'FAIL' }
        evidence = $Evidence
    }
}

function Get-RelativeFiles {
    param([Parameter(Mandatory)][string] $Root)

    return @(
        Get-ChildItem -LiteralPath $Root -Recurse -File |
            ForEach-Object {
                [pscustomobject][ordered]@{
                    path = $_.FullName.Substring($Root.Length + 1).
                        Replace('\', '/')
                    bytes = $_.Length
                    sha256 = Get-Sha256 $_.FullName
                }
            } |
            Sort-Object path
    )
}

function Get-ZipManifest {
    param([Parameter(Mandatory)][string] $ZipPath)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $seen = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        $manifest = @()
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrEmpty($entry.Name)) {
                continue
            }

            $path = $entry.FullName.Replace('\', '/')
            $segments = @($path.Split('/'))
            if (
                $path.StartsWith('/') -or
                $path -match '^[A-Za-z]:' -or
                $segments -contains '..' -or
                -not $path.StartsWith(
                    'open-ephys/',
                    [StringComparison]::OrdinalIgnoreCase)
            ) {
                throw "Unsafe or unexpected ZIP entry: $path"
            }

            $relative = $path.Substring('open-ephys/'.Length)
            if (-not $seen.Add($relative)) {
                throw "Duplicate or case-colliding ZIP entry: $relative"
            }

            $manifest += [pscustomobject][ordered]@{
                path = $relative
                bytes = $entry.Length
                sha256 = Get-StreamSha256 $entry.Open()
            }
        }
        return @($manifest | Sort-Object path)
    }
    finally {
        $archive.Dispose()
    }
}

function Compare-SourceResource {
    param(
        [Parameter(Mandatory)]
        [string] $Source,

        [Parameter(Mandatory)]
        [string] $Published,

        [Parameter(Mandatory)]
        [string] $Kind
    )

    $publishedExists = Test-Path -LiteralPath $Published
    $sourceHash = Get-Sha256 $Source
    $publishedHash = if ($publishedExists) {
        Get-Sha256 $Published
    }
    else {
        $null
    }

    return [pscustomobject][ordered]@{
        kind = $Kind
        source = $Source
        published = $Published
        published_exists = $publishedExists
        source_sha256 = $sourceHash
        published_sha256 = $publishedHash
        hash_match = $publishedExists -and
            $sourceHash -eq $publishedHash
    }
}

function Get-PeEvidence {
    param([Parameter(Mandatory)][string] $Executable)

    $file = Get-Item -LiteralPath $Executable
    $signature = Get-AuthenticodeSignature -LiteralPath $Executable
    $stream = [IO.File]::OpenRead($Executable)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        $signatureValue = $reader.ReadUInt32()
        $machineValue = $reader.ReadUInt16()
    }
    finally {
        if ($reader) {
            $reader.Dispose()
        }
        else {
            $stream.Dispose()
        }
    }

    return [pscustomobject][ordered]@{
        path = $file.FullName
        bytes = $file.Length
        sha256 = Get-Sha256 $file.FullName
        file_version = $file.VersionInfo.FileVersion
        product_version = $file.VersionInfo.ProductVersion
        company_name = $file.VersionInfo.CompanyName
        product_name = $file.VersionInfo.ProductName
        file_description = $file.VersionInfo.FileDescription
        pe_signature = ('0x{0:X8}' -f $signatureValue)
        machine = switch ($machineValue) {
            0x8664 { 'AMD64' }
            0x014c { 'I386' }
            0xAA64 { 'ARM64' }
            default { 'UNKNOWN_0x{0:X4}' -f $machineValue }
        }
        authenticode_status = $signature.Status.ToString()
        signer_subject = if ($signature.SignerCertificate) {
            $signature.SignerCertificate.Subject
        }
        else {
            $null
        }
    }
}

$sourceRootResolved =
    Resolve-ExistingPath $SourceRoot 'Official source root'
$officialZipResolved =
    Resolve-ExistingPath $OfficialZip 'Official release ZIP'
$officialReleaseResolved =
    Resolve-ExistingPath $OfficialReleaseRoot 'Official release root'

$commit = (& git -C $sourceRootResolved rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to read the official source commit.'
}
$tree = (& git -C $sourceRootResolved rev-parse 'HEAD^{tree}').Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to read the official source tree.'
}
$tag = (& git -C $sourceRootResolved describe --tags --exact-match).Trim()
if ($LASTEXITCODE -ne 0) {
    $tag = $null
}
$statusLines = @(
    & git -C $sourceRootResolved status --porcelain=v1
)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the official source worktree.'
}
$sourceFileCount = @(
    & git -C $sourceRootResolved ls-tree -r --full-tree HEAD
).Count

$sourceGate = New-Gate `
    -Name 'source_provenance' `
    -Passed (
        $commit -eq $expectedCommit -and
        $tree -eq $expectedTree -and
        $tag -eq 'v1.0.2' -and
        $statusLines.Count -eq 0 -and
        $sourceFileCount -eq 3386
    ) `
    -Evidence ([ordered]@{
        commit = $commit
        expected_commit = $expectedCommit
        tree = $tree
        expected_tree = $expectedTree
        exact_tag = $tag
        tracked_file_count = $sourceFileCount
        worktree_status = $statusLines
        gitmodules_present = Test-Path (
            Join-Path $sourceRootResolved '.gitmodules')
    })

$zipHash = Get-Sha256 $officialZipResolved
$zipGate = New-Gate `
    -Name 'official_zip_integrity' `
    -Passed ($zipHash -eq $expectedZipSha256) `
    -Evidence ([ordered]@{
        path = $officialZipResolved
        bytes = (Get-Item $officialZipResolved).Length
        sha256 = $zipHash
        expected_sha256 = $expectedZipSha256
        source = 'Open Ephys Artifactory X-Checksum-Sha256'
    })

$zipManifest = Get-ZipManifest $officialZipResolved
$extractedManifest = Get-RelativeFiles $officialReleaseResolved
$zipByPath = @{}
foreach ($item in $zipManifest) {
    $zipByPath[$item.path.ToLowerInvariant()] = $item
}
$extractedByPath = @{}
foreach ($item in $extractedManifest) {
    $extractedByPath[$item.path.ToLowerInvariant()] = $item
}
$zipMissingFromExtraction = @(
    $zipManifest |
        Where-Object {
            -not $extractedByPath.ContainsKey(
                $_.path.ToLowerInvariant())
        } |
        Select-Object -ExpandProperty path
)
$extractionUnexpected = @(
    $extractedManifest |
        Where-Object {
            -not $zipByPath.ContainsKey(
                $_.path.ToLowerInvariant())
        } |
        Select-Object -ExpandProperty path
)
$zipExtractionMismatches = @(
    $zipManifest |
        Where-Object {
            $key = $_.path.ToLowerInvariant()
            if (-not $extractedByPath.ContainsKey($key)) {
                return $false
            }
            $extracted = $extractedByPath[$key]
            return (
                $_.path -cne $extracted.path -or
                $_.bytes -ne $extracted.bytes -or
                $_.sha256 -ne $extracted.sha256
            )
        } |
        ForEach-Object {
            $extracted = $extractedByPath[
                $_.path.ToLowerInvariant()]
            [pscustomobject][ordered]@{
                zip_path = $_.path
                extracted_path = $extracted.path
                zip_bytes = $_.bytes
                extracted_bytes = $extracted.bytes
                zip_sha256 = $_.sha256
                extracted_sha256 = $extracted.sha256
            }
        }
)
$zipExtractionGate = New-Gate `
    -Name 'official_zip_extraction_binding' `
    -Passed (
        $zipMissingFromExtraction.Count -eq 0 -and
        $extractionUnexpected.Count -eq 0 -and
        $zipExtractionMismatches.Count -eq 0
    ) `
    -Evidence ([ordered]@{
        zip_file_count = $zipManifest.Count
        extracted_file_count = $extractedManifest.Count
        missing_from_extraction = $zipMissingFromExtraction
        unexpected_in_extraction = $extractionUnexpected
        mismatches = $zipExtractionMismatches
    })

$officialExe = Join-Path $officialReleaseResolved 'open-ephys.exe'
$officialPe = Get-PeEvidence $officialExe
$peGate = New-Gate `
    -Name 'official_pe_identity' `
    -Passed (
        $officialPe.file_version -eq '1.0.2' -and
        $officialPe.product_version -eq '1.0.2' -and
        $officialPe.company_name -eq 'Open Ephys' -and
        $officialPe.product_name -eq 'open-ephys' -and
        $officialPe.file_description -eq 'open-ephys' -and
        $officialPe.pe_signature -eq '0x00004550' -and
        $officialPe.machine -eq 'AMD64'
    ) `
    -Evidence $officialPe

$resourceChecks = @()
$configRoot = Join-Path $sourceRootResolved 'Resources\Configs'
Get-ChildItem -LiteralPath $configRoot -Recurse -File |
    ForEach-Object {
        $relative = $_.FullName.Substring($configRoot.Length + 1)
        $resourceChecks += Compare-SourceResource `
            -Source $_.FullName `
            -Published (
                Join-Path $officialReleaseResolved (
                    'configs\' + $relative)) `
            -Kind 'config'
    }

$bitfileRoot = Join-Path $sourceRootResolved 'Resources\Bitfiles'
Get-ChildItem -LiteralPath $bitfileRoot -Filter '*.bit' -File |
    ForEach-Object {
        $resourceChecks += Compare-SourceResource `
            -Source $_.FullName `
            -Published (
                Join-Path $officialReleaseResolved (
                    'shared\' + $_.Name)) `
            -Kind 'bitfile'
    }

$fileReaderRoot =
    Join-Path $sourceRootResolved 'Resources\FileReader\resources'
Get-ChildItem -LiteralPath $fileReaderRoot -Recurse -File |
    ForEach-Object {
        $relative = $_.FullName.Substring($fileReaderRoot.Length + 1)
        $resourceChecks += Compare-SourceResource `
            -Source $_.FullName `
            -Published (
                Join-Path $officialReleaseResolved (
                    'resources\' + $relative)) `
            -Kind 'file_reader'
    }

$vendoredDllRoot =
    Join-Path $sourceRootResolved 'Resources\DLLs\Win64'
Get-ChildItem -LiteralPath $vendoredDllRoot -Filter '*.dll' -File |
    ForEach-Object {
        $resourceChecks += Compare-SourceResource `
            -Source $_.FullName `
            -Published (
                Join-Path $officialReleaseResolved (
                    'shared\' + $_.Name)) `
            -Kind 'vendored_dll'
    }

$resourceChecks += Compare-SourceResource `
    -Source (
        Join-Path $sourceRootResolved 'Resources\Icons\icon-small.png') `
    -Published (
        Join-Path $officialReleaseResolved 'icon-small.png') `
    -Kind 'icon'

$resourceMismatches = @(
    $resourceChecks | Where-Object { -not $_.hash_match }
)
$resourceGate = New-Gate `
    -Name 'published_static_resources' `
    -Passed ($resourceMismatches.Count -eq 0) `
    -Evidence ([ordered]@{
        checked = $resourceChecks.Count
        matched = @(
            $resourceChecks | Where-Object hash_match
        ).Count
        mismatches = $resourceMismatches
        by_kind = @(
            $resourceChecks |
                Group-Object kind |
                ForEach-Object {
                    [pscustomobject][ordered]@{
                        kind = $_.Name
                        checked = $_.Count
                        matched = @(
                            $_.Group | Where-Object hash_match
                        ).Count
                    }
                }
        )
    })

$publishedPlugins = @(
    Get-ChildItem `
        -LiteralPath (
            Join-Path $officialReleaseResolved 'plugins') `
        -Filter '*.dll' `
        -File |
        Sort-Object Name |
        Select-Object -ExpandProperty Name
)
$pluginDelta = @(
    Compare-Object $expectedPlugins $publishedPlugins
)
$pluginGate = New-Gate `
    -Name 'published_builtin_plugins' `
    -Passed ($pluginDelta.Count -eq 0) `
    -Evidence ([ordered]@{
        expected = $expectedPlugins
        observed = $publishedPlugins
        delta = $pluginDelta
    })

$gates = @(
    $sourceGate,
    $zipGate,
    $zipExtractionGate,
    $peGate,
    $resourceGate,
    $pluginGate
)

$localBuild = $null
if ($LocalReleaseRoot) {
    if (-not $LocalBuildEvidencePath) {
        throw (
            'LocalBuildEvidencePath is required with LocalReleaseRoot. ' +
            'Use build-result.json from Invoke-OfficialV102Build.ps1.'
        )
    }
    $localRootResolved =
        Resolve-ExistingPath $LocalReleaseRoot 'Local Release root'
    $localEvidenceResolved =
        Resolve-ExistingPath `
            $LocalBuildEvidencePath `
            'Local build evidence'
    if ($localRootResolved -eq $officialReleaseResolved) {
        throw (
            'LocalReleaseRoot must not be the extracted historical ' +
            'official release.'
        )
    }
    $expectedLocalRoot = (
        Join-Path $sourceRootResolved 'Build\Release'
    )
    if (
        -not $localRootResolved.Equals(
            $expectedLocalRoot,
            [StringComparison]::OrdinalIgnoreCase)
    ) {
        throw (
            'LocalReleaseRoot must be the Release directory inside the ' +
            'verified official source worktree.'
        )
    }
    $buildEvidence = Get-Content `
        -LiteralPath $localEvidenceResolved `
        -Raw |
        ConvertFrom-Json
    $localExe = Join-Path $localRootResolved 'open-ephys.exe'
    $localPe = Get-PeEvidence $localExe
    $localPlugins = @(
        Get-ChildItem `
            -LiteralPath (Join-Path $localRootResolved 'plugins') `
            -Filter '*.dll' `
            -File |
            Sort-Object Name |
            Select-Object -ExpandProperty Name
    )
    $localPluginDelta = @(
        Compare-Object $expectedPlugins $localPlugins
    )
    $localPluginPe = @(
        $localPlugins |
            ForEach-Object {
                Get-PeEvidence (
                    Join-Path (
                        Join-Path $localRootResolved 'plugins'
                    ) $_
                )
            }
    )
    $invalidLocalPluginPe = @(
        $localPluginPe |
            Where-Object {
                $_.machine -ne 'AMD64' -or
                $_.pe_signature -ne '0x00004550'
            }
    )
    $localResourceChecks = @(
        $resourceChecks |
            ForEach-Object {
                $relative = $_.published.Substring(
                    $officialReleaseResolved.Length + 1)
                Compare-SourceResource `
                    -Source $_.source `
                    -Published (
                        Join-Path $localRootResolved $relative) `
                    -Kind $_.kind
            }
    )
    $localResourceMismatches = @(
        $localResourceChecks |
            Where-Object { -not $_.hash_match }
    )
    $localExeHash = Get-Sha256 $localExe
    $localManifest = Get-RelativeFiles $localRootResolved
    $evidenceManifest = @($buildEvidence.release_manifest)
    $localByPath = @{}
    foreach ($item in $localManifest) {
        $localByPath[$item.path.ToLowerInvariant()] = $item
    }
    $evidenceByPath = @{}
    foreach ($item in $evidenceManifest) {
        $evidenceByPath[$item.path.ToLowerInvariant()] = $item
    }
    $manifestMissing = @(
        $evidenceManifest |
            Where-Object {
                -not $localByPath.ContainsKey(
                    $_.path.ToLowerInvariant())
            } |
            Select-Object -ExpandProperty path
    )
    $manifestUnexpected = @(
        $localManifest |
            Where-Object {
                -not $evidenceByPath.ContainsKey(
                    $_.path.ToLowerInvariant())
            } |
            Select-Object -ExpandProperty path
    )
    $manifestMismatches = @(
        $evidenceManifest |
            Where-Object {
                $key = $_.path.ToLowerInvariant()
                if (-not $localByPath.ContainsKey($key)) {
                    return $false
                }
                $actual = $localByPath[$key]
                return (
                    $_.path -cne $actual.path -or
                    $_.bytes -ne $actual.bytes -or
                    $_.sha256 -ne $actual.sha256
                )
            } |
            Select-Object path, bytes, sha256
    )
    $buildManifestMatches = (
        $manifestMissing.Count -eq 0 -and
        $manifestUnexpected.Count -eq 0 -and
        $manifestMismatches.Count -eq 0
    )
    $expectedLocalPaths = @(
        $extractedManifest |
            Where-Object {
                $officialPackagingExtras -notcontains $_.path
            } |
            Select-Object -ExpandProperty path
    )
    $localLayoutDelta = @(
        Compare-Object $expectedLocalPaths (
            $localManifest | Select-Object -ExpandProperty path
        )
    )
    $configureLogMatches = (
        Test-Path -LiteralPath $buildEvidence.configure_log
    ) -and (
        (Get-Sha256 $buildEvidence.configure_log) -eq
            $buildEvidence.configure_log_sha256
    )
    $buildLogMatches = (
        Test-Path -LiteralPath $buildEvidence.build_log
    ) -and (
        (Get-Sha256 $buildEvidence.build_log) -eq
            $buildEvidence.build_log_sha256
    )
    $cmakeCacheMatches = (
        Test-Path -LiteralPath $buildEvidence.cmake_cache
    ) -and (
        (Get-Sha256 $buildEvidence.cmake_cache) -eq
            $buildEvidence.cmake_cache_sha256
    )
    $buildEvidenceMatches = (
        $buildEvidence.status -eq 'BUILT' -and
        $buildEvidence.source_commit -eq $expectedCommit -and
        $buildEvidence.source_tree -eq $expectedTree -and
        $buildEvidence.source_tag -eq 'v1.0.2' -and
        $buildEvidence.source_root -eq $sourceRootResolved -and
        $buildEvidence.release_directory -eq $localRootResolved -and
        $buildEvidence.executable -eq $localExe -and
        $buildEvidence.executable_sha256 -eq $localExeHash -and
        $buildEvidence.generator -eq 'Visual Studio 17 2022' -and
        $buildEvidence.architecture -eq 'x64' -and
        $buildEvidence.configuration -eq 'Release' -and
        $buildEvidence.build_tests -eq 'OFF' -and
        $configureLogMatches -and
        $buildLogMatches -and
        $cmakeCacheMatches -and
        $buildManifestMatches
    )
    $localGate = New-Gate `
        -Name 'local_pristine_release' `
        -Passed (
            $localPe.file_version -eq '1.0.2' -and
            $localPe.product_version -eq '1.0.2' -and
            $localPe.company_name -eq 'Open Ephys' -and
            $localPe.product_name -eq 'open-ephys' -and
            $localPe.file_description -eq 'open-ephys' -and
            $localPe.machine -eq 'AMD64' -and
            $localPluginDelta.Count -eq 0 -and
            $invalidLocalPluginPe.Count -eq 0 -and
            $localResourceMismatches.Count -eq 0 -and
            $localLayoutDelta.Count -eq 0 -and
            $buildEvidenceMatches
        ) `
        -Evidence ([ordered]@{
            pe = $localPe
            build_evidence_path = $localEvidenceResolved
            build_evidence_matches = $buildEvidenceMatches
            configure_log_matches = $configureLogMatches
            build_log_matches = $buildLogMatches
            cmake_cache_matches = $cmakeCacheMatches
            build_manifest_matches = $buildManifestMatches
            build_manifest_missing = $manifestMissing
            build_manifest_unexpected = $manifestUnexpected
            build_manifest_mismatches = $manifestMismatches
            expected_release_layout_count = $expectedLocalPaths.Count
            observed_release_layout_count = $localManifest.Count
            release_layout_delta = $localLayoutDelta
            expected_plugins = $expectedPlugins
            observed_plugins = $localPlugins
            plugin_delta = $localPluginDelta
            invalid_plugin_pe = $invalidLocalPluginPe
            static_resources_checked = $localResourceChecks.Count
            static_resource_mismatches = $localResourceMismatches
            note = 'Binary hashes are recorded, not required to match the historical ZIP.'
        })
    $gates += $localGate
    $localBuild = $localGate.evidence
}

$officialManifest = $extractedManifest
$failedGates = @($gates | Where-Object status -eq 'FAIL')
$report = [ordered]@{
    schema_version = 'oe-official-baseline/v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    overall_status = if ($failedGates.Count -eq 0) {
        'PASS'
    }
    else {
        'FAIL'
    }
    scope = if ($LocalReleaseRoot) {
        'source+published-release+local-pristine-build'
    }
    else {
        'source+published-release'
    }
    gates = $gates
    official_release_manifest = [ordered]@{
        root = $officialReleaseResolved
        file_count = $officialManifest.Count
        total_bytes = (
            $officialManifest |
                Measure-Object bytes -Sum
        ).Sum
        files = $officialManifest
    }
    local_build = $localBuild
    limitations = @(
        'Historical and locally rebuilt PE hashes need not match because the compiler, SDK, PDB GUID, and timestamps can differ.',
        'PASS without LocalReleaseRoot proves source and published-package provenance, not local build reproducibility.',
        'Runtime, Source Sim, recording integrity, and hardware qualification are separate gates.'
    )
}

$json = $report | ConvertTo-Json -Depth 12
if ($OutputPath) {
    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory) {
        New-Item -ItemType Directory -Force -Path $outputDirectory |
            Out-Null
    }
    Set-Content `
        -LiteralPath $OutputPath `
        -Value $json `
        -Encoding utf8
}

$json
if ($failedGates.Count -gt 0) {
    exit 1
}
