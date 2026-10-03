[CmdletBinding()]
param([ValidateRange(1,5)][int]$BenchmarkRuns=2)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $project 'build\lab\render_packets_test.exe'
if(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {throw 'Exit Skyrim normally for the isolated hardware benchmark.'}
$run=Join-Path $project ('measurements\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-owned-render-benchmark')
New-Item -ItemType Directory -Path $run | Out-Null
$manifest=[ordered]@{Status='running';EngineWorkOffloaded=$false;ExecutableSHA256=(Get-FileHash -LiteralPath $exe).Hash;StartedUTC=[DateTime]::UtcNow.ToString('o');Runs=@()}
$manifestPath=Join-Path $run 'manifest.json'
function Save-Manifest {$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8}
Save-Manifest
try {
    for($index=0;$index -le $BenchmarkRuns;$index++) {
        if(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {throw 'Skyrim started during isolated benchmark.'}
        $label=if($index -eq 0){'debug-correctness'}else{'benchmark-'+$index}
        $output=Join-Path $run ($label+'.json')
        $arguments=@{FilePath=$exe;WorkingDirectory=$project;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$output;RedirectStandardError=(Join-Path $run ($label+'.stderr.txt'))}
        if($index -gt 0) {$arguments.ArgumentList='--benchmark'}
        $target=Start-Process @arguments
        $target.WaitForExit();$code=$target.ExitCode;$target.Dispose()
        if($code -ne 0) {throw "Hardware run failed: $label (exit $code)."}
        $report=Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        if($report.skyrimEngineWorkOffloaded -ne $false -or $report.mismatches -ne 0) {throw 'Invalid report scope or image mismatches.'}
        if($index -eq 0) {
            if($report.kind -ne 'owned-render-packet-hardware-test' -or $report.validatedFullImages -ne 33 -or $report.debugStoredMessages -ne 0 -or $report.rows.Count -ne 24 -or -not $report.producerMutationTest -or -not $report.producerReleaseTest -or -not $report.immediateStateRestored) {throw 'Correctness report failed validation.'}
        } else {
            if($report.kind -ne 'owned-render-packet-end-to-end-benchmark' -or $report.debugLayerEnabled -ne $false -or $report.validatedFullImages -ne 360 -or $report.rows.Count -ne 8) {throw 'Benchmark report failed validation.'}
            $keys=@{}
            foreach($row in $report.rows) {
                $key=[string]$row.draws+'/'+[string]$row.workers
                if($keys.ContainsKey($key) -or $row.workers -notin @(1,2,4,6) -or $row.draws -notin @(1024,3072) -or $row.measuredPairs -ne 12 -or $row.serialFreezeUploadSubmitMedianMs -le 0 -or $row.parallelFreezeRecordReplayMedianMs -le 0 -or $row.serialAlreadyOwnedSubmitMedianMs -le 0 -or $row.mainSerialMedianCycles -le 0 -or $row.mainParallelMedianCycles -le 0 -or $row.workerMeasuredCycles -le 0) {throw "Invalid benchmark row $key"}
                $keys[$key]=$true
            }
        }
        $manifest.Runs+=,[ordered]@{Label=$label;Report=$output;ReportSHA256=(Get-FileHash -LiteralPath $output).Hash;ValidatedFullImages=$report.validatedFullImages}
        Save-Manifest
        Write-Output "$label passed: $($report.validatedFullImages) full-image checks."
    }
    $manifest.Status='complete';$manifest.CompletedUTC=[DateTime]::UtcNow.ToString('o');Save-Manifest
    Write-Output $run
} catch {$manifest.Status='failed';$manifest.Error=$_.Exception.Message;Save-Manifest;throw}
