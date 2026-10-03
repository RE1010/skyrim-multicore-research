"""Audit exact local getter templates and nearby list/lock references."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED = '846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F'


class Image:
    def __init__(self, path):
        self.data = path.read_bytes()
        if hashlib.sha256(self.data).hexdigest().upper() != EXPECTED:
            raise ValueError('Unknown runtime')
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        count = struct.unpack_from('<H', self.data, pe+6)[0]
        opt_size = struct.unpack_from('<H', self.data, pe+20)[0]
        self.sections = []
        for i in range(count):
            offset = pe+24+opt_size+i*40
            name = self.data[offset:offset+8].rstrip(b'\0').decode()
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', self.data, offset+8)
            flags = struct.unpack_from('<I', self.data, offset+36)[0]
            self.sections.append((name, rva, max(virtual_size, raw_size), raw, flags, raw_size))
        rva, size = struct.unpack_from('<II', self.data, pe+24+112+3*8)
        self.functions = [struct.unpack_from('<III', self.read(rva+i, 12))[:2] for i in range(0, size, 12)]

    def section(self, rva, length=1):
        for item in self.sections:
            if item[1] <= rva and rva+length <= item[1]+item[2]:
                return item
        raise ValueError(f'Unmapped RVA {rva:x}')

    def read(self, rva, length):
        section = self.section(rva, length)
        if rva-section[1]+length > section[5]:
            raise ValueError(f'RVA has no file-backed bytes: {rva:x}')
        offset = section[3]+rva-section[1]
        return self.data[offset:offset+length]


def getters(image):
    accepted = []
    fixed = ((0, '4883ec28'), (4, '8b05'), (10, '85c0751c4533c0'),
             (17, '488d15'), (24, '488d0d'), (31, 'e8'), (36, '8b05'), (42, '4883c428c3'))
    for start, end in image.functions:
        if end-start != 47:
            continue
        code = image.read(start, 47)
        if any(code[at:at+len(bytes.fromhex(pattern))] != bytes.fromhex(pattern) for at, pattern in fixed):
            continue
        relative = lambda at, displacement, length: start+at+length+struct.unpack_from('<i', code, at+displacement)[0]
        slot = relative(4, 2, 6)
        if slot != relative(24, 3, 7) or slot != relative(36, 2, 6) or relative(31, 1, 5) != 0xce2240:
            continue
        if not image.section(slot, 4)[4] & 0x80000000:
            continue
        string = relative(17, 3, 7)
        raw_name = image.read(string, 128).split(b'\0', 1)[0]
        try:
            name = raw_name.decode('ascii')
        except UnicodeDecodeError:
            continue
        if not name or len(name) > 100:
            continue
        accepted.append(dict(functionRVA=start, slotRVA=slot, typeName=name, mode=0, operand=slot, code=code))
    # Complete tiny leaf bodies referenced by read-only data (including vtables).
    # Recognition relies on the entire executed instruction sequence, not a name.
    referenced = set()
    for section in image.sections:
        if section[0] != '.rdata':
            continue
        raw = image.data[section[3]:section[3]+section[5]]
        for at in range(0, len(raw)-7, 8):
            address = struct.unpack_from('<Q', raw, at)[0]
            if 0x140001000 <= address < 0x1417c7000:
                referenced.add(address-0x140000000)
    existing = {entry['functionRVA'] for entry in accepted}
    for start in sorted(referenced-existing):
        code = image.read(start, 8)
        mode = operand = length = None
        if code[:2] == b'\x8b\x05' and code[6] == 0xc3:
            operand = start+6+struct.unpack_from('<i', code, 2)[0]
            try:
                image.section(operand, 4)
            except ValueError:
                continue
            mode, length = 1, 7
        elif code[0] == 0xb8 and code[5] == 0xc3:
            mode, operand, length = 3, struct.unpack_from('<I', code, 1)[0], 6
        elif code[:2] == b'\x8b\x41' and code[3] == 0xc3 and code[2] <= 0x40:
            mode, operand, length = 2, code[2], 4
        elif code[:2] == b'\x8b\x81' and code[6] == 0xc3 and struct.unpack_from('<i', code, 2)[0] in range(0x41):
            mode, operand, length = 2, struct.unpack_from('<i', code, 2)[0], 7
        elif code[:3] == b'\x8b\x01\xc3':
            mode, operand, length = 2, 0, 3
        if mode is not None:
            accepted.append(dict(functionRVA=start, slotRVA=operand if mode==1 else 0,
                                 typeName='verified-leaf-body', mode=mode, operand=operand, code=code[:length]))
    # One live-observed alias, manually audited in full including the CRT helper
    # calls. This is a readonly shadow description, not a replacement for TLS
    # bookkeeping. Completed guard (< -1) and nonzero value are prerequisites.
    alias, body = 0x7a6b10, 0x7a6b20
    thunk = image.read(alias, 5)
    if thunk[0] != 0xe9 or alias+5+struct.unpack_from('<i', thunk, 1)[0] != body:
        raise ValueError('Audited getter alias changed')
    code = image.read(body, 131)
    if code[:4] != bytes.fromhex('4883ec38') or code[-5:] != bytes.fromhex('4883c438c3'):
        raise ValueError('Audited guarded getter changed')
    type_name = image.read(0x1930f80, 128).split(b'\0', 1)[0].decode('ascii')
    accepted.append(dict(functionRVA=alias, bodyRVA=body, guardRVA=0x324fdfc,
                         slotRVA=0x324fdf8, typeName=type_name, mode=4, operand=0x324fdf8,
                         code=code, thunk=thunk))
    accepted.sort(key=lambda entry: entry['functionRVA'])
    return accepted


def generate(image, output):
    entries = getters(image)
    text = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace getter_evidence {\n'
    text += 'struct Entry {std::uint32_t function_rva,operand;unsigned char mode,length;std::array<unsigned char,131> bytes;std::uint32_t guard_rva,body_rva;std::array<unsigned char,5> thunk;};\n'
    text += f'inline constexpr std::array<Entry,{len(entries)}> entries={{{{\n'
    for entry in entries:
        text += '{'+hex(entry['functionRVA'])+','+hex(entry['operand'])+','+str(entry['mode'])+','+str(len(entry['code']))+', {'+','.join(hex(b) for b in entry['code'])+'},'+hex(entry.get('guardRVA',0))+','+hex(entry.get('bodyRVA',entry['functionRVA']))+', {'+','.join(hex(b) for b in entry.get('thunk',b''))+'}},\n'
    text += '}};\n}\n'
    # Keep private helper bytes beside the getter evidence, to reject changes
    # to the initialization protocol relied on by the guarded shadow reader.
    text = text[:-2]
    text += 'struct Helper {std::uint32_t rva,length;std::array<unsigned char,103> bytes;};\n'
    text += 'inline constexpr std::array<Helper,2> init_helpers={{\n'
    for start, length in ((0x15a7cac,96),(0x15a7d0c,103)):
        text += '{'+hex(start)+','+str(length)+', {'+','.join(hex(b) for b in image.read(start,length))+'}},\n'
    text += '}};\n}\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text)
    return entries


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--header', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    image = Image(args.exe)
    entries = generate(image, args.header) if args.header else getters(image)
    candidates = []
    for start, end in image.functions:
        if not 0x790000 <= start < 0x7b0000:
            continue
        code = image.read(start, end-start)
        if b'\x58\x01\x00\x00' not in code and b'\x68\x01\x00\x00' not in code:
            continue
        calls = []
        for at, byte in enumerate(code[:-4]):
            if byte == 0xe8 and start+at+5+struct.unpack_from('<i', code, at+1)[0] == 0x199fa0:
                calls.append(hex(start+at))
        candidates.append(dict(startRVA=hex(start), endRVA=hex(end), length=end-start, lockConstructorCalls=calls))
    report = dict(exeSHA256=EXPECTED, eligibleGetters=len(entries),
                  getters=[{k: (hex(v) if k.endswith('RVA') else v) for k, v in entry.items() if k not in ('code','thunk')} for entry in entries],
                  nearbyListReferenceCandidates=candidates,
                  limitations=['Byte-pattern candidates are not a complete mutation/call graph.',
                               'Cached getters require a nonzero initialized global; cold caches use the original resolver.',
                               'Leaf descriptors cover only the verified complete read/constant bodies; actual vtable getter equality is required.'])
    if args.report:
        args.report.write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(eligibleGetters=len(entries), movementGetters=sum(e['typeName'].startswith('MovementMessage') for e in entries),
                          nearbyCandidates=candidates), indent=2))
