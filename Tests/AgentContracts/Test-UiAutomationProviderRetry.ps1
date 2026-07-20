$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helper = Join-Path $repoRoot 'tools\windows\UiAutomationProviderRetry.ps1'

if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) {
    throw "Missing UI Automation provider retry helper: $helper"
}

. $helper

$script:transientAttempts = 0
$eventual = Invoke-UiaProviderRead -OperationName 'transient-test' `
    -TimeoutMilliseconds 1000 -PollMilliseconds 1 -Operation {
        ++$script:transientAttempts
        if ($script:transientAttempts -lt 3) {
            throw [System.Runtime.InteropServices.COMException]::new(
                'transient UIA provider fault', -2147417851)
        }
        'ready'
    }
if ($eventual.value -ne 'ready' -or $eventual.attempts -ne 3 `
    -or $eventual.transient_failures -ne 2) {
    throw 'Transient UIA provider faults were not retried exactly as required.'
}

$script:permanentAttempts = 0
$permanentFailed = $false
try {
    $null = Invoke-UiaProviderRead -OperationName 'permanent-test' `
        -TimeoutMilliseconds 1000 -PollMilliseconds 1 -Operation {
            ++$script:permanentAttempts
            throw [System.Runtime.InteropServices.COMException]::new(
                'non-transient COM fault', -2147024891)
        }
}
catch {
    $permanentFailed = $true
}
if (-not $permanentFailed -or $script:permanentAttempts -ne 1) {
    throw 'A non-transient COM fault must fail immediately without retry.'
}

$script:timeoutAttempts = 0
$timeoutFailed = $false
try {
    $null = Invoke-UiaProviderRead -OperationName 'timeout-test' `
        -TimeoutMilliseconds 20 -PollMilliseconds 1 -Operation {
            ++$script:timeoutAttempts
            throw [System.Runtime.InteropServices.COMException]::new(
                'persistent transient UIA provider fault', -2147417851)
        }
}
catch {
    $timeoutFailed = $_.Exception.Message -match 'timeout-test' `
        -and $_.Exception.Message -match 'timed out'
}
if (-not $timeoutFailed -or $script:timeoutAttempts -lt 2) {
    throw 'Persistent UIA provider faults must retry and then fail on timeout.'
}

Write-Host 'PASS bounded UI Automation provider retry tests'
