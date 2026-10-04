[CmdletBinding()]
param([Parameter(Mandatory)][string]$RequestPath)
$ErrorActionPreference='Stop'
$request = Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
try {
    $bridgeMode=if($request.bridgeMode){[string]$request.bridgeMode}else{'none'}
    $sampleEvery=if($request.sampleEvery){[int]$request.sampleEvery}else{16}
    & (Join-Path $PSScriptRoot 'Capture-Baseline.ps1') -ProcessId $request.processId -Scene $request.scene `
        -OutputDirectory $request.outputDirectory -DurationSeconds $request.durationSeconds -RequireUncapped:([bool]$request.requireUncapped) -BridgeMode $bridgeMode -SampleEvery $sampleEvery
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $request.outputDirectory 'launch-error.txt') -Encoding utf8
    exit 1
}
