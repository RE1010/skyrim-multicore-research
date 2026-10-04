"""Validate a reference without this project's diagnostic hooks; no multicore gain is inferred."""
import argparse
import csv
import importlib.util
import json
import re
from pathlib import Path

spec = importlib.util.spec_from_file_location("context_inventory_helpers", Path(__file__).with_name("Analyze-ContextInventory.py"))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
EXE_HASH = "846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F"
FORBIDDEN = {"renderworkerbridge.dll", "movementmessageprobe.dll", "renderpassprobe.dll", "multicorevisibilityshadow.dll"}
CALLS = {"0x1521289": "E8-B2-EC-03-00", "0x673d68": "E8-03-3E-12-00", "0x797b9b": "E8-00-24-A0-FF"}


def inspect_observation(row, pid, start):
    if row.get("schemaVersion") != 1 or row.get("kind") != "project-hook-free-reference":
        raise ValueError("Missing reference proof identity")
    if row.get("processId") != pid or helpers.file_time(row.get("processStartFileTime")) != start:
        raise ValueError("Reference process identity mismatch")
    for key in ("projectDiagnosticModulesAbsent", "contextMethodsInD3D11"):
        if row.get(key) is not True:
            raise ValueError("Project-hook-free proof failed")
    qpc = helpers.unsigned(row.get("observedQPC"), "observedQPC", positive=True)
    frequency = helpers.unsigned(row.get("qpcFrequency"), "qpcFrequency", positive=True)
    size = helpers.unsigned(row.get("d3d11Size"), "d3d11Size", positive=True)
    if row.get("contextTableStorage") not in ("inside-runtime-image", "outside-runtime-image"):
        raise ValueError("Missing context table storage observation")
    if row.get("baseContextMethodsChecked") != 115 or type(row.get("baseContextMethodsChecked")) is not int:
        raise ValueError("Incomplete base-context observation")
    methods = row.get("contextMethodRVAs")
    if type(methods) is not list or len(methods) != 115:
        raise ValueError("Missing context method identities")
    for value in methods:
        if type(value) is not str or not re.fullmatch(r"0x[0-9A-Fa-f]+", value) or not 0 <= int(value, 16) < size:
            raise ValueError("Context method outside runtime")
    calls = row.get("originalCallSites")
    if type(calls) is not list or len(calls) != len(CALLS):
        raise ValueError("Incomplete original call-site proof")
    if {r.get("rva"): r.get("bytes") for r in calls} != CALLS or any(r.get("original") is not True for r in calls):
        raise ValueError("Non-original project call site")
    if type(row.get("d3d11Base")) is not str or not re.fullmatch(r"0x[0-9A-Fa-f]+", row["d3d11Base"]):
        raise ValueError("Missing runtime address")
    # D3D11 can change its own draw/dispatch targets dynamically. Each target
    # is checked against the runtime image, but target arrays need not be equal.
    return qpc, frequency, (row["d3d11Base"], size)


def analyze(run):
    state = helpers.read(run / "capture-state.json")
    if state.get("status") != "captured" or state.get("bridgeMode") != "project-hook-free" or state.get("requireUncapped") is not True:
        raise ValueError("Completed uncapped project-hook-free capture required")
    if state.get("gameSha256", "").upper() != EXE_HASH:
        raise ValueError("Unknown reference executable")
    pid = helpers.unsigned(state.get("processId"), "processId", positive=True)
    start = helpers.iso_file_time(state["processStartUtc"])
    recording_start = helpers.iso_file_time(state.get("recordingStartedAt"))
    recording_end = helpers.iso_file_time(state.get("frameRecordingFinishedAt"))
    if recording_end <= recording_start:
        raise ValueError("Invalid recording UTC interval")
    if not state.get("modules") or any(m["name"].lower() in FORBIDDEN for m in state["modules"]):
        raise ValueError("Missing module inventory or project DLL still loaded")
    preflight = helpers.read(run / "project-hook-reference-preflight.json")
    if not re.fullmatch(r"[0-9A-Fa-f]{64}", preflight.get("d3d11SHA256") or ""):
        raise ValueError("Missing runtime file hash")
    observations = [preflight] + [json.loads(line) for line in (run / "project-hook-reference-history.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    if len(observations) < 4:
        raise ValueError("Too few reference observations")
    previous = None
    reference_times = []
    for row in observations:
        qpc, frequency, identity = inspect_observation(row, pid, start)
        observed_time = helpers.iso_file_time(row.get("observedAtUTC"))
        if reference_times and observed_time <= reference_times[-1]:
            raise ValueError("Reference UTC observations did not advance")
        reference_times.append(observed_time)
        if previous and (qpc <= previous[0] or (frequency, identity) != previous[1:]):
            raise ValueError("Reference clock/runtime changed")
        previous = (qpc, frequency, identity)
    uncap = helpers.history(run / "uncap-state-history.jsonl")
    for row in uncap:
        snapshot = row["snapshot"]
        if snapshot.get("processId") != pid or helpers.unsigned(snapshot.get("invalidTimes"), "invalidTimes"):
            raise ValueError("Uncapping identity/physics failure")
        if not all(snapshot.get(k) is True for k in ("enabled", "worldLoaded", "settingsReady", "uncappedActive")):
            raise ValueError("Uncapping is not active in the world")
        if "processStartFileTime" in snapshot and helpers.file_time(snapshot["processStartFileTime"]) != start:
            raise ValueError("Uncapping process start mismatch")
    if len(uncap) < 3:
        raise ValueError("Too few uncapping observations")
    uncap_start = helpers.iso_file_time(uncap[0]["readAtUTC"])
    uncap_end = helpers.iso_file_time(uncap[-1]["readAtUTC"])
    # Controls are read sequentially, not atomically. Require temporal overlap,
    # not exact bracketing by a history whose final uncap read precedes reference.
    if (max(uncap_start, recording_start) >= min(uncap_end, recording_end) or
        max(reference_times[0], recording_start) >= min(reference_times[-1], recording_end) or
        max(uncap_start, reference_times[0]) >= min(uncap_end, reference_times[-1])):
        raise ValueError("Uncapping/reference histories do not overlap recording")
    trace = (run / "trace-stats.txt").read_text(encoding="utf-8-sig")
    for kind in ("Events", "Buffers"):
        match = re.search(r"Total # Lost " + kind + r"\s*:\s*(\d+)", trace)
        if not match or int(match[1]):
            raise ValueError("Trace loss missing or nonzero")
    with (run / "frames.csv").open(encoding="utf-8-sig", newline="") as file:
        reader = csv.DictReader(file)
        if not {"ProcessID", "SwapChainAddress", "SyncInterval", "CPUStartQPC", "TimeInQPC", "MsBetweenPresents"}.issubset(reader.fieldnames or []):
            raise ValueError("Missing raw-QPC frame data")
        rows = list(reader)
    if not rows:
        raise ValueError("Empty frame stream")
    chains, previous_cpu, previous_present = set(), None, None
    for row in rows:
        if helpers.csv_integer(row["ProcessID"], "ProcessID") != pid or helpers.csv_integer(row["SyncInterval"], "SyncInterval") != 0:
            raise ValueError("Wrong process or capped frame stream")
        if not re.fullmatch(r"0x[0-9A-Fa-f]+", row["SwapChainAddress"] or "") or int(row["SwapChainAddress"], 16) == 0:
            raise ValueError("Invalid swap chain")
        chains.add(int(row["SwapChainAddress"], 16))
        cpu = helpers.csv_integer(row["CPUStartQPC"], "CPUStartQPC")
        present = helpers.csv_integer(row["TimeInQPC"], "TimeInQPC")
        if present < cpu or (previous_cpu is not None and (cpu <= previous_cpu or present <= previous_present)):
            raise ValueError("Unordered frame clock")
        previous_cpu, previous_present = cpu, present
    low = helpers.csv_integer(rows[0]["CPUStartQPC"], "CPUStartQPC")
    high = helpers.csv_integer(rows[-1]["TimeInQPC"], "TimeInQPC")
    if len(chains) != 1 or not observations[0]["observedQPC"] <= low < high <= observations[-1]["observedQPC"]:
        raise ValueError("Frames outside verified reference observation envelope")
    frame_stats = helpers.numeric_stats(rows, "MsBetweenPresents", positive=True)
    if frame_stats["missingOrInvalidSamples"]:
        raise ValueError("Invalid frame intervals")
    variants = {str(slot): sorted({row["contextMethodRVAs"][slot] for row in observations}) for slot in range(115)}
    variants = {slot: values for slot, values in variants.items() if len(values)>1}
    return dict(schemaVersion=1, kind="project-hook-free-reference-analysis", captureWindow=dict(startQPC=low, endQPC=high, qpcFrequency=frequency,
                durationSeconds=(high-low)/frequency, processId=pid, processStartFileTime=start), referenceObservations=len(observations),
                frameRows=len(rows), frameIntervals=frame_stats, averagePresentRate=1000/frame_stats["meanMs"],
                metrics={key: helpers.numeric_stats(rows, key) for key in ("MsCPUBusy", "MsCPUWait", "MsGPUBusy", "MsGPUWait", "MsInPresentAPI")},
                projectHooksAbsentAtAllObservations=True, originalCallSitesVerified=True, baseContextMethodsVerified=115,
                runtimeOwnedMethodVariants=variants,
                exclusiveMovableCPUFraction=None, predictedMulticoreGain=None,
                limitations=["SKSE, unchanged uncapping, overlays and OS/driver work remain; this is not completely vanilla Skyrim.",
                             "Reference guards are external periodic reads, not continuous proof or exclusion of third-party inline hooks.",
                             "Reference requires a new process; comparison to prior V12 is descriptive across launches, not a paired causal FPS result.",
                             "Camera/save/light conditions are supplied by the tester, not established by the recorder.",
                             "CPU attribution still requires exact ETL clock mapping, verified main thread identity and this frame-window filter."])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(args.run)
    output = args.output or args.run / "project-hook-free-analysis.json"
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(output)
