[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('off','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small','verify-constants','inline','serial','free-draw','original-clean','profile-context')][string]$Mode,
    [ValidateRange(2,120)][int]$DurationSeconds=10,[ValidateSet(1,16)][int]$SampleEvery=16)
$ErrorActionPreference='Stop'
if($Mode -in @('free-draw','profile-context') -and $DurationSeconds -gt 10) {throw 'Diagnostics are limited to ten seconds.'}
$project=Split-Path $PSScriptRoot -Parent
$current=Get-Content -LiteralPath (Join-Path $project 'measurements\live-render-bridge\current-session.json') -Raw | ConvertFrom-Json
$game=Get-Process -Id $current.processId
if($game.ProcessName -ne 'SkyrimSE' -or $game.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $current.processStartFileTime) {throw 'Stale/wrong game identity.'}
$root=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-render-bridge')).TrimEnd('\')
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
if(-not $session.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Session outside bridge output.'}
$temp=Join-Path $session 'mode.tmp'
$timed=$Mode -in @('inline','serial','parallel','parallel-uncached','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small','free-draw','profile-context')
$command=if($timed) {$Mode+' '+([Environment]::TickCount64+[long]$DurationSeconds*1000).ToString([Globalization.CultureInfo]::InvariantCulture)}else{$Mode}
if($Mode -eq 'profile-context') {$command+=' '+$SampleEvery}
$command | Set-Content -LiteralPath $temp -Encoding ascii
Move-Item -LiteralPath $temp -Destination (Join-Path $session 'mode.txt') -Force
if($timed) {Write-Output "Requested bridge mode: $Mode (experiment expires after $DurationSeconds seconds)"}
else {Write-Output "Requested bridge mode: $Mode"}
