# Study 013: Reference Without Project Diagnostic Hooks

October 4, 2026. This changes the measurement reference, not Skyrim's threading architecture. No new native plugin is built and no live hook is detached. V12's diagnostic binary remains recoverable.

## Why this reference is required

V12's observer/clean comparison removed about 3 ms of diagnostic upload-copy cost. Its subsequent independent CPU audit still placed roughly 29.5–30.0% of clean main-thread samples in disjoint direct-bridge and bridge-called SRW lock instruction bins. That is substantial measurement work, not an equivalent removable frame-time percentage or lock-contention result. An architecture decision needs evidence without those project hooks. [V12 findings](CONTEXT-INVENTORY-012.md).

## Startup preparation and verification

With Skyrim closed, the startup tool verifies the target executable and two exact known diagnostic DLL hashes. It copies the files to a private measurement backup, verifies the copies, and removes only `RenderWorkerBridge.dll` and `MovementMessageProbe.dll` from the SKSE loader directory. Existing disabled culling/probe files are left alone. The uncapping DLL and configuration hashes are checked for changes. Restoration requires a closed game, matching backup/destination identity and hash, and refuses to overwrite a different installed DLL.

Skyrim is then restarted through SKSE. An external reader requests only limited process query and read-memory rights; it cannot write memory, inject code or control game threads. Before and throughout capture it checks:

- The exact process ID/start identity and absence of the four project render/culling/movement diagnostic modules.
- Original bytes at the verified render-pass, movement-search and movement-lock call sites.
- The renderer's original context alias and all 115 base `ID3D11DeviceContext` method targets in the loaded D3D11 runtime.

The first startup check incorrectly required the vtable's *storage* to lie in the D3D11 image. The actual runtime stores the table next to the context object, while all 115 method targets lie in D3D11. The guard was corrected to verify each function target and separately report storage location. The rejected first check is not a valid reference measurement. No Context4 support or 149-slot layout is assumed by this reader.

The completed reference observations also show D3D11 changing nine of its own draw/dispatch method targets between polls. Requiring an identical target array throughout capture was therefore an incorrect analyzer assumption. The corrected analyzer requires stable runtime identity and verifies each observed target remains in D3D11, while reporting per-slot target variants explicitly. This is a runtime-owned dispatch change, not evidence that an external hook was installed or proof of the earlier replay shadow defect's cause.

This is a **project-hook-free reference**, not completely vanilla Skyrim. SKSE, the unchanged uncapping/physics component, overlays and OS/driver overhead remain. Periodic external observations do not prove absence of third-party inline hooks or transient changes between checks. Each QPC observation timestamps a completed multi-read check, not an instantaneous observation of every address.

## Capture and analysis protocol

The tester confirms the same gate/wall view with no menu or console and no new NPCs. Three five-second PresentMon/ETW recordings share one process and one elevation prompt. Each has an external startup preflight, periodic/final reference proof, physics/uncapping history, processor topology, module inventory and raw-QPC frame rows. There is no mode-file activation, replay, suppression, worker creation or cleanup mode change by the absent bridge.

The analyzer rejects wrong-mode, stale-process, missing original-call proof, incomplete/redirected context methods, a loaded project DLL, uncapping failures, nonzero/missing ETW losses, capped/mixed-process frame streams, multiple swap chains and unordered clocks. It records the complete observed frame envelope for a subsequent exact-QPC main-thread CPU export. CPU sample shares, PresentMon CPU Busy, inclusive API times and blocked wall time remain separate denominators.

Local validation covers the new report's positive fixture and contamination rejection cases. The external reader also passes a real startup inspection. V12's earlier 22 native/report checks and 952 images are historical validation, not repeated validation of a new native version.

Measurements and raw traces stay private. A later public update must use neutral paths/identity and curated evidence. A comparison against prior V12 uses different process launches and may include camera/light/cache differences; it is descriptive and cannot establish a multicore gain or a paired causal FPS increase.

## Remaining architecture decision

This reference is intended to reveal the underlying scheduled engine/runtime/driver work after removing project observer cost. Exact ETL clock mapping, verified entry-point main-thread identification and the recorded frame envelope are required before selecting a numerical movable-CPU fraction. The preset 25%/40% gates remain unclassified until that attribution and synchronization/ownership constraints are known.

The next diagnostic, if the resulting budget supports it, inventories the frequently mapped write resources: resource class, usage/access/bind flags, buffer size or texture/subresource/pitch, successful Map/Unmap pairing and reuse/lifetime. That instrumentation belongs in a separate bounded diagnostic run with its own cost control. It must not copy every upload or retain every resource inside this reference. No owned-upload queue or full submission proxy is implemented by this step.

Example commands, after a verified closed game or ready scene respectively:

```powershell
./tools/Set-ProjectHookReference.ps1 -Action Disable
./tools/Start-HookFreeStudy.ps1 -Scene 'fixed-gate-wall' -Repeats 3
./tools/Analyze-HookFreeReference.py <completed-run-directory>
./tools/Set-ProjectHookReference.ps1 -Action Restore -ManifestPath <saved-manifest>
```

## Completed live reference

All three captures pass executable/process identity, external project-hook proof, original call sites, uncapping/physics control, raw-QPC frame checks and zero lost ETW events/buffers. Each has six reference observations. An independent exact-window CPU/scheduling audit completes for references 1 and 3.

| Capture | Frame rows | Mean frame time | PresentMon CPU Busy | PresentMon GPU Busy | Average present rate |
| --- | ---: | ---: | ---: | ---: | ---: |
| Reference 1 | 1,416 | 3.526 ms | 3.445 ms | 3.197 ms | 283.64/s |
| Reference 2 | 1,439 | 3.473 ms | 3.394 ms | 3.202 ms | 287.95/s |
| Reference 3 | 1,441 | 3.471 ms | 3.393 ms | 3.211 ms | 288.08/s |

These are measured presents and frame metrics in the reported normal gate view. They are not a mod-capacity result or a multicore implementation. The CPU/GPU Busy means are close, which weakens the earlier claim of substantial GPU headroom in this normal scene. Means alone cannot identify an exact main-thread critical path or prove a GPU bottleneck. The earlier V12 clean control was a different process launch and still had project hooks; attributing the whole difference causally to one component is not justified.

The corrected analyzer accepts D3D11-owned target variation while rejecting targets outside the runtime. The target variants are preserved in each private analysis and the curated result. No additional live capture was needed to repair the analyzer assumptions; the raw capture evidence is unchanged.

## Exact main-thread attribution

ETW thread-start evidence independently identifies the main thread by its origin matching the verified executable's entry-point RVA `0x38ef310`. The audit clips to the first frame's `CPUStartQPC` through the last frame's `TimeInQPC`: **4.992089 seconds** in reference 1 and **5.0020169 seconds** in reference 3. Integer clock proofs derive the ETL origin from raw QPC and normalized nanoseconds; 199 and 66 proof events agree respectively. No UTC approximation is used for CPU clipping.

| Exclusive sampled main-thread instruction location | Reference 1 (4,474 samples) | Reference 3 (4,503 samples) |
| --- | ---: | ---: |
| Skyrim executable | 60.06% | 58.07% |
| D3D11 runtime | 16.23% | 17.17% |
| NVIDIA user-mode driver | 7.55% | 7.44% |
| ntdll | 2.39% | 2.91% |

Remaining samples are attributed to other modules, including kernel/driver/CRT/overlay components. These percentages use all raw main-thread sample events in the selected frame window. The symbolized-stack denominators are **4,446 / 4,451** and are kept separate. Neither denominator is frame wall time. Direct Skyrim samples are not automatically preparation before D3D11: they may include other engine work and need caller/pass attribution.

The main thread is scheduled/running **89.91–89.96%** of selected wall time, blocked until ready **9.51–9.58%**, ready-to-running **0.15–0.18%**, and runnable/preempted **0.35–0.36%**. Scheduling events cover the whole selected window with no unresolved intervals. Scheduled time can include interrupt/DPC execution. Generic `UserRequest` dominates blocking; absent context-switch stacks prevent assigning that blocking to Map, Present, GPU synchronization or a particular engine job.

Only **one SRW acquire/release CPU leaf sample occurs in each reference**, with D3D11 as the nearest component outside OS/CRT helpers through the Present path. No direct bridge or bridge-called-lock sample class remains. This verifies the removal of the earlier observer signature; it does not convert the cross-launch timing difference into a measured causal component cost.

Public runtime symbols identify the observed `DrawIndexed` targets as `CContext::TID3D11DeviceContext_DrawIndexed_<1>` and `CContext::TID3D11DeviceContext_DrawIndexed_Amortized<1>`. Both appear in these hook-free stacks. Existing generated thunks dereference the live original table for each forwarded call, so the observation establishes **no frozen-dispatch defect** in the existing bridge and no cause for the old shadow failure. Future code that caches callee pointers must account for this runtime behavior.

## Decision after this step

Do not build the complete submission proxy from this normal-scene result. The driver/runtime leaf share is approximately 24%, but that is **not** the preset movable-CPU budget: related wrapper/kernel/COM work, producer responsibilities, waiting and ownership remain unclassified. The 25%/40% gates remain formally unset. The proximity of CPU/GPU Busy in this scene also limits its value as a large CPU-headroom demonstration.

Prioritize verified engine caller/pass attribution and a representative CPU-heavy mod workload before another major architecture implementation. Mapped-write metadata remains a required separate diagnostic if upload/submission work is pursued. The immediate benefit of Study 013 is a trustworthy reference and a stronger reason to investigate earlier engine work, not a game-wide threading switch or a promised mod-capacity gain. [Curated evidence](PROJECT-HOOK-FREE-LIVE-013.json).
