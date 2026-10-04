> Historical V8 record. Later V10 live testing reproduced shadow flicker; worker replay remains slower than the original renderer and is not an accepted optimization. See the [current project status](PROJEKTSTAND-2026-10-04.md). The latest source is V12 diagnostics; this package remains V8.

# Skyrim Multicore Project: Progress and Results

Updated: October 3, 2026. The objective is to move suitable engine work from the main thread to additional CPU cores and improve frame rates while preserving correct rendering.

**Real rendering work already runs on four dedicated worker threads. A usable performance improvement over the original renderer has not yet been achieved.** Version 8 is installed and has been tested in-game; replacement is disabled again. Two comparison pairs show an average 25.7% increase in present rate from shared state groups within the worker path. The original renderer remains significantly faster.

## Investigation and measurement setup

- The local installation, hardware, and development tools were examined. The verified target is **SkyrimSE.exe 1.7.104.0**, running on an Intel Core i5-14400F and NVIDIA RTX 5060 Ti.
- **SKSE 2.3.1** was downloaded, installed, and used successfully. Plugins check the executable version and hash before attaching.
- CPU tracing with Windows Performance Recorder and present-rate measurements with PresentMon were set up. Reports record process identity, conditions, frame times, and trace loss.
- Normal scenes in and outside Whiterun and stress scenes with additional guards were examined. The 600-guard test showed expensive work on existing Skyrim workers and main/frame-thread waits for jobs. Additional NPCs therefore do not stress only the main thread.
- A serial rendering path was identified as another candidate. The investigated device-creation path already lacks the D3D11 single-thread restriction. Shared state and work ordering must be addressed to move additional work safely.

## Initial engine candidates

**Visibility/culling:** A C++20 core and an SKSE diagnostic plugin were built. Real engine inputs were saved and compared serially and on four workers. The complete live comparison contains **68,739 records with no result differences**. Several Skyrim threads already call this path. The diagnostic does not replace the original game decisions and does not establish an FPS gain.

**Movement messages:** The search that became prominent under heavy NPC load was instrumented. A stateless search prototype was built and compared live: **5,875 valid samples with no result differences**, including lists with up to 56,826 entries. No runtime improvement was demonstrated; the original search remains authoritative.

## Rendering development and integration

A standalone D3D11 laboratory backend was implemented to record commands on multiple workers and replay them in order. Full-image comparisons check ordering, changing materials, and constant contents. Owned render packets with retained resource references and copied constants were then developed.

**RenderWorkerBridge** integrates this approach with the verified Skyrim rendering path. It intercepts eligible real `DrawIndexed` commands, retains their resources, records them on four separate worker contexts, and lets the calling thread replay the finished lists in their original order. Unsupported states remain on the original path. Queued draws are replayed before relevant GPU mutations. Small groups are recorded serially.

The first game test confirmed more than **914,000 actual worker draws during one capture**, proving that rendering work was moved. It also produced FPS losses and bright/dark flicker, so that implementation was not a usable optimization.

| Bridge version | Completed work |
| --- | --- |
| 1–2 | Engine integration, real draw replacement, corrected thread assignment, and limited comparison of observed constant bytes against GPU data. |
| 3 | Separate immediate serial, batched serial, and parallel diagnostic modes; activation expires automatically. |
| 4 | Separate CPU timing for capture, upload copies, recording, worker waits, and replay. |
| 5 | Reuse of already-uploaded immutable constant versions per recorder. About 69.6% of repeated private uploads were avoided in a live interval. |
| 6 | Getter cache: unchanged render states are not queried again. About 37.4% fewer getters per captured draw and 30.9% less capture time in-game. |
| 7 | Skip unchanged bindings within a command list, with a separate full-binding control. Tested in the lab and in-game; backed up when V8 was installed. |
| 8 | Share immutable state groups between draws and create new versions for changed groups. A control copies every group per draw. Tested in the lab and in-game; currently installed with replacement disabled. |

Draw arguments and constant contents remain individual to each draw. Sharing a state block must not modify older draws. Agents independently reviewed cache invalidation, numeric binding arguments, resource lifetime, and test coverage.

## FPS limits and earlier game measurements

**UncappedBenchmark** removes the investigated engine/DXGI limits with adjusted physics time budgets. An early reference capture reached 236.04 presents/s. This was an uncapping result, not a multicore gain. Different scenes and sessions are not directly comparable.

The earlier V7 comparison used 15-second PresentMon captures outside Whiterun:

| Mode | Average present rate | Average frame time |
| --- | ---: | ---: |
| Original path with existing diagnostics | **119.26 presents/s** | 8.39 ms |
| Workers with full bindings | 32.94 presents/s | 30.35 ms |
| Workers with reduced bindings | **33.97 presents/s** | 29.44 ms |

The optimized interval skipped **68.2% of recording bindings**. All three traces reported zero lost events and buffers. The roughly 3.1% higher rate than the worker control came from one pair; earlier short pairs were inconsistent. This does not establish a reliable FPS gain or an improvement over the original renderer.

## Version 8: laboratory and live tests

- A fixed package containing the DLL, INI, hash manifest, and installation instructions was created: [RenderWorkerBridge V8](../artifacts/render-worker-bridge-v8/README.md).
- **17/17 tests passed.** Four lab runs across both ownership modes, with and without the debug layer, produced **164 full-image comparisons with no differences**.
- Targeted tests release temporary texture/SRV owners before queued draws replay, switch ownership modes with a nonempty queue, change constants without rebinding, and check high slots, NULL bindings, and numeric state.
- In a laboratory case with mostly unchanged bindings, approximately **92.3% fewer groups were copied**. Publication and queue release are now timed separately.
- The new control uses the same group representation as the optimized mode. It is not an exact reconstruction of V7's flat snapshots. The lab results alone do not establish a Skyrim FPS gain.

The package was then installed while Skyrim was closed. SKSE confirmed V8. The tester prepared the gate and wall outside Whiterun without adding guards. Five 15-second PresentMon captures compare the original path and two pairs of ownership modes, reversing the pair order for the second comparison:

| Mode | Average present rate | Average frame time |
| --- | ---: | ---: |
| Original path with existing diagnostics | **120.51 presents/s** | 8.30 ms |
| V8 copy control, first / second interval | 33.91 / 33.14 presents/s | 29.49 / 30.18 ms |
| V8 shared groups, first / second interval | **41.56 / 42.69 presents/s** | 24.06 / 23.42 ms |

The pairs show 22.6% and 28.8% higher rates within the worker path. Comparing the means of both rates gives **25.7%**. Approximately **88.5% of group copies** are avoided. Weighted capture time per attempt falls by **41.1%**, and queue-release time falls by **66.2%**. The intended overhead was measurably reduced. Four fixed workers record a combined 2,962,878 actual draws in the two optimized intervals. All five traces report zero lost events and buffers; all adapter errors remain zero.

The optimized rate is still approximately **65% below the original reference**. Two short pairs in one session are not broad statistical validation. Camera readiness was supplied by the tester and was not continuously recorded during FPS measurement. The changed control representation prevents a direct V7 performance claim. Full provenance and CPU timing budgets: [V8 measurement report](../measurements/20261003-v8-live-comparison/analysis.json).

A separately announced ten-second visual test produced six observations with confirmed parallel mode. No obvious bright/dark flicker was visible. The tester subsequently confirmed that flicker was **also gone during movement**. No visible rendering error was reported in the current test. The individual screenshots do not capture every intermediate change or the complete movement sequence; the tester's observation is not an automatic image-equality check. The original path was restored after testing, and `off` was confirmed.

## Remaining work

Capture, resource management, recording, and synchronization overhead is still too high in-game. Shader preparation, material/visibility decisions, and ordered submission remain partly serial. The overall main-thread bottleneck is not solved.

According to the tester, the earlier flicker was **no longer visible during movement in the current V8 test**. The assistant's sparse observations also showed no obvious bright/dark alternation. The exact earlier cause has not been isolated; other scenes and longer movement tests remain unverified. Lab image equality and the current observation do not prove complete rendering correctness in every Skyrim scene.

V8 remains installed with `Enabled=0`. The next step is to identify the remaining performance gap: worker waits, serial small batches, command-list boundaries, upload work, and other engine/driver costs. The new phase budgets confirm lower management overhead but do not explain all FPS losses. The positive visual observation should also be confirmed in other scenes and longer movement tests.

Detailed earlier notes, currently in German: [Render bridge](../research/RENDER-BRIDGE-001.md), [visibility comparison](../research/LIVE-CULLING-001.md), [NPC stress](../research/NPC-STRESS-002.md), [movement search](../research/LIVE-MOVEMENT-003.md), and [uncapping](../research/UNCAPPED-001.md).
