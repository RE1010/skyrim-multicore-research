param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',[switch]$PluginOnly)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
if(Get-Process SkyrimSE,skse64_loader -ErrorAction SilentlyContinue) {throw 'Exit Skyrim before installation.'}
if((Get-FileHash -LiteralPath "$game\SkyrimSE.exe").Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown Skyrim executable.'}
$package=Join-Path $project 'vendor\skse\extracted\skse64_2_03_01'
if((Get-FileHash -LiteralPath (Join-Path $project 'vendor\skse\skse64_2_3_1.7z')).Hash -ne '7BAD616ED360823A027F8828801D91E3A4AA2EACD952023AF7F3E31ADB2AE250') {throw 'SKSE archive hash mismatch.'}
$files=@(
    @{Source=Join-Path $package 'skse64_loader.exe';Relative='skse64_loader.exe'},
    @{Source=Join-Path $package 'skse64_1_7_104.dll';Relative='skse64_1_7_104.dll'},
    @{Source=Join-Path $project 'build\lab\MulticoreVisibilityShadow.dll';Relative='Data\SKSE\Plugins\MulticoreVisibilityShadow.dll'},
    @{Source=Join-Path $project 'build\lab\MulticoreVisibilityShadow.ini';Relative='Data\SKSE\Plugins\MulticoreVisibilityShadow.ini'}
)
$files+=@(Get-ChildItem -LiteralPath (Join-Path $package 'Data\Scripts') -File -Filter '*.pex' | ForEach-Object {
    @{Source=$_.FullName;Relative='Data\Scripts\'+$_.Name}
})
if($PluginOnly) {$files=@($files | Where-Object {$_.Relative.StartsWith('Data\SKSE\Plugins\')})}
# Validate every source and destination before changing the game directory.
foreach($file in $files) {
    if(-not(Test-Path -LiteralPath $file.Source -PathType Leaf)) {throw "Missing source: $($file.Source)"}
    $target=[IO.Path]::GetFullPath((Join-Path $game $file.Relative))
    if(-not $target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Destination escapes game directory.'}
}
$session=Join-Path $project ('measurements\installation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$manifest=[ordered]@{GameDirectory=$game;Status='installing';Files=@()}
foreach($file in $files) {
    $target=Join-Path $game $file.Relative
    $backup=$null
    if(Test-Path -LiteralPath $target) {
        $backup=Join-Path $session ('backup\'+$file.Relative)
        New-Item -ItemType Directory -Path (Split-Path $backup -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $target -Destination $backup
    }
    $entry=[ordered]@{Relative=$file.Relative;Target=$target;Backup=$backup;SourceHash=(Get-FileHash -LiteralPath $file.Source).Hash;Installed=$false}
    $manifest.Files+=,$entry
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session 'manifest.json') -Encoding utf8
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne $entry.SourceHash) {throw "Copy verification failed: $target"}
    $entry.Installed=$true
}
$manifest.Status='complete'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session 'manifest.json') -Encoding utf8
Write-Output "Installed and verified $($files.Count) files. Manifest: $session\manifest.json"
