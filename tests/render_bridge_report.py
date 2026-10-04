"""Ensure a loaded DLL, stale report, or diagnostic-only work cannot prove live offload."""
import copy,importlib.util,json,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bridge_analysis',root/'tools/Analyze-BridgeCapture.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def run_case(mode='parallel',mutation=None):
    s=dict(processId=42,enabled=mode!='off',requestedMode=mode,worldLoaded=True,contextAttached=True,initializationError=False,offloadDisabled=False,
           renderPassCalls=100,drawIndexedCalls=200,replacedDraws=50,workerRecordedDraws=40,serialRecordedDraws=10,originalDraws=150,batches=4,errors=0,foreignContextCalls=0,workerIds=[10,11,12,13])
    rows=[]
    for i in range(3):
        row=copy.deepcopy(s);row['renderPassCalls']+=i*10;row['drawIndexedCalls']+=i*100;row['originalDraws']+=i*(20 if mode!='off' else 100)
        if mode!='off':row['replacedDraws']+=i*80;row['workerRecordedDraws']+=i*80
        if mode=='parallel-full-bindings':row.update(recordingBindings=100+i*100,recordingBindingsSkipped=50)
        if mode=='parallel-owned-snapshots':row.update(sharedSnapshotBindings=False,snapshotGroupCopies=100+i*100,snapshotGroupReuses=50)
        if mode=='parallel-map-uploads':row.update(sharedSnapshotBindings=True,flatUploadLookup=False,temporaryUploadMapEntries=100+i*100,flatUploadEntries=0,drawUploadDuplicates=50+i*20)
        if mode=='parallel-direct-small':
            row.update(sharedSnapshotBindings=True,flatUploadLookup=True,temporaryUploadMapEntries=0,flatUploadEntries=100+i*100,drawUploadDuplicates=50+i*20,
                       directSmallRequested=True,directSmallAvailable=True,directSmallDraws=5+i*10,directSmallBatches=2+i,directSmallTicks=100+i*50)
            row['serialRecordedDraws']+=i*10;row['replacedDraws']+=i*10;row['originalDraws']-=i*10;row['batches']+=i
        rows.append(dict(snapshot=row))
    if mutation:mutation(rows)
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp)
        (p/'capture-state.json').write_text(json.dumps(dict(status='captured',bridgeMode=mode,processId=42,requireUncapped=True)))
        (p/'frame-summary.json').write_text(json.dumps(dict(swapChains=[dict(processId=42,invalidFrameIntervals=0,syncIntervals=['0'],csvRows=10,averagePresentRate=100,frameIntervals={},metrics=dict(MsGPUBusy={},MsCPUBusy={}))])))
        (p/'bridge-state-history.jsonl').write_text('\n'.join(map(json.dumps,rows)))
        uncap=dict(processId=42,enabled=True,worldLoaded=True,settingsReady=True,uncappedActive=True,invalidTimes=0)
        (p/'uncap-state-history.jsonl').write_text('\n'.join(json.dumps(dict(snapshot=uncap)) for _ in range(3)))
        return module.analyze(p)
assert run_case()['actualEngineWorkOffloadedDuringCapture']
assert not run_case('off')['actualEngineWorkOffloadedDuringCapture']
assert run_case('parallel-full-bindings')['actualEngineWorkOffloadedDuringCapture']
assert run_case('parallel-owned-snapshots')['actualEngineWorkOffloadedDuringCapture']
assert run_case('parallel-map-uploads')['actualEngineWorkOffloadedDuringCapture']
assert run_case('parallel-direct-small')['counterDelta']['directSmallDraws']==20
def rejects(mutate,mode='parallel'):
    try:run_case(mode,mutate)
    except ValueError:return
    raise AssertionError('Invalid live evidence accepted')
rejects(lambda rows:[r['snapshot'].update(workerRecordedDraws=40,replacedDraws=50) for r in rows])
rejects(lambda rows:[r['snapshot'].update(contextAttached=False) for r in rows])
rejects(lambda rows:rows[-1]['snapshot'].update(errors=1))
rejects(lambda rows:rows[-1]['snapshot'].update(offloadDisabled=True))
rejects(lambda rows:rows[-1]['snapshot'].update(drawIndexedCalls=1))
rejects(lambda rows:rows[-1]['snapshot'].update(workerIds=[10,10,12,13]))
rejects(lambda rows:rows[-1]['snapshot'].update(workerRecordedDraws=10000))
rejects(lambda rows:rows[-1]['snapshot'].update(requestedMode='serial'))
rejects(lambda rows:rows[-1]['snapshot'].update(workerRecordedDraws=45,replacedDraws=55),'off')
rejects(lambda rows:rows[-1]['snapshot'].update(recordingBindingsSkipped=51),'parallel-full-bindings')
rejects(lambda rows:[r['snapshot'].pop('recordingBindings') for r in rows],'parallel-full-bindings')
rejects(lambda rows:rows[-1]['snapshot'].update(sharedSnapshotBindings=True),'parallel-owned-snapshots')
rejects(lambda rows:rows[-1]['snapshot'].update(snapshotGroupReuses=51),'parallel-owned-snapshots')
rejects(lambda rows:[r['snapshot'].pop('snapshotGroupCopies') for r in rows],'parallel-owned-snapshots')
rejects(lambda rows:rows[-1]['snapshot'].update(snapshotGroupCopies=99),'parallel-owned-snapshots')
rejects(lambda rows:[r['snapshot'].update(snapshotGroupCopies=100) for r in rows],'parallel-owned-snapshots')
rejects(lambda rows:[r['snapshot'].update(sharedSnapshotBindings=False) for r in rows])
rejects(lambda rows:rows[-1]['snapshot'].update(flatUploadLookup=True),'parallel-map-uploads')
rejects(lambda rows:rows[-1]['snapshot'].update(flatUploadEntries=1),'parallel-map-uploads')
rejects(lambda rows:[r['snapshot'].update(temporaryUploadMapEntries=100) for r in rows],'parallel-map-uploads')
rejects(lambda rows:[r['snapshot'].pop('flatUploadLookup') for r in rows],'parallel-map-uploads')
rejects(lambda rows:rows[-1]['snapshot'].update(sharedSnapshotBindings=False),'parallel-map-uploads')
print('PASS: live growth/original/full-binding/owned-snapshot/map-upload modes; 22 invalid evidence cases rejected')
rejects(lambda rows:rows[-1]['snapshot'].update(directSmallAvailable=False),'parallel-direct-small')
rejects(lambda rows:rows[-1]['snapshot'].update(directSmallRequested=False),'parallel-direct-small')
rejects(lambda rows:[r['snapshot'].update(directSmallDraws=5) for r in rows],'parallel-direct-small')
rejects(lambda rows:rows[-1]['snapshot'].update(directSmallDraws=10000),'parallel-direct-small')
rejects(lambda rows:rows[-1]['snapshot'].update(directSmallBatches=10000),'parallel-direct-small')
rejects(lambda rows:rows[-1]['snapshot'].update(directSmallTicks=99),'parallel-direct-small')
rejects(lambda rows:[r['snapshot'].update(directSmallRequested=True) for r in rows])
print('PASS: direct small-batch activation/growth/accounting; seven invalid evidence cases rejected')

# Batch diagnostics must conserve draws and distinguish finish from inclusive
# recording, without accepting counter resets or missing replay work.
diagnostic_rows=[]
for i in range(3):
    buckets=[dict(batches=0,draws=0,workerBatches=0,serialBatches=0,workerDraws=0,serialDraws=0) for _ in range(6)]
    buckets[0].update(batches=i,draws=i,serialBatches=i,serialDraws=i)
    buckets[3].update(batches=i,draws=32*i,workerBatches=i,workerDraws=32*i)
    diagnostic_rows.append(dict(snapshot=dict(batchSizeBuckets=buckets,batches=2*i,workerRecordedDraws=32*i,serialRecordedDraws=i,
        flushReasons=dict(scopeEnd=i,capacity=0,unsupportedDraw=0,gpuBarrier=i,foreignCall=0,shutdown=0),
        serialFinishTicks=2*i,serialRecordTicks=10*i,workerFinishTicks=[3*i]*4,workerRecordTicks=[20*i]*4)))
diagnostics=module.batch_diagnostics(diagnostic_rows)
assert diagnostics['batchSizeBuckets'][3]['workerDraws']==64
assert diagnostics['flushReasons']['scopeEnd']==2 and diagnostics['serialFinishTicks']==4
assert module.batch_diagnostics([dict(snapshot={})]) is None
def rejects_diagnostics(mutate):
    rows=copy.deepcopy(diagnostic_rows);mutate(rows)
    try:module.batch_diagnostics(rows)
    except ValueError:return
    raise AssertionError('Invalid batch diagnostics accepted')
rejects_diagnostics(lambda rows:rows[-1]['snapshot']['batchSizeBuckets'][3].update(draws=63))
rejects_diagnostics(lambda rows:rows[-1]['snapshot']['flushReasons'].update(capacity=1))
rejects_diagnostics(lambda rows:rows[-1]['snapshot'].update(serialRecordedDraws=3))
rejects_diagnostics(lambda rows:rows[-1]['snapshot'].update(serialFinishTicks=21))
rejects_diagnostics(lambda rows:rows[-1]['snapshot'].update(workerFinishTicks=[41]*4))
rejects_diagnostics(lambda rows:[r['snapshot'].update(serialFinishTicks=v) for r,v in zip(rows,[0,3,2])])
print('PASS: batch size/reason/replay conservation, six invalid timing/accounting cases rejected')
