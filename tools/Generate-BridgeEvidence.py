"""Exact binary evidence for the five-argument render loop and actual D3D device/context."""
import argparse,importlib.util,struct
from pathlib import Path
spec=importlib.util.spec_from_file_location('image',Path(__file__).with_name('Inspect-MovementRuntime.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('exe',type=Path);p.add_argument('output',type=Path);a=p.parse_args();image=module.Image(a.exe)
    if (0x1521190,0x15212c9) not in image.functions or (0x155ff40,0x15602a8) not in image.functions:raise ValueError('Render loop boundaries changed')
    call=0x1521289;instruction=image.read(call,5)
    if instruction!=bytes.fromhex('e8b2ec0300') or call+5+struct.unpack_from('<i',instruction,1)[0]!=0x155ff40:raise ValueError('Render CALL changed')
    if image.read(0x1521274,5)!=bytes.fromhex('896c2420'+'4c') or image.read(0x152128e,8)!=bytes.fromhex('88834001000084c0'):raise ValueError('Fifth argument / AL return changed')
    if image.read(0x1012445,7)!=bytes.fromhex('488905e4fa3102'):raise ValueError('Immediate context global changed')
    text='#pragma once\n#include <array>\n#include <cstdint>\nnamespace bridge_evidence {\n'
    text+=f'inline constexpr char sha256[]="{module.EXPECTED}";\ninline constexpr std::uint32_t version=0x01070680;\n'
    for name,rva,length in [('caller',0x1521190,0x139),('function',0x155ff40,0x368),('device_creation',0x1012060,0x418),('call',call,5)]:
        text+=f'inline constexpr std::uintptr_t {name}_rva=0x{rva:x};\ninline constexpr std::array<unsigned char,{length}> {name}_bytes={{'+','.join(hex(b) for b in image.read(rva,length))+'};\n'
    text+='inline constexpr std::uintptr_t device_rva=0x3330190, context_rva=0x3331f30, renderer_rva=0x3330188;\n}\n'
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text,encoding='utf-8')
