"""Locate x64 MSVC RTTI and vtables in a file; does not write to the game."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

def locate(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE')
    count = struct.unpack_from('<H', data, pe+6)[0]
    opt_size = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    if struct.unpack_from('<H', data, opt)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    image_base = struct.unpack_from('<Q', data, opt+24)[0]
    sections = []
    for i in range(count):
        off = opt+opt_size+i*40
        name = data[off:off+8].rstrip(b'\0').decode()
        vs, rva, size, raw = struct.unpack_from('<IIII', data, off+8)
        sections.append((name, rva, size, raw, vs))
    def to_rva(offset):
        for name, rva, size, raw, vs in sections:
            if raw <= offset < raw+size:
                return rva+offset-raw
        raise ValueError('Unmapped file offset')
    def to_offset(rva):
        for name, start, size, raw, vs in sections:
            if start <= rva < start+size:
                return raw+rva-start
        raise ValueError('Unmapped RVA')
    findings = []
    class_names=[name.decode() for name in re.findall(rb'\.\?AV([^\x00]*CullingProcess)@@\x00',data)]
    for class_name in class_names:
        name = ('.?AV'+class_name+'@@\0').encode()
        type_pos = data.index(name)-16
        type_rva = to_rva(type_pos)
        needle = struct.pack('<I', type_rva)
        position = 0
        while True:
            ref = data.find(needle, position)
            if ref < 0:
                break
            position = ref+1
            col = ref-12
            if col < 0:
                continue
            fields = struct.unpack_from('<IIIIII', data, col)
            if fields[0] != 1 or fields[1] != 0 or fields[5] != to_rva(col):
                continue
            absolute = struct.pack('<Q', image_base+to_rva(col))
            vref = data.find(absolute)
            while vref >= 0:
                table = vref+8
                slots = []
                for slot in range(0x19 if class_name == 'NiCullingProcess' else 0x1d):
                    target = struct.unpack_from('<Q', data, table+slot*8)[0]-image_base
                    try:
                        offset = to_offset(target)
                    except ValueError:
                        break
                    slots.append({'slot': hex(slot), 'targetRva': hex(target), 'first16Bytes': data[offset:offset+16].hex()})
                if len(slots) >= 0x19:
                    findings.append({'class':class_name, 'typeDescriptorRva':hex(type_rva),
                                     'completeObjectLocatorRva':hex(to_rva(col)),
                                     'vtableRva':hex(to_rva(table)), 'slots':slots})
                vref = data.find(absolute, vref+1)
    return {'sha256':hashlib.sha256(data).hexdigest().upper(), 'imageBase':hex(image_base), 'vtables':findings}

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path',type=Path)
    args=parser.parse_args()
    print(json.dumps(locate(args.path),indent=2))
