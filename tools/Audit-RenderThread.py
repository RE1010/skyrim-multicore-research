"""Read-only PE evidence and exclusive CPU attribution for the broader render investigation."""
import argparse
import bisect
import importlib.util
import json
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location('movement_image', Path(__file__).with_name('Inspect-MovementRuntime.py'))
image_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image_module)


def audit(exe, report_path):
    image = image_module.Image(exe)
    pe = struct.unpack_from('<I', image.data, 0x3c)[0]
    import_rva, import_size = struct.unpack_from('<II', image.data, pe+24+112+8)
    imports = {}
    for delta in range(0, import_size-19, 20):
        original, _, _, name, first = struct.unpack('<IIIII', image.read(import_rva+delta, 20))
        if not any((original, name, first)):
            break
        dll = image.read(name, 128).split(b'\0', 1)[0].decode('ascii')
        i = 0
        while True:
            hint_name = struct.unpack('<Q', image.read((original or first)+i*8, 8))[0]
            if not hint_name:
                break
            if not hint_name >> 63:
                function = image.read(hint_name+2, 128).split(b'\0', 1)[0].decode('ascii')
                if function in ('D3D11CreateDeviceAndSwapChain', 'D3D11CreateDevice', 'CreateThread', 'GetSystemInfo', 'GetActiveProcessorCount'):
                    imports[first+i*8] = f'{dll}!{function}'
            i += 1
    starts = [f[0] for f in image.functions]
    calls = []
    aliases = {}
    for section in image.sections:
        if section[0] != '.text':
            continue
        raw = image.data[section[3]:section[3]+section[5]]
        at = raw.find(b'\xff\x25')
        while at >= 0:
            rva = section[1]+at
            destination = rva+6+struct.unpack_from('<i', raw, at+2)[0]
            if destination in imports:
                aliases[rva] = destination
            at = raw.find(b'\xff\x25', at+2)
        at = raw.find(b'\xff\x15')
        while at >= 0:
            rva = section[1]+at
            destination = rva+6+struct.unpack_from('<i', raw, at+2)[0]
            if destination in imports:
                index = bisect.bisect_right(starts, rva)-1
                start, end = image.functions[index] if index >= 0 else (0, 0)
                calls.append(dict(callRVA=hex(rva), importRVA=hex(destination), target=imports[destination],
                                  containingFunctionStartRVA=hex(start) if rva < end else None,
                                  containingFunctionEndRVA=hex(end) if rva < end else None))
            at = raw.find(b'\xff\x15', at+2)
        at = raw.find(b'\xe8')
        while 0 <= at < len(raw)-4:
            rva = section[1]+at
            destination = rva+5+struct.unpack_from('<i', raw, at+1)[0]
            if destination in aliases:
                index = bisect.bisect_right(starts, rva)-1
                start, end = image.functions[index] if index >= 0 else (0, 0)
                slot = aliases[destination]
                calls.append(dict(callRVA=hex(rva), importRVA=hex(slot), importThunkRVA=hex(destination), target=imports[slot],
                                  containingFunctionStartRVA=hex(start) if rva < end else None,
                                  containingFunctionEndRVA=hex(end) if rva < end else None))
            at = raw.find(b'\xe8', at+1)
    report = json.loads(report_path.read_text(encoding='utf-8-sig'))
    total = sum(int(r[1]) for r in report['TblSE'][1:])
    if total <= 0 or total != sum(int(r[1]) for r in report['TblME'][1:]):
        raise ValueError('Exclusive sample totals inconsistent')
    render_start, render_end = next(f for f in image.functions if f[0] == 0x1560340)
    render_hits = sum(int(r[1]) for r in report['TblSE'][1:]
                      if r[0].lower().startswith('tesv.exe!') and render_start <= int(r[4], 16) < render_end)
    modules = [dict(module=r[0], exclusiveSamples=int(r[1]), percent=int(r[1])/total*100) for r in report['TblME'][1:]]
    return dict(exeSHA256=image_module.EXPECTED, importCallCandidates=calls, renderFunctionStartRVA=hex(render_start),
                renderFunctionEndRVA=hex(render_end), mainThreadSamples=total, renderFunctionExclusiveSamples=render_hits,
                renderFunctionExclusivePercent=render_hits/total*100, exclusiveModules=modules,
                topExclusive=[dict(function=r[0], rva=r[4], samples=int(r[1]), percent=int(r[1])/total*100) for r in report['TblSE'][1:21]],
                limitations=['Raw opcode matches must be checked in disassembly before they are hook candidates.',
                             'Exclusive CPU samples are running CPU work, not elapsed frame stage or blocked wait durations.',
                             'A function range and code writes do not establish a complete thread-safety or data-dependency proof.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('stack_report', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.exe, args.stack_report)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('importCallCandidates','renderFunctionStartRVA','renderFunctionEndRVA','renderFunctionExclusiveSamples','renderFunctionExclusivePercent')}, indent=2))
