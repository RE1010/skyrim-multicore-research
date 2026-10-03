[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CsvPath,
    [string]$OutputPath
)
$ErrorActionPreference='Stop'
$rows=@(Import-Csv -LiteralPath $CsvPath)
if (-not $rows.Count) { throw 'Frame CSV is empty.' }
$columns=@($rows[0].PSObject.Properties.Name)
if ('ProcessID' -notin $columns -or 'SwapChainAddress' -notin $columns) { throw 'CSV lacks process/swap-chain identifiers.' }
$frameColumn=if ('MsBetweenPresents' -in $columns) {'MsBetweenPresents'} elseif ('CPUFrameTime' -in $columns) {'CPUFrameTime'} else {throw 'No supported frame interval column found.'}
function Get-NumericValues($Items, [string]$Column, [bool]$PositiveOnly=$false) {
    foreach($item in $Items) {
        $number=0.0
        if ([double]::TryParse([string]$item.$Column,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$number) -and
            -not [double]::IsNaN($number) -and -not [double]::IsInfinity($number) -and
            (($PositiveOnly -and $number -gt 0) -or (-not $PositiveOnly -and $number -ge 0))) { $number }
    }
}
function Get-Quantile([double[]]$Sorted,[double]$Percent) {
    $position=($Sorted.Count-1)*$Percent
    $lo=[int][Math]::Floor($position); $hi=[int][Math]::Ceiling($position)
    return $Sorted[$lo]+($Sorted[$hi]-$Sorted[$lo])*($position-$lo)
}
function Get-Stats([double[]]$Values) {
    if (-not $Values.Count) { return $null }
    $sorted=@($Values | Sort-Object)
    [ordered]@{samples=$Values.Count;meanMs=($Values | Measure-Object -Average).Average;
        medianMs=(Get-Quantile $sorted 0.5);p95Ms=(Get-Quantile $sorted 0.95);p99Ms=(Get-Quantile $sorted 0.99)}
}
$groups=@($rows | Group-Object -Property ProcessID,SwapChainAddress)
$chains=@(foreach($group in $groups) {
    $first=$group.Group[0]
    $values=@(Get-NumericValues $group.Group $frameColumn $true)
    $stats=Get-Stats $values
    $metrics=[ordered]@{}
    foreach($column in @('MsCPUBusy','MsCPUWait','MsGPUTime','MsGPUBusy','MsGPUWait','MsInPresentAPI','CPUBusy','CPUWait','GPUTime','GPUBusy','GPUWait')) {
        if ($column -in $columns) { $metrics[$column]=Get-Stats @(Get-NumericValues $group.Group $column) }
    }
    [ordered]@{
        application=$first.Application;processId=$first.ProcessID;swapChain=$first.SwapChainAddress
        csvRows=$group.Count;frameIntervalColumn=$frameColumn;frameIntervals=$stats
        invalidFrameIntervals=($group.Count-$values.Count)
        averagePresentRate=if($stats) {1000.0/$stats.meanMs} else {$null}
        syncIntervals=@(if('SyncInterval' -in $columns) {$group.Group.SyncInterval | Sort-Object -Unique})
        presentModes=@(if('PresentMode' -in $columns) {$group.Group.PresentMode | Sort-Object -Unique})
        frameTypes=@(if('FrameType' -in $columns) {$group.Group.FrameType | Sort-Object -Unique})
        metrics=$metrics
    }
})
$report=[ordered]@{
    schemaVersion=1;source=[IO.Path]::GetFullPath($CsvPath);columns=$columns;totalRows=$rows.Count
    swapChains=$chains
    limitations=@(
        'Chains are kept separate; select the gameplay swap chain before making comparisons.',
        'Average present rate is not necessarily displayed or genuinely rendered FPS.',
        'ETW CPU frame metrics do not identify the engine main thread or scheduled thread CPU time.',
        'No bottleneck classification is made from CSV averages alone; inspect CPU/GPU stacks and timeline.',
        'Invalid/missing metric values are excluded and each metric records its valid sample count.'
    )
}
if (-not $OutputPath) { $OutputPath=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($CsvPath))) 'frame-summary.json' }
$report | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output ([IO.Path]::GetFullPath($OutputPath))
