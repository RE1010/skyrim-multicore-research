[CmdletBinding()]
param([ValidateRange(0,30)][int]$FocusDelaySeconds=8)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$current=Get-Content -LiteralPath (Join-Path $project 'measurements\live-render\current-session.json') -Raw | ConvertFrom-Json
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
$allowed=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-render'))+'\'
if(-not $session.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unexpected session path.'}
$process=Get-Process -Id $current.processId
if($process.ProcessName -ne 'SkyrimSE' -or $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $current.processStartFileTime) {throw 'Session process exited or restarted.'}
if('RenderPassProbe.dll' -notin @($process.Modules.ModuleName)) {throw 'Render plugin is not loaded.'}
if(Test-Path -LiteralPath (Join-Path $session 'capture.trigger')) {throw 'Session already triggered.'}
$summary=Get-Content -LiteralPath (Join-Path $session 'summary.json') -Raw | ConvertFrom-Json
if($summary.status -ne 'waiting-for-trigger' -or $summary.mode -ne 'render-pass-diagnostic' -or -not $summary.originalRendererPreserved -or $summary.engineWorkOffloaded) {throw 'Render diagnostics not ready.'}
if($FocusDelaySeconds) {Start-Sleep -Seconds $FocusDelaySeconds}
$process.Refresh()
if($process.HasExited) {throw 'Skyrim exited before capture.'}
Set-Content -LiteralPath (Join-Path $session 'capture.trigger') -Value 'start' -Encoding ascii
$process.Dispose()
Write-Output "Render capture requested: $session"
