[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunDirectory
)
$ErrorActionPreference='Stop'
$run=[IO.Path]::GetFullPath($RunDirectory)
$state=Get-Content -LiteralPath (Join-Path $run 'capture-state.json') -Raw | ConvertFrom-Json
$stats=Get-Content -LiteralPath (Join-Path $run 'trace-stats.txt') -Raw
function Read-Count([string]$Pattern) {
    $match=[regex]::Match($stats,$Pattern)
    if (-not $match.Success) { throw "Missing trace quality field: $Pattern" }
    return [long]$match.Groups[1].Value
}
$lostEvents=Read-Count 'Total # Lost Events\s*:\s*(\d+)'
$lostBuffers=Read-Count 'Total # Lost Buffers\s*:\s*(\d+)'
if ($lostEvents -ne 0 -or $lostBuffers -ne 0) { throw "CPU summary refused: $lostEvents lost events, $lostBuffers lost buffers." }
if ((Read-Count 'Number of Traces\s*:\s*(\d+)') -ne 1) { throw 'Only a single trace is supported.' }
$start=[regex]::Match($stats,'Start time \(UTC\)\s*:\s*(\d{4}/\d{2}/\d{2}:\d{2}:\d{2}:\d{2}\.\d{7})')
$end=[regex]::Match($stats,'End time \(UTC\)\s*:\s*(\d{4}/\d{2}/\d{2}:\d{2}:\d{2}:\d{2}\.\d{7})')
if (-not $start.Success -or -not $end.Success) { throw 'Unsupported or missing trace timestamps.' }
$culture=[Globalization.CultureInfo]::InvariantCulture
$duration=([DateTime]::ParseExact($end.Groups[1].Value,'yyyy/MM/dd:HH:mm:ss.fffffff',$culture)-
    [DateTime]::ParseExact($start.Groups[1].Value,'yyyy/MM/dd:HH:mm:ss.fffffff',$culture)).TotalSeconds
if ($duration -le 0) { throw 'Trace duration must be positive.' }
$processorCount=Read-Count 'Number of Processors\s*:\s*(\d+)'
if ($processorCount -le 0) { throw 'Processor count must be positive.' }
$target=[regex]::Escape("SkyrimSE.exe ($($state.processId))")
$threads=@(Get-Content -LiteralPath (Join-Path $run 'cpu-threads.txt') | ForEach-Object {
    if ($_ -match "^\s*(\d+),\s*$target,\s*(\d+)\s*$") {
        [pscustomobject]@{threadId=[int]$Matches[2];scheduledCpuMicroseconds=[long]$Matches[1];
            oneLogicalProcessorPercent=([long]$Matches[1]/($duration*1e6)*100)}
    }
})
if (-not $threads.Count) { throw 'No target thread records found.' }
$threadIds=@($threads.threadId | Sort-Object -Unique)
if ($threads.Count -ne $threadIds.Count) { throw 'Duplicate target thread records are not supported.' }
$modules=@(Get-Content -LiteralPath (Join-Path $run 'cpu-profile.txt') | ForEach-Object {
    if ($_ -match "^\s*$target,\s*(\d+),\s*([0-9.]+),\s*(.+?)\s*$") {
        [pscustomobject]@{module=$Matches[3];sampleWeightMicroseconds=[long]$Matches[1];
            machinePercentFromXperf=[double]::Parse($Matches[2],$culture)}
    }
})
if (-not $modules.Count) { throw 'No target module records found.' }
$cpuUs=($threads | Measure-Object scheduledCpuMicroseconds -Sum).Sum
$summary=[ordered]@{
    schemaVersion=1;sourceRun=$run;processId=$state.processId;gameVersion=$state.gameVersion;gameSha256=$state.gameSha256
    traceDurationSeconds=$duration;logicalProcessors=$processorCount;lostEvents=$lostEvents;lostBuffers=$lostBuffers
    schedulingCpuSeconds=$cpuUs/1e6;oneLogicalProcessorEquivalent=$cpuUs/($duration*1e6)
    machineCpuPercent=$cpuUs/($duration*1e6*$processorCount)*100
    threads=@($threads | Sort-Object scheduledCpuMicroseconds -Descending)
    sampledModules=@($modules | Sort-Object sampleWeightMicroseconds -Descending)
    limitations=@(
        'Zero event loss is a quality check, not proof of zero recorder overhead or representative gameplay.',
        'Thread scheduling covers the full ETL interval, which differs from the PresentMon frame interval.',
        'A logical-processor equivalent is running time, not a capacity-adjusted P-core/E-core performance measure.',
        'CPU time includes polling and kernel work; it does not identify useful work or a main-thread bottleneck.',
        'Module samples and context-switch CPU times are different estimators; their sums need not agree.',
        'No main-thread role or frame critical path is inferred from thread ID or CPU ranking.'
    )
}
$output=Join-Path $run 'cpu-summary.json'
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $output -Encoding utf8
Write-Output $output
