[CmdletBinding()]
param([Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
      [Parameter(Mandatory)][long]$ProcessStartFileTime,[switch]$HashRuntime)
$ErrorActionPreference='Stop'
if(-not [Environment]::Is64BitProcess) {throw '64-bit recorder required.'}
if(-not ('SkyrimReferenceReadOnly' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class SkyrimReferenceReadOnly {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static byte[][] Read(uint pid, long[] addresses, int[] lengths) {
        if(addresses.Length!=lengths.Length) throw new ArgumentException("Mismatched reads");
        // Query-limited + VM_READ only: no write, remote execution or thread control rights.
        IntPtr process=OpenProcess(0x1010,false,pid);
        if(process==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            byte[][] result=new byte[addresses.Length][];
            for(int i=0;i<result.Length;i++) {
                if(addresses[i]<=0 || lengths[i]<1 || lengths[i]>4096) throw new ArgumentException("Invalid read");
                result[i]=new byte[lengths[i]]; UIntPtr read;
                if(!ReadProcessMemory(process,new IntPtr(addresses[i]),result[i],new UIntPtr((uint)lengths[i]),out read) || read.ToUInt64()!=(ulong)lengths[i])
                    throw new Win32Exception(Marshal.GetLastWin32Error(),"Incomplete read-only reference observation");
            }
            return result;
        } finally {CloseHandle(process);}
    }
}
'@
}
$process=Get-Process -Id $ProcessId
try {
    if($process.ProcessName -ne 'SkyrimSE' -or $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $ProcessStartFileTime) {throw 'Stale reference process identity.'}
    $modules=@($process.Modules)
    $forbidden=@('RenderWorkerBridge.dll','MovementMessageProbe.dll','RenderPassProbe.dll','MulticoreVisibilityShadow.dll')
    if(@($modules | Where-Object ModuleName -in $forbidden).Count) {throw 'Project diagnostic DLL is still loaded.'}
    $d3d=@($modules | Where-Object ModuleName -eq 'd3d11.dll')
    if($d3d.Count -ne 1) {throw 'One live D3D11 runtime required.'}
    $image=$process.MainModule.BaseAddress.ToInt64()
    $reads=[SkyrimReferenceReadOnly]::Read([uint32]$ProcessId,
        [long[]]@(($image+0x1521289),($image+0x673d68),($image+0x797b9b),($image+0x3331f30),($image+0x3330188)),[int[]]@(5,5,5,8,8))
    $expected=@('E8-B2-EC-03-00','E8-03-3E-12-00','E8-00-24-A0-FF')
    $callSites=@()
    for($index=0;$index -lt 3;$index++) {
        $actual=[BitConverter]::ToString($reads[$index])
        if($actual -ne $expected[$index]) {throw 'Project render/movement call site is not original.'}
        $callSites+=@{rva=@('0x1521289','0x673d68','0x797b9b')[$index];bytes=$actual;original=$true}
    }
    $context=[BitConverter]::ToInt64($reads[3],0)
    $renderer=[BitConverter]::ToInt64($reads[4],0)
    if($context -le 0 -or $renderer -le 0) {throw 'Renderer/context not ready.'}
    $identity=[SkyrimReferenceReadOnly]::Read([uint32]$ProcessId,[long[]]@($context,($renderer+0x40)),[int[]]@(8,8))
    $vtable=[BitConverter]::ToInt64($identity[0],0)
    if([BitConverter]::ToInt64($identity[1],0) -ne $context) {throw 'Live renderer context alias mismatch.'}
    $d3dBase=$d3d[0].BaseAddress.ToInt64();$d3dEnd=$d3dBase+$d3d[0].ModuleMemorySize
    # The base ID3D11DeviceContext has 115 methods. Do not assume Context4 support.
    # The runtime can store this table beside the context object in writable
    # allocated memory. Its storage address does not identify the callees.
    if($vtable -le 0) {throw 'Invalid context table address.'}
    $table=[SkyrimReferenceReadOnly]::Read([uint32]$ProcessId,[long[]]@($vtable),[int[]]@(115*8))
    $methodRVAs=@()
    for($slot=0;$slot -lt 115;$slot++) {
        $pointer=[BitConverter]::ToInt64($table[0],$slot*8)
        if($pointer -lt $d3dBase -or $pointer -ge $d3dEnd) {throw "Context method $slot is redirected outside D3D11."}
        $methodRVAs+=('0x{0:X}' -f ($pointer-$d3dBase))
    }
    $process.Refresh()
    if($process.HasExited -or $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $ProcessStartFileTime) {throw 'Process changed during reference observation.'}
    [pscustomobject]@{
        schemaVersion=1;kind='project-hook-free-reference';processId=$ProcessId;processStartFileTime=$ProcessStartFileTime
        observedQPC=[Diagnostics.Stopwatch]::GetTimestamp();qpcFrequency=[Diagnostics.Stopwatch]::Frequency
        observedAtUTC=[DateTime]::UtcNow.ToString('o');projectDiagnosticModulesAbsent=$true
        originalCallSites=$callSites;contextMethodsInD3D11=$true;baseContextMethodsChecked=115;contextMethodRVAs=$methodRVAs
        contextTableStorage=$(if($vtable -ge $d3dBase -and $vtable+115*8 -le $d3dEnd){'inside-runtime-image'}else{'outside-runtime-image'})
        contextTableOffsetFromObject=($vtable-$context)
        d3d11Base=('0x{0:X}' -f $d3dBase);d3d11Size=$d3d[0].ModuleMemorySize
        d3d11SHA256=$(if($HashRuntime){(Get-FileHash -LiteralPath $d3d[0].FileName).Hash}else{$null})
        limitation='Project hooks absent at observation points; this does not exclude third-party inline hooks, validate scene/camera, or remove SKSE, uncapping, Steam/OS/driver overhead.'
    }
} finally {$process.Dispose()}
