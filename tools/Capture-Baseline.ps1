[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][string]$Scene,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(5, 60)][int]$DurationSeconds = 30,
    [ValidateRange(0, 30)][int]$FocusDelaySeconds = 8,
    [switch]$RequireUncapped,
    [ValidateSet('none','project-hook-free','off','parallel','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small','free-draw','original-clean','profile-context')][string]$BridgeMode='none',
    [ValidateSet(1,16)][int]$SampleEvery=16
)

$ErrorActionPreference = 'Stop'
if($BridgeMode -eq 'free-draw' -and ($DurationSeconds -ne 5 -or -not $RequireUncapped)) {throw 'Free-draw capture requires uncapped verification and a five-second recording.'}
if($BridgeMode -eq 'profile-context' -and ($DurationSeconds -ne 5 -or -not $RequireUncapped)) {throw 'Context profiling requires uncapped verification and a five-second recording.'}
if($BridgeMode -eq 'project-hook-free' -and -not $RequireUncapped) {throw 'Project-hook-free reference requires unchanged, verified uncapping.'}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'CPU/GPU ETW capture requires an elevated process. Launch this script through Start-Baseline.ps1 and confirm the Windows UAC prompt.'
}
$process = Get-Process -Id $ProcessId
if ($process.ProcessName -ne 'SkyrimSE') { throw 'The target must be the running SkyrimSE process.' }
$processStart = $process.StartTime.ToUniversalTime()
$exe = Get-Item -LiteralPath $process.Path
$modules = @($process.Modules | ForEach-Object {
    [pscustomobject]@{ name=$_.ModuleName; path=$_.FileName; baseAddress=('0x{0:X}' -f $_.BaseAddress.ToInt64()) }
})
$process.Dispose()
if($BridgeMode -eq 'project-hook-free' -and (Get-FileHash -LiteralPath $exe.FullName).Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown reference executable.'}
$presentMon = Join-Path $PSScriptRoot 'vendor\presentmon\PresentMon-2.6.0-x64.exe'
$expectedHash = 'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF'
if ((Get-FileHash -LiteralPath $presentMon -Algorithm SHA256).Hash -ne $expectedHash) { throw 'PresentMon hash mismatch.' }
$signature = Get-AuthenticodeSignature -LiteralPath $presentMon
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Intel Corporation') { throw 'PresentMon Intel signature is not valid.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath (Join-Path $output 'capture-state.json')) { throw 'Capture directory already contains a session.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$tempOutput = Join-Path $output 'etl-temp'
New-Item -ItemType Directory -Path $tempOutput -Force | Out-Null
$instance = 'SkyrimResearch_' + [Guid]::NewGuid().ToString('N')
$statePath = Join-Path $output 'capture-state.json'
$profilePath = Join-Path $PSScriptRoot 'SkyrimCPU.wprp'
Copy-Item -LiteralPath $profilePath -Destination (Join-Path $output 'recorder-profile.wprp')
& (Join-Path $PSScriptRoot 'Collect-Setup.ps1') -OutputPath (Join-Path $output 'setup-before-capture.json') | Out-Null
& (Join-Path $PSScriptRoot 'Collect-CpuTopology.ps1') -OutputPath (Join-Path $output 'cpu-topology.json') | Out-Null
$metadata = [ordered]@{
    schemaVersion=1; scene=$Scene; processId=$ProcessId; processStartUtc=$processStart.ToString('o')
    gamePath=$exe.FullName; gameVersion=$exe.VersionInfo.FileVersion
    gameSha256=(Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
    modules=$modules; presentMonVersion='2.6.0'; presentMonSha256=$expectedHash
    durationSeconds=$DurationSeconds; focusDelaySeconds=$FocusDelaySeconds
    recorderInstance=$instance; status='arming'; requestedAt=[DateTimeOffset]::Now.ToString('o')
    recorderProfile='SkyrimCPU'; recorderProfileSha256=(Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash
    traceQuality='not checked'; schedulingStacks=$false; sampledCpuStacks=$true
    limitation='Scene readiness is supplied by the user; this script does not identify menus, loading screens or camera movement.'
    requireUncapped=[bool]$RequireUncapped;bridgeMode=$BridgeMode;profileSampleEvery=$SampleEvery
}
function Save-State { $metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statePath -Encoding utf8 }
function Save-UncappedState {
    if(-not $RequireUncapped) {return}
    $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\measurements\live-uncap'))
    $current=Get-Content -LiteralPath (Join-Path $root 'current-session.json') -Raw | ConvertFrom-Json
    $session=[IO.Path]::GetFullPath($current.sessionDirectory)
    if($current.processId -ne $ProcessId -or [long]$current.processStartFileTime -ne $processStart.ToFileTimeUtc() -or -not $session.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Uncapped session identity mismatch.'}
    $snapshot=Get-Content -LiteralPath (Join-Path $session 'summary.json') -Raw | ConvertFrom-Json
    if($snapshot.processId -ne $ProcessId -or -not $snapshot.enabled -or -not $snapshot.worldLoaded -or -not $snapshot.settingsReady -or -not $snapshot.uncappedActive) {throw 'Uncapped mode not active in loaded world.'}
    [ordered]@{readAtUTC=[DateTime]::UtcNow.ToString('o');session=$session;snapshot=$snapshot} |
        ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath (Join-Path $output 'uncap-state-history.jsonl') -Encoding utf8
}
function Save-BridgeState([string]$ExpectedMode=$BridgeMode,[switch]$Preflight) {
    if($BridgeMode -eq 'none') {return}
    if($BridgeMode -eq 'project-hook-free') {
        $reference=& (Join-Path $PSScriptRoot 'Get-ProjectHookReference.ps1') -ProcessId $ProcessId -ProcessStartFileTime $processStart.ToFileTimeUtc() -HashRuntime:$Preflight
        if($Preflight) {$reference | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'project-hook-reference-preflight.json') -Encoding utf8}
        else {$reference | ConvertTo-Json -Depth 6 -Compress | Add-Content -LiteralPath (Join-Path $output 'project-hook-reference-history.jsonl') -Encoding utf8}
        return
    }
    $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\measurements\live-render-bridge'))
    $current=Get-Content -LiteralPath (Join-Path $root 'current-session.json') -Raw | ConvertFrom-Json
    $session=[IO.Path]::GetFullPath($current.sessionDirectory)
    if($current.processId -ne $ProcessId -or [long]$current.processStartFileTime -ne $processStart.ToFileTimeUtc() -or -not $session.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Bridge session identity mismatch.'}
    $snapshot=Get-Content -LiteralPath (Join-Path $session 'summary.json') -Raw | ConvertFrom-Json
    if($snapshot.processId -ne $ProcessId -or -not $snapshot.worldLoaded -or -not $snapshot.contextAttached -or $snapshot.initializationError -or $snapshot.errors -or $snapshot.offloadDisabled -or [bool]$snapshot.enabled -ne ($ExpectedMode -in @('parallel','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small')) -or ($snapshot.requestedMode -and $snapshot.requestedMode -ne $ExpectedMode)) {throw 'Bridge mode/context validity mismatch.'}
    if($ExpectedMode -eq 'free-draw' -and ($snapshot.pluginVersion -lt 11 -or -not $snapshot.drawSuppressionActive)) {throw 'Bounded draw suppression is not active.'}
    if($ExpectedMode -in @('original-clean','profile-context') -and ($snapshot.pluginVersion -lt 12 -or -not $snapshot.contextProfile.cleanForwarding -or [long]$snapshot.processStartFileTime -ne $processStart.ToFileTimeUtc())) {throw 'Clean original forwarding or process identity is not verified.'}
    if($ExpectedMode -eq 'profile-context' -and (-not $snapshot.contextProfile.active -or $snapshot.contextProfile.sampleEvery -ne $SampleEvery)) {throw 'Bounded context profiling is not active at the requested density.'}
    if($Preflight) {
        $preflightName=if($BridgeMode -eq 'profile-context'){'context-profile-preflight.json'}else{'free-draw-preflight.json'}
        [ordered]@{readAtUTC=[DateTime]::UtcNow.ToString('o');session=$session;snapshot=$snapshot} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output $preflightName) -Encoding utf8
        return
    }
    [ordered]@{readAtUTC=[DateTime]::UtcNow.ToString('o');session=$session;snapshot=$snapshot} |
        ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath (Join-Path $output 'bridge-state-history.jsonl') -Encoding utf8
}
Save-State
$recordingOwned = $false
$presentProcess = $null
$etlPath = Join-Path $output 'cpu.etl'
try {
    if ($FocusDelaySeconds) { Start-Sleep -Seconds $FocusDelaySeconds }
    $current = Get-Process -Id $ProcessId
    try { if ($current.StartTime.ToUniversalTime() -ne $processStart) { throw 'Target process was restarted.' } }
    finally { $current.Dispose() }
    Save-UncappedState
    if($BridgeMode -in @('original-clean','profile-context')) {
        & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode original-clean | Out-Null
        $cleanCurrent=Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\measurements\live-render-bridge\current-session.json') -Raw | ConvertFrom-Json
        $cleanWatch=[Diagnostics.Stopwatch]::StartNew()
        do {Start-Sleep -Milliseconds 100;$cleanState=Get-Content -LiteralPath (Join-Path $cleanCurrent.sessionDirectory 'summary.json') -Raw | ConvertFrom-Json}
        while($cleanWatch.Elapsed.TotalSeconds -lt 2 -and ($cleanState.requestedMode -ne 'original-clean' -or -not $cleanState.contextProfile.cleanForwarding))
    }
    if($BridgeMode -eq 'project-hook-free') {Save-BridgeState -Preflight}
    elseif($BridgeMode -eq 'free-draw') {Save-BridgeState -ExpectedMode off -Preflight}
    elseif($BridgeMode -eq 'profile-context') {Save-BridgeState -ExpectedMode original-clean -Preflight}
    else{Save-BridgeState}
    $startResult = & "$env:SystemRoot\System32\wpr.exe" -start "${profilePath}!SkyrimCPU" -filemode -recordtempto $tempOutput -instancename $instance 2>&1
    $startCode = $LASTEXITCODE
    $startResult | Out-File -LiteralPath (Join-Path $output 'wpr-start.log') -Encoding utf8
    if ($startCode -ne 0) { throw "WPR start failed (exit $startCode). Other recorder sessions were not stopped." }
    $recordingOwned = $true
    if($BridgeMode -eq 'free-draw') {
        $preflight=Get-Content -LiteralPath (Join-Path $output 'free-draw-preflight.json') -Raw | ConvertFrom-Json
        if($preflight.snapshot.pluginVersion -lt 11 -or -not $preflight.snapshot.deviceCapabilities.threadingQuerySucceeded) {throw 'Live device capability query and V11 required.'}
        & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode free-draw -DurationSeconds 10 | Out-Null
        $armWatch=[Diagnostics.Stopwatch]::StartNew()
        do {
            Start-Sleep -Milliseconds 100
            $armed=Get-Content -LiteralPath (Join-Path $preflight.session 'summary.json') -Raw | ConvertFrom-Json
        } while($armWatch.Elapsed.TotalSeconds -lt 2 -and ($armed.requestedMode -ne 'free-draw' -or -not $armed.drawSuppressionActive))
        Save-BridgeState
    }
    if($BridgeMode -eq 'profile-context') {
        $preflight=Get-Content -LiteralPath (Join-Path $output 'context-profile-preflight.json') -Raw | ConvertFrom-Json
        & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode profile-context -DurationSeconds 10 -SampleEvery $SampleEvery | Out-Null
        $armWatch=[Diagnostics.Stopwatch]::StartNew()
        do {Start-Sleep -Milliseconds 100;$armed=Get-Content -LiteralPath (Join-Path $preflight.session 'summary.json') -Raw | ConvertFrom-Json}
        while($armWatch.Elapsed.TotalSeconds -lt 2 -and ($armed.requestedMode -ne 'profile-context' -or -not $armed.contextProfile.active))
        Save-BridgeState
    }
    $presentArgs = @('--process_id', "$ProcessId", '--timed', "$DurationSeconds", '--terminate_after_timed',
        '--no_track_input', '--no_console_stats', '--qpc_time', '--session_name', $instance,
        '--output_file', ('"{0}"' -f (Join-Path $output 'frames.csv')))
    $presentProcess = Start-Process -FilePath $presentMon -ArgumentList $presentArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $output 'presentmon-stdout.log') -RedirectStandardError (Join-Path $output 'presentmon-stderr.log')
    $metadata.status='recording'; $metadata.recordingStartedAt=[DateTimeOffset]::Now.ToString('o'); Save-State
    if($RequireUncapped -or $BridgeMode -ne 'none') {
        $deadline=[DateTime]::UtcNow.AddSeconds($DurationSeconds+10)
        while(-not $presentProcess.WaitForExit(1000)) {
            Save-UncappedState
            Save-BridgeState
            if([DateTime]::UtcNow -ge $deadline) {throw 'PresentMon exceeded its capture timeout.'}
        }
        Save-UncappedState
        Save-BridgeState
    } elseif (-not $presentProcess.WaitForExit(($DurationSeconds + 10) * 1000)) { throw 'PresentMon exceeded its capture timeout.' }
    $metadata.presentMonExitCode=$presentProcess.ExitCode
    if ($presentProcess.ExitCode -ne 0) { throw "PresentMon failed (exit $($presentProcess.ExitCode)); see its stderr log." }
    $metadata.status='saving'; $metadata.frameRecordingFinishedAt=[DateTimeOffset]::Now.ToString('o'); Save-State
} catch {
    $metadata.status='failed'; $metadata.error=$_.Exception.Message
} finally {
    if($BridgeMode -eq 'profile-context') {
        try {
            & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode original-clean | Out-Null
            if($preflight) {
                $restoreWatch=[Diagnostics.Stopwatch]::StartNew()
                do {Start-Sleep -Milliseconds 100;$restored=Get-Content -LiteralPath (Join-Path $preflight.session 'summary.json') -Raw | ConvertFrom-Json}
                while($restoreWatch.Elapsed.TotalSeconds -lt 2 -and ($restored.requestedMode -ne 'original-clean' -or $restored.contextProfile.active -or -not $restored.contextProfile.cleanForwarding))
                $restored | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'context-profile-restoration.json') -Encoding utf8
                if($restored.requestedMode -ne 'original-clean' -or $restored.contextProfile.active -or -not $restored.contextProfile.cleanForwarding) {throw 'Original clean restoration not confirmed.'}
            }
        } catch {$metadata.status='failed';$metadata.error='Context profiling cleanup: '+$_.Exception.Message}
    }
    if($BridgeMode -eq 'free-draw') {
        try {
            & (Join-Path $PSScriptRoot 'Set-RenderBridgeMode.ps1') -Mode off | Out-Null
            if($preflight) {
                $restoreWatch=[Diagnostics.Stopwatch]::StartNew()
                do {Start-Sleep -Milliseconds 100;$restored=Get-Content -LiteralPath (Join-Path $preflight.session 'summary.json') -Raw | ConvertFrom-Json}
                while($restoreWatch.Elapsed.TotalSeconds -lt 2 -and ($restored.requestedMode -ne 'off' -or $restored.drawSuppressionActive))
                $restored | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'free-draw-restoration.json') -Encoding utf8
                if($restored.requestedMode -ne 'off' -or $restored.drawSuppressionActive) {throw 'Original mode restoration not confirmed.'}
            }
        } catch {$metadata.status='failed';$metadata.error='Free-draw cleanup: '+$_.Exception.Message}
    }
    if ($presentProcess -and -not $presentProcess.HasExited) { $presentProcess.Kill(); $presentProcess.WaitForExit() }
    if ($presentProcess) { $presentProcess.Dispose() }
    if ($recordingOwned) {
        $stopResult = & "$env:SystemRoot\System32\wpr.exe" -stop $etlPath -skipPdbGen -instancename $instance 2>&1
        $stopCode = $LASTEXITCODE
        $stopResult | Out-File -LiteralPath (Join-Path $output 'wpr-stop.log') -Encoding utf8
        $metadata.wprStopExitCode=$stopCode
        if ($stopCode -ne 0) {
            # Only our uniquely named session may be cancelled; never a foreign recorder.
            & "$env:SystemRoot\System32\wpr.exe" -cancel -instancename $instance 2>&1 |
                Out-File -LiteralPath (Join-Path $output 'wpr-cancel.log') -Encoding utf8
            $metadata.status='failed'; $metadata.recorderError="WPR stop failed (exit $stopCode). Cancellation attempted for our instance only."
        }
    }
    $metadata.finishedAt=[DateTimeOffset]::Now.ToString('o')
    if ($metadata.status -eq 'saving') {
        $csv = Join-Path $output 'frames.csv'
        if (-not (Test-Path -LiteralPath $csv) -or -not (Test-Path -LiteralPath $etlPath)) {
            $metadata.status='failed'; $metadata.error='Expected frame CSV or CPU/GPU ETL is missing.'
        } elseif ((@(Import-Csv -LiteralPath $csv)).Count -eq 0) {
            $metadata.status='failed'; $metadata.error='Frame CSV has no frame rows.'
        } else { $metadata.status='captured' }
    }
    Save-State
}
if ($metadata.status -ne 'captured') { throw "Capture failed. Details: $statePath" }
Write-Output $statePath
