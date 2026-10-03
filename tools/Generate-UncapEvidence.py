"""Evidence for verified limiter settings, physics CALL, timer and frame CALL."""
import argparse
import importlib.util
import struct
from pathlib import Path
spec=importlib.util.spec_from_file_location('image',Path(__file__).with_name('Inspect-MovementRuntime.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('exe',type=Path);parser.add_argument('output',type=Path);a=parser.parse_args()
    image=module.Image(a.exe)
    chunks=[('physics_caller',0x659d10,0xd9),('physics_function',0x104ab70,0xaf),('physics_call',0x659db6,5),
            ('present_function',0x1009f70,0xd6),('present_call',0x656bb1,5),('timer',0xce36b0,0x2b8),('physics_threshold',0x1b5bdc4,4)]
    for call,target in [(0x659db6,0x104ab70),(0x656bb1,0x1009f70)]:
        b=image.read(call,5)
        if b[0]!=0xe8 or call+5+struct.unpack_from('<i',b,1)[0]!=target:raise ValueError('Unexpected CALL')
    settings=[('lock',0x20d01f8,'bLockFramerate:Display'),('clamp',0x20b5b88,'iFPSClamp:General'),
              ('max_time',0x20d1778,'fMaxTime:HAVOK'),('complex_time',0x20d1910,'fMaxTimeComplex:HAVOK'),
              ('steps',0x20d18e0,'uMaxNumPhysicsStepsPerUpdate:HAVOK'),('complex_steps',0x20d18f8,'uMaxNumPhysicsStepsPerUpdateComplex:HAVOK')]
    text='#pragma once\n#include <array>\n#include <cstdint>\nnamespace uncap_evidence {\n'
    text+=f'inline constexpr char sha256[]="{module.EXPECTED}";\ninline constexpr std::uint32_t version=0x01070680;\n'
    for name,rva,length in chunks:
        text+=f'inline constexpr std::uintptr_t {name}_rva=0x{rva:x};\ninline constexpr std::array<unsigned char,{length}> {name}_bytes={{'+','.join(hex(b) for b in image.read(rva,length))+'};\n'
    for name,rva,label in settings:
        pointer=struct.unpack('<Q',image.read(rva+16,8))[0]-0x140000000
        if image.read(pointer,len(label)+1)!=label.encode()+b'\0':raise ValueError('Setting identity')
        text+=f'inline constexpr std::uintptr_t {name}_rva=0x{rva:x}, {name}_name_rva=0x{pointer:x};\ninline constexpr char {name}_name[]="{label}";\n'
    text+='inline constexpr std::uintptr_t unscaled_delta_rva=0x327560c, renderer_rva=0x3330188;\n}\n'
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
