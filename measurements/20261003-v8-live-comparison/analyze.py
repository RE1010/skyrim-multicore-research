"""Reproduce the v8 comparison from saved capture and phase reports."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
NAMES = [
    '20261003-150655-094-baseline',
    '20261003-150815-301-baseline',
    '20261003-150930-923-baseline',
    '20261003-151139-070-baseline',
    '20261003-151242-144-baseline',
]
PHASES = ['captureTicks', 'uploadCopyTicks', 'serialRecordTicks',
          'workerWaitTicks', 'executeTicks', 'snapshotPublishTicks',
          'queueReleaseTicks']
runs = []
for name in NAMES:
    directory = ROOT / 'measurements' / name
    result = json.loads((directory / 'bridge-analysis.json').read_text(encoding='utf-8-sig'))
    history = [json.loads(line) for line in
               (directory / 'bridge-state-history.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    first, last = history[0]['snapshot'], history[-1]['snapshot']
    assert first['requestedMode'] == result['mode'] == last['requestedMode']
    assert first['qpcFrequency'] == last['qpcFrequency'] > 0
    delta = lambda key: last[key] - first[key]
    attempts = delta('captureAttempts')
    frequency = first['qpcFrequency']
    phases = {key: delta(key) / frequency * 1000 for key in PHASES}
    trace = (directory / 'trace-stats.txt').read_text(encoding='utf-8-sig')
    lost = {kind: int(re.search(r'Total # Lost ' + kind + r'\s*:\s*(\d+)', trace)[1])
            for kind in ['Events', 'Buffers']}
    assert not any(lost.values())
    assert result['counterDelta']['errors'] == 0
    if result['mode'] == 'parallel-owned-snapshots':
        assert all(not row['snapshot']['sharedSnapshotBindings'] for row in history)
        assert result['counterDelta']['snapshotGroupReuses'] == 0
    copies = result['counterDelta']['snapshotGroupCopies']
    reuses = result['counterDelta']['snapshotGroupReuses']
    runs.append(dict(source=f'measurements/{name}', analysis=result, traceLoss=lost,
        phaseCounterWindowUTC=[history[0]['readAtUTC'], history[-1]['readAtUTC']],
        captureAttempts=attempts, phaseMilliseconds=phases,
        phaseMicrosecondsPerCaptureAttempt={key: value * 1000 / attempts
             for key, value in phases.items()} if attempts else None,
        workerRecordMilliseconds=[(b-a)/frequency*1000 for a,b in
             zip(first['workerRecordTicks'], last['workerRecordTicks'])],
        groupReusePercent=100 * reuses / (copies+reuses) if copies+reuses else None))

fps = lambda i: runs[i]['analysis']['averagePresentsPerSecond']
owned_mean = (fps(1)+fps(4))/2
shared_mean = (fps(2)+fps(3))/2
owned_attempts = sum(runs[i]['captureAttempts'] for i in [1,4])
shared_attempts = sum(runs[i]['captureAttempts'] for i in [2,3])
weighted = lambda indices, attempts: {key: sum(runs[i]['phaseMilliseconds'][key]
    for i in indices)*1000/attempts for key in PHASES}
summary = dict(kind='v8-same-session-live-ownership-comparison', pluginVersion=8,
    processId=38920, processStartFileTime=134355062115800952,
    installationManifest='measurements/bridge-installation-20261003-150331-337/manifest.json',
    runs=runs, pairGainPercent=[100*(fps(2)/fps(1)-1),100*(fps(3)/fps(4)-1)],
    meanOwnedPresentsPerSecond=owned_mean, meanSharedPresentsPerSecond=shared_mean,
    meanRateGainPercent=100*(shared_mean/owned_mean-1),
    sharedRateChangeFromOriginalPercent=100*(shared_mean/fps(0)-1),
    weightedPhaseMicrosecondsPerCaptureAttempt={
        'owned':weighted([1,4],owned_attempts), 'shared':weighted([2,3],shared_attempts)},
    visualObservation='visual-observation.json',
    acceptedAsCorrectOptimization=False, overallPerformanceGain=False,
    limitations=[
        'Two short pairs in one session, no broad statistical validation.',
        'Readiness/camera supplied by user; camera not continuously recorded during FPS captures.',
        'Original includes forwarding and upload observation; not uninstrumented vanilla.',
        'Owned control is the new v8 group representation, not exact v7.',
        'Phase counters are independently published; their window is not exact PresentMon boundaries.',
        'Publication is included in capture time. Worker timings overlap and are CPU-side, not GPU durations.',
        'Normalized phase timings use capture attempts, not matching frame or worker-draw counts.',
        'Earlier flicker unresolved; sparse visual snapshots do not prove correctness.',
    ])
visual = json.loads((OUT/'visual-observation.json').read_text(encoding='utf-8'))
summary['userReportedNoFlickerDuringMovement'] = visual.get(
    'userObservation', {}).get('reportedNoFlickerDuringMovement', False)
if summary['userReportedNoFlickerDuringMovement']:
    summary['limitations'][-1] = (
        'User reports no flicker even during movement in current test; '
        'sparse screenshots do not prove full correctness across scenes.')
session = OUT / 'final-bridge-state.json'
if not session.exists():
    session = ROOT / 'measurements/live-render-bridge/session-38920-107484828/summary.json'
final = json.loads(session.read_text(encoding='utf-8-sig'))
assert final['requestedMode'] == 'off' and not final['enabled'] and final['errors'] == 0
summary['finalState'] = {key: final[key] for key in
    ['processId','requestedMode','enabled','worldLoaded','errors']}
(OUT/'final-bridge-state.json').write_text(json.dumps(final,indent=2)+'\n',encoding='utf-8')
(OUT/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps({key:summary[key] for key in [
    'pairGainPercent','meanOwnedPresentsPerSecond','meanSharedPresentsPerSecond',
    'meanRateGainPercent','sharedRateChangeFromOriginalPercent',
    'weightedPhaseMicrosecondsPerCaptureAttempt','finalState']},indent=2))
