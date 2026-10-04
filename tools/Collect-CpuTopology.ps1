[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
if(-not ('SkyrimResearchCpuSets' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class SkyrimResearchCpuSets {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetSystemCpuSetInformation(IntPtr info, uint size, out uint required, IntPtr process, uint flags);
    public sealed class CpuSet {
        public uint Id; public ushort Group; public byte LogicalProcessor, Core, EfficiencyClass;
        public bool Parked;
    }
    public static CpuSet[] Read() {
        uint length; GetSystemCpuSetInformation(IntPtr.Zero, 0, out length, IntPtr.Zero, 0);
        if(length==0) throw new Win32Exception(Marshal.GetLastWin32Error());
        IntPtr buffer=Marshal.AllocHGlobal(checked((int)length));
        try {
            uint valid;
            if(!GetSystemCpuSetInformation(buffer,length,out valid,IntPtr.Zero,0)) throw new Win32Exception(Marshal.GetLastWin32Error());
            var result=new List<CpuSet>(); int offset=0;
            while(offset<valid) {
                if(valid-offset<8) throw new InvalidOperationException("Truncated CPU Set header");
                IntPtr p=IntPtr.Add(buffer,offset);int size=Marshal.ReadInt32(p,0);
                if(size<8 || size>valid-offset) throw new InvalidOperationException("Invalid CPU Set size");
                if(Marshal.ReadInt32(p,4)==0) {
                    if(size<32) throw new InvalidOperationException("Truncated CpuSet record");
                    result.Add(new CpuSet {Id=unchecked((uint)Marshal.ReadInt32(p,8)),Group=unchecked((ushort)Marshal.ReadInt16(p,12)),
                        LogicalProcessor=Marshal.ReadByte(p,14),Core=Marshal.ReadByte(p,15),EfficiencyClass=Marshal.ReadByte(p,18),Parked=(Marshal.ReadByte(p,19)&1)!=0});
                }
                offset+=size;
            }
            return result.ToArray();
        } finally {Marshal.FreeHGlobal(buffer);}
    }
}
'@
}
$sets=@([SkyrimResearchCpuSets]::Read())
if(-not $sets.Count) {throw 'No CPU Sets returned.'}
[ordered]@{
    schemaVersion=1;source='GetSystemCpuSetInformation';capturedAtUTC=[DateTime]::UtcNow.ToString('o')
    affinityChanged=$false;cpuSets=$sets
    limitation='CPU topology only. Join group/logical-processor IDs to the same-window ETW scheduling/sample records; topology alone does not show thread placement. Higher EfficiencyClass means faster hardware, not a measured throughput ratio.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
