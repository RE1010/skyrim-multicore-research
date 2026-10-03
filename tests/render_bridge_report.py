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
print('PASS: live growth/original/full-binding/owned-snapshot modes; 17 invalid evidence cases rejected')
