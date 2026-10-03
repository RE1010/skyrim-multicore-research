"""Validate bounded render-pass samples and summarize original synchronous calls."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def quantile(values, fraction):
    values = sorted(values)
    index = (len(values)-1)*fraction
    lo, hi = math.floor(index), math.ceil(index)
    return values[lo]+(values[hi]-values[lo])*(index-lo)


def stats(values):
    return dict(samples=len(values), meanMicroseconds=statistics.mean(values), medianMicroseconds=statistics.median(values),
                p95Microseconds=quantile(values, .95), maximumMicroseconds=max(values)) if values else None


def analyze(session):
    summary = json.loads((session/'summary.json').read_text(encoding='utf-8-sig'))
    if (summary.get('mode') != 'render-pass-diagnostic' or summary.get('status') != 'capture-complete'
            or summary.get('originalRendererPreserved') is not True or summary.get('engineWorkOffloaded') is not False
            or summary.get('codeValidAtCapture') is not True):
        raise ValueError('Completed original-preserving capture with valid code required')
    counters = ['qpcFrequency','captureStartQPC','captureStopQPC','sampleEvery','attemptedCalls','completedCalls',
                'abortedCalls','sampleAttempts','completedSamples','droppedSamples','threadCapacityOverflowCalls','activeCalls']
    if any(type(summary.get(k)) is not int or summary[k] < 0 for k in counters):
        raise ValueError('Invalid counters')
    if not summary['qpcFrequency'] or not 1 <= summary['sampleEvery'] <= 65536 or summary['captureStopQPC'] <= summary['captureStartQPC'] or summary['activeCalls']:
        raise ValueError('Invalid timing/sampling or capture not quiescent')
    threads = {}
    for thread in summary['threads']:
        if any(type(thread.get(k)) is not int or thread[k] < 0 for k in ['threadId','attempted','completed','aborted']):
            raise ValueError('Invalid thread counters')
        if not thread['threadId'] or thread['threadId'] in threads or thread['attempted'] != thread['completed']+thread['aborted']:
            raise ValueError('Inconsistent thread identity/counts')
        threads[thread['threadId']] = thread
    for field, name in [('attemptedCalls','attempted'),('completedCalls','completed'),('abortedCalls','aborted')]:
        if summary[field] != sum(t[name] for t in threads.values()):
            raise ValueError('Thread totals disagree')
    expected_samples = sum((t['attempted']+summary['sampleEvery']-1)//summary['sampleEvery'] for t in threads.values())
    if expected_samples != summary['sampleAttempts'] or summary['completedSamples']+summary['droppedSamples'] > expected_samples:
        raise ValueError('Sample attempts/coverage disagree')
    fields = ['ThreadId','Sequence','BeginQPC','EndQPC','PacketToken','ShaderToken','PropertyToken','GeometryToken','Technique',
              'Arg3','Arg4','Flags','ShaderBefore','ShaderAfter','MaterialBefore','MaterialAfter','TechniqueBefore','TechniqueAfter']
    with (session/'samples.csv').open(encoding='utf-8-sig', newline='') as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != fields:
            raise ValueError('Unexpected CSV schema')
        rows = [{k: int(v) for k, v in row.items()} for row in reader]
    if len(rows) != summary['completedSamples'] or not rows:
        raise ValueError('Sample count mismatch or no render samples')
    seen = set()
    grouped = defaultdict(list)
    changes = Counter()
    for row in rows:
        if any(value < 0 for value in row.values()):
            raise ValueError('Negative sample values')
        thread = threads.get(row['ThreadId'])
        key = row['ThreadId'], row['Sequence']
        if (not thread or key in seen or not 1 <= row['Sequence'] <= thread['attempted']
                or (row['Sequence']-1) % summary['sampleEvery']):
            raise ValueError('Invalid/duplicate sample sequence')
        seen.add(key)
        if row['BeginQPC'] < summary['captureStartQPC'] or row['EndQPC'] < row['BeginQPC']:
            raise ValueError('Invalid sample clock')
        if row['Flags'] > 255 or any(row[k] > 0xffffffff for k in ['Technique','Arg3','Arg4','TechniqueBefore','TechniqueAfter']):
            raise ValueError('Integer argument outside ABI range')
        duration = (row['EndQPC']-row['BeginQPC'])*1e6/summary['qpcFrequency']
        grouped[row['ThreadId']].append(duration)
        for name in ['Shader','Material','Technique']:
            changes[name] += row[name+'Before'] != row[name+'After']
        changes['inFlightAtStop'] += row['EndQPC'] > summary['captureStopQPC']
    complete = not (summary['droppedSamples'] or summary['threadCapacityOverflowCalls'] or summary['abortedCalls'])
    if complete and len(rows) != expected_samples:
        raise ValueError('Complete capture has missing samples')
    durations = [v for group in grouped.values() for v in group]
    seconds = (summary['captureStopQPC']-summary['captureStartQPC'])/summary['qpcFrequency']
    techniques = Counter(row['Technique'] for row in rows)
    return dict(schemaVersion=1, sourceSession=str(session.resolve()), mode='original-render-diagnostic', engineWorkOffloaded=False,
                captureDurationSeconds=seconds, completedCalls=summary['completedCalls'], callRatePerSecond=summary['completedCalls']/seconds,
                validSamples=len(rows), completePeriodicSampleCoverage=complete, originalCallDuration=stats(durations),
                stateChangesInSamples=dict(changes), uniqueSampledPacketTokens=len({r['PacketToken'] for r in rows}),
                uniqueSampledGeometryTokens=len({r['GeometryToken'] for r in rows}),
                mostSampledTechniques=[dict(technique=hex(k),samples=v) for k,v in techniques.most_common(12)],
                threads=[dict(threadId=k,completed=threads[k]['completed'],originalCallDuration=stats(v)) for k,v in sorted(grouped.items())],
                summarySHA256=hashlib.sha256((session/'summary.json').read_bytes()).hexdigest(),
                samplesSHA256=hashlib.sha256((session/'samples.csv').read_bytes()).hexdigest(),
                limitations=['Only caller RVA 0x15601cf is counted, not every render pass, draw call or frame.',
                             'QPC durations include original descendants, preemption and any original waits; they are not exclusive CPU preparation time.',
                             'Sampling overhead can perturb execution.',
                             'Periodic sampling is not unbiased random sampling; sampled durations cannot establish whole-frame savings.',
                             'Tokens are opaque identities of copied pointer values, not lifetime guarantees or safe worker references.',
                             'Global field names are inferred from inspected code; stable before/after values do not prove absence of internal writes.',
                             'No FPS or engine-parallelization gain is measured by this diagnostic.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    result = analyze(args.session)
    output = args.session/'analysis.json'
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(output=str(output), samples=result['validSamples'], calls=result['completedCalls'], coverage=result['completePeriodicSampleCoverage'])))
