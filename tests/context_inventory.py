"""One Python runner for paired context timing, ownership and contamination checks."""
import copy
import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("context_inventory", Path(__file__).parents[1] / "tools/Analyze-ContextInventory.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixtures():
    before = dict(schemaVersion=1, processId=42, processStartFileTime="133700000000000001", pluginVersion=12,
                  contextAttached=True, worldLoaded=True, initializationError=False, offloadDisabled=False,
                  enabled=False, errors=0, requestedMode="profile-context", drawSuppressionActive=False,
                  qpcFrequency=1000000, captureAttempts=10, replacedDraws=10, workerRecordedDraws=10,
                  serialRecordedDraws=0, batches=1, suppressedIndexedDraws=0,
                  contextProfile=dict(active=True, cleanForwarding=True, sampleEvery=16, qpcFrequency=1000000,
                                      timingSemantics="QPC around actual original call; hook callbacks and lock excluded.",
                                      mapModes={k: 0 for k in module.MAP_MODES}, methods=[]))
    for slot, name in enumerate(module.METHOD_NAMES):
        before["contextProfile"]["methods"].append(dict(slot=slot, name=name, category=module.category(name),
                                                        **{k: 0 for k in module.COUNTERS}))
    after = copy.deepcopy(before)
    for name, calls, samples, ticks in (("VSSetConstantBuffers", 32, 2, 100), ("Map", 16, 1, 1000),
                                       ("Unmap", 16, 1, 20), ("GetData", 16, 1, 40), ("Begin", 16, 1, 10),
                                       ("End", 16, 1, 10), ("UpdateSubresource", 5, 0, 0), ("DrawIndexed", 32, 2, 80)):
        m = after["contextProfile"]["methods"][module.METHOD_NAMES.index(name)]
        m.update(calls=calls, samples=samples, ticks=ticks, scopedCalls=calls//2, scopedSamples=samples//2, scopedTicks=ticks//2 if samples//2 else 0)
    after["contextProfile"]["methods"][14]["failed"] = 2
    after["contextProfile"]["methods"][29].update(failed=1, pending=6)
    after["contextProfile"]["mapModes"].update(discard=10, noOverwrite=4, read=2, doNotWait=3)
    return before, after


before, after = fixtures()
report = module.analyze(before, after, 100)
assert report["methodInventoryComplete"]
assert report["exclusiveCpuFraction"] is None and report["amdahlFraction"] is None and report["payloadByteVolume"] is None
rows = {r["name"]: r for r in report["methods"]}
assert rows["VSSetConstantBuffers"]["wholeOwner"]["estimatedElapsedWallMilliseconds"] == 1.6
assert rows["VSSetConstantBuffers"]["scoped"]["estimatedElapsedWallMilliseconds"] == 0.8
assert rows["UpdateSubresource"]["wholeOwner"]["estimatedElapsedWallMilliseconds"] is None
assert report["categories"]["update"]["wholeOwner"]["estimatedInclusiveElapsedWallMilliseconds"] is None
assert rows["GetData"]["counters"]["pending"] == 6 and rows["Map"]["counters"]["failed"] == 2
assert report["mapModes"]["doNotWaitOverlapsMapTypes"] and report["mapModes"]["unclassifiedMapCalls"] == 0
assert {r["name"] for r in report["queryBeginEnd"]} == {"Begin", "End"}
assert not {"Begin", "End"}.intersection(r["name"] for r in report["potentialDrains"])
assert "SwapDeviceContextState" in {r["name"] for r in report["potentialDrains"]}
assert rows["VSSetConstantBuffers"]["payloadOwnership"]["transientArraysOrBytes"]
assert rows["VSGetConstantBuffers"]["payloadOwnership"]["comReferenceOwnership"]
assert rows["UpdateSubresource"]["payloadOwnership"]["byteVolume"] is None
assert module.category("SwapDeviceContextState") == "state"
assert module.category("ClearState") == module.category("ResizeTilePool") == "resource-copy-clear"
assert len(module.METHOD_NAMES) == 149 and module.METHOD_NAMES[12] == "DrawIndexed" and module.METHOD_NAMES[-1] == "Wait"


def rejects(mutation, frames=100):
    a, b = copy.deepcopy(before), copy.deepcopy(after)
    mutation(a, b)
    try:
        module.analyze(a, b, frames)
    except ValueError:
        return
    raise AssertionError("Contaminated/invalid context report accepted")


for key, value in (("processId", 43), ("processStartFileTime", "133700000000000002"), ("pluginVersion", 11),
                   ("contextAttached", False), ("worldLoaded", False), ("initializationError", True),
                   ("offloadDisabled", True), ("enabled", True), ("errors", 1), ("requestedMode", "off"),
                   ("drawSuppressionActive", True), ("captureAttempts", 11), ("workerRecordedDraws", 11),
                   ("suppressedIndexedDraws", 1), ("qpcFrequency", 1), ("schemaVersion", 2)):
    rejects(lambda a, b, k=key, v=value: b.update({k: v}))
for key, value in (("active", False), ("cleanForwarding", False), ("sampleEvery", 1), ("sampleEvery", True),
                   ("qpcFrequency", 0), ("timingSemantics", "different")):
    rejects(lambda a, b, k=key, v=value: b["contextProfile"].update({k: v}))
for key, value in (("calls", -1), ("calls", True), ("samples", 33), ("ticks", -1), ("failed", 33),
                   ("pending", 1), ("scopedCalls", 33), ("scopedSamples", 3), ("scopedTicks", 101),
                   ("category", "query-sync"), ("name", "PSSetConstantBuffers"), ("slot", 16)):
    rejects(lambda a, b, k=key, v=value: b["contextProfile"]["methods"][7].update({k: v}))
rejects(lambda a, b: a["contextProfile"]["methods"][7].update(calls=40, samples=2, ticks=90))
rejects(lambda a, b: b["contextProfile"]["methods"][7].update(samples=0, scopedSamples=0, scopedTicks=0))
rejects(lambda a, b: b["contextProfile"]["methods"][7].update(scopedCalls=32, scopedSamples=1))
rejects(lambda a, b: b["contextProfile"]["methods"].append(copy.deepcopy(b["contextProfile"]["methods"][7])))
rejects(lambda a, b: b["contextProfile"]["methods"].pop())
rejects(lambda a, b: b["contextProfile"]["mapModes"].update(read=17))
rejects(lambda a, b: b["contextProfile"]["mapModes"].update(doNotWait=17))
rejects(lambda a, b: a["contextProfile"]["mapModes"].update(discard=1))
rejects(lambda a, b: a.update(errors=1))
rejects(lambda a, b: None, frames=0)
rejects(lambda a, b: None, frames=True)

# A missing method is disclosed, not invented as a zero-call method.
a, b = copy.deepcopy(before), copy.deepcopy(after)
a["contextProfile"]["methods"].pop()
b["contextProfile"]["methods"].pop()
partial = module.analyze(a, b, 100)
assert not partial["methodInventoryComplete"] and "Wait" in partial["missingKnownMethods"]
assert partial["categories"]["query-sync"]["wholeOwner"]["estimatedInclusiveElapsedWallMilliseconds"] is None

# Counter deltas remain valid after earlier work in the same profiling session.
a, b = copy.deepcopy(before), copy.deepcopy(after)
a["contextProfile"]["methods"][7].update(calls=16, samples=1, ticks=20, scopedCalls=8, scopedSamples=0, scopedTicks=0)
b["contextProfile"]["methods"][7].update(calls=48, samples=3, ticks=120, scopedCalls=24, scopedSamples=1, scopedTicks=50)
assert module.analyze(a, b, 100)["methods"][7]["counters"]["calls"] == 32

with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as temporary:
    directory = Path(temporary)
    for name, data in (("before.json", before), ("after.json", after)):
        (directory / name).write_text(json.dumps(data), encoding="utf-8")
    tool = Path(__file__).parents[1] / "tools/Analyze-ContextInventory.py"
    args = [sys.executable, str(tool), "--before", str(directory / "before.json"), "--after", str(directory / "after.json"),
            "--frames", "100", "--output", str(directory / "analysis.json")]
    completed = subprocess.run(args, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    saved = json.loads((directory / "analysis.json").read_text())
    assert saved["processStartFileTime"] == before["processStartFileTime"] and saved["sources"]["before"]
    dirty = copy.deepcopy(after)
    dirty["errors"] = 1
    (directory / "after.json").write_text(json.dumps(dirty), encoding="utf-8")
    args[-1] = str(directory / "must-not-exist.json")
    completed = subprocess.run(args, capture_output=True, text=True)
    assert completed.returncode != 0 and not (directory / "must-not-exist.json").exists()

assert module.iso_file_time("1970-01-01T00:00:00Z") == 116444736000000000
assert module.iso_file_time("1970-01-01T02:00:00.0000001+02:00") == 116444736000000001


def capture_fixtures():
    a, b = fixtures()
    a["processStartFileTime"] = b["processStartFileTime"] = 116444736000000001
    observations = []
    for i, snapshot in enumerate((a, b, b)):
        snapshot = copy.deepcopy(snapshot)
        snapshot["contextProfile"].update(snapshotQPC=(i+1)*1000000, ownerThreadId=101)
        observations.append(dict(readAtUTC=f"2026-10-04T00:00:0{i}.9000000Z", session="fixture-bridge", snapshot=snapshot))
    restored = copy.deepcopy(observations[-1]["snapshot"])
    restored.update(requestedMode="original-clean")
    restored["contextProfile"].update(active=False, snapshotQPC=3100000)
    uncap = [dict(readAtUTC=f"2026-10-04T00:00:0{i}.8000000Z", session="fixture-uncap",
                  snapshot=dict(processId=42, enabled=True, worldLoaded=True, settingsReady=True, uncappedActive=True, invalidTimes=0)) for i in range(3)]
    rows = []
    for cpu, present, api in ((999999, 1000999, "20"), (1000000, 1010000, "0.2"), (1900000, 2000000, "NA"),
                              (2900000, 3000000, "0.4"), (2999000, 3001000, "30")):
        rows.append(dict(Application="SkyrimSE.exe", ProcessID="42", SwapChainAddress="0x123", SyncInterval="0",
                         CPUStartQPC=str(cpu), TimeInQPC=str(present), MsBetweenPresents="10", MsInPresentAPI=api,
                         MsCPUBusy="8", MsCPUWait="2", MsGPUTime="6", MsGPUBusy="3", MsGPUWait="3"))
    return {"capture-state.json": dict(status="captured", bridgeMode="profile-context", requireUncapped=True, processId=42,
                                       processStartUtc="1970-01-01T00:00:00.0000001Z", profileSampleEvery=16,
                                       recordingStartedAt="2026-10-04T02:00:01+02:00", frameRecordingFinishedAt="2026-10-04T00:00:03.1000000Z"),
            "bridge-state-history.jsonl": observations, "uncap-state-history.jsonl": uncap,
            "context-profile-restoration.json": restored,
            "trace-stats.txt": "Total # Lost Events: 0\nTotal # Lost Buffers: 0\n", "frames.csv": rows}


def write_capture(directory, data):
    for name, value in data.items():
        if name.endswith(".csv"):
            with (directory / name).open("w", encoding="utf-8", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=list(value[0]))
                writer.writeheader()
                writer.writerows(value)
        else:
            text = "\n".join(json.dumps(row) for row in value) if name.endswith(".jsonl") else value if name.endswith(".txt") else json.dumps(value)
            (directory / name).write_text(text, encoding="utf-8")


def run_capture(data):
    with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as temporary:
        directory = Path(temporary)
        write_capture(directory, data)
        return module.analyze_capture(directory)


capture = capture_fixtures()
captured = run_capture(capture)
assert captured["frames"] == 3 and captured["captureWindow"]["totalCSVRows"] == 5
assert captured["captureWindow"]["counterWindowIncludesPartialEdgeFrames"]
assert captured["captureWindow"]["startQPC"] == 1000000 and captured["captureWindow"]["endQPC"] == 3000000
assert captured["cpuAttributionRequired"]["ownerThreadId"] == 101 and captured["cpuAttributionRequired"]["exactWindowRequired"]
assert not captured["cpuAttributionRequired"]["currentAnalyzerProducesCPUExports"]
present = captured["frameMetrics"]["metrics"]["MsInPresentAPI"]
assert present["samples"] == 2 and present["missingOrInvalidSamples"] == 1 and abs(present["meanMs"]-0.3)<1e-12
assert captured["frameMetrics"]["metrics"]["MsGPUBusy"]["meanMs"] == 3

# Reporter JSON can be polled twice before its next publication. Only an exact
# duplicate of native counters/config/identity may reuse snapshotQPC.
cached = copy.deepcopy(capture)
duplicate = copy.deepcopy(cached["bridge-state-history.jsonl"][-1])
duplicate["readAtUTC"] = "2026-10-04T00:00:03.0000000Z"
cached["bridge-state-history.jsonl"].append(duplicate)
coalesced = run_capture(cached)
assert coalesced["frames"] == 3 and coalesced["captureWindow"]["nativeSnapshots"] == 3
assert coalesced["captureWindow"]["identicalNativeSnapshotsCoalesced"] == 1
assert coalesced["captureWindow"]["bridgeFileObservations"] == 4

changed = copy.deepcopy(cached)
changed["bridge-state-history.jsonl"][-1]["snapshot"]["contextProfile"]["methods"][7]["calls"] += 1
try:
    run_capture(changed)
except ValueError as error:
    assert "without advancing QPC" in str(error)
else:
    raise AssertionError("Changing counters accepted at reused native QPC")

too_few = copy.deepcopy(capture)
too_few["bridge-state-history.jsonl"][-1]["snapshot"] = copy.deepcopy(too_few["bridge-state-history.jsonl"][1]["snapshot"])
try:
    run_capture(too_few)
except ValueError as error:
    assert "three distinct" in str(error)
else:
    raise AssertionError("Only two distinct native endpoints accepted")


def rejects_capture(mutation):
    data = copy.deepcopy(capture)
    mutation(data)
    try:
        run_capture(data)
    except ValueError:
        return
    raise AssertionError("Invalid context capture accepted")


for key, value in (("status", "recording"), ("bridgeMode", "off"), ("requireUncapped", False), ("processId", 43),
                   ("processStartUtc", "1970-01-01T00:00:00.0000002Z"), ("profileSampleEvery", 1),
                   ("frameRecordingFinishedAt", "2026-10-03T00:00:00Z")):
    rejects_capture(lambda data, k=key, v=value: data["capture-state.json"].update({k: v}))
for key, value in (("errors", 1), ("requestedMode", "original-clean"), ("captureAttempts", 11), ("worldLoaded", False)):
    rejects_capture(lambda data, k=key, v=value: data["bridge-state-history.jsonl"][1]["snapshot"].update({k: v}))
for key, value in (("active", False), ("cleanForwarding", False), ("snapshotQPC", 1000000), ("ownerThreadId", 102),
                   ("qpcFrequency", 1), ("sampleEvery", 1)):
    rejects_capture(lambda data, k=key, v=value: data["bridge-state-history.jsonl"][1]["snapshot"]["contextProfile"].update({k: v}))
rejects_capture(lambda data: data["bridge-state-history.jsonl"][1]["snapshot"]["contextProfile"]["methods"][7].update(ticks=110))
rejects_capture(lambda data: data["bridge-state-history.jsonl"].pop())
rejects_capture(lambda data: data["bridge-state-history.jsonl"][1].update(session="other"))
rejects_capture(lambda data: data["bridge-state-history.jsonl"][1].update(readAtUTC=data["bridge-state-history.jsonl"][0]["readAtUTC"]))
for key, value in (("invalidTimes", 1), ("uncappedActive", False), ("processId", 43), ("settingsReady", False)):
    rejects_capture(lambda data, k=key, v=value: data["uncap-state-history.jsonl"][1]["snapshot"].update({k: v}))
rejects_capture(lambda data: data.update({"trace-stats.txt": "Total # Lost Events: 1\nTotal # Lost Buffers: 0\n"}))
rejects_capture(lambda data: data.update({"trace-stats.txt": "Total # Lost Events: 0\n"}))
for key, value in (("requestedMode", "off"), ("errors", 1), ("processStartFileTime", 116444736000000002)):
    rejects_capture(lambda data, k=key, v=value: data["context-profile-restoration.json"].update({k: v}))
for key, value in (("active", True), ("cleanForwarding", False), ("ownerThreadId", 102), ("snapshotQPC", 2999999)):
    rejects_capture(lambda data, k=key, v=value: data["context-profile-restoration.json"]["contextProfile"].update({k: v}))
for key, value in (("ProcessID", "NA"), ("ProcessID", "43"), ("SwapChainAddress", "NA"), ("SwapChainAddress", "0x456"),
                   ("SyncInterval", "1"), ("CPUStartQPC", "NA"), ("TimeInQPC", "999999"), ("MsBetweenPresents", "NA")):
    rejects_capture(lambda data, k=key, v=value: data["frames.csv"][1].update({k: v}))
rejects_capture(lambda data: data["frames.csv"].reverse())
rejects_capture(lambda data: data.update({"frames.csv": [data["frames.csv"][0], data["frames.csv"][-1]]}))

with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as temporary:
    directory = Path(temporary)
    write_capture(directory, capture)
    args = [sys.executable, str(tool), "--capture", str(directory), "--output", str(directory / "analysis.json")]
    completed = subprocess.run(args, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    saved = json.loads((directory / "analysis.json").read_text())
    assert saved["frames"] == 3 and saved["sources"]["capture"] == str(directory.resolve())
    for option, value in (("--frames", "3"), ("--before", "before.json"), ("--after", "after.json")):
        completed = subprocess.run(args+[option, value], capture_output=True, text=True)
        assert completed.returncode != 0 and "mutually exclusive" in completed.stderr

print("PASS: paired/capture context identity, clean controls/restoration, native QPC frame selection, inclusive scoped timing, expected HRESULTs and contamination rejection")
