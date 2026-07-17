[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$evidenceRoot = Join-Path $repoRoot 'docs\agent\evidence'
$defaultOutput = Join-Path $evidenceRoot 'channel-c-logs'

function Resolve-PathWithinEvidenceRoot {
    param([string]$Candidate)

    $root = [IO.Path]::GetFullPath($evidenceRoot).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Candidate).TrimEnd('\')
    if ($full -ne $root -and -not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputDirectory must stay within docs\agent\evidence: $full"
    }
    return $full
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $defaultOutput
}
$safeOutput = Resolve-PathWithinEvidenceRoot $OutputDirectory
if ($ValidateOnly) {
    Write-Output $safeOutput
    exit 0
}

[IO.Directory]::CreateDirectory($safeOutput) | Out-Null
$summaryPath = Join-Path $safeOutput 'agentchecks-summary.json'
if (Test-Path -LiteralPath $summaryPath) {
    throw "Refusing to overwrite existing evidence: $summaryPath"
}

$sourceCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$rounds = @()
$anyFailure = $false
for ($round = 1; $round -le 3; $round++) {
    $logPath = Join-Path $safeOutput ("agentchecks-round-{0}.log" -f $round)
    if (Test-Path -LiteralPath $logPath) {
        throw "Refusing to overwrite existing evidence: $logPath"
    }
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'Invoke-AgentChecks.ps1') *> $logPath
    $exitCode = $LASTEXITCODE
    $stopwatch.Stop()
    $text = Get-Content -Raw -LiteralPath $logPath
    $pytestMatches = [regex]::Matches($text, '(?m)(\d+) passed')
    $pytestPassed = ($pytestMatches | ForEach-Object { [int]$_.Groups[1].Value } |
        Measure-Object -Sum).Sum
    $lines = @(Get-Content -LiteralPath $logPath)
    $finalLine = if ($lines.Count -gt 0) { $lines[-1] } else { '' }
    $entry = [ordered]@{
        Round = $round
        ExitCode = $exitCode
        ElapsedSeconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        PytestPassed = $pytestPassed
        ExplicitPassLines = [regex]::Matches($text, '(?m)^PASS\b').Count
        FinalLine = $finalLine
        LogFile = [IO.Path]::GetRelativePath($repoRoot, $logPath).Replace('\', '/')
        LogSha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $rounds += [pscustomobject]$entry
    if ($exitCode -ne 0) {
        $anyFailure = $true
    }
}

$summary = [ordered]@{
    Schema = 'open-ephys-agent/channel-c-agentchecks/v1'
    SourceCommit = $sourceCommit
    Rounds = $rounds
    Decision = if ($anyFailure) { 'FAIL' } else { 'PASS' }
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
$summary | ConvertTo-Json -Depth 6
if ($anyFailure) { exit 1 }
exit 0
