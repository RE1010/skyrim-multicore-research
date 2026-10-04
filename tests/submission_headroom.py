"""Reject stale, replay-contaminated or un-restored draw-ablation reports."""
import copy
import importlib.util
import json
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("headroom", Path(__file__).parents[1] / "tools/Analyze-SubmissionHeadroom.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixtures():
    result = []
    for mode in ("off", "free-draw", "off"):
        rows = []
        for i in range(4):
            s = dict(processId=42, requestedMode=mode, pluginVersion=11, enabled=False, errors=0,
                     initializationError=False, offloadDisabled=False, worldLoaded=True, contextAttached=True,
                     deviceCapabilities=dict(available=True, creationFlags=0, featureLevel=45056, threadingQueryHRESULT=0,
                                             threadingQuerySucceeded=True, driverCommandLists=True, driverConcurrentCreates=True),
                     drawSuppressionActive=mode == "free-draw", renderPassCalls=i*10,
                     drawIndexedCalls=i*1000, originalDraws=i*(200 if mode == "free-draw" else 1000),
                     suppressedIndexedDraws=i*(800 if mode == "free-draw" else 0), suppressionQueryFallbacks=0,
                     replacedDraws=0, workerRecordedDraws=0, serialRecordedDraws=0, captureAttempts=0, batches=0)
            rows.append(dict(readAtUTC=f"2026-10-04T12:01:0{i}Z", snapshot=s))
        result.append({"capture-state.json": dict(status="captured", bridgeMode=mode, requireUncapped=True,
                                                   processId=42, processStartUtc="2026-10-04T12:00:00Z", scene="fixed-gate"),
                       "bridge-state-history.jsonl": rows,
                       "uncap-state-history.jsonl": [dict(snapshot=dict(processId=42, invalidTimes=0, enabled=True,
                                                                        worldLoaded=True, settingsReady=True, uncappedActive=True))]*4,
                       "trace-stats.txt": "Total # Lost Events: 0\nTotal # Lost Buffers: 0\n",
                       "frame-summary.json": dict(swapChains=[dict(processId="42", invalidFrameIntervals=0, syncIntervals=["0"],
                                                                  csvRows=100, averagePresentRate=100, frameIntervals=dict(mean=10),
                                                                  metrics=dict(MsCPUBusy=dict(mean=9), MsGPUBusy=dict(mean=5)))]),
                       "free-draw-restoration.json": dict(processId=42, requestedMode="off", drawSuppressionActive=False)})
    return result


def run(data):
    with tempfile.TemporaryDirectory() as temporary:
        paths = []
        for i, files in enumerate(data):
            directory = Path(temporary) / str(i)
            directory.mkdir()
            paths.append(directory)
            for name, value in files.items():
                text = "\n".join(json.dumps(row) for row in value) if name.endswith(".jsonl") else value if name.endswith(".txt") else json.dumps(value)
                (directory / name).write_text(text, encoding="utf-8")
        return module.analyze(*paths)


valid = fixtures()
report = run(valid)
assert report["amdahlFraction"] is None
assert report["runs"][1]["indexedDrawSuppressionPercent"] == 80


def rejects(mutation):
    data = copy.deepcopy(valid)
    mutation(data)
    try:
        run(data)
    except ValueError:
        return
    raise AssertionError("Invalid ablation report accepted")


rejects(lambda data: data[1]["bridge-state-history.jsonl"][-1]["snapshot"].update(processId=41))
rejects(lambda data: data[1]["bridge-state-history.jsonl"][-1]["snapshot"].update(workerRecordedDraws=1))
rejects(lambda data: data[1]["bridge-state-history.jsonl"][-1]["snapshot"].update(captureAttempts=1))
rejects(lambda data: data[1]["bridge-state-history.jsonl"][-1]["snapshot"].update(drawSuppressionActive=False))
rejects(lambda data: data[0]["bridge-state-history.jsonl"][-1]["snapshot"].update(suppressedIndexedDraws=10))
rejects(lambda data: data[1]["free-draw-restoration.json"].update(drawSuppressionActive=True))
rejects(lambda data: data[1].update({"trace-stats.txt": "Total # Lost Events: 1\nTotal # Lost Buffers: 0\n"}))
rejects(lambda data: data[1]["bridge-state-history.jsonl"][-1]["snapshot"]["deviceCapabilities"].update(threadingQuerySucceeded=False))
rejects(lambda data: data[2]["capture-state.json"].update(processStartUtc="different-process"))
rejects(lambda data: data[1]["uncap-state-history.jsonl"][-1]["snapshot"].update(invalidTimes=1))
print("PASS: draw ablation identity, counters, clean original restoration, trace integrity and no pure Amdahl claim")
