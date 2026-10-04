[CmdletBinding()]
param([Parameter(Mandatory)][string]$RequestPath)
$ErrorActionPreference='Stop'
$request=Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\measurements')).TrimEnd('\')
$study=[IO.Path]::GetFullPath([string]$request.studyDirectory)
if(-not $study.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFullPath($RequestPath) -ne (Join-Path $study 'request.json')) {throw 'Study request escapes the measurement directory.'}
if($request.repeats -lt 1 -or $request.repeats -gt 5) {throw 'Invalid repetition count.'}
$metadata=[ordered]@{schemaVersion=1;kind='original-context-inventory-study';processId=$request.processId;processStartFileTime=$request.processStartFileTime;scene=$request.scene;status='arming';runs=@();startedAtUTC=[DateTime]::UtcNow.ToString('o')}
$state=Join-Path $study 'study-state.json'
function Save-Study { $metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $state -Encoding utf8 }
Save-Study
try {
    $game=Get-Process -Id $request.processId
    try {if($game.ProcessName -ne 'SkyrimSE' -or $game.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $request.processStartFileTime) {throw 'Game identity changed.'}}
    finally {$game.Dispose()}
    # Observed/clean/observed screens observer contamination separately from
    # clean/profile/clean triplets, which estimate profiling perturbation.
    $sequence=@('off','original-clean','off')
    for($repeat=0;$repeat -lt $request.repeats;$repeat++) {$sequence+=@('original-clean','profile-context','original-clean')}
    $sequence+=@('profile-context-full','original-clean')
    for($index=0;$index -lt $sequence.Count;$index++) {
        $mode=$sequence[$index];$every=16
        if($mode -eq 'profile-context-full') {$mode='profile-context';$every=1}
        $run=Join-Path $study ('{0:D2}-{1}-{2}' -f ($index+1),$mode,$every)
        $metadata.status='recording';$metadata.currentRun=$index+1;Save-Study
        if($mode -eq 'off') {
            & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode off | Out-Null
            $current=Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\measurements\live-render-bridge\current-session.json') -Raw | ConvertFrom-Json
            $watch=[Diagnostics.Stopwatch]::StartNew()
            do {Start-Sleep -Milliseconds 100;$observed=Get-Content -LiteralPath (Join-Path $current.sessionDirectory 'summary.json') -Raw | ConvertFrom-Json}
            while($watch.Elapsed.TotalSeconds -lt 2 -and ($observed.requestedMode -ne 'off' -or $observed.contextProfile.cleanForwarding))
            if($observed.requestedMode -ne 'off' -or $observed.contextProfile.cleanForwarding) {throw 'Observed original control not confirmed.'}
        }
        & (Join-Path $PSScriptRoot 'Capture-Baseline.ps1') -ProcessId $request.processId -Scene $request.scene -OutputDirectory $run -DurationSeconds 5 -FocusDelaySeconds $(if($index -eq 0){8}else{0}) -RequireUncapped -BridgeMode $mode -SampleEvery $every | Out-Null
        $metadata.runs+=@{directory=$run;mode=$mode;sampleEvery=$every};Save-Study
    }
    $metadata.status='captured'
} catch {$metadata.status='failed';$metadata.error=$_.Exception.Message}
finally {
    try { & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode original-clean | Out-Null }
    catch {$metadata.status='failed';$metadata.cleanupError=$_.Exception.Message}
    $metadata.finishedAtUTC=[DateTime]::UtcNow.ToString('o');Save-Study
}
if($metadata.status -ne 'captured') {throw "Study failed; see $state"}
