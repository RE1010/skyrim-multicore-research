"""Validate a captured uncapped serial baseline; no multicore gain is inferred."""
import argparse
import hashlib
import json
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def analyze(run):
    state = read(run / 'capture-state.json')
    cpu = read(run / 'cpu-summary.json')
    frames = read(run / 'frame-summary.json')
    stack = read(run / 'main-thread-symbols.json')
    origins = read(run / 'thread-origins.json')
    history = [json.loads(line) for line in (run / 'uncap-state-history.jsonl').read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if state['status'] != 'captured' or not state['requireUncapped'] or cpu['lostEvents'] or cpu['lostBuffers']:
        raise ValueError('Uncapped capture or ETW quality invalid')
    if origins['ProcessId'] != state['processId'] or origins['ProcessStartUTC'] != state['processStartUtc']:
        raise ValueError('Thread/process identity mismatch')
    first_thread = min(origins['Threads'], key=lambda t: t['StartUTC'])['ThreadId']
    total = sum(int(r[2]) for r in stack['TblP'][1:] if r[1])
    if not total or total != sum(int(r[1]) for r in stack['TblME'][1:]) or total != sum(int(r[1]) for r in stack['TblSE'][1:]):
        raise ValueError('CPU sample sums inconsistent')
    if len(history) < 3:
        raise ValueError('Insufficient uncapped state observations')
    previous = None
    for row in history:
        s = row['snapshot']
        if s['processId'] != state['processId'] or s['engineWorkOffloaded'] or not all(s[k] for k in ('enabled', 'worldLoaded', 'settingsReady', 'uncappedActive')) or s['invalidTimes']:
            raise ValueError('Uncapped state or physics validity changed')
        if previous and (s['physicsCalls'] < previous['physicsCalls'] or s['presents'] < previous['presents']):
            raise ValueError('Plugin counters decreased')
        previous = s
    if history[-1]['snapshot']['physicsCalls'] <= history[0]['snapshot']['physicsCalls']:
        raise ValueError('Plugin report was stale throughout capture')
    if len(frames['swapChains']) != 1:
        raise ValueError('Select the gameplay swap chain explicitly')
    chain = frames['swapChains'][0]
    if int(chain['processId']) != state['processId'] or chain['invalidFrameIntervals'] or chain['syncIntervals'] != ['0']:
        raise ValueError('Frame identity/interval/sync check failed')
    inclusive = {r[4]: r for r in stack['TblSI'][1:] if r[0].startswith('TESV.exe!')}
    render = inclusive.get('0x015601d4')
    main = next(t for t in cpu['threads'] if t['threadId'] == first_thread)
    return dict(kind='uncapped-serial-reference', engineWorkOffloaded=False, processId=state['processId'], mainThreadId=first_thread,
                frameRows=chain['csvRows'], averagePresentsPerSecond=chain['averagePresentRate'], frameTimes=chain['frameIntervals'],
                gpuBusy=chain['metrics']['MsGPUBusy'], cpuBusy=chain['metrics']['MsCPUBusy'],
                mainThreadLogicalProcessorPercent=main['oneLogicalProcessorPercent'], processLogicalProcessorEquivalent=cpu['oneLogicalProcessorEquivalent'],
                mainThreadSamples=total, renderPacketCallInclusiveSamples=int(render[1]) if render else None,
                renderPacketCallInclusivePercent=int(render[1])/total*100 if render else None,
                uncappedStateObservations=len(history), invalidPhysicsTimes=0, lostEvents=0, lostBuffers=0,
                frameCSV_SHA256=hashlib.sha256((run/'frames.csv').read_bytes()).hexdigest(),
                limits=['This plugin does not offload engine work; the present rate is not a multicore improvement.',
                        'Exact viewpoint was not supplied; the user confirmed a static normal scene.',
                        'Uncapped state was polled once per second; this is not per-frame physics validation.',
                        'Physics-budget tests do not cover the full Havok simulation or all high-FPS game behavior.',
                        'Inclusive render samples include nested work and are not directly removable frame time.',
                        'CPU/GPU mean times alone do not prove a critical-path bottleneck.'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    a = p.parse_args()
    result = analyze(a.run)
    (a.run/'uncapped-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
