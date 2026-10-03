[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('off','uncapped')][string]$Mode)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$root=[IO.Path]::GetFullPath((Join-Path $project 'measurements\live-uncap'))
$current=Get-Content -LiteralPath (Join-Path $root 'current-session.json') -Raw | ConvertFrom-Json
$target=Get-Process -Id $current.processId
try {
    if($target.ProcessName -ne 'SkyrimSE' -or $target.StartTime.ToUniversalTime().ToFileTimeUtc() -ne [long]$current.processStartFileTime) {throw 'Stale process identity.'}
} finally {$target.Dispose()}
$session=[IO.Path]::GetFullPath($current.sessionDirectory)
if(-not $session.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Session outside benchmark output.'}
Set-Content -LiteralPath (Join-Path $session 'mode.txt') -Value $Mode -Encoding ascii
Write-Output "Requested mode: $Mode. Check summary.json after the next active engine frames: $session"
