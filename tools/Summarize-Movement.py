"""Validate and summarize bounded, sampled movement diagnostics; no game access."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def stats(values):
    values = sorted(values)
    if not values:
        return None
    def quantile(fraction):
        position = (len(values)-1)*fraction
        low = int(position)
        high = min(low+1, len(values)-1)
        return values[low]+(values[high]-values[low])*(position-low)
    return dict(samples=len(values), mean=sum(values)/len(values), median=quantile(.5),
                p95=quantile(.95), p99=quantile(.99), maximum=values[-1])


def analyse(summary, rows):
    fast_mode = summary['mode'] == 'movement-fast-shadow'
    if summary['status'] != 'capture-complete' or summary['mode'] not in ('movement-diagnostic-only', 'movement-fast-shadow') or not summary['originalResultsPreserved']:
        raise ValueError('Not a completed original-preserving movement capture')
    if summary['activeCalls'] or len(rows) != summary['samples'] or not rows:
        raise ValueError('Incomplete/empty sample output or still-active writers')
    if summary['completedCalls'] != summary['positive']+summary['negative']:
        raise ValueError('Result counters are inconsistent')
    threads = {item['threadId']: item for item in summary['threads']}
    if len(threads) != len(summary['threads']) or sum(item['completed'] for item in threads.values()) != summary['completedCalls']:
        raise ValueError('Thread counters are inconsistent')
    if sum(item['positive'] for item in threads.values()) != summary['positive'] or sum(item['negative'] for item in threads.values()) != summary['negative']:
        raise ValueError('Thread result counters are inconsistent')
    frequency = summary['qpcFrequency']
    elapsed = (summary['captureStopQPC']-summary['captureStartQPC'])/frequency if frequency > 0 else 0
    if elapsed <= 0 or summary['sampleEvery'] < 1 or summary['sampleAttempts'] < len(rows):
        raise ValueError('Invalid timing/sampling metadata')
    seen = set()
    fast_statuses = collections.Counter()
    mismatches = 0
    for row in rows:
        key = (row['ThreadId'], row['Sequence'])
        if key in seen or row['ThreadId'] not in threads or row['Sequence'] < 1 or (row['Sequence']-1) % summary['sampleEvery']:
            raise ValueError('Invalid/duplicate thread sequence')
        seen.add(key)
        if row['LockObservations'] != 1 or row['ListSize'] < 0 or row['Result'] not in (0, 1):
            raise ValueError('List size not observed under exactly one original lock, or invalid result')
        if row['LockTicks'] < 0 or row['FunctionTicks'] < row['LockTicks']:
            raise ValueError('Invalid timing record')
        if fast_mode:
            status = row['FastStatus']
            if status not in range(5) or row['FastResult'] not in (0, 1):
                raise ValueError('Invalid shadow status/result')
            if not 0 <= row['NonNull'] <= row['Inspected'] <= row['ListSize'] or not 0 <= row['FastTicks'] <= row['FunctionTicks']-row['LockTicks']:
                raise ValueError('Invalid shadow scan extent/timing')
            if not 0 <= row['UnsupportedGetterRVA'] <= 2**32-1:
                raise ValueError('Invalid getter RVA')
            if status == 0:
                if row['UnsupportedGetterRVA'] or (not row['FastResult'] and row['Inspected'] != row['ListSize']) or (row['FastResult'] and not row['NonNull']):
                    raise ValueError('Invalid completed shadow scan')
                mismatches += row['FastResult'] != row['Result']
            elif row['FastResult']:
                raise ValueError('An incomplete scan cannot report a hit')
            if status in (1, 2) and (not row['UnsupportedGetterRVA'] or not row['NonNull']):
                raise ValueError('Missing unsupported getter evidence')
            if status == 3 and (row['UnsupportedGetterRVA'] or not row['NonNull']):
                raise ValueError('Invalid cold-target evidence')
            if status == 4 and any(row[k] for k in ('FastTicks', 'Inspected', 'NonNull', 'UnsupportedGetterRVA')):
                raise ValueError('Disabled candidate recorded work')
            fast_statuses[status] += 1
    dropped = summary['sampleAttempts']-len(rows)
    if fast_mode:
        compared, unsupported, total_mismatches = (summary[k] for k in ('fastCompared', 'fastUnsupported', 'fastMismatches'))
        observed_unsupported = sum(fast_statuses[k] for k in (1, 2, 3))
        if not 0 <= total_mismatches <= compared or compared+unsupported > summary['sampleAttempts'] or unsupported < 0:
            raise ValueError('Invalid shadow counters')
        if not 0 <= compared-fast_statuses[0] <= dropped or not 0 <= unsupported-observed_unsupported <= dropped or not 0 <= total_mismatches-mismatches <= dropped:
            raise ValueError('Shadow counters disagree with stored samples')
        if summary.get('schemaVersion') != 2 or summary['verifiedGetterBodies'] < 0 or not isinstance(summary['getterCodeValidAtCapture'], bool):
            raise ValueError('Invalid shadow schema/code metadata')
    as_us = lambda ticks: ticks/frequency*1e6
    buckets = []
    for low, high in ((0, 0), (1, 8), (9, 64), (65, 256), (257, 1024), (1025, 4096), (4097, 2**32-1)):
        subset = [r for r in rows if low <= r['ListSize'] <= high]
        if subset:
            buckets.append(dict(minListSize=low, maxListSize=high, samples=len(subset),
                                positive=sum(r['Result'] for r in subset),
                                functionMicroseconds=stats([as_us(r['FunctionTicks']) for r in subset]),
                                lockMicroseconds=stats([as_us(r['LockTicks']) for r in subset])))
    controllers = collections.defaultdict(list)
    for row in rows:
        controllers[row['ControllerToken']].append(row)
    report = dict(processId=summary['processId'], mode=summary['mode'], scope=summary['scope'], captureElapsedSeconds=elapsed,
                completedCalls=summary['completedCalls'], callsPerSecond=summary['completedCalls']/elapsed,
                positive=summary['positive'], negative=summary['negative'], samples=len(rows),
                sampleEvery=summary['sampleEvery'], droppedSamples=summary['sampleAttempts']-len(rows),
                abortedCalls=summary['aborted'], threadCapacityOverflowCalls=summary['threadCapacityOverflowCalls'],
                listSize=stats([r['ListSize'] for r in rows]),
                functionMicroseconds=stats([as_us(r['FunctionTicks']) for r in rows]),
                lockMicroseconds=stats([as_us(r['LockTicks']) for r in rows]),
                remainderMicroseconds=stats([as_us(r['FunctionTicks']-r['LockTicks']) for r in rows]),
                listBuckets=buckets, uniqueSampledControllers=len(controllers),
                controllersWithDifferentSampledSizes=sum(len({r['ListSize'] for r in items}) > 1 for items in controllers.values()),
                threads=summary['threads'], limitations=[
                    'Only calls through verified caller RVA 0x673d68 are counted; other callers are out of scope.',
                    'Durations are wall time including diagnostic overhead, scheduling and lock acquisition, not exclusive CPU time.',
                    'Timing/list sizes are systematic per-thread samples; a repeating workload can bias them.',
                    'List size is read under the original lock; exact mutation sites and message type identities are not recorded.',
                    'Different sampled list sizes do not identify mutation sites or distinguish same-size replacement.',
                    'Controller tokens are process-local opaque identifiers, not actor FormIDs.',
                    'An inactive hook still adds call forwarding; no engine optimization or FPS gain is claimed.'
                ])
    if fast_mode:
        complete = [r for r in rows if r['FastStatus'] == 0]
        valid = [r for r in complete if r['FastResult'] == r['Result']]
        unknown = collections.Counter(r['UnsupportedGetterRVA'] for r in rows if r['FastStatus'] in (1, 2))
        def paired(items):
            candidate = sum(r['FastTicks'] for r in items)
            original = sum(r['FunctionTicks']-r['FastTicks'] for r in items)
            return dict(samples=len(items), candidateMicroseconds=stats([as_us(r['FastTicks']) for r in items]),
                        originalEstimateMicroseconds=stats([as_us(r['FunctionTicks']-r['FastTicks']) for r in items]),
                        aggregateOriginalEstimateToCandidateRatio=original/candidate if candidate else None)
        report['fastShadow'] = dict(verifiedGetterBodies=summary['verifiedGetterBodies'],
            codeValidAtCapture=summary['getterCodeValidAtCapture'], compared=compared, mismatches=total_mismatches,
            unsupported=unsupported, storedStatuses={str(k): fast_statuses[k] for k in range(5)},
            storedComparableFraction=len(complete)/len(rows), storedMismatches=mismatches,
            completedComparisonsAgree=total_mismatches == 0,
            eligibleForFurtherEvaluation=bool(compared and total_mismatches == 0 and summary['getterCodeValidAtCapture'] and not summary['aborted']),
            pairedMatchingComplete=paired(valid),
            unsupportedGetters=[dict(rva=hex(rva), samples=count) for rva, count in unknown.most_common()],
            listBuckets=[dict(minListSize=b['minListSize'], maxListSize=b['maxListSize'],
                **paired([r for r in valid if b['minListSize'] <= r['ListSize'] <= b['maxListSize']])) for b in buckets])
        report['originalEstimateMicroseconds'] = stats([as_us(r['FunctionTicks']-r['FastTicks']) for r in rows])
        report['limitations'] += [
            'FunctionTicks includes the sampled candidate prepass; FunctionTicks-FastTicks is an original wall-time estimate including lock and instrumentation.',
            'The candidate runs first under the original lock and affects cache state and lock hold time; paired ratios are not an unbiased speedup or FPS measurement.',
            'Complete scans alone are compared; cold or unknown getter paths and disabled samples retain the authoritative original search.',
            'Getter code is rechecked before capture, not continuously; code changes during capture are outside this prototype compatibility guarantee.'
        ]
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    summary = json.loads((args.session/'summary.json').read_text(encoding='utf-8-sig'))
    samples = args.session/'samples.csv'
    with samples.open(encoding='utf-8-sig', newline='') as stream:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    report = analyse(summary, rows)
    report['samplesSHA256'] = hashlib.sha256(samples.read_bytes()).hexdigest().upper()
    output = args.session/'analysis.json'
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(output=str(output), completedCalls=report['completedCalls'], samples=report['samples'],
                          callsPerSecond=report['callsPerSecond'], listSize=report['listSize'])))
