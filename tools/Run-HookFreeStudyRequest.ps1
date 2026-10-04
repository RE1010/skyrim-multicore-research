[CmdletBinding()]
param([Parameter(Mandatory)][string]$RequestPath)
$ErrorActionPreference='Stop'
$request=Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\measurements')).TrimEnd('\')
$study=[IO.Path]::GetFullPath([string]$request.studyDirectory)
if(-not $study.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFullPath($RequestPath) -ne (Join-Path $study 'request.json')) {throw 'Study request escapes project measurements.'}
if($request.repeats -lt 2 -or $request.repeats -gt 5) {throw 'Invalid repeat count.'}
$metadata=[ordered]@{schemaVersion=1;kind='project-hook-free-reference-study';status='arming';processId=$request.processId;processStartFileTime=$request.processStartFileTime;scene=$request.scene;runs=@();startedAtUTC=[DateTime]::UtcNow.ToString('o')}
$state=Join-Path $study 'study-state.json'
function Save-Study { $metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $state -Encoding utf8 }
Save-Study
try {
    $process=Get-Process -Id $request.processId
    try {if($process.ProcessName -ne 'SkyrimSE' -or $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $request.processStartFileTime) {throw 'Game identity changed.'}}
    finally {$process.Dispose()}
    for($index=0;$index -lt $request.repeats;$index++) {
        $run=Join-Path $study ('{0:D2}-project-hook-free' -f ($index+1))
        $metadata.status='recording';$metadata.currentRun=$index+1;Save-Study
        & (Join-Path $PSScriptRoot 'Capture-Baseline.ps1') -ProcessId $request.processId -Scene $request.scene -OutputDirectory $run -DurationSeconds 5 -FocusDelaySeconds $(if($index -eq 0){8}else{0}) -RequireUncapped -BridgeMode project-hook-free | Out-Null
        $metadata.runs+=@{directory=$run;mode='project-hook-free'};Save-Study
    }
    $metadata.status='captured'
} catch {$metadata.status='failed';$metadata.error=$_.Exception.Message}
finally {$metadata.finishedAtUTC=[DateTime]::UtcNow.ToString('o');Save-Study}
if($metadata.status -ne 'captured') {throw "Reference study failed; see $state"}
