"""Report saved CPU-side bridge budgets without adding overlapping phases."""
import argparse
import importlib.util
import json
import re
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def analyze(run):
    spec = importlib.util.spec_from_file_location("bridge_capture", Path(__file__).with_name("Analyze-BridgeCapture.py"))
    capture_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(capture_module)
    capture = capture_module.analyze(run)
    trace = (run / "trace-stats.txt").read_text(encoding="utf-8-sig")
    loss = {}
    for kind in ("Events", "Buffers"):
        match = re.search(r"Total # Lost " + kind + r"\s*:\s*(\d+)", trace)
        if not match or int(match[1]):
            raise ValueError("Trace loss missing or nonzero")
        loss[kind] = int(match[1])
    rows = capture_module.history(run / "bridge-state-history.jsonl")
    snapshots = [r["snapshot"] for r in rows]
    frequency = snapshots[0]["qpcFrequency"]
    if frequency <= 0 or any(s["qpcFrequency"] != frequency for s in snapshots):
        raise ValueError("Inconsistent timing frequency")

    def delta(values):
        if any(v < 0 for v in values) or any(a > b for a, b in zip(values, values[1:])):
            raise ValueError("Phase counter reset")
        return values[-1] - values[0]

    fields = ("captureTicks", "uploadCopyTicks", "serialRecordTicks", "workerWaitTicks", "executeTicks",
              "snapshotPublishTicks", "queueReleaseTicks", "serialFinishTicks", "directSmallTicks")
    phases = {k: delta([s[k] for s in snapshots]) * 1000 / frequency
              for k in fields if all(k in s for s in snapshots)}
    worker_times = {k: [delta([s[k][i] for s in snapshots]) * 1000 / frequency for i in range(4)]
                    for k in ("workerRecordTicks", "workerFinishTicks") if all(k in s for s in snapshots)}
    attempts = delta([s["captureAttempts"] for s in snapshots])
    result = dict(kind="render-bridge-phase-budget", source=run.name, capture=capture, traceLoss=loss,
                  phaseWindowUTC=[rows[0]["readAtUTC"], rows[-1]["readAtUTC"]],
                  captureAttempts=attempts, phaseMilliseconds=phases, workerMilliseconds=worker_times,
                  limitations=["Phase-counter, scheduling, and PresentMon intervals differ slightly.",
                               "Publication is included in capture; finish and direct-small time are included in recording.",
                               "Worker wait overlaps worker recording. Do not sum them as independent frame costs.",
                               "Execute measures submission, not GPU completion. Other engine and adapter work is excluded."])
    if capture["batchDiagnostics"]:
        buckets = capture["batchDiagnostics"]["batchSizeBuckets"]
        batches = capture["counterDelta"]["batches"]
        result["smallBatchPercent"] = sum(b["batches"] for b in buckets[:3]) * 100 / batches if batches else None
        result["scopeEndBatchPercent"] = capture["batchDiagnostics"]["flushReasons"]["scopeEnd"] * 100 / batches if batches else None
    cpu_file = run / "cpu-summary.json"
    if cpu_file.exists():
        cpu = read(cpu_file)
        if cpu["processId"] != capture["processId"] or cpu["lostEvents"] or cpu["lostBuffers"]:
            raise ValueError("CPU identity or trace quality mismatch")
        ids = set(capture["workerIds"])
        workers = [t for t in cpu["threads"] if t["threadId"] in ids]
        if len(workers) != 4:
            raise ValueError("Missing scheduled bridge worker")
        result["cpuScheduling"] = dict(traceDurationSeconds=cpu["traceDurationSeconds"],
                                       processLogicalProcessorEquivalent=cpu["oneLogicalProcessorEquivalent"],
                                       hottestThread=max(cpu["threads"], key=lambda t: t["scheduledCpuMicroseconds"]),
                                       bridgeWorkers=workers,
                                       roleNote="CPU ranking does not identify the main thread or frame critical path.")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    result = analyze(args.run)
    (args.run / "bridge-phase-budget.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
