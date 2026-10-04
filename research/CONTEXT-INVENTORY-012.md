# V12: Context Inventory Before Choosing a Thread Boundary

October 4, 2026. This is a diagnostic extension, not a submission proxy or a performance release. It keeps the original rendering path and does not refine the rejected draw replay architecture.

## New evidence and its implication

The V11 gate scene's verified main thread is scheduled for approximately 95.6–95.8% of one processor in the clipped off/free-draw/off windows. Entry-point and render-stack evidence identify this thread independently of CPU ranking. Removing 83.3% of observed indexed draws reduces GPU Busy but does not improve CPU/frame time beyond the original references' drift.

There is also concrete observer contamination: direct sampled instruction-pointer attribution places approximately **10.93%, 10.47%, and 11.52%** of the main thread's samples in the loaded bridge image. These are sample shares, not removable frame-time fractions. The old `off` reference still queries resource descriptors, retains COM objects and copies mapped constant-buffer bytes for future replay. Stack leaf labels alone miss runtime/heap work underneath those operations.

The main thread's sampled placement is overwhelmingly on the higher-performance CPU class: **99.65%, 98.96%, and 99.65%** in the three windows. That rules out predominantly lower-performance-core placement for this main thread in these windows; it says nothing about a future submission worker or exact scheduled durations. The CPU topology was captured later, so dynamic parked flags are not applied retrospectively. Private clipped attribution (private artifact; excluded from publication).

## Two complementary attribution views

The requested categories are not all disjoint. A state setter can spend time in D3D11, NVIDIA and a wait during one call. Use two views of the **same native QPC window and identified owner thread**, rather than summing these axes:

| Requested category | Measurement | Current availability |
| --- | --- | --- |
| Engine work before D3D11 | Stack samples plus verified frame/pass boundaries | Broad engine module attribution exists; exact preparation boundaries remain unidentified |
| D3D11 state calls | Per-method count and sampled original-call wall duration | Four validated V12 native profiles completed |
| Uploads / Map / Unmap / UpdateSubresource | Separate methods, Map modes and outcomes | V12 implemented; upload byte volume remains unknown |
| NVIDIA driver | Exclusive sampled-IP module attribution on the identified thread | V12 matched-window attribution completed; driver work also lies inside API durations |
| Draw / dispatch | Separate methods and sampled original-call durations | V12 implemented; V11 draw ablation already completed |
| Queries / synchronization | Begin/End, GetData outcomes, fence/flush/list counts | V12 implemented; potential drains are not actual stalls |
| Present / DXGI | PresentMon Present API metric and DXGI stack attribution | Existing recorder reused; this profiler does not hook the swapchain |
| True wait / idle | ETW context switches and ready-thread events | Blocked versus runnable delay classified in three V12 windows; responsible API remains unknown |
| Work between Present N and submission for N+1 | Frame timeline and first relevant submission marker | Present timestamps exist; a verified preparation/submission boundary marker remains open |

Exclusive module samples are a statistical attribution of scheduled CPU work. Sampled call wall durations include driver and any blocking; they are not exclusive CPU work. `MsCPUWait` or unscheduled wall time alone is not a classification of true idle versus runnable scheduling delay. Unknown categories stay unknown rather than being assigned the residual.

## What V12 changes

`original-clean` forwards all actual immediate-context calls, including original draw, query and Map results, without the bridge's upload snapshots or replay preparation. It still has the context hook, mutex, counters and query bookkeeping. It is a cleaner instrumented reference, **not an unhooked vanilla reference**.

`profile-context` uses that same forwarding path and inventories all **149 SDK-typed context methods** on the recorded owner thread, both inside and outside the guarded render scope. Per method it records call count, sampled QPC duration, scoped subsets, failed HRESULTs and `GetData`'s legitimate pending `S_FALSE`. Map types and `DO_NOT_WAIT` are separate counters; the flag overlaps the type counters.

Timing surrounds the real original call, excluding before/after callbacks, mutex acquisition and payload copies. Sampling can be every call or one in sixteen per method. Sparse deterministic sampling may alias periodic work; zero sampled calls with positive call count mean unknown duration. Timer overhead has to be measured using the matched control and sampling-density comparison. Native timing expires after at most ten seconds even if the reporter stops. Expiry stops profiling and continues correct original drawing.

Local validation passes **22 checks and 952 full images** across four debug-layer and four hardware-only variants, including 128 shadow/lighting images. New native tests verify original image equality, absence of upload copies/replay, Map modes, getter COM outputs, query HRESULTs, both sampling densities and native expiry. Analyzer fixtures also cover capture identity, active controls, edge-frame trimming and rejected contaminated windows. These checks do not establish a game performance gain. Validation record (private artifact; excluded from publication).

The recorder arms profiling internally after ETW begins and restores `original-clean` in its cleanup. The report carries process-start identity, native snapshot QPC and owner thread ID. The analyzer validates paired counters, original forwarding and cumulative/scoped consistency. Capture analysis must select complete frame rows inside the native counter window, clip ETW to that window, and disclose partial frame edges; never divide a clipped counter total by an unrelated full CSV.

The practical inventory classifies asynchronous **candidates**, owned payload obligations and conservative potential drain calls. A method's classification is not proof that it can safely be queued. Setters require owned arrays and retained objects; resource copies retain identity without freezing mutable content. Device-level initial resource data is outside this context inventory. A successful map must retain its actual pointer and pairing. Readbacks and query results cannot be fabricated.

## Completed live inventory screen

V12 is installed, SKSE confirms version 12 and all 149 context methods are reported. The corrected series completes **14 five-second captures**, with original drawing throughout, zero bridge errors and no draw replacement/suppression. The game finishes in `original-clean` with timing inactive. All four native context profiles pass identity, control, counter, trace-loss and restoration validation. Identical repeated file publications are coalesced only when the complete native snapshot is identical; changing counters at one QPC is rejected.

| Control / density | Observed-original frame mean | Clean frame mean | Profile frame mean |
| --- | ---: | ---: | ---: |
| Observer-cost triplet | 8.649 / 8.578 ms | 5.484 ms | — |
| Sparse triplet 1 | — | 5.373 / 5.380 ms | 5.881 ms |
| Sparse triplet 2 | — | 5.640 / 5.543 ms | 6.124 ms |
| Sparse triplet 3 | — | 5.461 / 5.699 ms | 5.919 ms |
| Full-density diagnostic / subsequent clean | — | 5.462 ms | 7.635 ms |

These table values summarize each capture's full frame CSV. They establish that observer copying and timing substantially perturb the original renderer. They **do not establish a multicore gain** or an unhooked vanilla reference. GPU Busy stays approximately 3.6–3.8 ms. Sparse profiling adds approximately 0.3–0.6 ms relative to its nearby clean controls; full-density profiling adds about 2.2 ms relative to the subsequent clean control. These are descriptive short-interval differences with reference drift, not confidence bounds. A background export probe occurred near the second sparse interval's transition; preserve that interval's timing qualification until its exact overlap is checked.

The first and third validated sparse **native QPC windows** contain 832 and 819 complete frame intervals. These are distinct from their full CSV row counts of 846 and 841; the native counter windows include partial edge work.

| Whole-owner API category | Sparse window 1, approximate ms / complete frame | Sparse window 3, approximate ms / complete frame |
| --- | ---: | ---: |
| State setting | 1.256 | 1.279 |
| Map + Unmap | 0.756 | 0.769 |
| Draw + dispatch | 0.105 | 0.108 |
| UpdateSubresource family | 0.006 | 0.005 |
| Resource copy / clear | 0.015 | 0.017 |
| Queries / sync API family | 0.010 | 0.010 |
| Present API, separately from PresentMon | 0.088 | 0.087 |

These are estimated **inclusive elapsed call durations**, not exclusive removable CPU budgets. They include timer cost and any driver work or blocking inside those calls. Add neither NVIDIA module samples nor Present wait to them. Whole-owner and scoped counts are alternative views; scoped values are subsets, and sparse scoped estimates can remain unknown when their samples miss a rare call. Full-density timing gives a similar qualitative state/Map/draw ranking but changes throughput substantially.

Map frequency is approximately **9,951–10,265 per complete recorded frame**, with **99.60–99.61% `WRITE_DISCARD`** and approximately 40 `WRITE_NO_OVERWRITE` calls per frame. No READ/READ_WRITE/DO_NOT_WAIT maps or failed maps occur in these two windows. `GetData` occurs about **2.05–2.07 times per frame**, with no pending or failed results there. Those observed successful write maps are not proof that arbitrary future maps can use scratch storage.

The architectural consequence is concrete: a conservative acknowledgment for every Map would impose roughly ten thousand producer/consumer handshakes per frame. They are **potential queue drains**, not ten thousand measured GPU stalls. Even an illustrative 0.1 microsecond of extra cost per handshake would add about one millisecond; no actual queue-hop cost is measured yet. A submission pipeline therefore needs a verified upload ownership/renaming strategy or a higher boundary that avoids individual Map round trips. The old draw-only replay boundary addresses the smallest measured API category.

Current recommendation: retain full submission pipelining as a candidate, **do not implement a game-wide proxy yet**, and prioritize an unhooked reference, mapped-write resource metadata/lifetimes and the earlier preparation caller boundary. The remaining instrumentation itself still has cost, so the preset exclusive-CPU gates cannot yet be applied to these inclusive API estimates. The numerical overlap range, actual drain count and upload bytes remain open. [Sanitized frame/API results](CONTEXT-INVENTORY-LIVE-012.json).

## Matched-window CPU and scheduling audit

The independent audit identifies the native context owner as Skyrim's main thread by matching its ETW start address to the verified executable entry point. Raw sampled instruction pointers establish exclusive module attribution without relying on missing private Skyrim symbols. ETL-relative nanoseconds are converted to the native QPC clock using raw export events: all 99 proof events in the first sparse profile imply the same ETL origin, also present in the ETL header. Integer-nanosecond range arguments avoid UTC approximation.

| Exclusive owner-thread leaf samples | Clean before (3,746 samples) | Sparse profile (4,696 samples) | Clean after (3,717 samples) |
| --- | ---: | ---: | ---: |
| Skyrim executable | 41.11% | 39.99% | 42.18% |
| Bridge DLL | 13.16% | 14.54% | 13.64% |
| D3D11 runtime | 11.32% | 11.61% | 10.14% |
| NVIDIA user-mode driver | 4.43% | 4.17% | 4.41% |
| ntdll | 18.02% | 17.82% | 17.86% |

Other modules account for the remaining samples. These percentages use all sampled owner-thread CPU instruction pointers as their denominator; they are not percentages of frame wall time. The clean CPU-reference windows are approximately 4.018 seconds, starting at their first native publication inside ETL; the sparse profile uses its complete 5.021379-second native window. Do not multiply these shares by the unrelated full-CSV frame means above.

Even the cleaner references spend **13–14% of sampled owner CPU directly in the bridge DLL**, before accounting for runtime work called by it. Caller-stack classification independently attributes another **611/3,746 (16.31%)** and **607/3,717 (16.33%)** exclusive SRW acquire/release leaf samples to bridge calls in the clean references. These disjoint direct-bridge and bridge-called-lock bins total approximately **29.5–30.0% of sampled owner CPU**. They establish substantial remaining instrumentation work, not the same percentage of removable frame time, and do not establish lock contention. Remaining ntdll work is not automatically assigned to the bridge. Consequently, the measured CPU limitation applies to the instrumented renderer and is not yet proof of the same budget in an unhooked game.

The first sparse profile also provides verified nearest Skyrim return addresses for important D3D11 calls: `0x100bd53` for `IASetIndexBuffer`, `0x100bd97` for `IASetVertexBuffers`, and `0x10113fb` for a shader-resource setter. Map has several callers, including `0x154960c`, `0x154964f` and `0x156746c`. These stack return RVAs identify useful investigation points in this exact executable; they are not recovered source-level function names or safe thread boundaries.

Context-switch and ready-thread events cover the complete selected windows. The owner is running **93.24–93.42%** of wall time, blocked until ready **6.21–6.36%**, ready but awaiting execution **0.10–0.12%**, and runnable/preempted **0.26–0.28%**. There are no unresolved scheduling intervals in this audit. The blocked portion is predominantly the generic `UserRequest` wait reason. Since context-switch stacks were not enabled, this does **not** establish GPU waits, Present waits or a particular synchronous API. Scheduled lock instructions also cannot be counted as blocked wall time.

This audit strengthens the measurement diagnosis rather than establishing a proxy ceiling. Next priorities are a hook-free or verified minimal-forwarding reference, timer-cost calibration, write-map resource metadata and ownership, and a verified earlier engine boundary. A numerical movable-CPU fraction and the 25%/40% architecture gate remain unset.

## Preset decision policy and overlap model

These are engineering gates fixed before the next live result, not universal performance laws:

- Below **25%** of verified exclusive main-thread CPU work attributable to submission work that can actually move: do not build a submission pipeline based on that budget alone. Investigate earlier preparation.
- **25–40%**: undecided. Require demonstrable producer/consumer overlap, a small queueing cost and few unavoidable drains.
- At least **40%**: a bounded laboratory producer/consumer prototype is a candidate only if synchronization and payload ownership leave useful overlap.

Do not compare these gates directly with exclusive D3D11/NVIDIA DLL leaf percentages or inclusive API wall times. Runtime allocation, COM work, wrapper work and waits need separate attribution; some interface/reference work must remain on the producer. The current scene is not yet classified against these gates.

Use `max(P + enqueue_and_owned_copy, C) + unhidden_sync` as a first steady-state CPU throughput floor, additionally constrained by GPU throughput and presentation. `P` is unavoidable engine/producer CPU work and `C` is movable ordered submission CPU work. For an ideal split with no new cost, fractions of 25% and 40% imply ceilings of roughly 1.33x and 1.67x while the producer dominates. Those are illustrations of the model, **not measured Skyrim improvements**. Finite queues, frame latency, resource reuse and synchronous Present may prevent that floor from being reached. A numerical realistic range remains unavailable until the live call/drain timeline and queueing costs exist.

## Why not a universal free-all-D3D switch

Skipping setters leaves the real context inconsistent with the engine's assumptions. Skipping uploads loses content consumed by later scopes or frames. Scratch `Map` changes read/write semantics, renaming, `NO_OVERWRITE`, pitches, error behavior and memory characteristics. Query no-ops can also alter engine branches or polling behavior. Restoring only bindings cannot restore resource contents or query progress. An intentionally incorrect image does not make those CPU feedback effects disappear.

Such an ablation could be useful as an explicitly incomplete sensitivity experiment in an isolated disposable workload, after an inventory defines its semantics. It is not a clean Amdahl bound for a full proxy and is not enabled in Skyrim. [Microsoft Map contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map), [query return contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).

## Next live protocol

1. Use the same loaded gate view and game process. Make an observer-on versus `original-clean` comparison first; do not compare old V11 and new V12 FPS as an optimization claim.
2. Collect five matched `original-clean` / `profile-context` / `original-clean` triplets. These measure instrumentation cost and drift, not draw suppression. Add full-density versus 1/16 sampling only when the first run is valid.
3. Reuse ETW, PresentMon and the existing trace-loss checks. Save CPU Set topology for joining group/logical-processor IDs to same-window scheduling events. No affinity changes. Higher EfficiencyClass denotes faster hardware; it is not a throughput ratio. [CPU Set layout](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_cpu_set_information).
4. Report setter/draw/upload/Map/GetData frequencies, expected outcomes and potential drains per complete frame. Do not report upload bytes until resource/subresource metadata is verified without reinstating the previous copy overhead.
5. Add preparation/submission boundary markers and wait classification where the first inventory exposes dominant work. Only then populate the overlap budget and choose the lab pipeline or a specific earlier engine cut.

Example, sequentially after scene readiness:

```powershell
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode original-clean
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode profile-context -SampleEvery 16
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode original-clean
```

`tools/Start-ContextStudy.ps1 -Scene 'fixed-gate' -Repeats 3` bundles the observer and timing-density controls through the same capture helper, with one elevation prompt. Every mode change is verified before capture. Analyze a completed profile run with `tools/Analyze-ContextInventory.py --capture <run-directory> --output <report.json>` after exporting its trace-loss statistics. The first launcher attempt is retained as failed: it did not explicitly restore observed `off` before its third control. The corrected helper verifies that transition; this was a recorder-state failure, not a renderer failure.

No complete submission proxy, scratch-Map game ablation, affinity tweak or promised mod/FPS improvement is implemented here. V10's shadow defect remains an open failure of the separate replay path.
