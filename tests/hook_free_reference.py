"""Reject contaminated/capped/stale reference evidence before assigning a CPU budget."""
import csv
import importlib.util
import json
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("hook_free", Path(__file__).parents[1] / "tools" / "Analyze-HookFreeReference.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def write(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


def fixtures(run):
    start_iso = "2026-10-04T00:00:00.0000000Z"
    start = module.helpers.iso_file_time(start_iso)
    write(run / "capture-state.json", dict(status="captured", bridgeMode="project-hook-free", requireUncapped=True,
          gameSha256=module.EXE_HASH, processId=42, processStartUtc=start_iso,
          recordingStartedAt='2026-10-04T00:00:00.1500000Z', frameRecordingFinishedAt='2026-10-04T00:00:02.5000000Z',
          modules=[dict(name="SkyrimSE.exe"), dict(name="d3d11.dll"), dict(name="UncappedBenchmark.dll")]))
    row = dict(schemaVersion=1, kind="project-hook-free-reference", processId=42, processStartFileTime=start,
               observedQPC=100, observedAtUTC=start_iso, qpcFrequency=1000, d3d11Size=4096, d3d11Base="0x10000", d3d11SHA256="A"*64,
               projectDiagnosticModulesAbsent=True, contextMethodsInD3D11=True, contextTableStorage='outside-runtime-image', baseContextMethodsChecked=115,
               contextMethodRVAs=["0x100"]*115,
               originalCallSites=[dict(rva=rva, bytes=value, original=True) for rva, value in module.CALLS.items()])
    write(run / "project-hook-reference-preflight.json", row)
    (run / "project-hook-reference-history.jsonl").write_text("\n".join(json.dumps(dict(row, observedQPC=qpc, observedAtUTC=f"2026-10-04T00:00:0{index}.0000000Z")) for index,qpc in enumerate((200, 300, 500),1)), encoding="utf-8")
    uncap = dict(processId=42, processStartFileTime=start, invalidTimes=0, enabled=True, worldLoaded=True, settingsReady=True, uncappedActive=True)
    (run / "uncap-state-history.jsonl").write_text("\n".join(json.dumps(dict(snapshot=uncap, session="uncap-test-session", readAtUTC=f"2026-10-04T00:00:0{index}.0000000Z")) for index in range(3)), encoding="utf-8")
    (run / "trace-stats.txt").write_text("Total # Lost Events: 0\nTotal # Lost Buffers: 0\n", encoding="utf-8")
    with (run / "frames.csv").open("w", newline="", encoding="utf-8") as file:
        fields = ["ProcessID", "SwapChainAddress", "SyncInterval", "CPUStartQPC", "TimeInQPC", "MsBetweenPresents", "MsCPUBusy", "MsGPUBusy"]
        writer = csv.DictWriter(file, fields)
        writer.writeheader()
        for cpu, present in ((150, 190), (210, 250), (310, 350)):
            writer.writerow(dict(ProcessID=42, SwapChainAddress="0x123", SyncInterval=0, CPUStartQPC=cpu, TimeInQPC=present,
                                 MsBetweenPresents=5, MsCPUBusy=4, MsGPUBusy=3))


with tempfile.TemporaryDirectory() as directory:
    run = Path(directory)
    fixtures(run)
    result = module.analyze(run)
    assert result["frameRows"] == 3 and result["averagePresentRate"] == 200
    assert result["exclusiveMovableCPUFraction"] is None and result["predictedMulticoreGain"] is None
    variants_path=run/'project-hook-reference-history.jsonl'
    variant_rows=[json.loads(line) for line in variants_path.read_text(encoding='utf-8').splitlines()]
    variant_rows[0]['contextMethodRVAs'][12]='0x200'
    variants_path.write_text('\n'.join(json.dumps(r) for r in variant_rows), encoding='utf-8')
    assert module.analyze(run)['runtimeOwnedMethodVariants']=={'12':['0x100','0x200']}

    def rejects_json(file, update):
        fixtures(run)
        path = run / file
        row = module.helpers.read(path)
        update(row)
        write(path, row)
        try:
            module.analyze(run)
        except ValueError:
            return
        raise AssertionError("Invalid reference accepted")

    for key, value in (("status", "failed"), ("requireUncapped", False), ("bridgeMode", "original-clean"), ("gameSha256", "B"*64), ("processId", 43)):
        rejects_json("capture-state.json", lambda r, k=key, v=value: r.update({k:v}))
    rejects_json('capture-state.json', lambda r: r.update(recordingStartedAt=r['frameRecordingFinishedAt']))
    rejects_json("capture-state.json", lambda r: r["modules"].append(dict(name="RenderWorkerBridge.dll")))
    for key, value in (("projectDiagnosticModulesAbsent", False), ("contextMethodsInD3D11", False), ("baseContextMethodsChecked", 114),
                       ("processStartFileTime", 1), ("qpcFrequency", 0), ("observedQPC", 999), ("d3d11SHA256", "A")):
        rejects_json("project-hook-reference-preflight.json", lambda r, k=key, v=value: r.update({k:v}))
    rejects_json("project-hook-reference-preflight.json", lambda r: r["contextMethodRVAs"].pop())
    rejects_json("project-hook-reference-preflight.json", lambda r: r["contextMethodRVAs"].__setitem__(14, "0x10000"))
    rejects_json("project-hook-reference-preflight.json", lambda r: r["originalCallSites"][0].update(bytes="FF-15-00-00-00"))
    for contaminant in ("SyncInterval", "ProcessID"):
        fixtures(run)
        path = run / "frames.csv"
        rows = list(csv.DictReader(path.open(encoding="utf-8")))
        rows[1][contaminant] = "1"
        with path.open("w", encoding="utf-8", newline="") as file:
            writer = csv.DictWriter(file, rows[0].keys()); writer.writeheader(); writer.writerows(rows)
        try:
            module.analyze(run)
        except ValueError:
            continue
        raise AssertionError("Capped/wrong-process frame accepted")
    fixtures(run)
    uncap_path=run/'uncap-state-history.jsonl'
    uncap_path.write_text(uncap_path.read_text(encoding='utf-8').replace('T00:00:', 'T01:00:'), encoding='utf-8')
    try:
        module.analyze(run)
    except ValueError:
        pass
    else:
        raise AssertionError('Unrelated earlier/later uncapping history accepted')
    fixtures(run)
    (run / "trace-stats.txt").write_text("Total # Lost Events: 1\nTotal # Lost Buffers: 0\n", encoding="utf-8")
    try:
        module.analyze(run)
    except ValueError:
        pass
    else:
        raise AssertionError("Lost ETW events accepted")
print("Hook-free reference validation and rejection checks passed.")
