"""Validate paired V12 context profiles; describe inclusive call timings and payload ownership."""
import argparse
import csv
import datetime
import json
import math
import re
import statistics
from pathlib import Path


# SDK ID3D11DeviceContext4 order. Validate reported slots instead of trusting labels.
METHOD_NAMES = """
QueryInterface AddRef Release GetDevice GetPrivateData SetPrivateData SetPrivateDataInterface
VSSetConstantBuffers PSSetShaderResources PSSetShader PSSetSamplers VSSetShader DrawIndexed Draw Map Unmap
PSSetConstantBuffers IASetInputLayout IASetVertexBuffers IASetIndexBuffer DrawIndexedInstanced DrawInstanced
GSSetConstantBuffers GSSetShader IASetPrimitiveTopology VSSetShaderResources VSSetSamplers Begin End GetData
SetPredication GSSetShaderResources GSSetSamplers OMSetRenderTargets OMSetRenderTargetsAndUnorderedAccessViews
OMSetBlendState OMSetDepthStencilState SOSetTargets DrawAuto DrawIndexedInstancedIndirect DrawInstancedIndirect
Dispatch DispatchIndirect RSSetState RSSetViewports RSSetScissorRects CopySubresourceRegion CopyResource
UpdateSubresource CopyStructureCount ClearRenderTargetView ClearUnorderedAccessViewUint
ClearUnorderedAccessViewFloat ClearDepthStencilView GenerateMips SetResourceMinLOD GetResourceMinLOD
ResolveSubresource ExecuteCommandList HSSetShaderResources HSSetShader HSSetSamplers HSSetConstantBuffers
DSSetShaderResources DSSetShader DSSetSamplers DSSetConstantBuffers CSSetShaderResources CSSetUnorderedAccessViews
CSSetShader CSSetSamplers CSSetConstantBuffers VSGetConstantBuffers PSGetShaderResources PSGetShader PSGetSamplers
VSGetShader PSGetConstantBuffers IAGetInputLayout IAGetVertexBuffers IAGetIndexBuffer GSGetConstantBuffers GSGetShader
IAGetPrimitiveTopology VSGetShaderResources VSGetSamplers GetPredication GSGetShaderResources GSGetSamplers
OMGetRenderTargets OMGetRenderTargetsAndUnorderedAccessViews OMGetBlendState OMGetDepthStencilState SOGetTargets
RSGetState RSGetViewports RSGetScissorRects HSGetShaderResources HSGetShader HSGetSamplers HSGetConstantBuffers
DSGetShaderResources DSGetShader DSGetSamplers DSGetConstantBuffers CSGetShaderResources CSGetUnorderedAccessViews
CSGetShader CSGetSamplers CSGetConstantBuffers ClearState Flush GetType GetContextFlags FinishCommandList
CopySubresourceRegion1 UpdateSubresource1 DiscardResource DiscardView VSSetConstantBuffers1 HSSetConstantBuffers1
DSSetConstantBuffers1 GSSetConstantBuffers1 PSSetConstantBuffers1 CSSetConstantBuffers1 VSGetConstantBuffers1
HSGetConstantBuffers1 DSGetConstantBuffers1 GSGetConstantBuffers1 PSGetConstantBuffers1 CSGetConstantBuffers1
SwapDeviceContextState ClearView DiscardView1 UpdateTileMappings CopyTileMappings CopyTiles UpdateTiles ResizeTilePool
TiledResourceBarrier IsAnnotationEnabled SetMarkerInt BeginEventInt EndEvent Flush1 SetHardwareProtectionState
GetHardwareProtectionState Signal Wait
""".split()
CATEGORIES = {"state", "draw-dispatch", "map-unmap", "update", "resource-copy-clear", "query-sync",
              "getter", "interface-other"}
COUNTERS = ("calls", "samples", "ticks", "failed", "pending", "scopedCalls", "scopedSamples", "scopedTicks")
MAP_MODES = ("read", "write", "readWrite", "discard", "noOverwrite", "doNotWait")
POTENTIAL_DRAINS = {"Map", "GetData", "Flush", "Flush1", "Signal", "Wait", "FinishCommandList",
                    "ExecuteCommandList", "SwapDeviceContextState"}
CONTAMINATION_COUNTERS = ("captureAttempts", "replacedDraws", "workerRecordedDraws", "serialRecordedDraws",
                          "batches", "suppressedIndexedDraws")


def category(name):
    if name in {"QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
                "SetPrivateDataInterface", "IsAnnotationEnabled", "SetMarkerInt", "BeginEventInt", "EndEvent"}:
        return "interface-other"
    if "Get" in name and name != "GetData":
        return "getter"
    if name.startswith(("Draw", "Dispatch")):
        return "draw-dispatch"
    if name in {"Map", "Unmap"}:
        return "map-unmap"
    if name in {"UpdateSubresource", "UpdateSubresource1", "UpdateTiles"}:
        return "update"
    if name in {"Begin", "End", "GetData", "Flush", "Flush1", "FinishCommandList", "ExecuteCommandList",
                "Signal", "Wait", "TiledResourceBarrier"}:
        return "query-sync"
    if name.startswith(("Copy", "Clear", "Discard", "Resolve")) or name in {"GenerateMips", "ResizeTilePool"}:
        return "resource-copy-clear"
    if name.startswith(("VSSet", "PSSet", "GSSet", "HSSet", "DSSet", "CSSet", "IASet", "OMSet", "RSSet", "SOSet")) or name in {"SetPredication", "SetResourceMinLOD", "SwapDeviceContextState", "SetHardwareProtectionState"}:
        return "state"
    return "interface-other"


def unsigned(value, label, positive=False):
    if type(value) is not int or value < (1 if positive else 0) or value > (1 << 64) - 1:
        raise ValueError(f"Invalid integer {label}")
    return value


def file_time(value):
    # Native FILETIME may be serialized as a decimal string to preserve integer precision.
    if type(value) is str and re.fullmatch(r"[0-9]+", value):
        value = int(value)
    return unsigned(value, "processStartFileTime", positive=True)


def validate_counters(method, label):
    for key in COUNTERS:
        unsigned(method.get(key), f"{label}.{key}")
    if method["samples"] > method["calls"] or method["failed"] + method["pending"] > method["calls"]:
        raise ValueError(f"Impossible method counters: {label}")
    if method["pending"] and method["name"] != "GetData":
        raise ValueError("Only GetData may report pending S_FALSE")
    for scoped, total in (("scopedCalls", "calls"), ("scopedSamples", "samples"), ("scopedTicks", "ticks")):
        if method[scoped] > method[total]:
            raise ValueError(f"Scoped counters exceed whole-owner counters: {label}")
    if (method["scopedSamples"] > method["scopedCalls"] or
            method["samples"] - method["scopedSamples"] > method["calls"] - method["scopedCalls"]):
        raise ValueError(f"Impossible scoped sampling: {label}")
    if ((not method["samples"] and method["ticks"]) or
            (not method["scopedSamples"] and method["scopedTicks"])):
        raise ValueError(f"Ticks without samples: {label}")


def inspect(report, expected_mode="profile-context", expected_active=True):
    unsigned(report.get("processId"), "processId", positive=True)
    file_time(report.get("processStartFileTime"))
    if report.get("pluginVersion") != 12 or type(report.get("pluginVersion")) is not int:
        raise ValueError("A V12 context profile is required")
    for key in ("contextAttached", "worldLoaded"):
        if report.get(key) is not True:
            raise ValueError(f"Inactive context: {key}")
    for key in ("initializationError", "offloadDisabled", "enabled"):
        if report.get(key) is not False:
            raise ValueError(f"Invalid bridge state: {key}")
    if unsigned(report.get("errors"), "errors") or report.get("requestedMode") != expected_mode:
        raise ValueError("Context profile has bridge errors or incorrect mode")
    if report.get("drawSuppressionActive", False) is not False:
        raise ValueError("Context profiling must preserve original draws")
    profile = report.get("contextProfile")
    if type(profile) is not dict or profile.get("active") is not expected_active or profile.get("cleanForwarding") is not True:
        raise ValueError("Active clean context forwarding is required")
    every = unsigned(profile.get("sampleEvery"), "sampleEvery", positive=True)
    if every not in (1, 16):
        raise ValueError("Unsupported sampling interval")
    frequency = unsigned(profile.get("qpcFrequency"), "qpcFrequency", positive=True)
    if "qpcFrequency" in report:
        if unsigned(report["qpcFrequency"], "root.qpcFrequency", positive=True) != frequency:
            raise ValueError("Root and context QPC frequencies disagree")
    if type(profile.get("timingSemantics")) is not str or not profile["timingSemantics"].strip():
        raise ValueError("Missing timing semantics")
    for key in ("snapshotQPC", "ownerThreadId"):
        if key in profile:
            unsigned(profile[key], key, positive=True)
    raw_methods = profile.get("methods")
    if type(raw_methods) is not list or not raw_methods:
        raise ValueError("Missing context method inventory")
    methods = {}
    names = set()
    for method in raw_methods:
        if type(method) is not dict:
            raise ValueError("Invalid context method row")
        slot = unsigned(method.get("slot"), "slot")
        name = method.get("name")
        if type(name) is not str or not name or slot in methods or name in names:
            raise ValueError("Invalid or duplicate context method")
        if slot < len(METHOD_NAMES) and name != METHOD_NAMES[slot]:
            raise ValueError("Context slot/name mismatch")
        if name in METHOD_NAMES and METHOD_NAMES.index(name) != slot:
            raise ValueError("Known method reported at incorrect slot")
        if method.get("category") not in CATEGORIES:
            raise ValueError("Unknown method category")
        if name in METHOD_NAMES and method["category"] != category(name):
            raise ValueError(f"Known method category mismatch: {name}")
        validate_counters(method, name)
        methods[slot] = method
        names.add(name)
    modes = profile.get("mapModes")
    if type(modes) is not dict:
        raise ValueError("Missing Map-mode counters")
    for name in MAP_MODES:
        unsigned(modes.get(name), f"mapModes.{name}")
    maps = methods.get(14, {}).get("calls", 0)
    if sum(modes[name] for name in MAP_MODES[:-1]) > maps or modes["doNotWait"] > maps:
        raise ValueError("Map-mode counters exceed Map calls")
    for key in CONTAMINATION_COUNTERS:
        if key in report:
            unsigned(report[key], key)
    return profile, methods


def payload_inventory(name):
    """Qualitative queued-call obligations, not proof that a method can be deferred."""
    arrays, ownership, notes = [], [], []
    if name in {"AddRef", "Release", "QueryInterface"}:
        ownership.append("Preserve the public COM reference-count and returned-interface contract; caller releases successful interface outputs.")
    if name == "GetDevice":
        ownership.append("Returned device carries a caller-owned COM reference.")
    if name.startswith(("Draw", "Dispatch")) and "Indirect" in name:
        ownership.append("Retain the argument buffer COM reference; its contents remain mutable until execution.")
    if "ConstantBuffers" in name or "ShaderResources" in name or "Samplers" in name:
        arrays.append("Counted buffer/view/sampler pointer array.")
        if "ConstantBuffers1" in name:
            arrays.append("Optional first-constant and constant-count numeric arrays.")
    elif re.match(r"^[VPGHDC]S(?:Set|Get)Shader$", name):
        arrays.append("Shader class-instance pointer array and count.")
    if name in {"IASetVertexBuffers", "IAGetVertexBuffers"}:
        arrays.extend(["Counted buffer pointer array.", "Stride and offset numeric arrays."])
    if name.startswith("OM") and "RenderTargets" in name:
        arrays.append("Counted render-target pointer array; optional depth-stencil COM view.")
        if "UnorderedAccessViews" in name:
            arrays.append("Counted UAV pointer array and optional initial-count numeric array; preserve KEEP sentinels.")
    if "UnorderedAccessViews" in name and name.startswith("CS"):
        arrays.append("Counted UAV pointer array and optional initial-count numeric array; preserve counter sentinels.")
    if name in {"SOSetTargets", "SOGetTargets"}:
        arrays.append("Counted stream-output buffer array; Set also has numeric offsets.")
    if name in {"RSSetViewports", "RSGetViewports", "RSSetScissorRects", "RSGetScissorRects"}:
        arrays.append("Counted viewport or rectangle value array.")
    if name in {"OMSetBlendState", "OMGetBlendState"}:
        arrays.append("Four blend-factor values.")
    binding_com = ("ConstantBuffers", "ShaderResources", "Samplers", "Shader", "VertexBuffers", "IndexBuffer",
                   "InputLayout", "RenderTargets", "UnorderedAccessViews", "BlendState", "DepthStencilState")
    has_binding_com = any(x in name for x in binding_com) or name in {"SOSetTargets", "SOGetTargets", "RSSetState", "RSGetState", "SetPredication", "GetPredication"}
    if has_binding_com:
        if "Get" in name:
            ownership.append("Returned bound COM interfaces carry caller-owned references; preserve outputs and Release obligations.")
        else:
            ownership.append("Retain every non-null input COM interface across a queued call; copying pointer arrays alone does not retain objects.")
    if arrays:
        notes.append("Input arrays need owned value copies before returning if execution is delayed; getter outputs require synchronous compatible semantics.")
    if name in {"Map", "Unmap"}:
        ownership.append("Retain resource identity and successful Map/Unmap pairing.")
        notes.append("Mapped data lifetime ends at Unmap. Read modes, DISCARD renaming, NO_OVERWRITE, pitches and DO_NOT_WAIT cannot be replaced by a universal scratch pointer.")
    if name in {"SetResourceMinLOD", "GetResourceMinLOD"}:
        ownership.append("Retain input resource identity; preserve GetResourceMinLOD's scalar return and resource state ordering.")
    if name.startswith("Update"):
        arrays.append("Source data and optional region/box/mapping arrays; format, pitches and regions determine required bytes.")
        ownership.append("Retain destination resource and any tile-pool interfaces until execution.")
        notes.append("Copy source bytes before their caller-owned storage changes. Byte volume is unknown from call counts.")
    if name.startswith(("Copy", "Resolve", "Discard")) or name in {"GenerateMips", "ResizeTilePool", "TiledResourceBarrier"}:
        ownership.append("Retain each input resource/view/tile-pool COM interface until execution; pointer ownership does not freeze resource contents.")
        if name in {"CopySubresourceRegion", "CopySubresourceRegion1", "CopyTileMappings", "CopyTiles", "DiscardView1"}:
            arrays.append("Optional region box or counted rectangle/tile-coordinate/region-size value data.")
    if name.startswith("Clear") and name != "ClearState":
        ownership.append("Retain the input view COM interface until execution.")
        if name in {"ClearRenderTargetView", "ClearUnorderedAccessViewUint", "ClearUnorderedAccessViewFloat", "ClearView"}:
            arrays.append("Four color/clear values; ClearView also has a counted rectangle array.")
    if name in {"Begin", "End", "GetData"}:
        ownership.append("Retain the asynchronous/query COM interface; query lifetimes and results remain observable.")
        if name == "GetData":
            arrays.append("Caller-provided output data block and byte size; S_FALSE is an expected pending result.")
    if name in {"Signal", "Wait"}:
        ownership.append("Retain fence COM interface and preserve fence-value ordering.")
    if name in {"ExecuteCommandList", "FinishCommandList", "SwapDeviceContextState"}:
        ownership.append("Retain command-list/context-state inputs; preserve caller-owned output COM references and restore-state flags.")
    if name == "Flush1":
        notes.append("Event HANDLE lifetime/completion is observable; it is not a COM reference or a copyable pointed-to payload.")
    if name in {"SetPrivateData", "GetPrivateData"}:
        arrays.append("GUID-keyed byte data and size; output/input semantics differ.")
    if name == "SetPrivateDataInterface":
        ownership.append("Preserve the private-data interface COM reference contract.")
    if name in {"SetMarkerInt", "BeginEventInt"}:
        arrays.append("Caller-owned annotation string; copy string content for delayed execution.")
    if not arrays and not ownership and not notes:
        notes.append("Scalar/interface operation; verify its observable result and ordering before deferral.")
    return dict(transientArraysOrBytes=arrays, comReferenceOwnership=ownership, correctnessNotes=notes,
                byteVolume=None, byteVolumeReason="Native context profile reports call counts and timing, not a verified payload byte inventory.")


def estimate(calls, samples, ticks, frequency, frames):
    measured = ticks * 1000 / frequency
    estimated = measured * calls / samples if samples else (0.0 if not calls else None)
    return dict(calls=calls, sampledCalls=samples, sampledElapsedWallMilliseconds=measured,
                estimatedElapsedWallMilliseconds=estimated,
                estimatedElapsedWallMillisecondsPerFrame=None if estimated is None else estimated / frames,
                estimation="Per-method calls/samples scaling; deterministic sampling, no statistical confidence interval.")


def sum_estimates(rows, key, frames, missing=False):
    amounts = [row[key]["estimatedElapsedWallMilliseconds"] for row in rows]
    total = None if missing or any(x is None for x in amounts) else sum(amounts)
    return dict(calls=sum(row[key]["calls"] for row in rows),
                estimatedInclusiveElapsedWallMilliseconds=total,
                estimatedInclusiveElapsedWallMillisecondsPerFrame=None if total is None else total / frames,
                meaning="Calls count provided methods only. Arithmetic sum of inclusive call estimates; not exclusive CPU work or a frame-time saving.")


def analyze(before, after, frames):
    unsigned(frames, "frames", positive=True)
    bp, bm = inspect(before)
    ap, am = inspect(after)
    identity = (before["processId"], file_time(before["processStartFileTime"]), before["pluginVersion"])
    if identity != (after["processId"], file_time(after["processStartFileTime"]), after["pluginVersion"]):
        raise ValueError("Process identity or plugin version changed")
    if before.get("schemaVersion") != after.get("schemaVersion"):
        raise ValueError("Report schema changed")
    for key in ("sampleEvery", "qpcFrequency", "timingSemantics"):
        if bp[key] != ap[key]:
            raise ValueError(f"Context profiler configuration changed: {key}")
    for key in ("snapshotQPC", "ownerThreadId"):
        if (key in bp) != (key in ap):
            raise ValueError(f"Native context window identity changed: {key}")
    if "ownerThreadId" in bp and bp["ownerThreadId"] != ap["ownerThreadId"]:
        raise ValueError("Owner thread changed")
    if "snapshotQPC" in bp and ap["snapshotQPC"] <= bp["snapshotQPC"]:
        raise ValueError("Native snapshot QPC did not advance")
    if bm.keys() != am.keys():
        raise ValueError("Method inventory changed")
    for key in CONTAMINATION_COUNTERS:
        if (key in before) != (key in after) or before.get(key) != after.get(key):
            raise ValueError(f"Capture/replay/suppression contamination: {key}")
    result = []
    for slot in sorted(bm):
        b, a = bm[slot], am[slot]
        if (b["name"], b["category"]) != (a["name"], a["category"]):
            raise ValueError("Method identity/category changed")
        delta = {key: a[key] - b[key] for key in COUNTERS}
        delta.update(name=a["name"])
        validate_counters(delta, f"delta.{a['name']}")
        result.append(dict(slot=slot, name=a["name"], category=a["category"], counters={k: delta[k] for k in COUNTERS},
                           wholeOwner=estimate(delta["calls"], delta["samples"], delta["ticks"], ap["qpcFrequency"], frames),
                           scoped=estimate(delta["scopedCalls"], delta["scopedSamples"], delta["scopedTicks"], ap["qpcFrequency"], frames),
                           potentialDrain=a["name"] in POTENTIAL_DRAINS,
                           payloadOwnership=payload_inventory(a["name"])))
    map_modes = {}
    for key in MAP_MODES:
        map_modes[key] = unsigned(ap["mapModes"][key] - bp["mapModes"][key], f"delta.mapModes.{key}")
    maps = next((r["counters"]["calls"] for r in result if r["name"] == "Map"), 0)
    if sum(map_modes[key] for key in MAP_MODES[:-1]) > maps or map_modes["doNotWait"] > maps:
        raise ValueError("Delta Map modes exceed delta Map calls")
    categories = {}
    for cat in sorted(CATEGORIES):
        selected = [r for r in result if r["category"] == cat]
        missing = [name for slot, name in enumerate(METHOD_NAMES) if slot not in bm and category(name) == cat]
        categories[cat] = dict(missingKnownMethods=missing, completeKnownInventory=not missing,
                               wholeOwner=sum_estimates(selected, "wholeOwner", frames, missing=bool(missing)),
                               scoped=sum_estimates(selected, "scoped", frames, missing=bool(missing)))
    return dict(schemaVersion=1, kind="original-context-inclusive-call-inventory", processId=identity[0],
                processStartFileTime=str(identity[1]), pluginVersion=identity[2], frames=frames,
                sampleEvery=ap["sampleEvery"], qpcFrequency=ap["qpcFrequency"], nativeTimingSemantics=ap["timingSemantics"],
                methods=result, methodInventoryComplete=len(bm) == len(METHOD_NAMES) and set(bm) == set(range(len(METHOD_NAMES))),
                missingKnownMethods=[name for slot, name in enumerate(METHOD_NAMES) if slot not in bm],
                mapModes=dict(**map_modes, unclassifiedMapCalls=maps-sum(map_modes[key] for key in MAP_MODES[:-1]),
                              doNotWaitOverlapsMapTypes=True, failedMapCalls=next((r["counters"]["failed"] for r in result if r["name"] == "Map"), 0)),
                categories=categories,
                potentialDrains=[dict(name=r["name"], counters=r["counters"], wholeOwner=r["wholeOwner"], scoped=r["scoped"]) for r in result if r["potentialDrain"]],
                queryBeginEnd=[dict(name=r["name"], counters=r["counters"]) for r in result if r["name"] in {"Begin", "End"}],
                exclusiveCpuFraction=None, amdahlFraction=None, payloadByteVolume=None,
                limitations=["QPC spans cover actual original calls, including driver work and waits; they exclude surrounding hook lock/before/after costs.",
                             "Original-clean forwarding still includes mutex/counter/query bookkeeping; it is not an unhooked original baseline.",
                             "Whole-owner counts include calls outside guarded rendering scopes; scoped counts are subsets and must not be added.",
                             "Per-method estimates use deterministic calls/samples scaling and may alias periodic work; short sparse intervals have no statistical uncertainty bound.",
                             "Active calls may nest. Category sums are inclusive elapsed wall spans, not exclusive CPU time, main-thread capacity or removable frame-time fractions.",
                             "QPC and profiler overhead have not been calibrated; sampled small-call durations may include timer overhead.",
                             "Calls with zero samples have unknown elapsed time; missing methods have unknown calls, not zero activity.",
                             "Potential-drain classification is conservative. Map/GetData/Flush/list/fence/state-swap calls do not establish an actual CPU/GPU drain.",
                             "Begin/End are query boundaries and are reported separately, not automatically classified as drains.",
                             "Failed HRESULTs and GetData S_FALSE are expected observable API results, not profiler corruption.",
                             "Payload inventory describes ownership obligations for future queuing; it does not prove that full context submission or scratch Map is correct.",
                             "Frame denominator is caller-supplied; this paired cumulative report does not prove aligned capture windows or unchanged gameplay."])


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def iso_file_time(value):
    """Preserve .NET's seventh fractional digit without floating-point timestamps."""
    if type(value) is not str:
        raise ValueError("UTC timestamp must remain an original ISO string")
    match = re.fullmatch(r"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,7}))?(Z|[+-]\d{2}:\d{2})", value)
    if not match:
        raise ValueError("Missing or invalid timezone-aware ISO timestamp")
    date = datetime.datetime.fromisoformat(match[1] + match[3].replace("Z", "+00:00"))
    difference = date.astimezone(datetime.timezone.utc) - datetime.datetime(1601, 1, 1, tzinfo=datetime.timezone.utc)
    return unsigned((difference.days * 86400 + difference.seconds) * 10000000 + int((match[2] or "").ljust(7, "0")), "ISO FILETIME")


def history(path):
    try:
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    except (json.JSONDecodeError, OSError) as error:
        raise ValueError(f"Invalid observation history: {path}") from error
    if len(rows) < 3:
        raise ValueError("At least three observations are required")
    times = [iso_file_time(row.get("readAtUTC")) for row in rows]
    if any(a >= b for a, b in zip(times, times[1:])):
        raise ValueError("Observation timestamps did not advance")
    sessions = {row.get("session") for row in rows}
    if len(sessions) != 1 or not next(iter(sessions)):
        raise ValueError("Observation session changed or is missing")
    if any(type(row.get("snapshot")) is not dict for row in rows):
        raise ValueError("Missing observation snapshot")
    return rows


def numeric_stats(rows, column, positive=False):
    values = []
    for row in rows:
        try:
            value = float(row[column])
        except (KeyError, TypeError, ValueError):
            continue
        if math.isfinite(value) and (value > 0 if positive else value >= 0):
            values.append(value)
    if not values:
        return dict(samples=0, missingOrInvalidSamples=len(rows), meanMs=None, medianMs=None, p95Ms=None, p99Ms=None)
    ordered = sorted(values)
    def quantile(fraction):
        position = (len(ordered) - 1) * fraction
        lower, upper = math.floor(position), math.ceil(position)
        return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)
    return dict(samples=len(values), missingOrInvalidSamples=len(rows)-len(values), meanMs=statistics.fmean(values),
                medianMs=quantile(0.5), p95Ms=quantile(0.95), p99Ms=quantile(0.99))


def csv_integer(value, label):
    if type(value) is not str or not re.fullmatch(r"[0-9]+", value):
        raise ValueError(f"Invalid frame CSV integer: {label}")
    return unsigned(int(value), label)


def analyze_capture(directory):
    state = read(directory / "capture-state.json")
    if state.get("status") != "captured" or state.get("bridgeMode") != "profile-context" or state.get("requireUncapped") is not True:
        raise ValueError("Captured profile-context with verified uncapping is required")
    pid = unsigned(state.get("processId"), "capture.processId", positive=True)
    start = iso_file_time(state.get("processStartUtc"))
    recording_start = iso_file_time(state.get("recordingStartedAt"))
    recording_end = iso_file_time(state.get("frameRecordingFinishedAt"))
    if recording_end <= recording_start:
        raise ValueError("Invalid frame-recording interval")
    observations = history(directory / "bridge-state-history.jsonl")
    raw_snapshots = [row["snapshot"] for row in observations]
    snapshots, profiles = [], []
    for snapshot in raw_snapshots:
        profile, _ = inspect(snapshot)
        if snapshot["processId"] != pid or file_time(snapshot["processStartFileTime"]) != start:
            raise ValueError("Capture/native process identity mismatch")
        unsigned(profile.get("snapshotQPC"), "snapshotQPC", positive=True)
        unsigned(profile.get("ownerThreadId"), "ownerThreadId", positive=True)
        if profile["sampleEvery"] != state.get("profileSampleEvery") or type(state.get("profileSampleEvery")) is not int:
            raise ValueError("Capture/native sampling interval mismatch")
        if profiles and profile["snapshotQPC"] == profiles[-1]["snapshotQPC"]:
            if snapshot != snapshots[-1]:
                raise ValueError("Native snapshot changed without advancing QPC")
            # Re-reading the reporter's identical published JSON is legitimate.
            continue
        if profiles and profile["snapshotQPC"] < profiles[-1]["snapshotQPC"]:
            raise ValueError("Native snapshot QPC moved backwards")
        snapshots.append(snapshot)
        profiles.append(profile)
    if len(snapshots) < 3:
        raise ValueError("At least three distinct native snapshot endpoints are required")
    # Every adjacent report is validated, not merely the two endpoints.
    for a, b in zip(snapshots, snapshots[1:]):
        analyze(a, b, 1)
    uncap_rows = history(directory / "uncap-state-history.jsonl")
    for row in uncap_rows:
        snapshot = row["snapshot"]
        if snapshot.get("processId") != pid or unsigned(snapshot.get("invalidTimes"), "uncap.invalidTimes"):
            raise ValueError("Uncapping/physics identity or timing failure")
        if not all(snapshot.get(key) is True for key in ("enabled", "worldLoaded", "settingsReady", "uncappedActive")):
            raise ValueError("Uncapping/physics control not active")
        if "processStartFileTime" in snapshot and file_time(snapshot["processStartFileTime"]) != start:
            raise ValueError("Uncap process start mismatch")
    # The collector reads uncapping immediately BEFORE bridge on each poll, and
    # records an active bridge snapshot before PresentMon starts. Neither UTC
    # history can establish an exact native-QPC control boundary.
    if iso_file_time(uncap_rows[0]["readAtUTC"]) > iso_file_time(observations[-1]["readAtUTC"]) or iso_file_time(uncap_rows[-1]["readAtUTC"]) < iso_file_time(observations[0]["readAtUTC"]):
        raise ValueError("Uncapping and bridge observation histories do not overlap")
    trace = (directory / "trace-stats.txt").read_text(encoding="utf-8-sig")
    for kind in ("Events", "Buffers"):
        match = re.search(r"Total # Lost " + kind + r"\s*:\s*(\d+)", trace)
        if not match or int(match[1]):
            raise ValueError("CPU trace loss is missing or nonzero")
    restored = read(directory / "context-profile-restoration.json")
    restored_profile, _ = inspect(restored, expected_mode="original-clean", expected_active=False)
    if (restored["processId"], file_time(restored["processStartFileTime"]), restored["pluginVersion"]) != (pid, start, 12):
        raise ValueError("Original-clean restoration identity mismatch")
    if restored_profile.get("ownerThreadId") != profiles[-1]["ownerThreadId"] or restored_profile.get("qpcFrequency") != profiles[-1]["qpcFrequency"]:
        raise ValueError("Original-clean restoration context identity changed")
    if unsigned(restored_profile.get("snapshotQPC"), "restoration.snapshotQPC", positive=True) < profiles[-1]["snapshotQPC"]:
        raise ValueError("Restoration precedes the measured native window")
    for key in CONTAMINATION_COUNTERS:
        if (key in restored) != (key in snapshots[-1]) or restored.get(key) != snapshots[-1].get(key):
            raise ValueError(f"Original-clean restoration contamination: {key}")
    with (directory / "frames.csv").open(encoding="utf-8-sig", newline="") as file:
        reader = csv.DictReader(file)
        required = {"ProcessID", "SwapChainAddress", "SyncInterval", "CPUStartQPC", "TimeInQPC", "MsBetweenPresents", "MsInPresentAPI"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("Missing raw-QPC/Present API frame CSV columns")
        rows = list(reader)
    if not rows:
        raise ValueError("Empty frame CSV")
    low, high = profiles[0]["snapshotQPC"], profiles[-1]["snapshotQPC"]
    selected, chains = [], set()
    previous_cpu = previous_present = None
    for row in rows:
        if csv_integer(row["ProcessID"], "ProcessID") != pid or csv_integer(row["SyncInterval"], "SyncInterval") != 0:
            raise ValueError("Frame process/sync control mismatch")
        if not re.fullmatch(r"0x[0-9a-fA-F]+", row["SwapChainAddress"] or "") or int(row["SwapChainAddress"], 16) == 0:
            raise ValueError("Invalid gameplay swap chain")
        chains.add(int(row["SwapChainAddress"], 16))
        cpu = csv_integer(row["CPUStartQPC"], "CPUStartQPC")
        present = csv_integer(row["TimeInQPC"], "TimeInQPC")
        if present < cpu or (previous_cpu is not None and (cpu <= previous_cpu or present <= previous_present)):
            raise ValueError("Invalid or unordered frame QPC intervals")
        previous_cpu, previous_present = cpu, present
        if cpu >= low and present <= high:
            selected.append(row)
    if len(chains) != 1:
        raise ValueError("One gameplay swap chain is required")
    if not selected:
        raise ValueError("No complete CPUStartQPC..TimeInQPC frame intervals in the native window")
    intervals = numeric_stats(selected, "MsBetweenPresents", positive=True)
    if intervals["missingOrInvalidSamples"]:
        raise ValueError("Invalid selected frame intervals")
    result = analyze(snapshots[0], snapshots[-1], len(selected))
    result["captureWindow"] = dict(startQPC=low, endQPC=high, qpcFrequency=profiles[0]["qpcFrequency"],
                                    durationSeconds=(high-low)/profiles[0]["qpcFrequency"], ownerThreadId=profiles[0]["ownerThreadId"],
                                    nativeSnapshots=len(snapshots), bridgeFileObservations=len(observations),
                                    identicalNativeSnapshotsCoalesced=len(raw_snapshots)-len(snapshots),
                                    bridgeReadWindowUTC=[observations[0]["readAtUTC"], observations[-1]["readAtUTC"]],
                                    recordingWindowISO=[state["recordingStartedAt"], state["frameRecordingFinishedAt"]],
                                    completeFrameIntervals=len(selected), totalCSVRows=len(rows), excludedEdgeOrOutsideRows=len(rows)-len(selected),
                                    frameSelection="CPUStartQPC >= first native snapshotQPC and TimeInQPC <= last native snapshotQPC; raw QPC ticks.",
                                    counterWindowIncludesPartialEdgeFrames=True)
    result["frameMetrics"] = dict(swapChain=hex(next(iter(chains))), frameIntervals=intervals,
                                    averagePresentRate=1000/intervals["meanMs"],
                                    metrics={column: numeric_stats(selected, column) for column in
                                             ("MsInPresentAPI", "MsCPUBusy", "MsCPUWait", "MsGPUTime", "MsGPUBusy", "MsGPUWait")},
                                    meaning="Metrics use only selected complete raw-QPC frame intervals; Present API/CPU wait/NVIDIA samples overlap and are not additive.")
    result["cpuAttributionRequired"] = dict(processId=pid, ownerThreadId=profiles[0]["ownerThreadId"], startQPC=low, endQPC=high,
                                              qpcFrequency=profiles[0]["qpcFrequency"], exactWindowRequired=True,
                                              currentAnalyzerProducesCPUExports=False,
                                              meaning="Any CPU sample export must filter this process AND owner thread to this same native QPC window. Whole-ETL/process-wide exports are not accepted as main-thread evidence.")
    result["sources"] = dict(capture=str(directory.resolve()))
    result["limitations"][-1:] = ["Counter deltas cover the entire native snapshot window, including arming, partial edge frames and owner calls outside/between recorded frames; dividing by complete-frame count is an approximate per-frame inventory.",
                                  "Bridge read UTC values describe file observation, not the exact QPC clock correlation of the native statistics read.",
                                  "Uncapping observations verify all recorded controls; separate file polls do not prove an exact continuous native-QPC boundary.",
                                  "Selected frame intervals end at PresentMon TimeInQPC, not GPU completion; no GPU peak or frame critical-path inference is made.",
                                  "No CPU sample export is generated here. Attribution must use the declared exact native QPC/process/owner window.",
                                  "Control/trace validation does not prove fixed camera, equivalent scene work or zero recorder overhead."]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--before", type=Path)
    parser.add_argument("--after", type=Path)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.capture and any(value is not None for value in (args.before, args.after, args.frames)):
        parser.error("--capture is mutually exclusive with --before/--after/--frames")
    if not args.capture and any(value is None for value in (args.before, args.after, args.frames)):
        parser.error("Manual inventory requires --before, --after and --frames")
    try:
        if args.capture:
            report = analyze_capture(args.capture)
        else:
            report = analyze(read(args.before), read(args.after), args.frames)
            report["sources"] = dict(before=str(args.before.resolve()), after=str(args.after.resolve()))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(args.output)
