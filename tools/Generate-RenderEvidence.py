"""Emit startup evidence only for the inspected Skyrim render CALL and full bodies."""
import argparse
import importlib.util
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location('movement_image', Path(__file__).with_name('Inspect-MovementRuntime.py'))
image_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image_module)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    image = image_module.Image(args.exe)
    call, function = 0x15601cf, 0x1560340
    instruction = image.read(call, 5)
    if instruction[0] != 0xe8 or call+5+struct.unpack_from('<i', instruction, 1)[0] != function:
        raise ValueError('Unexpected render CALL')
    if (0x155ff40, 0x15602a8) not in image.functions or (function, 0x15606a1) not in image.functions:
        raise ValueError('Unexpected render function boundaries')
    # Caller prepares RCX/RDX/R8D/R9D, consumes no return value. Callee reads
    # RCX, EDX, R8B, R9D; no floating-point or stack arguments are assumed.
    if image.read(0x15601c1, 14) != bytes.fromhex('418b5500458bcc440fb6c6488bcb'):
        raise ValueError('Unexpected argument setup')
    if image.read(call+5, 4) != bytes.fromhex('488b5b30'):
        raise ValueError('Unexpected void-return call continuation')
    text = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace render_evidence {\n'
    text += f'inline constexpr char sha256[]="{image_module.EXPECTED}";\n'
    text += 'inline constexpr std::uint32_t version=0x01070680;\n'
    for name, rva, length in [('caller',0x155ff40,0x368),('function',function,0x361),('call',call,5)]:
        code = image.read(rva, length)
        text += f'inline constexpr std::uintptr_t {name}_rva=0x{rva:x};\n'
        text += f'inline constexpr std::array<unsigned char,{length}> {name}_bytes={{'+','.join(hex(b) for b in code)+'};\n'
    text += 'inline constexpr std::uintptr_t shader_rva=0x36939d8, technique_rva=0x36939d4, material_rva=0x36939e0;\n}\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding='utf-8')
