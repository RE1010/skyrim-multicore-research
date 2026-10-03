[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('off','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots','verify-constants','inline','serial')][string]$Mode,
    [ValidateRange(2,120)][int]$DurationSeconds=10)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$current=Get-Content -LiteralPath (Join-Path $project 'measurements\live-render-bridge\current-session.json') -Raw | ConvertFrom-Json
$game=Get-Process -Id $current.processId
if($game.ProcessName -ne 'SkyrimSE' -or $game.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $current.processStartFileTime) {throw 'Stale/wrong game identity.'}
$root=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-render-bridge')).TrimEnd('\')
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
if(-not $session.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Session outside bridge output.'}
$temp=Join-Path $session 'mode.tmp'
$command=if($Mode -in @('inline','serial','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots')) {$Mode+' '+([Environment]::TickCount64+[long]$DurationSeconds*1000).ToString([Globalization.CultureInfo]::InvariantCulture)}else{$Mode}
$command | Set-Content -LiteralPath $temp -Encoding ascii
Move-Item -LiteralPath $temp -Destination (Join-Path $session 'mode.txt') -Force
if($Mode -in @('inline','serial','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots')) {Write-Output "Requested bridge mode: $Mode (replacement expires after $DurationSeconds seconds)"}
else {Write-Output "Requested bridge mode: $Mode"}
