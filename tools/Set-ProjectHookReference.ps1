[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Disable','Restore')][string]$Action,
    [string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',
    [string]$ManifestPath=''
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)).TrimEnd('\')
$gameRoot=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
$pluginRoot=[IO.Path]::GetFullPath((Join-Path $gameRoot 'Data\SKSE\Plugins'))
if(-not $pluginRoot.StartsWith($gameRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Plugin directory escapes game root.'}
function Assert-Closed {
    if(Get-Process -Name SkyrimSE,skse64_loader -ErrorAction SilentlyContinue) {throw 'Exit Skyrim normally before changing startup plugins.'}
}
Assert-Closed
if((Get-FileHash -LiteralPath (Join-Path $gameRoot 'SkyrimSE.exe') -Algorithm SHA256).Hash -ne '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F') {throw 'Unknown Skyrim executable.'}
$names=@('RenderWorkerBridge.dll','MovementMessageProbe.dll','RenderPassProbe.dll','MulticoreVisibilityShadow.dll')
# Exact installed, locally verified diagnostic versions. Unknown binaries are not disabled.
$knownHashes=@{
    'RenderWorkerBridge.dll'='46CC7670F208FB94C122017DED24710C7BC3A513CB976CB4C3D8FF1EAA82ACEC'
    'MovementMessageProbe.dll'='5B1A4514291FEDD84D2133B389F01FC4AB623BA6ECD2576BA095DE2DF6D8555F'
}
$uncap=Join-Path $pluginRoot 'UncappedBenchmark.dll'
$uncapIni=Join-Path $pluginRoot 'UncappedBenchmark.ini'
if($Action -eq 'Disable') {
    if($ManifestPath) {throw 'Disable creates a new backup manifest; do not supply one.'}
    $selected=@()
    foreach($fileName in $names) {
        $target=Join-Path $pluginRoot $fileName
        if(Test-Path -LiteralPath $target -PathType Leaf) {
            $hash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            if(-not $knownHashes.ContainsKey($fileName) -or $knownHashes[$fileName] -ne $hash) {throw "Unrecognized research DLL: $fileName; no files changed."}
            $selected+=@{name=$fileName;hash=$hash;target=$target;backup=$null;disabled=$false}
        }
    }
    if(-not $selected.Count) {throw 'No verified project diagnostic DLLs to disable.'}
    $run=Join-Path $project ('measurements\project-hook-reference-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $backup=Join-Path $run 'backup'
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $ManifestPath=Join-Path $run 'manifest.json'
    $manifest=[ordered]@{schemaVersion=1;kind='project-hook-free-startup';status='backing-up';gameDirectory=$gameRoot;createdAtUTC=[DateTime]::UtcNow.ToString('o');files=$selected;uncapDLLHash=(Get-FileHash -LiteralPath $uncap).Hash;uncapINIHash=(Get-FileHash -LiteralPath $uncapIni).Hash;scope='Only this project render/culling/movement diagnostics; SKSE, uncapping and other software remain.'}
    foreach($entry in $manifest.files) {
        $entry.backup=Join-Path $backup $entry.name
        Copy-Item -LiteralPath $entry.target -Destination $entry.backup
        if((Get-FileHash -LiteralPath $entry.backup).Hash -ne $entry.hash) {throw 'Backup hash mismatch; originals retained.'}
    }
} else {
    if(-not $ManifestPath) {throw 'Restore needs the exact backup manifest.'}
    $ManifestPath=[IO.Path]::GetFullPath($ManifestPath)
    $referenceRoot=Join-Path $project 'measurements'
    if(-not $ManifestPath.StartsWith($referenceRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Manifest outside project measurements.'}
    $manifest=Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if($manifest.kind -ne 'project-hook-free-startup' -or $manifest.gameDirectory -ne $gameRoot) {throw 'Wrong manifest identity.'}
    $backupRoot=Join-Path (Split-Path $ManifestPath -Parent) 'backup'
    foreach($entry in $manifest.files) {
        if($entry.name -notin $names -or -not $knownHashes.ContainsKey($entry.name) -or $entry.hash -ne $knownHashes[$entry.name] -or
           [IO.Path]::GetFullPath($entry.target) -ne (Join-Path $pluginRoot $entry.name) -or
           [IO.Path]::GetFullPath($entry.backup) -ne (Join-Path $backupRoot $entry.name)) {throw 'Unexpected backup or destination identity.'}
        if((Get-FileHash -LiteralPath $entry.backup).Hash -ne $entry.hash) {throw 'Backup hash mismatch.'}
        if((Test-Path -LiteralPath $entry.target) -and (Get-FileHash -LiteralPath $entry.target).Hash -ne $entry.hash) {throw 'Restore would overwrite a different installed DLL.'}
    }
}
function Save-Manifest { $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ManifestPath -Encoding utf8 }
Save-Manifest
try {
    foreach($entry in $manifest.files) {
        Assert-Closed
        # Both absolute paths have been verified inside the exact game/backup roots.
        if($Action -eq 'Disable') {
            if((Get-FileHash -LiteralPath $entry.target).Hash -ne $entry.hash) {throw 'Installed DLL changed after backup.'}
            Remove-Item -LiteralPath $entry.target
            $entry.disabled=$true
        } else {
            if((Get-FileHash -LiteralPath $entry.backup).Hash -ne $entry.hash) {throw 'Backup changed before restoration.'}
            if(Test-Path -LiteralPath $entry.target) {
                if((Get-FileHash -LiteralPath $entry.target).Hash -ne $entry.hash) {throw 'A different DLL appeared before restoration.'}
            } else {
                # Atomic no-overwrite copy refuses a destination created concurrently.
                [IO.File]::Copy($entry.backup,$entry.target,$false)
            }
            if((Get-FileHash -LiteralPath $entry.target).Hash -ne $entry.hash) {throw 'Restored DLL hash mismatch.'}
            $entry.disabled=$false
        }
        Save-Manifest
    }
    if((Get-FileHash -LiteralPath $uncap).Hash -ne $manifest.uncapDLLHash -or (Get-FileHash -LiteralPath $uncapIni).Hash -ne $manifest.uncapINIHash) {throw 'Uncapping files changed during startup preparation.'}
    $manifest.status=if($Action -eq 'Disable'){'disabled'}else{'restored'}
} catch {$manifest.status='failed';$manifest | Add-Member -NotePropertyName error -NotePropertyValue $_.Exception.Message -Force;throw}
finally {Save-Manifest}
Write-Output $ManifestPath
