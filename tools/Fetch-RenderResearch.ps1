[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$headers=@{'User-Agent'='Skyrim-local-render-research';'Accept'='application/vnd.github+json'}
$repositories=@(
    @{Name='community-shaders/skyrim-community-shaders';Files=@('src/Hooks.cpp','src/State.cpp','src/ShaderCache.cpp','src/Features/PerformanceOverlay.cpp')},
    @{Name='aers/EngineFixesSkyrim64';Files=@('src/patches/form_caching.h','src/patches/tree_lod_reference_caching.cpp','src/patches/memorymanager.cpp')}
)
$records=@()
foreach($repository in $repositories){
    $info=Invoke-RestMethod -Uri ('https://api.github.com/repos/'+$repository.Name) -Headers $headers
    $commit=Invoke-RestMethod -Uri ('https://api.github.com/repos/'+$repository.Name+'/commits/'+$info.default_branch) -Headers $headers
    $root=Join-Path $project ('vendor\render-research\'+$info.name+'-'+$commit.sha.Substring(0,12))
    foreach($file in $repository.Files){
        $url='https://raw.githubusercontent.com/'+$repository.Name+'/'+$commit.sha+'/'+$file
        $target=Join-Path $root $file
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        try{
            Invoke-WebRequest -Uri $url -Headers $headers -OutFile $target -UseBasicParsing
            $records+=@{Repository=$repository.Name;Commit=$commit.sha;Source=$file;URL=$url;LocalPath=$target;SHA256=(Get-FileHash -LiteralPath $target).Hash;Status='fetched'}
        }catch{
            $records+=@{Repository=$repository.Name;Commit=$commit.sha;Source=$file;URL=$url;Status='unavailable';Reason=$_.Exception.Message}
        }
    }
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $project 'research/render-source-manifest.json') -Encoding utf8
$records | Select-Object Repository,Commit,Source,Status | Format-Table -AutoSize
