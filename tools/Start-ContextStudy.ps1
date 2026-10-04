[CmdletBinding()]
param([Parameter(Mandatory)][string]$Scene,[ValidateRange(1,5)][int]$Repeats=3)
$ErrorActionPreference='Stop'
$game=@(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue)
if($game.Count -ne 1) {throw 'Exactly one ready SkyrimSE process required.'}
$study=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\measurements\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-context-study')))
New-Item -ItemType Directory -Path $study -Force | Out-Null
$request=Join-Path $study 'request.json'
[ordered]@{scene=$Scene;repeats=$Repeats;processId=$game[0].Id;processStartFileTime=$game[0].StartTime.ToUniversalTime().ToFileTimeUtc();studyDirectory=$study} |
    ConvertTo-Json | Set-Content -LiteralPath $request -Encoding utf8
$game[0].Dispose()
$helper=Join-Path $PSScriptRoot 'Run-ContextStudyRequest.ps1'
$hostExe=(Get-Process -Id $PID).Path
Start-Process -FilePath $hostExe -ArgumentList @('-NoProfile','-File',('"{0}"' -f $helper),'-RequestPath',('"{0}"' -f $request)) -Verb RunAs -WindowStyle Hidden | Out-Null
Write-Output $study
