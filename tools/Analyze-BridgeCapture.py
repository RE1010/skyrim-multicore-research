"""Validate live worker replacement using monotonic plugin observations and actual Presents."""
import argparse,json
from pathlib import Path
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def history(p):return [json.loads(s) for s in p.read_text(encoding='utf-8-sig').splitlines() if s.strip()]
def batch_diagnostics(rows):
    snapshots=[r['snapshot'] for r in rows]
    if not all('batchSizeBuckets' in s for s in snapshots):return None
    ranges=('1','2-3','4-15','16-63','64-255','256+')
    keys=('batches','draws','workerBatches','serialBatches','workerDraws','serialDraws')
    reasons=('scopeEnd','capacity','unsupportedDraw','gpuBarrier','foreignCall','shutdown')
    def delta(values):
        if any(type(v) is not int or v<0 for v in values) or any(a>b for a,b in zip(values,values[1:])):raise ValueError('Invalid batch/timing counter')
        return values[-1]-values[0]
    for s in snapshots:
        buckets=s['batchSizeBuckets']
        if len(buckets)!=6 or len(s['workerFinishTicks'])!=4 or len(s['workerRecordTicks'])!=4:raise ValueError('Invalid batch/timing dimensions')
        if any(b['batches']!=b['workerBatches']+b['serialBatches'] or b['draws']!=b['workerDraws']+b['serialDraws'] for b in buckets):raise ValueError('Inconsistent batch bucket')
        if sum(b['batches'] for b in buckets)!=s['batches'] or sum(s['flushReasons'][r] for r in reasons)!=s['batches']:raise ValueError('Inconsistent flush totals')
        if sum(b['workerDraws'] for b in buckets)!=s['workerRecordedDraws'] or sum(b['serialDraws'] for b in buckets)!=s['serialRecordedDraws']:raise ValueError('Inconsistent replay totals')
        if s['serialFinishTicks']>s['serialRecordTicks'] or any(f>r for f,r in zip(s['workerFinishTicks'],s['workerRecordTicks'])):raise ValueError('Finish time exceeds inclusive recording')
    buckets=[dict(drawRange=label,**{k:delta([s['batchSizeBuckets'][i][k] for s in snapshots]) for k in keys}) for i,label in enumerate(ranges)]
    return dict(batchSizeBuckets=buckets,flushReasons={r:delta([s['flushReasons'][r] for s in snapshots]) for r in reasons},
                serialFinishTicks=delta([s['serialFinishTicks'] for s in snapshots]),
                workerFinishTicks=[delta([s['workerFinishTicks'][i] for s in snapshots]) for i in range(4)],
                timingNote='Finish ticks are included in recording; worker-wait time includes worker recording and cannot be added to it.')

def analyze(run):
    state=read(run/'capture-state.json');frames=read(run/'frame-summary.json');rows=history(run/'bridge-state-history.jsonl');uncap=history(run/'uncap-state-history.jsonl')
    mode=state['bridgeMode'];pid=state['processId']
    if state['status']!='captured' or mode not in ('off','parallel','parallel-full-bindings','parallel-owned-snapshots','parallel-map-uploads','parallel-direct-small') or not state['requireUncapped'] or len(rows)<3:raise ValueError('Invalid capture/mode/history')
    counters=['renderPassCalls','drawIndexedCalls','replacedDraws','workerRecordedDraws','serialRecordedDraws','originalDraws','batches','errors','foreignContextCalls']
    previous=None
    for row in rows:
        s=row['snapshot']
        if s['processId']!=pid or s['enabled']!=(mode!='off') or not s['worldLoaded'] or not s['contextAttached'] or s['initializationError'] or s['offloadDisabled'] or s.get('requestedMode',mode)!=mode or s['errors']:raise ValueError('Invalid bridge observation')
        if mode=='parallel-full-bindings' and (s.get('requestedMode')!=mode or 'recordingBindings' not in s or 'recordingBindingsSkipped' not in s):raise ValueError('Full-binding control not proven by report')
        if mode=='parallel-owned-snapshots' and (s.get('requestedMode')!=mode or s.get('sharedSnapshotBindings') is not False or 'snapshotGroupCopies' not in s or 'snapshotGroupReuses' not in s):raise ValueError('Owned snapshot control not proven by report')
        if mode=='parallel' and 'sharedSnapshotBindings' in s and s['sharedSnapshotBindings'] is not True:raise ValueError('Shared snapshot mode not active')
        if mode=='parallel-map-uploads' and (s.get('flatUploadLookup') is not False or s.get('sharedSnapshotBindings') is not True or any(k not in s for k in ('temporaryUploadMapEntries','flatUploadEntries','drawUploadDuplicates'))):raise ValueError('Map upload control not proven by report')
        if mode=='parallel' and 'flatUploadLookup' in s and s['flatUploadLookup'] is not True:raise ValueError('Flat upload mode not active')
        if mode=='parallel-direct-small' and (s.get('directSmallRequested') is not True or s.get('directSmallAvailable') is not True or s.get('flatUploadLookup') is not True or s.get('sharedSnapshotBindings') is not True or any(k not in s for k in ('directSmallDraws','directSmallBatches','directSmallTicks'))):raise ValueError('Direct small-batch mode not proven')
        if mode=='parallel' and s.get('directSmallRequested',False):raise ValueError('Deferred control used direct small-batch mode')
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
    if all('flatUploadEntries' in r['snapshot'] for r in rows):
        for key in ('temporaryUploadMapEntries','flatUploadEntries','drawUploadDuplicates'):
            values=[r['snapshot'][key] for r in rows]
            if any(v<0 for v in values) or any(a>b for a,b in zip(values,values[1:])):raise ValueError('Upload lookup counters invalid')
            delta[key]=values[-1]-values[0]
        if mode=='parallel-map-uploads' and (delta['flatUploadEntries'] or delta['temporaryUploadMapEntries']<=0):raise ValueError('Map control used flat lookup or did no upload lookup')
        if mode in ('parallel','parallel-direct-small') and (delta['temporaryUploadMapEntries'] or delta['flatUploadEntries']<=0):raise ValueError('Flat mode used map lookup or did no upload lookup')
    if all('directSmallDraws' in r['snapshot'] for r in rows):
        for key in ('directSmallDraws','directSmallBatches','directSmallTicks'):
            values=[r['snapshot'][key] for r in rows]
            if any(v<0 for v in values) or any(a>b for a,b in zip(values,values[1:])):raise ValueError('Direct small-batch counters invalid')
            delta[key]=values[-1]-values[0]
        if delta['directSmallDraws']>delta['serialRecordedDraws'] or delta['directSmallBatches']>delta['batches']:raise ValueError('Duplicate direct small-batch accounting')
        if mode=='parallel-direct-small' and (delta['directSmallDraws']<=0 or delta['directSmallBatches']<=0):raise ValueError('No actual direct small-batch work')
        if mode=='parallel' and (delta['directSmallDraws'] or delta['directSmallBatches']):raise ValueError('Deferred control emitted direct work')
    if len(frames['swapChains'])!=1:raise ValueError('Explicit gameplay chain selection required')
    chain=frames['swapChains'][0]
    if int(chain['processId'])!=pid or chain['invalidFrameIntervals'] or chain['syncIntervals']!=['0']:raise ValueError('Invalid frame identity/sync')
    visual=read(run/'visual-verification.json') if (run/'visual-verification.json').exists() else dict(Status='not-verified')
    return dict(kind='skyrim-render-worker-live-capture',mode=mode,processId=pid,actualEngineWorkOffloadedDuringCapture=delta['workerRecordedDraws']>0,visualVerification=visual,
                acceptedAsCorrectOptimization=visual.get('Status')=='passed',
                counterDelta=delta,batchDiagnostics=batch_diagnostics(rows),workerIds=rows[-1]['snapshot']['workerIds'],observations=len(rows),frameRows=chain['csvRows'],averagePresentsPerSecond=chain['averagePresentRate'],
                frameTimes=chain['frameIntervals'],gpuBusy=chain['metrics']['MsGPUBusy'],cpuBusy=chain['metrics']['MsCPUBusy'],
                limitations=['Original mode retains adapter forwarding/upload observation.','Camera/readiness supplied by user.','Offload proves moved D3D recording only; shader preparation and replay remain serial.','Frame result alone is not a multicore performance comparison.'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();result=analyze(a.run)
    (a.run/'bridge-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
