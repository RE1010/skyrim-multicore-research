[CmdletBinding(DefaultParameterSetName = 'Name')]
param(
    [Parameter(ParameterSetName = 'Name')][string]$ProcessName = 'SkyrimSE',
    [Parameter(Mandatory, ParameterSetName = 'Id')][int]$ProcessId,
    [ValidateRange(1, 3600)][int]$DurationSeconds = 30,
    [ValidateRange(100, 5000)][int]$IntervalMs = 500,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = 'baseline',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\measurements')
)

$ErrorActionPreference = 'Stop'
$targets = if ($PSCmdlet.ParameterSetName -eq 'Id') {
    @(Get-Process -Id $ProcessId)
} else { @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue) }
if ($targets.Count -ne 1) { throw "Expected one running process, found $($targets.Count). Start the game first or specify -ProcessId." }
$target = $targets[0]
$targetId = $target.Id
$targetStart = $target.StartTime.ToUniversalTime()
$targetName = $target.ProcessName
$rows = [System.Collections.Generic.List[object]]::new()
$statistics = @{}
$clock = [Diagnostics.Stopwatch]::StartNew()
$readErrors = 0
$newThreadBaselines = 0
$endReason = 'duration'

function Get-ThreadSnapshot {
    $current = Get-Process -Id $targetId -ErrorAction Stop
    try {
        if ($current.StartTime.ToUniversalTime() -ne $targetStart) { throw 'Process ID was reused.' }
        $snapshot = @{}
        foreach ($thread in $current.Threads) {
            try {
                $born = $thread.StartTime.ToUniversalTime().Ticks
                $key = "$($thread.Id):$born"
                $snapshot[$key] = [pscustomobject]@{
                    id = $thread.Id; born = $born
                    cpuMs = $thread.TotalProcessorTime.TotalMilliseconds
                    wallMs = $clock.Elapsed.TotalMilliseconds
                }
            } catch { $script:readErrors++ }
            finally { $thread.Dispose() }
        }
        return $snapshot
    } finally { $current.Dispose() }
}

try {
    $previous = Get-ThreadSnapshot
    if ($previous.Count -eq 0) { throw 'No readable thread counters. No measurement was taken.' }
    while ($clock.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $remainingMs = [int][Math]::Ceiling(($DurationSeconds * 1000) - $clock.Elapsed.TotalMilliseconds)
        Start-Sleep -Milliseconds ([Math]::Max(1, [Math]::Min($IntervalMs, $remainingMs)))
        try { $snapshot = Get-ThreadSnapshot }
        catch { $endReason = "process unavailable: $($_.Exception.Message)"; break }
        foreach ($key in $snapshot.Keys) {
            $now = $snapshot[$key]
            if (-not $previous.ContainsKey($key)) { $newThreadBaselines++; continue }
            $before = $previous[$key]
            $wallMs = $now.wallMs - $before.wallMs
            $cpuMs = $now.cpuMs - $before.cpuMs
            if ($wallMs -le 0 -or $cpuMs -lt 0) { continue }
            $corePercent = 100.0 * $cpuMs / $wallMs
            $rows.Add([pscustomobject]@{
                elapsed_ms = [Math]::Round($now.wallMs, 3)
                thread_id = $now.id
                thread_start_utc_ticks = $now.born
                interval_ms = [Math]::Round($wallMs, 3)
                cpu_ms = [Math]::Round($cpuMs, 3)
                one_logical_cpu_percent = [Math]::Round($corePercent, 3)
            })
            if (-not $statistics.ContainsKey($key)) {
                $statistics[$key] = [pscustomobject]@{ id = $now.id; born = $now.born; cpu = 0.0; wall = 0.0; peak = 0.0; samples = 0 }
            }
            $stat = $statistics[$key]
            $stat.cpu += $cpuMs; $stat.wall += $wallMs; $stat.samples++
            $stat.peak = [Math]::Max($stat.peak, $corePercent)
        }
        $previous = $snapshot
    }
} finally { $clock.Stop(); $target.Dispose() }

if ($rows.Count -eq 0) { throw "No complete readable intervals. End reason: $endReason" }
$summary = @($statistics.Values | Sort-Object cpu -Descending | ForEach-Object {
    [pscustomobject]@{
        threadId = $_.id; threadStartUtcTicks = $_.born
        observedCpuMs = [Math]::Round($_.cpu, 3)
        observedWallMs = [Math]::Round($_.wall, 3)
        averageOneLogicalCpuPercent = [Math]::Round(100 * $_.cpu / $_.wall, 3)
        peakIntervalOneLogicalCpuPercent = [Math]::Round($_.peak, 3)
        intervals = $_.samples
    }
})
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$stem = '{0}-{1}-{2}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $Label, $targetId
$csvPath = Join-Path $outputPath "$stem.csv"
$jsonPath = Join-Path $outputPath "$stem.json"
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding utf8
[ordered]@{
    schemaVersion = 1; label = $Label; processName = $targetName; processId = $targetId
    processStartUtc = $targetStart.ToString('o'); elapsedSeconds = $clock.Elapsed.TotalSeconds
    intervalTargetMs = $IntervalMs; readErrors = $readErrors; newThreadBaselines = $newThreadBaselines
    endReason = $endReason; csv = $csvPath; threads = $summary
    limitations = @(
        '100 percent represents one logical CPU worth of scheduled execution, not total machine usage.',
        'Thread IDs are not identified as engine main/render/worker roles.',
        'No call stacks, GPU timings, frame times, core placement or causal critical path are captured.',
        'Short-lived threads and work before first/after last readable snapshot can be missed.',
        'Counter granularity and sequential reads can yield interval percentages slightly above 100.',
        'Sampler overhead must be checked separately; no performance improvement is established.'
    )
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8
$summary | Select-Object -First 10 | Format-Table
Write-Output $jsonPath
