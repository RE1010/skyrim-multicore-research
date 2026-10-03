"""Read selected PE imports and float constants; never attach to or modify a process."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path, import_rvas, float_rvas):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    if machine != 0x8664 or struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise ValueError("Only x64 PE32+ is supported")
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    headers_size = struct.unpack_from("<I", data, opt + 60)[0]
    sections = []
    for i in range(count):
        sec = opt + opt_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, sec + 8)
        sections.append((rva, virtual_size, raw_size, raw))

    def offset(rva, size=1):
        if 0 <= rva < headers_size and rva + size <= min(headers_size, len(data)):
            return rva
        for start, virtual_size, raw_size, raw in sections:
            if start <= rva < start + max(virtual_size, raw_size):
                delta = rva - start
                if delta + size <= raw_size and raw + delta + size <= len(data):
                    return raw + delta
        raise ValueError(f"RVA 0x{rva:X} is not file-backed")

    def cstring(rva):
        pos = offset(rva)
        return data[pos:data.index(b"\0", pos)].decode("ascii")

    import_start, import_size = struct.unpack_from("<II", data, opt + 112 + 8)
    imports = {}
    if import_start:
        for delta in range(0, import_size - 19, 20):
            descriptor = struct.unpack_from("<IIIII", data, offset(import_start + delta, 20))
            if not any(descriptor):
                break
            original, _, _, name_rva, first = descriptor
            dll = cstring(name_rva)
            index = 0
            while True:
                value = struct.unpack_from("<Q", data, offset((original or first) + index * 8, 8))[0]
                if not value:
                    break
                name = f"ordinal:{value & 0xFFFF}" if value >> 63 else cstring(value + 2)
                imports[first + index * 8] = f"{dll}!{name}"
                index += 1
    return {
        "source": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "imageBase": hex(image_base),
        "imports": [{"iatRva": hex(rva), "name": imports.get(rva)} for rva in import_rvas],
        "floatConstants": [{"rva": hex(rva), "value": struct.unpack_from("<f", data, offset(rva, 4))[0]}
                           for rva in float_rvas],
        "limitation": "File evidence only; callers, live state and units must be verified separately."
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--import-rva", type=lambda s: int(s, 0), action="append", default=[])
    parser.add_argument("--float-rva", type=lambda s: int(s, 0), action="append", default=[])
    args = parser.parse_args()
    print(json.dumps(inspect(args.path, args.import_rva, args.float_rva), indent=2))
