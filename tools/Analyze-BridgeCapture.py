"""Validate live worker replacement using monotonic plugin observations and actual Presents."""
import argparse,json
from pathlib import Path
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def history(p):return [json.loads(s) for s in p.read_text(encoding='utf-8-sig').splitlines() if s.strip()]
def analyze(run):
    state=read(run/'capture-state.json');frames=read(run/'frame-summary.json');rows=history(run/'bridge-state-history.jsonl');uncap=history(run/'uncap-state-history.jsonl')
    mode=state['bridgeMode'];pid=state['processId']
    if state['status']!='captured' or mode not in ('off','parallel','parallel-full-bindings','parallel-owned-snapshots') or not state['requireUncapped'] or len(rows)<3:raise ValueError('Invalid capture/mode/history')
    counters=['renderPassCalls','drawIndexedCalls','replacedDraws','workerRecordedDraws','serialRecordedDraws','originalDraws','batches','errors','foreignContextCalls']
    previous=None
    for row in rows:
        s=row['snapshot']
        if s['processId']!=pid or s['enabled']!=(mode!='off') or not s['worldLoaded'] or not s['contextAttached'] or s['initializationError'] or s['offloadDisabled'] or s.get('requestedMode',mode)!=mode or s['errors']:raise ValueError('Invalid bridge observation')
        if mode=='parallel-full-bindings' and (s.get('requestedMode')!=mode or 'recordingBindings' not in s or 'recordingBindingsSkipped' not in s):raise ValueError('Full-binding control not proven by report')
        if mode=='parallel-owned-snapshots' and (s.get('requestedMode')!=mode or s.get('sharedSnapshotBindings') is not False or 'snapshotGroupCopies' not in s or 'snapshotGroupReuses' not in s):raise ValueError('Owned snapshot control not proven by report')
        if mode=='parallel' and 'sharedSnapshotBindings' in s and s['sharedSnapshotBindings'] is not True:raise ValueError('Shared snapshot mode not active')
        if len(set(s['workerIds']))!=4 or not all(s['workerIds']):raise ValueError('Worker identities invalid')
        if s['workerRecordedDraws']+s['serialRecordedDraws']>s['replacedDraws']:raise ValueError('Duplicate replay accounting')
        if previous and any(s[k]<previous[k] for k in counters):raise ValueError('Counters decreased')
        previous=s
    for row in uncap:
        s=row['snapshot']
        if s['processId']!=pid or not all(s[k] for k in ('enabled','worldLoaded','settingsReady','uncappedActive')) or s['invalidTimes']:raise ValueError('Uncap/physics observation invalid')
    if len(uncap)<3:raise ValueError('Insufficient uncap observations')
    delta={k:rows[-1]['snapshot'][k]-rows[0]['snapshot'][k] for k in counters}
    if delta['drawIndexedCalls']<=0 or delta['renderPassCalls']<=0:raise ValueError('Stale report')
    if mode!='off' and delta['workerRecordedDraws']<=0:raise ValueError('No actual worker replacement during capture')
    if all('recordingBindings' in r['snapshot'] and 'recordingBindingsSkipped' in r['snapshot'] for r in rows):
        for key in ('recordingBindings','recordingBindingsSkipped'):
            values=[r['snapshot'][key] for r in rows]
            if any(a>b for a,b in zip(values,values[1:])):raise ValueError('Binding counters decreased')
            delta[key]=values[-1]-values[0]
        if mode=='parallel-full-bindings' and (delta['recordingBindings']<=0 or delta['recordingBindingsSkipped']):raise ValueError('Full-binding control skipped bindings or did no recording')
    if all('snapshotGroupCopies' in r['snapshot'] and 'snapshotGroupReuses' in r['snapshot'] for r in rows):
        for key in ('snapshotGroupCopies','snapshotGroupReuses'):
            values=[r['snapshot'][key] for r in rows]
            if any(a>b for a,b in zip(values,values[1:])):raise ValueError('Snapshot ownership counters decreased')
            delta[key]=values[-1]-values[0]
        if mode=='parallel-owned-snapshots' and (delta['snapshotGroupCopies']<=0 or delta['snapshotGroupReuses']):raise ValueError('Owned snapshot control reused groups or did no copying')
    if mode=='off' and (delta['workerRecordedDraws'] or delta['replacedDraws']):raise ValueError('Off phase still replaced draws')
    if len(frames['swapChains'])!=1:raise ValueError('Explicit gameplay chain selection required')
    chain=frames['swapChains'][0]
    if int(chain['processId'])!=pid or chain['invalidFrameIntervals'] or chain['syncIntervals']!=['0']:raise ValueError('Invalid frame identity/sync')
    visual=read(run/'visual-verification.json') if (run/'visual-verification.json').exists() else dict(Status='not-verified')
    return dict(kind='skyrim-render-worker-live-capture',mode=mode,processId=pid,actualEngineWorkOffloadedDuringCapture=delta['workerRecordedDraws']>0,visualVerification=visual,
                acceptedAsCorrectOptimization=visual.get('Status')=='passed',
                counterDelta=delta,workerIds=rows[-1]['snapshot']['workerIds'],observations=len(rows),frameRows=chain['csvRows'],averagePresentsPerSecond=chain['averagePresentRate'],
                frameTimes=chain['frameIntervals'],gpuBusy=chain['metrics']['MsGPUBusy'],cpuBusy=chain['metrics']['MsCPUBusy'],
                limitations=['Original mode retains adapter forwarding/upload observation.','Camera/readiness supplied by user.','Offload proves moved D3D recording only; shader preparation and replay remain serial.','Frame result alone is not a multicore performance comparison.'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();result=analyze(a.run)
    (a.run/'bridge-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
