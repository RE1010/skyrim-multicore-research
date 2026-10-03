import copy
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('movement_report', Path(__file__).resolve().parents[1]/'tools'/'Summarize-Movement.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
summary = dict(status='capture-complete', mode='movement-diagnostic-only', originalResultsPreserved=True,
               activeCalls=0, samples=2, sampleAttempts=3, completedCalls=128, positive=64, negative=64,
               threads=[dict(threadId=1, completed=128, positive=64, negative=64, aborted=0)],
               qpcFrequency=10000000, captureStartQPC=100000000, captureStopQPC=300000000,
               sampleEvery=64, processId=123, scope='caller-rva-673d68', aborted=0, threadCapacityOverflowCalls=0)
rows = [dict(ThreadId=1, Sequence=1, ControllerToken=11, ListSize=8, LockObservations=1, Result=0, FunctionTicks=100, LockTicks=20),
        dict(ThreadId=1, Sequence=65, ControllerToken=11, ListSize=1000, LockObservations=1, Result=1, FunctionTicks=1000, LockTicks=100)]
report = module.analyse(summary, rows)
assert report['callsPerSecond'] == 6.4 and report['droppedSamples'] == 1
assert report['controllersWithDifferentSampledSizes'] == 1 and report['functionMicroseconds']['mean'] == 55
for change in ('missing_lock', 'duplicate', 'partial', 'bad_counter', 'bad_timing'):
    modified_summary = copy.deepcopy(summary)
    modified_rows = copy.deepcopy(rows)
    if change == 'missing_lock': modified_rows[0]['LockObservations'] = 0
    if change == 'duplicate': modified_rows[1]['Sequence'] = 1
    if change == 'partial': modified_summary['status'] = 'capturing'
    if change == 'bad_counter': modified_summary['positive'] = 63
    if change == 'bad_timing': modified_rows[0]['LockTicks'] = 1000
    try:
        module.analyse(modified_summary, modified_rows)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid diagnostic accepted: '+change)
fast_summary = copy.deepcopy(summary)
fast_summary.update(schemaVersion=2, mode='movement-fast-shadow', samples=2, sampleAttempts=2,
                    verifiedGetterBodies=1122, getterCodeValidAtCapture=True,
                    fastCompared=2, fastMismatches=0, fastUnsupported=0)
fast_rows = copy.deepcopy(rows)
for row in fast_rows:
    row.update(FastTicks=10, FastStatus=0, FastResult=row['Result'], Inspected=row['ListSize'],
               NonNull=row['ListSize'], UnsupportedGetterRVA=0)
fast_report = module.analyse(fast_summary, fast_rows)
assert fast_report['fastShadow']['completedComparisonsAgree']
assert fast_report['fastShadow']['pairedMatchingComplete']['samples'] == 2
assert fast_report['originalEstimateMicroseconds']['mean'] == 54
unknown_summary, unknown_rows = copy.deepcopy(fast_summary), copy.deepcopy(fast_rows)
unknown_summary.update(fastCompared=1, fastUnsupported=1)
unknown_rows[0].update(FastStatus=1, Inspected=1, NonNull=1, UnsupportedGetterRVA=0x1234)
assert module.analyse(unknown_summary, unknown_rows)['fastShadow']['storedComparableFraction'] == .5
mismatch_summary, mismatch_rows = copy.deepcopy(fast_summary), copy.deepcopy(fast_rows)
mismatch_summary['fastMismatches'] = 1
mismatch_rows[0]['FastResult'] = 1
mismatch_report = module.analyse(mismatch_summary, mismatch_rows)
assert not mismatch_report['fastShadow']['completedComparisonsAgree']
assert not mismatch_report['fastShadow']['eligibleForFurtherEvaluation']
assert mismatch_report['fastShadow']['storedMismatches'] == 1
disabled_summary, disabled_rows = copy.deepcopy(fast_summary), copy.deepcopy(fast_rows)
disabled_summary.update(fastCompared=1, getterCodeValidAtCapture=False)
disabled_rows[0].update(FastStatus=4, FastTicks=0, Inspected=0, NonNull=0)
assert not module.analyse(disabled_summary, disabled_rows)['fastShadow']['eligibleForFurtherEvaluation']
for change in ('bad_extent', 'bad_shadow_time', 'bad_status', 'unknown_without_getter', 'unreported_mismatch',
               'bad_compared', 'disabled_work', 'incomplete_hit', 'bad_schema'):
    modified_summary = copy.deepcopy(fast_summary)
    modified_rows = copy.deepcopy(fast_rows)
    row = modified_rows[0]
    if change == 'bad_extent': row['Inspected'] = row['ListSize']+1
    if change == 'bad_shadow_time': row['FastTicks'] = row['FunctionTicks']
    if change == 'bad_status': row['FastStatus'] = 99
    if change == 'unknown_without_getter': row['FastStatus'] = 1
    if change == 'unreported_mismatch': row['FastResult'] = 1
    if change == 'bad_compared': modified_summary['fastCompared'] = 3
    if change == 'disabled_work': row['FastStatus'] = 4
    if change == 'incomplete_hit': row.update(FastStatus=1, FastResult=1, UnsupportedGetterRVA=1)
    if change == 'bad_schema': modified_summary['schemaVersion'] = 1
    try:
        module.analyse(modified_summary, modified_rows)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid shadow diagnostic accepted: '+change)
print('PASS: v1/v2 reports, unknown/disabled paths, mismatches surfaced; invalid counters, extents and timings rejected')
