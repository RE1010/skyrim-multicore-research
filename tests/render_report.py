import copy
import csv
import importlib.util
import json
import tempfile
from pathlib import Path

spec=importlib.util.spec_from_file_location('render_report',Path(__file__).resolve().parents[1]/'tools/Summarize-RenderCapture.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
summary=dict(mode='render-pass-diagnostic',status='capture-complete',originalRendererPreserved=True,engineWorkOffloaded=False,
             codeValidAtCapture=True,qpcFrequency=10000000,captureStartQPC=100,captureStopQPC=200000100,sampleEvery=16,
             attemptedCalls=17,completedCalls=17,abortedCalls=0,sampleAttempts=2,completedSamples=2,droppedSamples=0,
             threadCapacityOverflowCalls=0,activeCalls=0,threads=[dict(threadId=42,attempted=17,completed=17,aborted=0)])
fields=['ThreadId','Sequence','BeginQPC','EndQPC','PacketToken','ShaderToken','PropertyToken','GeometryToken','Technique','Arg3','Arg4',
        'Flags','ShaderBefore','ShaderAfter','MaterialBefore','MaterialAfter','TechniqueBefore','TechniqueAfter']
rows=[]
for seq in [1,17]:
    row=dict.fromkeys(fields,0);row.update(ThreadId=42,Sequence=seq,BeginQPC=200+seq,EndQPC=400+seq,PacketToken=seq,ShaderAfter=5)
    rows.append(row)
with tempfile.TemporaryDirectory() as temporary:
    path=Path(temporary)
    def run(s,r):
        (path/'summary.json').write_text(json.dumps(s))
        with (path/'samples.csv').open('w',newline='') as out:
            writer=csv.DictWriter(out,fieldnames=fields);writer.writeheader();writer.writerows(r)
        return module.analyze(path)
    valid=run(summary,rows)
    assert valid['validSamples']==2 and valid['stateChangesInSamples']['Shader']==2 and valid['originalCallDuration']['meanMicroseconds']==20
    rejected=0
    for field,value in [('completedCalls',18),('sampleEvery',0),('activeCalls',1),('qpcFrequency',0),('engineWorkOffloaded',True),('codeValidAtCapture',False),('sampleAttempts',3),('abortedCalls',-1)]:
        s=copy.deepcopy(summary);s[field]=value
        try:run(s,rows)
        except ValueError:rejected+=1
        else:raise AssertionError(field)
    for field,value in [('ThreadId',99),('Sequence',2),('BeginQPC',500),('Flags',256)]:
        r=copy.deepcopy(rows);r[0][field]=value
        try:run(summary,r)
        except ValueError:rejected+=1
        else:raise AssertionError(field)
    try:run(summary,[rows[0],rows[0]])
    except ValueError:rejected+=1
    else:raise AssertionError('duplicate')
    missing=copy.deepcopy(summary);missing['completedSamples']=1
    try:run(missing,rows[:1])
    except ValueError:rejected+=1
    else:raise AssertionError('missing periodic sample')
    dropped=copy.deepcopy(summary);dropped['completedSamples']=1;dropped['droppedSamples']=1
    assert run(dropped,rows[:1])['completePeriodicSampleCoverage'] is False
print(f'PASS: original durations and state changes; {rejected} invalid reports rejected; bounded drops explicitly incomplete')
