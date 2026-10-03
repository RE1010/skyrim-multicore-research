[CmdletBinding()]
param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',[string]$SourceDirectory='')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$sourceRoot=if($SourceDirectory) {[IO.Path]::GetFullPath($SourceDirectory)} else {Join-Path $project 'build\lab'}
$game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
if(Get-Process -Name SkyrimSE,skse64_loader -ErrorAction SilentlyContinue) {throw 'Exit Skyrim normally before installation.'}
if((Get-FileHash -LiteralPath (Join-Path $game 'SkyrimSE.exe')).Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown Skyrim executable.'}
$files=@('RenderWorkerBridge.dll','RenderWorkerBridge.ini')
$plugins=[IO.Path]::GetFullPath((Join-Path $game 'Data\SKSE\Plugins'))
$probe=[IO.Path]::GetFullPath((Join-Path $plugins 'RenderPassProbe.dll'))
if(-not $plugins.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $probe.StartsWith($plugins+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Target escapes game directory.'}
foreach($file in $files) {if(-not(Test-Path -LiteralPath (Join-Path $sourceRoot $file) -PathType Leaf)) {throw "Missing build file: $file"}}
$session=Join-Path $project ('measurements\bridge-installation-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$backup=Join-Path $session 'backup'
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$manifest=[ordered]@{Status='installing';GameDirectory=$game;SourceDirectory=$sourceRoot;Kind='actual-indexed-draw-worker-bridge';LiveOffloadVerified=$false;DisabledProbe=$null;Files=@()}
$manifestPath=Join-Path $session 'manifest.json'
if(Test-Path -LiteralPath $probe) {
    $probeBackup=Join-Path $backup 'RenderPassProbe.dll'
    $hash=(Get-FileHash -LiteralPath $probe).Hash
    Copy-Item -LiteralPath $probe -Destination $probeBackup
    if((Get-FileHash -LiteralPath $probeBackup).Hash -ne $hash) {throw 'Probe backup failed.'}
    $manifest.DisabledProbe=[ordered]@{Target=$probe;Backup=$probeBackup;Hash=$hash;Disabled=$false}
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    # Exact verified single DLL only. Backup remains recoverable in the project.
    Remove-Item -LiteralPath $probe
    $manifest.DisabledProbe.Disabled=$true
}
foreach($file in $files) {
    $source=Join-Path $sourceRoot $file
    $target=[IO.Path]::GetFullPath((Join-Path $plugins $file))
    if(-not $target.StartsWith($plugins+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid target.'}
    $saved=$null
    if(Test-Path -LiteralPath $target) {$saved=Join-Path $backup $file;Copy-Item -LiteralPath $target -Destination $saved}
    $entry=[ordered]@{File=$file;Target=$target;Backup=$saved;SourceHash=(Get-FileHash -LiteralPath $source).Hash;Installed=$false}
    $manifest.Files+=,$entry
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Copy-Item -LiteralPath $source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne $entry.SourceHash) {throw "Copy verification failed: $file"}
    $entry.Installed=$true
}
$manifest.Status='complete'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "Worker bridge installed; live verification pending. Manifest: $manifestPath"
