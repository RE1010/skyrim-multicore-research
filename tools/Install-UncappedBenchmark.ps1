[CmdletBinding()]
param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
if(Get-Process -Name SkyrimSE,skse64_loader -ErrorAction SilentlyContinue) {throw 'Exit Skyrim normally before installing.'}
if((Get-FileHash -LiteralPath (Join-Path $game 'SkyrimSE.exe')).Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown Skyrim executable.'}
$files=@('UncappedBenchmark.dll','UncappedBenchmark.ini')
foreach($file in $files) {
    $source=Join-Path $project ('build\lab\'+$file)
    $target=[IO.Path]::GetFullPath((Join-Path $game ('Data\SKSE\Plugins\'+$file)))
    if(-not(Test-Path -LiteralPath $source -PathType Leaf) -or -not $target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) {throw "Invalid source/target: $file"}
}
$session=Join-Path $project ('measurements\uncap-installation-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $session | Out-Null
$manifest=[ordered]@{Status='installing';GameDirectory=$game;Kind='uncapped-physics-budget-benchmark';EngineWorkOffloaded=$false;Files=@()}
$manifestPath=Join-Path $session 'manifest.json'
foreach($file in $files) {
    $source=Join-Path $project ('build\lab\'+$file)
    $target=Join-Path $game ('Data\SKSE\Plugins\'+$file)
    $backup=$null
    if(Test-Path -LiteralPath $target) {
        $backup=Join-Path $session ('backup\'+$file)
        New-Item -ItemType Directory -Path (Split-Path $backup -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $target -Destination $backup
    }
    $entry=[ordered]@{File=$file;Target=$target;Backup=$backup;SourceHash=(Get-FileHash -LiteralPath $source).Hash;Installed=$false}
    $manifest.Files+=,$entry
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne $entry.SourceHash) {throw "Copy verification failed: $file"}
    $entry.Installed=$true
}
$manifest.Status='complete'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "Uncapped benchmark installed; no engine work offloaded. Manifest: $manifestPath"

