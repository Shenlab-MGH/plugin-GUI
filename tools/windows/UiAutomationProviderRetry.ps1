function Invoke-UiaProviderRead {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [scriptblock] $Operation,

        [Parameter(Mandatory)]
        [string] $OperationName,

        [ValidateRange(1, 60000)]
        [int] $TimeoutMilliseconds = 10000,

        [ValidateRange(1, 1000)]
        [int] $PollMilliseconds = 100
    )

    $rpcServerFault = -2147417851 # 0x80010105 RPC_E_SERVERFAULT
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $attempts = 0
    $transientFailures = 0

    while ($true) {
        ++$attempts
        try {
            $value = & $Operation
            $clock.Stop()
            return [pscustomobject]@{
                value = $value
                attempts = $attempts
                transient_failures = $transientFailures
                elapsed_milliseconds = $clock.ElapsedMilliseconds
            }
        }
        catch {
            $providerException = $_.Exception
            while ($null -ne $providerException.InnerException) {
                $providerException = $providerException.InnerException
            }
            if ($providerException.HResult -ne $rpcServerFault) {
                throw
            }

            ++$transientFailures
            if ($clock.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
                throw [System.TimeoutException]::new(
                    "UI Automation operation '$OperationName' timed out after " +
                    "$transientFailures transient RPC_E_SERVERFAULT response(s).",
                    $_.Exception)
            }

            $remaining = $TimeoutMilliseconds - $clock.ElapsedMilliseconds
            Start-Sleep -Milliseconds ([Math]::Min($PollMilliseconds, $remaining))
        }
    }
}
