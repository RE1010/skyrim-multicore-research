"""Extract a private exact-runtime queue-init seam for owned-memory validation."""
import argparse
import hashlib
import importlib.util
import struct
from pathlib import Path

spec=importlib.util.spec_from_file_location('image',Path(__file__).with_name('Inspect-MovementRuntime.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def generate(exe, output):
    image=module.Image(exe)
    if (0xfed6e0,0xfed8cb) not in image.functions:
        raise ValueError('Constructor interval changed')
    loop=image.read(0xfed7b0,0xfed844-0xfed7b0)
    if loop[:6]!=bytes.fromhex('8b8b0c000100') or loop[26:28]!=bytes.fromhex('ff15'):
        raise ValueError('Queue-loop shape changed')
    if 0xfed7ca+6+struct.unpack_from('<i',loop,28)[0]!=0x17c8448:
        raise ValueError('Wait import changed')
    if loop[-6:]!=bytes.fromhex('0f856cffffff'):
        raise ValueError('Queue-loop back edge changed')
    # Every initialization immediately before the seam is guarded, not merely
    # constants inferred from a profiler symbol or a partial disassembly.
    pre=image.read(0xfed732,0xfed7b0-0xfed732)
    locator=struct.unpack('<Q',image.read(0x1a6ef90-8,8))[0]-0x140000000
    descriptor=struct.unpack('<I',image.read(locator+12,4))[0]
    if image.read(descriptor+16,80).split(b'\0')[0]!=b'.?AVBSCullingProcess@@':
        raise ValueError('Culling class changed')
    text='#pragma once\n#include <array>\n#include <cstdint>\nnamespace culling_init_evidence {\n'
    text+=f'inline constexpr char sha256[]="{module.EXPECTED}";\n'
    text+='inline constexpr std::uint32_t version=0x01070680;\n'
    text+='inline constexpr std::uintptr_t loop_rva=0xfed7b0, resume_rva=0xfed844, wait_iat_rva=0x17c8448;\n'
    for name,code in [('loop',loop),('prelude',pre),('constructor',image.read(0xfed6e0,0x1eb))]:
        text+=f'inline constexpr std::array<unsigned char,{len(code)}> {name}_bytes={{'+','.join(hex(b) for b in code)+'};\n'
    text+='}\n'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(text,encoding='utf-8')
    print('Verified constructor and private queue-loop evidence: '+hashlib.sha256(loop).hexdigest())

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    generate(args.exe,args.output)
