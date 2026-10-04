[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('off','inline','serial','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small')][string]$Mode,
    [ValidateRange(5,30)][int]$DurationSeconds=10,
    [ValidateRange(0,15)][int]$FocusDelaySeconds=5,
    [ValidateRange(5,60)][int]$ReadyTimeoutSeconds=30
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
function Read-Json([string]$Path) {Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json}
$identity=Read-Json (Join-Path $project 'measurements\live-render-bridge\current-session.json')
$uncapIdentity=Read-Json (Join-Path $project 'measurements\live-uncap\current-session.json')
if($identity.processId -ne $uncapIdentity.processId -or $identity.processStartFileTime -ne $uncapIdentity.processStartFileTime) {throw 'Bridge/uncapping process identities differ.'}
$game=Get-Process -Id $identity.processId
if($game.ProcessName -ne 'SkyrimSE' -or $game.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $identity.processStartFileTime) {throw 'Stale game identity.'}
$bridgeRoot=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-render-bridge')).TrimEnd('\')
$uncapRoot=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-uncap')).TrimEnd('\')
if(-not [IO.Path]::GetFullPath($identity.sessionDirectory).StartsWith($bridgeRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or
   -not [IO.Path]::GetFullPath($uncapIdentity.sessionDirectory).StartsWith($uncapRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Session path outside expected output.'}
$bridgePath=Join-Path $identity.sessionDirectory 'summary.json'
$uncapPath=Join-Path $uncapIdentity.sessionDirectory 'summary.json'
Start-Sleep -Seconds $FocusDelaySeconds
$readyWatch=[Diagnostics.Stopwatch]::StartNew()
$previousBridge=Read-Json $bridgePath;$previousUncap=Read-Json $uncapPath
while($true) {
    Start-Sleep -Milliseconds 1000
    $candidateBridge=Read-Json $bridgePath;$candidateUncap=Read-Json $uncapPath
    if($candidateBridge.processId -ne $identity.processId -or $candidateUncap.processId -ne $identity.processId) {throw 'Process identity changed while waiting for rendering.'}
    if($candidateBridge.worldLoaded -and $candidateUncap.uncappedActive -and
       $candidateBridge.renderPassCalls -gt $previousBridge.renderPassCalls -and $candidateUncap.presents -gt $previousUncap.presents) {break}
    if($readyWatch.Elapsed.TotalSeconds -ge $ReadyTimeoutSeconds) {throw 'No active world rendering observed. Activate Skyrim and close menus/console before the next test.'}
    $previousBridge=$candidateBridge;$previousUncap=$candidateUncap
}
$before=Read-Json $bridgePath
if(-not $before.worldLoaded -or -not $before.contextAttached -or $before.initializationError -or $before.offloadDisabled -or $before.errors -ne 0 -or $before.requestedMode -ne 'off' -or $before.qpcFrequency -le 0) {throw 'Clean, loaded original path with phase timers required.'}
$run=Join-Path $project ('measurements\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-bridge-phase-'+$Mode)
New-Item -ItemType Directory -Path $run | Out-Null
$identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'process-identity.json') -Encoding utf8
$history=@()
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
    & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode $Mode -DurationSeconds $DurationSeconds
    while($watch.Elapsed.TotalSeconds -lt $DurationSeconds+3) {
        Start-Sleep -Milliseconds 500
        $bridge=Read-Json $bridgePath
        $uncap=Read-Json $uncapPath
        if($bridge.processId -ne $identity.processId -or $uncap.processId -ne $identity.processId) {throw 'Process identity changed during capture.'}
        $history+=,[ordered]@{elapsedSeconds=$watch.Elapsed.TotalSeconds;utc=(Get-Date).ToUniversalTime().ToString('o');bridge=$bridge;uncap=$uncap}
    }
} finally {
    $history | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'history.json') -Encoding utf8
    & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode off
}
$valid=@($history | Where-Object {$_.bridge.requestedMode -eq $Mode -and $_.bridge.worldLoaded -and $_.uncap.worldLoaded -and $_.uncap.uncappedActive})
if($valid.Count -lt 4) {throw "Insufficient uncapped observations for $Mode; raw history saved in $run"}
$first=$valid[0];$last=$valid[-1]
if($last.uncap.presents-$first.uncap.presents -lt 10 -or $last.bridge.renderPassCalls -le $first.bridge.renderPassCalls) {throw "No live frame/render growth; capture rejected. Raw history: $run"}
$seconds=$last.elapsedSeconds-$first.elapsedSeconds
$frequency=$first.bridge.qpcFrequency
if($frequency -ne $last.bridge.qpcFrequency -or $seconds -le 0 -or $last.bridge.errors -ne $first.bridge.errors) {throw 'Invalid timer/error interval.'}
$timings=[ordered]@{}
foreach($field in @('captureTicks','uploadCopyTicks','serialRecordTicks','workerWaitTicks','executeTicks')) {
    $delta=[long]$last.bridge.$field-[long]$first.bridge.$field
    if($delta -lt 0) {throw "Counter reset: $field"}
    $timings[$field]=[math]::Round($delta*1000.0/$frequency,3)
}
$workerTimings=@()
for($i=0;$i -lt $first.bridge.workerRecordTicks.Count;$i++) {
    $delta=[long]$last.bridge.workerRecordTicks[$i]-[long]$first.bridge.workerRecordTicks[$i]
    if($delta -lt 0) {throw 'Worker timer counter reset.'}
    $workerTimings+=[math]::Round($delta*1000.0/$frequency,3)
}
if($Mode -in @('parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small') -and ($last.bridge.workerRecordedDraws -le $first.bridge.workerRecordedDraws -or @($last.bridge.workerIds | Sort-Object -Unique).Count -ne 4 -or @($last.bridge.workerIds | Where-Object {$_ -le 0}).Count)) {throw 'Parallel mode did not prove four distinct live workers.'}
$result=[ordered]@{
    processId=$identity.processId;requestedMode=$Mode;activeObservations=$valid.Count;intervalSeconds=$seconds
    approximatePresentsPerSecond=($last.uncap.presents-$first.uncap.presents)/$seconds
    rateLimit='Counters are published independently every 500 ms; rate is approximate, not an ETW FPS capture.'
    replacedDrawsDelta=($last.bridge.replacedDraws-$first.bridge.replacedDraws)
    workerDrawsDelta=($last.bridge.workerRecordedDraws-$first.bridge.workerRecordedDraws)
    serialDrawsDelta=($last.bridge.serialRecordedDraws-$first.bridge.serialRecordedDraws)
    captureAttemptsDelta=($last.bridge.captureAttempts-$first.bridge.captureAttempts)
    uploadCopiedBytesDelta=($last.bridge.uploadCopyBytes-$first.bridge.uploadCopyBytes)
    mainPhaseElapsedMilliseconds=$timings
    workerRecordingElapsedMilliseconds=$workerTimings;workerIds=$last.bridge.workerIds
    timingLimit='Elapsed CPU-side wall time. Worker wait includes overlapping worker recording; execute measures submission, not GPU duration. Other engine/adapter work is excluded.'
    automaticallyReturnedToOriginal=($history[-1].bridge.requestedMode -eq 'off')
    visualStatus='pending';directory=$run
}
if($first.bridge.PSObject.Properties.Name -contains 'privateUploads') {
    $result.privateUploadsDelta=$last.bridge.privateUploads-$first.bridge.privateUploads
    $result.privateUploadReusesDelta=$last.bridge.privateUploadReuses-$first.bridge.privateUploadReuses
    $result.privateUploadedBytesDelta=$last.bridge.privateUploadBytes-$first.bridge.privateUploadBytes
}
if($first.bridge.PSObject.Properties.Name -contains 'snapshotGetters') {
    $result.snapshotGettersDelta=$last.bridge.snapshotGetters-$first.bridge.snapshotGetters
    $result.stateGroupRefreshesDelta=$last.bridge.stateGroupRefreshes-$first.bridge.stateGroupRefreshes
    $result.stateGroupReusesDelta=$last.bridge.stateGroupReuses-$first.bridge.stateGroupReuses
}
if($first.bridge.PSObject.Properties.Name -contains 'recordingBindings') {
    $result.recordingBindingsDelta=$last.bridge.recordingBindings-$first.bridge.recordingBindings
    $result.recordingBindingsSkippedDelta=$last.bridge.recordingBindingsSkipped-$first.bridge.recordingBindingsSkipped
}
if($first.bridge.PSObject.Properties.Name -contains 'snapshotGroupCopies') {
    $result.sharedSnapshotBindings=$last.bridge.sharedSnapshotBindings
    $result.snapshotGroupCopiesDelta=$last.bridge.snapshotGroupCopies-$first.bridge.snapshotGroupCopies
    $result.snapshotGroupReusesDelta=$last.bridge.snapshotGroupReuses-$first.bridge.snapshotGroupReuses
    $result.snapshotPublishMilliseconds=($last.bridge.snapshotPublishTicks-$first.bridge.snapshotPublishTicks)*1000.0/$frequency
    $result.queueReleaseMilliseconds=($last.bridge.queueReleaseTicks-$first.bridge.queueReleaseTicks)*1000.0/$frequency
    if($Mode -eq 'parallel-owned-snapshots' -and ($result.sharedSnapshotBindings -or $result.snapshotGroupReusesDelta -ne 0 -or $result.snapshotGroupCopiesDelta -le 0)) {throw 'Owned snapshot control not proven.'}
}
if($first.bridge.PSObject.Properties.Name -contains 'flatUploadLookup') {
    $result.flatUploadLookup=$last.bridge.flatUploadLookup
    foreach($field in @('temporaryUploadMapEntries','flatUploadEntries','drawUploadDuplicates')) {
        $delta=[long]$last.bridge.$field-[long]$first.bridge.$field
        if($delta -lt 0) {throw "Upload lookup counter reset: $field"}
        $result[$field+'Delta']=$delta
    }
    if($Mode -eq 'parallel-map-uploads' -and ($result.flatUploadLookup -or $result.flatUploadEntriesDelta -ne 0 -or $result.temporaryUploadMapEntriesDelta -le 0)) {throw 'Map upload control not proven.'}
    if($Mode -eq 'parallel' -and (-not $result.flatUploadLookup -or $result.temporaryUploadMapEntriesDelta -ne 0 -or $result.flatUploadEntriesDelta -le 0)) {throw 'Flat upload mode not proven.'}
}
if($Mode -eq 'parallel-direct-small') {
    if(-not $last.bridge.directSmallRequested -or -not $last.bridge.directSmallAvailable) {throw 'Direct small-batch mode unavailable.'}
    $result.directSmallDrawsDelta=$last.bridge.directSmallDraws-$first.bridge.directSmallDraws
    $result.directSmallBatchesDelta=$last.bridge.directSmallBatches-$first.bridge.directSmallBatches
    $result.directSmallMilliseconds=($last.bridge.directSmallTicks-$first.bridge.directSmallTicks)*1000.0/$frequency
    if($result.directSmallDrawsDelta -le 0 -or $result.directSmallBatchesDelta -le 0 -or $result.directSmallDrawsDelta -gt $result.serialDrawsDelta) {throw 'Direct small-batch execution not proven.'}
}
$result | ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $run 'analysis.json')
