[CmdletBinding()]
param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',[switch]$PauseVisibility)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
if(Get-Process -Name SkyrimSE,skse64_loader -ErrorAction SilentlyContinue) {throw 'Exit Skyrim normally before installation.'}
if((Get-FileHash -LiteralPath (Join-Path $game 'SkyrimSE.exe')).Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown Skyrim executable.'}
$files=@('MovementMessageProbe.dll','MovementMessageProbe.ini')
foreach($file in $files) {
    $source=Join-Path $project ('build\lab\'+$file)
    if(-not(Test-Path -LiteralPath $source -PathType Leaf)) {throw "Missing build: $file"}
    $target=[IO.Path]::GetFullPath((Join-Path $game ('Data\SKSE\Plugins\'+$file)))
    if(-not $target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid target.'}
}
$session=Join-Path $project ('measurements\movement-installation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$manifest=[ordered]@{GameDirectory=$game;Status='installing';Files=@();PausedVisibility=$null}
foreach($file in $files) {
    $source=Join-Path $project ('build\lab\'+$file)
    $target=Join-Path $game ('Data\SKSE\Plugins\'+$file)
    $backup=$null
    if(Test-Path -LiteralPath $target) {$backup=Join-Path $session ('backup\'+$file);New-Item -ItemType Directory -Path (Split-Path $backup -Parent) -Force | Out-Null;Copy-Item -LiteralPath $target -Destination $backup}
    $entry=[ordered]@{File=$file;Target=$target;Backup=$backup;SourceHash=(Get-FileHash -LiteralPath $source).Hash;Installed=$false}
    $manifest.Files+=,$entry
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session 'manifest.json') -Encoding utf8
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne $entry.SourceHash) {throw "Copy verification failed: $file"}
    $entry.Installed=$true
}
if($PauseVisibility) {
    $visibility=[IO.Path]::GetFullPath((Join-Path $game 'Data\SKSE\Plugins\MulticoreVisibilityShadow.dll'))
    $paused=$visibility+'.movement-test-disabled'
    if(-not $visibility.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $paused.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid pause target.'}
    if(Test-Path -LiteralPath $visibility) {
        if(Test-Path -LiteralPath $paused) {throw 'Existing paused visibility file: do not overwrite.'}
        $backup=Join-Path $session 'backup\MulticoreVisibilityShadow.dll'
        New-Item -ItemType Directory -Path (Split-Path $backup -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $visibility -Destination $backup
        $manifest.PausedVisibility=[ordered]@{Original=$visibility;Paused=$paused;Backup=$backup;SHA256=(Get-FileHash -LiteralPath $visibility).Hash;Moved=$false}
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session 'manifest.json') -Encoding utf8
        Move-Item -LiteralPath $visibility -Destination $paused
        $manifest.PausedVisibility.Moved=$true
    }
}
$manifest.Status='complete'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session 'manifest.json') -Encoding utf8
Write-Output "Installed two verified movement diagnostic files. Manifest: $session\manifest.json"
