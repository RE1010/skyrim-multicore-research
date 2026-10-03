param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$current=Get-Content -LiteralPath (Join-Path $project 'measurements\live-culling\current-session.json') | ConvertFrom-Json
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
$allowed=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-culling'))+'\'
if(-not $session.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unexpected session directory.'}
$gameProcess=Get-Process -Id $current.processId
if($gameProcess.ProcessName -ne 'SkyrimSE') {throw 'Session does not belong to a running Skyrim process.'}
$summary=Get-Content -LiteralPath (Join-Path $session 'summary.json') | ConvertFrom-Json
if($summary.status -ne 'waiting-for-trigger') {throw "Capture is not ready: $($summary.status)"}
Set-Content -LiteralPath (Join-Path $session 'capture.trigger') -Value 'start' -Encoding ascii
Write-Output "Capture requested: $session. Maximum 20 seconds or 100000 attempts; original decisions preserved."
