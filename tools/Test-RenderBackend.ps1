[CmdletBinding()]
param([ValidateRange(1,10)][int]$ReleaseRuns=3)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $project 'build\lab\d3d11_parallel_lab.exe'
if(-not(Test-Path -LiteralPath $exe -PathType Leaf)) {throw 'Build the renderer before testing.'}
if(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {throw 'Close Skyrim normally for the isolated hardware test.'}
$run=Join-Path $project ('measurements\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-render-backend')
New-Item -ItemType Directory -Path $run | Out-Null
$manifest=[ordered]@{Status='running';Kind='standalone-render-backend';StartedUTC=[DateTime]::UtcNow.ToString('o');ExecutableSHA256=(Get-FileHash -LiteralPath $exe).Hash;SkyrimEngineWorkReplaced=$false;Runs=@()}
$manifestPath=Join-Path $run 'manifest.json'
function Save-Manifest {$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8}
Save-Manifest
try {
    foreach($index in 0..$ReleaseRuns) {
        if(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {throw 'Skyrim started during isolated testing; do not treat this run as an isolated benchmark.'}
        $debug=$index -eq 0
        $label=if($debug){'debug'}else{'release-'+$index}
        $output=Join-Path $run ($label+'.json')
        $errors=Join-Path $run ($label+'.stderr.txt')
        $arguments=@{FilePath=$exe;WorkingDirectory=$project;RedirectStandardOutput=$output;RedirectStandardError=$errors;PassThru=$true;WindowStyle='Hidden'}
        if($debug) {$arguments.ArgumentList='--debug'}
        $process=Start-Process @arguments
        $process.WaitForExit()
        $code=$process.ExitCode
        $process.Dispose()
        if($code -ne 0) {throw "Renderer failed ($label), exit $code. See $errors"}
        $report=Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        if($report.kind -ne 'standalone-d3d11-parallel-recording' -or $report.debugLayerEnabled -ne $debug -or $report.validatedImages -ne 372 -or $report.mismatches -ne 0 -or $report.rows.Count -ne 12) {throw "Invalid renderer report: $label"}
        if($debug -and $report.debugStoredMessages -ne 0) {throw 'Debug layer recorded messages.'}
        $keys=@{}
        foreach($row in $report.rows) {
            $key=[string]$row.workers+'/'+[string]$row.draws
            if($keys.ContainsKey($key) -or $row.workers -notin @(1,2,4,6) -or $row.draws -notin @(4096,16384,32768) -or @($row.workerThreadIds | Select-Object -Unique).Count -ne $row.workers -or $row.workerRecordingCyclesWholeCase -le 0 -or $row.measuredPairs -ne 12 -or $row.serialSubmitMedianMs -le 0 -or $row.parallelRecordAndReplayMedianMs -le 0) {throw "Invalid worker/timing data: $key"}
            $keys[$key]=$true
        }
        $manifest.Runs+=,[ordered]@{Label=$label;Debug=$debug;Report=$output;SHA256=(Get-FileHash -LiteralPath $output).Hash;ValidatedImages=$report.validatedImages;Mismatches=$report.mismatches;DebugStoredMessages=$report.debugStoredMessages}
        Save-Manifest
        Write-Output "$label passed: 372 image checks, 12 configurations."
    }
    $manifest.Status='complete'
    $manifest.CompletedUTC=[DateTime]::UtcNow.ToString('o')
    $manifest.TotalValidatedImages=372*($ReleaseRuns+1)
    Save-Manifest
    Write-Output "Reports: $run"
} catch {
    $manifest.Status='failed'
    $manifest.ErrorMessage=$_.Exception.Message
    Save-Manifest
    throw
}
