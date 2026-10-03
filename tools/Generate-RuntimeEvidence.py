"""Generate private build evidence from the explicitly verified local Skyrim EXE."""
import argparse
import hashlib
import struct
from pathlib import Path

EXPECTED = '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest().upper() != EXPECTED:
        raise ValueError('Unknown runtime hash: do not generate hook evidence')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe+6)[0]
    size = struct.unpack_from('<H', data, pe+20)[0]
    def read_rva(rva, length):
        for i in range(count):
            section = pe+24+size+i*40
            vs, start, raw_size, raw = struct.unpack_from('<IIII', data, section+8)
            if start <= rva and rva+length <= start+raw_size:
                return data[raw+rva-start:raw+rva-start+length]
        raise ValueError('Unmapped RVA')
    code = read_rva(0xfee940, 0xc6)
    if code[:9] != bytes.fromhex('48895c2408f30f101d') or code[-1] != 0xc3:
        raise ValueError('Unexpected function shape')
    sign = read_rva(0x1b5c730, 4)
    if sign != bytes.fromhex('00000080'):
        raise ValueError('Unexpected sign mask')
    tables = [0x1a6ef90,0x1868af8]
    for table in tables:
        if struct.unpack('<Q',read_rva(table+0x1c*8,8))[0] != 0x140fee940:
            raise ValueError('Vtable does not point to the verified function')
    text = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace runtime_evidence {\n'
    text += f'inline constexpr char sha256[] = "{EXPECTED}";\n'
    text += 'inline constexpr std::uint32_t version = 0x01070680;\n'
    text += 'inline constexpr std::uintptr_t vtable_rva = 0x1a6ef90, function_rva = 0xfee940;\n'
    text += 'inline constexpr std::array<std::uintptr_t,2> vtable_rvas = {0x1a6ef90,0x1868af8};\n'
    text += f'inline constexpr std::array<unsigned char,{len(code)}> function_bytes = {{'
    text += ','.join(hex(byte) for byte in code)+'};\n'
    movement = read_rva(0x797b70, 0xe2)
    lock = read_rva(0x199fa0, 0xbb)
    caller = read_rva(0x673d68, 5)
    lock_call = read_rva(0x797b9b, 5)
    for instruction, address, target in ((caller, 0x673d68, 0x797b70), (lock_call, 0x797b9b, 0x199fa0)):
        if instruction[0] != 0xe8 or address+5+struct.unpack_from('<i', instruction, 1)[0] != target:
            raise ValueError('Unexpected movement call target')
    if movement[-1] != 0xc3 or lock[-1] != 0xc3 or read_rva(0x191c128, 23) != b'MovementMessageNewPath\0':
        raise ValueError('Unexpected movement function/type shape')
    text += 'inline constexpr std::uintptr_t movement_rva=0x797b70, movement_caller_rva=0x673d68;\n'
    text += 'inline constexpr std::uintptr_t movement_lock_rva=0x199fa0, movement_lock_call_rva=0x797b9b;\n'
    for name, values in (('movement_bytes', movement), ('movement_lock_bytes', lock),
                         ('movement_caller_bytes', caller), ('movement_lock_call_bytes', lock_call)):
        text += f'inline constexpr std::array<unsigned char,{len(values)}> {name} = {{'
        text += ','.join(hex(byte) for byte in values)+'};\n'
    text += '}\n'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(text,encoding='utf-8')
    print(args.output)
