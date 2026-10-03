[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Scene,
    [ValidateRange(5, 60)][int]$DurationSeconds = 30,
    [switch]$RequireUncapped,
    [ValidateSet('none','off','parallel','parallel-full-bindings','parallel-owned-snapshots')][string]$BridgeMode='none'
)
$ErrorActionPreference='Stop'
$game = @(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue)
if ($game.Count -ne 1) { throw 'Exactly one SkyrimSE process must be running in the ready test scene.' }
$run = Join-Path $PSScriptRoot ('..\measurements\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-baseline')
$run = [IO.Path]::GetFullPath($run)
New-Item -ItemType Directory -Path $run -Force | Out-Null
# Scene text goes into a JSON file instead of being interpolated into an elevated command.
$request = Join-Path $run 'request.json'
@{scene=$Scene;durationSeconds=$DurationSeconds;processId=$game[0].Id;outputDirectory=$run;requireUncapped=[bool]$RequireUncapped;bridgeMode=$BridgeMode} |
    ConvertTo-Json | Set-Content -LiteralPath $request -Encoding utf8
$game[0].Dispose()
$helper = Join-Path $PSScriptRoot 'Run-CaptureRequest.ps1'
$hostExe = (Get-Process -Id $PID).Path
$arguments = @('-NoProfile','-File',('"{0}"' -f $helper),'-RequestPath',('"{0}"' -f $request))
Start-Process -FilePath $hostExe -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden | Out-Null
Write-Output $run
