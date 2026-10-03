[CmdletBinding()]
param([ValidateRange(0,30)][int]$FocusDelaySeconds=8)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$current=Get-Content -LiteralPath (Join-Path $project 'measurements\live-movement\current-session.json') | ConvertFrom-Json
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
$allowed=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-movement'))+'\'
if(-not $session.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unexpected session path.'}
$process=Get-Process -Id $current.processId
if($process.ProcessName -ne 'SkyrimSE' -or $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $current.processStartFileTime) {throw 'Session process exited or was restarted.'}
if('MovementMessageProbe.dll' -notin @($process.Modules.ModuleName)) {throw 'Movement plugin not loaded.'}
if(Test-Path -LiteralPath (Join-Path $session 'capture.trigger')) {throw 'This session was already triggered.'}
$summary=Get-Content -LiteralPath (Join-Path $session 'summary.json') | ConvertFrom-Json
if($summary.status -ne 'waiting-for-trigger' -or $summary.mode -notin @('movement-diagnostic-only','movement-fast-shadow')) {throw "Movement diagnostic is not ready: $($summary.status)"}
if($FocusDelaySeconds) {Start-Sleep -Seconds $FocusDelaySeconds}
$process.Refresh()
if($process.HasExited) {throw 'Skyrim exited before capture.'}
Set-Content -LiteralPath (Join-Path $session 'capture.trigger') -Value 'start' -Encoding ascii
Write-Output "Movement capture requested: $session"
