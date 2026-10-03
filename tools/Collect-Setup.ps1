[CmdletBinding()]
param(
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\research\local-setup.json')
)

$ErrorActionPreference = 'Stop'
$exe = Get-Item -LiteralPath (Join-Path $GamePath 'SkyrimSE.exe')
$issues = [System.Collections.Generic.List[string]]::new()

function Get-Names([string]$Path, [string]$Filter = '*') {
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    try { return @(Get-ChildItem -LiteralPath $Path -File -Filter $Filter | Select-Object -ExpandProperty Name) }
    catch { $issues.Add("Cannot list ${Path}: $($_.Exception.Message)"); return @() }
}

$cpu = (Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction SilentlyContinue).ProcessorNameString
$gpu = @(Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Video\*\0000' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.DriverDesc } | Where-Object { $_ } | Sort-Object -Unique)
$gameFiles = Get-Names $GamePath
$nativePlugins = Get-Names (Join-Path $GamePath 'Data\SKSE\Plugins') '*.dll'
$addressLibraries = Get-Names (Join-Path $GamePath 'Data\SKSE\Plugins') '*version*.bin'
$dataPlugins = @(Get-Names (Join-Path $GamePath 'Data') | Where-Object { $_ -match '\.(esm|esp|esl)$' })

# Read only a small allowlist; do not copy account identifiers or complete user files.
$settings = [ordered]@{}
$documents = [Environment]::GetFolderPath('MyDocuments')
$gameSettingsPath = Join-Path $documents 'My Games\Skyrim Special Edition'
foreach ($name in @('Skyrim.ini', 'SkyrimPrefs.ini', 'SkyrimCustom.ini')) {
    $path = Join-Path $gameSettingsPath $name
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $section = ''
    try {
        foreach ($line in Get-Content -LiteralPath $path) {
            if ($line -match '^\s*\[([^]]+)\]') { $section = $Matches[1] }
            if ($line -match '^\s*(iSize [HW]|bFull Screen|bBorderless|iVSyncPresentInterval|fShadowDistance|iShadowMapResolution|fUpdateBudgetMS|fExtraTaskletBudgetMS)\s*=\s*([^;]+)') {
                $settings["${name}/${section}/$($Matches[1])"] = $Matches[2].Trim()
            }
        }
    } catch { $issues.Add("Cannot read ${name}: $($_.Exception.Message)") }
}

$activePlugins = $null
$activePath = Join-Path $env:LOCALAPPDATA 'Skyrim Special Edition\plugins.txt'
if (Test-Path -LiteralPath $activePath) {
    try { $activePlugins = @(Get-Content -LiteralPath $activePath | Where-Object { $_ -match '^\*' }) }
    catch { $issues.Add('Active plugins.txt cannot be read. Active load order is unknown.') }
} else { $issues.Add('No plugins.txt found at the standard path. Active load order is unknown.') }

$tools = @{}
foreach ($name in @('cmake', 'git', 'wpr', 'wpa', 'PresentMon')) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    $tools[$name] = if ($command) { $command.Source } else { $null }
}
$report = [ordered]@{
    schemaVersion = 1
    collectedAt = [DateTimeOffset]::Now.ToString('o')
    game = [ordered]@{
        path = $exe.FullName
        fileVersion = $exe.VersionInfo.FileVersion
        bytes = $exe.Length
        sha256 = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
        skseRootFiles = @($gameFiles | Where-Object { $_ -match '^skse64' })
        graphicsWrapperFiles = @($gameFiles | Where-Object { $_ -match '^(d3d11|dxgi|d3dcompiler_46e)\.dll$' })
        nativePluginsOnDisk = @($nativePlugins)
        addressLibrariesOnDisk = @($addressLibraries)
        dataPluginsOnDisk = @($dataPlugins)
        activePluginsFromStandardFile = $activePlugins
        saveGameCount = @(Get-Names (Join-Path $gameSettingsPath 'Saves') '*.ess').Count
    }
    hardware = @{ cpuRegistry = $cpu; gpuRegistry = $gpu }
    settingsOnDisk = $settings
    toolsOnPath = $tools
    limitations = @(
        'On-disk files do not establish the effective MO2/Vortex load order or loaded modules.',
        'Registry hardware names are inventory, not measured utilization.',
        'No game launch, frame measurement, memory read or engine patch was performed.'
    ) + $issues.ToArray()
}
$absoluteOutput = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($absoluteOutput)) -Force | Out-Null
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $absoluteOutput -Encoding utf8
Write-Output $absoluteOutput
