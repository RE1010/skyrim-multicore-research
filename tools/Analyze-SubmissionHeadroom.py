"""Validate off/free-draw/off captures; report a mixed CPU/GPU ablation, not Amdahl."""
import argparse
import json
import re
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def history(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]


def inspect(run, mode):
    state = read(run / "capture-state.json")
    if state["status"] != "captured" or state["bridgeMode"] != mode or not state["requireUncapped"]:
        raise ValueError("Incomplete or wrong-mode uncapped capture")
    pid = state["processId"]
    rows = history(run / "bridge-state-history.jsonl")
    uncap = history(run / "uncap-state-history.jsonl")
    if len(rows) < 3 or len(uncap) < 3:
        raise ValueError("Too few live observations")
    snapshots = [row["snapshot"] for row in rows]
    fields = ("renderPassCalls", "drawIndexedCalls", "originalDraws", "suppressedIndexedDraws", "suppressionQueryFallbacks",
              "replacedDraws", "workerRecordedDraws", "serialRecordedDraws", "captureAttempts", "batches")
    for s in snapshots:
        if s["processId"] != pid or s["requestedMode"] != mode or s["pluginVersion"] < 11 or s["enabled"] or s["errors"] or s["initializationError"] or s["offloadDisabled"] or not s["worldLoaded"] or not s["contextAttached"]:
            raise ValueError("Invalid live bridge state")
        caps = s["deviceCapabilities"]
        if not caps["available"] or not caps["threadingQuerySucceeded"] or caps["threadingQueryHRESULT"] < 0:
            raise ValueError("Actual game device capability query failed")
        if caps != snapshots[0]["deviceCapabilities"]:
            raise ValueError("Device capabilities changed")
        if bool(s["drawSuppressionActive"]) != (mode == "free-draw"):
            raise ValueError("Suppression not confined to requested interval")
    delta = {}
    for field in fields:
        values = [s[field] for s in snapshots]
        if any(type(v) is not int or v < 0 for v in values) or any(a > b for a, b in zip(values, values[1:])):
            raise ValueError("Invalid or reset diagnostic counter")
        delta[field] = values[-1] - values[0]
    if not delta["renderPassCalls"] or not delta["drawIndexedCalls"]:
        raise ValueError("No active scene rendering")
    if any(delta[k] for k in ("replacedDraws", "workerRecordedDraws", "serialRecordedDraws", "captureAttempts", "batches")):
        raise ValueError("Ablation was contaminated by capture/replay")
    if delta["drawIndexedCalls"] != delta["originalDraws"] + delta["suppressedIndexedDraws"]:
        raise ValueError("Indexed-draw conservation failed")
    if (delta["suppressedIndexedDraws"] > 0) != (mode == "free-draw"):
        raise ValueError("Missing suppression or suppression during original mode")
    for row in uncap:
        s = row["snapshot"]
        if s["processId"] != pid or s["invalidTimes"] or not all(s[k] for k in ("enabled", "worldLoaded", "settingsReady", "uncappedActive")):
            raise ValueError("Uncapping/physics verification failed")
    trace = (run / "trace-stats.txt").read_text(encoding="utf-8-sig")
    for kind in ("Events", "Buffers"):
        match = re.search(r"Total # Lost " + kind + r"\s*:\s*(\d+)", trace)
        if not match or int(match[1]):
            raise ValueError("Trace loss missing or nonzero")
    frames = read(run / "frame-summary.json")
    if len(frames["swapChains"]) != 1:
        raise ValueError("Explicit gameplay-chain selection required")
    chain = frames["swapChains"][0]
    if int(chain["processId"]) != pid or chain["invalidFrameIntervals"] or chain["syncIntervals"] != ["0"]:
        raise ValueError("Invalid gameplay frame stream")
    if mode == "free-draw":
        restored = read(run / "free-draw-restoration.json")
        if restored["processId"] != pid or restored["requestedMode"] != "off" or restored["drawSuppressionActive"]:
            raise ValueError("Return to original not proven")
    return dict(source=run.name, mode=mode, processId=pid, processStartUtc=state["processStartUtc"], scene=state["scene"],
                verifiedBridgeWindowUTC=[rows[0]["readAtUTC"], rows[-1]["readAtUTC"]],
                deviceCapabilities=snapshots[0]["deviceCapabilities"], counters=delta, frameRows=chain["csvRows"],
                presentsPerSecond=chain["averagePresentRate"], frameTimes=chain["frameIntervals"],
                cpuBusy=chain["metrics"]["MsCPUBusy"], gpuBusy=chain["metrics"]["MsGPUBusy"],
                indexedDrawSuppressionPercent=100 * delta["suppressedIndexedDraws"] / delta["drawIndexedCalls"])


def analyze(off, free, restored):
    runs = [inspect(off, "off"), inspect(free, "free-draw"), inspect(restored, "off")]
    if len({(r["processId"], r["processStartUtc"], r["scene"]) for r in runs}) != 1:
        raise ValueError("Different process or declared scene")
    if any(r["deviceCapabilities"] != runs[0]["deviceCapabilities"] for r in runs):
        raise ValueError("Different device")
    return dict(kind="scoped-indexed-draw-ablation", runs=runs, amdahlFraction=None,
                limitations=["Draw removal changes CPU submission, GPU workload, queue pressure and downstream rendering.",
                             "Only scoped DrawIndexed is suppressed; query-enclosed and other draw kinds remain original.",
                             "Geometry/material preparation, setters and source uploads remain on the main thread.",
                             "Original mode retains adapter forwarding and upload observation.",
                             "PresentMon CPU Busy is not exclusive main-thread or driver time.",
                             "WPR begins before free-draw arming and stops after restoration; clip stack attribution to verified active frames.",
                             "Counter and frame intervals differ slightly; user supplies camera/readiness.",
                             "One short triplet is screening evidence, not a general performance or mod-capacity claim.",
                             "A full submission thread also moves setters/uploads; small draw-only savings do not exclude it."])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("off", type=Path)
    parser.add_argument("free", type=Path)
    parser.add_argument("restored", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.off, args.free, args.restored)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
