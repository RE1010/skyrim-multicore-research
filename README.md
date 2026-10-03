# Skyrim Multicore Research

An experimental project exploring how to move suitable Skyrim engine work from the main thread to additional CPU cores while preserving correct rendering.

**Current status: Version 8 records real Skyrim draw commands on four worker threads, but it is still slower than the original renderer.** This is a research prototype. It does not yet provide a usable overall FPS improvement or solve Skyrim's main-thread bottleneck.

Updated: October 3, 2026. The verified target is Skyrim Special Edition **1.7.104.0**, with **SKSE 2.3.1**. The plugins check the executable version and hash before attaching.

[Project progress, results, and remaining work](docs/PROJEKTSTAND-2026-10-03.md) · [Repository contents and setup](docs/REPOSITORY.md) · [Publication cleanup](docs/VEROEFFENTLICHUNG.md)

## Version 8: measured results

RenderWorkerBridge replaces eligible `DrawIndexed` calls with command recording on four separate D3D11 deferred contexts. The calling thread replays the finished command lists in their original order. Unsupported states use the original rendering path. Shader preparation and other shared engine work still run on the calling thread.

Version 8 shares immutable binding groups between draws. Each draw retains its own arguments and constant-buffer byte versions. This reduces repeated copying and resource-reference management.

Five 15-second PresentMon captures were taken in the same session near Whiterun's gate and wall. The worker modes were compared twice, reversing their order for the second pair:

| Rendering mode | Average present rate | Average frame time |
| --- | ---: | ---: |
| Original path, with existing diagnostics | **120.51 presents/s** | 8.30 ms |
| V8 copy control, first / second capture | 33.91 / 33.14 presents/s | 29.49 / 30.18 ms |
| V8 shared groups, first / second capture | **41.56 / 42.69 presents/s** | 24.06 / 23.42 ms |

Shared groups improved the average of the two measured rates by **25.7% within the V8 worker path**. About **88.5% of group copies** were avoided. Capture time per attempt fell by **41.1%**, and queue-release time fell by **66.2%**. The optimized path remained about **65% slower than the original reference**.

These are two short comparison pairs in one session, not broad statistical validation. The copy control uses V8's new group representation and is not an exact reconstruction of V7. The original reference includes adapter forwarding and upload observation; it is not an uninstrumented vanilla benchmark. Camera readiness was supplied by the tester and was not continuously recorded during the FPS captures.

All five traces reported zero lost events and buffers, and all adapter error counters stayed at zero. The two optimized intervals recorded **2,962,878 actual worker draws** across four fixed worker threads. [Raw summary and measurement provenance](measurements/20261003-v8-live-comparison/analysis.json).

## Validation and visual observations

Version 8 passed **17/17 tests** and **164 full-image comparisons** across shared/owned modes, with and without the D3D11 debug layer. Tests cover resource lifetime, mode changes with queued draws, changing constants without rebinding, high slots, NULL bindings, numeric state arguments, and resource hazards.

A separate short visual observation showed no obvious bright/dark flicker. The tester subsequently reported that the earlier flicker was also gone during movement. This is a positive observation from the current test, not an automatic correctness check across all Skyrim scenes. The earlier failure's exact cause has not been isolated, and longer movement tests and other scenes remain to be checked.

The test installation was returned to the original rendering path. The prototype starts with **`Enabled=0`**. [V8 package and test instructions](artifacts/render-worker-bridge-v8/README.md).

## Work completed so far

- **Visibility/culling diagnostics:** 68,739 real engine records compared without result differences. The observed path already runs on several Skyrim threads. The diagnostic leaves the original visibility decisions in control.
- **Movement-message search:** 5,875 valid live samples matched the original search, including lists of up to 56,826 entries. No runtime improvement has been demonstrated.
- **NPC stress tests:** A scene with 600 additional guards showed expensive work on existing engine workers and main/frame-thread waits. More NPCs do not stress only the main thread.
- **Standalone D3D11 backend and owned render packets:** Parallel recording, resource ownership, and ordered replay were implemented and tested before integration. Large synthetic batches reduce some main-thread work, but do not establish a Skyrim FPS gain.
- **Uncapping:** The UncappedBenchmark plugin removes the investigated engine/DXGI limits with adjusted physics time budgets. An early, different reference scene reached 236.04 presents/s. That was an uncapping result, not a multicore improvement or a matched V8 baseline.
- **V5 upload reuse:** About 69.6% of repeated private constant-buffer uploads were avoided in one live interval.
- **V6 getter cache:** Repeated state queries were reduced; short live comparisons showed an improvement within the worker path, while the original renderer remained faster.
- **V7 recorder bindings:** Redundant bindings were skipped within each command list. The precise live comparison showed 32.94 versus 33.97 presents/s, with an original reference of 119.26. Earlier short pairs were mixed, so this small benefit was not reliably established.
- **V8 immutable groups:** The current live comparison confirms reduced ownership/publication overhead, with the results above.

## Next research steps

The remaining capture, resource-management, recording, upload, synchronization, and replay costs still outweigh the benefit of parallel recording. Shader preparation, visibility/material decisions, and ordered submission remain partly serial.

The next step is to identify the remaining cost of worker waits, serial small batches, command-list boundaries, and engine/driver work, while checking visual behavior across additional scenes and movement. The measured phase reductions explain part of the overhead; they do not yet explain the entire gap to the original renderer.

## Technical notes and tools

The README, project summary, repository guide, and V8 instructions are available in English. Earlier detailed research notes remain in German; filenames are retained so existing links continue to work.

- [Render integration, implementation, and live comparisons](research/RENDER-BRIDGE-001.md)
- [Main-thread and renderer investigation](research/MAIN-THREAD-004.md)
- [Owned render packets](research/OWNED-RENDER-001.md)
- [Standalone D3D11 parallel backend](research/D3D11-PARALLEL-LAB.md)
- [Additional backend tests and normal scene](research/RENDER-TEST-001.md)
- [First live render capture](research/LIVE-RENDER-001.md)
- [Uncapped measurements](research/UNCAPPED-001.md) and [integration details](docs/UNCAPPED-BENCHMARK.md)
- [Visibility comparison](research/LIVE-CULLING-001.md) and [engine diagnostics](docs/ENGINE-ANBINDUNG.md)
- [Movement prototype](docs/MOVEMENT-FAST-PROTOTYP.md) and [live results](research/LIVE-MOVEMENT-003.md)
- [NPC stress: 200 guards](research/NPC-STRESS-001.md), [600 guards](research/NPC-STRESS-002.md), and [SKSE scene](research/NPC-STRESS-003.md)
- [Feasibility study](docs/MACHBARKEIT.md), [measurement plan](docs/MESSPLAN.md), and [multicore core](docs/MULTICORE-PROTOTYP.md)
- [Successful baseline repeat](research/BASELINE-002.md) and [capture setup](docs/AUFNAHME.md)

`tools/Collect-Setup.ps1` collects local setup information. `tools/Sample-Threads.ps1` measures per-thread CPU time without injection or engine patches. The busiest thread is not automatically identified as the main thread; that requires trace stacks and timing relative to frame presentation.

```powershell
.\tools\Collect-Setup.ps1
# Run once the game is in a repeatable scene:
.\tools\Sample-Threads.ps1 -DurationSeconds 30 -Label whiterun-baseline
```
