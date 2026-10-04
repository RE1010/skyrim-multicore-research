# RenderWorkerBridge V9: Per-Draw Upload Lookup

Date: October 3, 2026. Status: hardware validation complete; one Skyrim worker-path capture complete; no contemporary comparison or verified performance gain.

## Reason for this experiment

V8 moves actual draw recording to four workers but remains substantially slower than the original renderer. Its two optimized intervals averaged 41.56 and 42.69 presents/s, compared with 120.51 for the original reference. Immutable binding groups reduced one source of overhead, but the result does not solve the main-thread bottleneck.

Code inspection found an additional temporary allocation in recording: a per-draw `std::map` deduplicates constant buffers before upload. V9 replaces that temporary lookup with a bounded scratch table and retains the map implementation as an explicit control. This removes temporary map nodes, not all allocations in the rendering path. The maximum-size table uses linear search, so improved performance must be measured rather than assumed.

Resource ownership remains with the immutable snapshots and persistent per-recorder upload cache. The scratch table contains only transient pointers and changed flags. The changed flag is retained for duplicate bindings so both VS and PS are rebound after an upload that can rename storage. Constant versions are still resolved for every draw. No barrier, worker count, batch threshold, or replay order was relaxed.

## Validation

The Release build compiles with `/W4 /WX`. **19/19 CTest checks pass.** Six complete renderer runs cover flat lookup, map lookup, and owned binding snapshots with and without the D3D11 debug layer: **56 full-image checks per run, 336 total**, no image differences, bridge errors, or unexpected debug messages.

Fifteen additional image checks in each renderer run consume every constant slot and compare original rendering with flat/map lookup on workers and in serial recording. They cover 28 distinct buffers, 14 buffers shared across VS/PS, and sparse bindings with NULL holes. Exact upload and duplicate counts are checked, so a repeated source must be uploaded once and its changed flag must reach both stages. Existing changing-constant, lifetime, high-slot, ownership-switch, resource-hazard, ordering, and deliberate untracked-writer tests remain active.

Batch histograms must conserve recorded draw totals and replay counts; reason counters must conserve batch counts. Command-list finish ticks must remain below their inclusive recording ticks. The report validator rejects a control with the wrong lookup mode, missing counters, no growing map activity, or flat activity in the map control. Separate tests reject inconsistent batch and timing evidence.

## Diagnostics for the next structural decision

V8's first shared interval recorded 1,643,252 replaced draws in 88,683 batches, approximately 18.5 draws per batch on average. This average includes serial and worker batches; it does not reveal their distribution or prove that scheduling is the dominant cost.

V9 records six batch-size ranges, split between serial and worker paths, and reasons why a batch is closed. Separate `FinishCommandList` timings distinguish list construction from the rest of recording. These data will help decide whether to change work partitioning, reduce safe boundaries, or pursue an earlier engine integration point. They do not justify removing resource barriers.

Timing overlap: publication is included in capture; finish is included in recording; the calling thread's worker wait overlaps worker recording. Submission timing does not measure GPU completion.

## Live observation and next decision

At the user's request, only the new `parallel` path was captured in the normal view outside Whiterun. No fresh original-path or map-control interval was recorded. The 15-second PresentMon recording contains **733 frames, averaging 49.05 presents/s**, with mean frame time 20.39 ms and p95 22.64 ms. Mean GPU busy is 4.22 ms. The bridge-counter window contains **1,636,944 actual worker-recorded draws**, 270,762 serial draws, 107,460 batches, and no bridge errors. ETW reports zero lost events and buffers. Replacement was returned to `off`.

**80.00% of batches contain fewer than 16 draws**, the four-worker dispatch threshold. **77.45% end at an engine render scope boundary**. Serial command-list finishing accounts for 410.82 ms within 1,313.09 ms of inclusive serial recording in the counter interval. These results motivate V10's optional direct replay of already-serial small groups; they do not justify moving draws across engine boundaries or removing GPU barriers.

The process averages 2.08 logical processors of scheduled CPU time in the ETW interval. The busiest thread occupies 86.34% of one logical processor; CPU ranking alone does not identify its engine role or the frame critical path. The bridge's four known workers each occupy approximately 8.6–8.9% of one logical processor.

Sparse static screenshots showed no obvious gross rendering error. They cannot verify movement, intermittent shadow flicker, or general visual correctness. This single run does **not** establish an FPS improvement. Historical V8 captures come from another session and are not a paired comparison. Counter, PresentMon, and ETW intervals differ slightly; finish time is included in recording and worker wait overlaps worker recording.

The package manifest and laboratory analysis describe their creation-time state before installation. The later live-test record (private artifact; excluded from publication) records installation and the completed capture. The phase report (private artifact; excluded from publication) contains the measured distributions and timing limitations. The next requested capture will measure the new V10 path directly; a new original-path recording is not required for that observation, but no paired FPS gain will be claimed without comparison evidence.

The D3D11 recording and ordered-replay constraints are documented by [Microsoft: Immediate and Deferred Rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render). Dynamic upload restrictions are documented by [Microsoft: Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map).

V9 test package (private artifact; excluded from publication) · Laboratory results (private artifact; excluded from publication) · [V8 live evidence](../measurements/20261003-v8-live-comparison/analysis.json)
