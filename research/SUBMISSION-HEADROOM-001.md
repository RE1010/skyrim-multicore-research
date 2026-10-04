# Submission Headroom and the Pipelining Decision

Date: October 4, 2026. Status: historical budgets reviewed; V11 passes local validation and the actual-game off/free-draw/off screen. No large CPU draw-only benefit demonstrated; complete submission-thread feasibility remains open.

## What the existing results establish

The current interception/replay bridge is slower than the original reference. V8's second shared-state interval takes 23.424 ms per present versus 8.298 ms in that session's original interval: a 15.126 ms difference. Optimizing small parts of this implementation does not establish its architectural viability.

| Saved interval | Mean present interval | Mean CPU Busy | Mean GPU Busy |
| --- | ---: | ---: | ---: |
| V8 original/off | 8.298 ms | 8.212 ms | 4.471 ms |
| V8 shared, first | 24.063 ms | 23.985 ms | 6.958 ms |
| V8 shared, second | 23.424 ms | 23.347 ms | 7.103 ms |
| V9 worker only, different session | 20.389 ms | 20.316 ms | 4.224 ms |

V8 used sequential intervals in one session, with camera readiness supplied by the user. V9 has no contemporary reference. Original/off still includes adapter forwarding and upload observation. The values do not describe the cost or benefit of a future complete submission proxy. [V8 report](../measurements/20261003-v8-live-comparison/analysis.json), sanitized budget extract (private artifact; excluded from publication).

| Instrumented phase | V8 second shared, approximately 15.475 s | V9, approximately 15.512 s |
| --- | ---: | ---: |
| Capture | 3,187.51 ms | 3,226.09 ms |
| Publication, included in capture | 923.78 ms | 992.83 ms |
| Original upload copying | 1,008.46 ms | 1,045.91 ms |
| Serial recording | 1,405.96 ms | 1,313.09 ms |
| Calling-thread worker wait | 2,262.31 ms | 2,020.25 ms |
| Execute/submission | 614.35 ms | 618.43 ms |
| Queue release | 909.45 ms | 931.52 ms |
| Serial finish, included in recording | Not exported | 410.82 ms |

These are accumulated CPU-side elapsed times in counter windows, not per-frame exclusive costs. Wait overlaps worker recording; publication and finish are nested. Execute excludes GPU completion. Other engine/adapter work is excluded. Do not add nested/overlapping totals. V9 also reports 80.00% of groups below 16 draws and 77.45% ending at an engine scope boundary.

The saved V8/V9 exports do not identify a reliable exclusive main-thread Skyrim-versus-driver split. V9's process-wide sample weights and busiest-thread ranking cannot supply it. A different, historical 600-NPC trace identified the render/main thread and showed 58.69% exclusive Skyrim, 8.46% NVIDIA and 9.21% D3D11 samples; those proportions cannot be assigned to V8's 8.298 ms reference. [Historical audit](integration/render-thread-audit.json).

## Screening implementation: V11

The plugin now queries the verified actual Skyrim device's creation flags, feature level and `D3D11_FEATURE_THREADING`, reporting the HRESULT separately from `DriverCommandLists` and `DriverConcurrentCreates`. A failed query is not interpreted as unsupported hardware. Native command-list support was previously established only for a separately created laboratory device. Microsoft specifies runtime emulation when `DriverCommandLists` is false; neither boolean predicts a specific replay cost or speedup. [Threading capabilities](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_threading).

The new `free-draw` diagnostic:

- Suppresses original `DrawIndexed` only inside the existing guarded engine render scope and on its owner thread, with no snapshot capture or replay.
- Leaves material/geometry preparation, setters, uploads, other draw kinds and query-enclosed indexed draws running. Foreign-thread conflicts disable suppression within the affected scope.
- Requires an absolute deadline no more than ten seconds ahead. Every draw checks it natively; a stalled reporter cannot keep suppression active indefinitely.
- Starts only through an explicit timed command, defaults to off, and reports suppression and query-fallback counters independently of replacement counters.
- Is an intentionally incorrect image experiment, not an optimization mode or a fix for the shadow error.

Local validation passes **21/21 CTest checks** and **920 full images** across four debug-layer and four hardware-only variants, including 128 shadow/lighting images. Each variant checks 32 genuinely suppressed draws without capture/replay, unchanged outside-scope draws, query fallback, explicit recovery and native expiry. The report validator rejects contaminated, stale, lossy and un-restored captures. Synthetic success does not resolve Skyrim's live flicker. Validation report (private artifact; excluded from publication).

## How to perform and interpret the screen

### Completed live screen

The actual Skyrim device returned HRESULT 0, `DriverCommandLists=true`, `DriverConcurrentCreates=true`, feature level **11_0**, and creation flags **0**. Runtime-emulated command lists and the single-threaded-device flag do not explain the old bridge's measured slowdown on this installation. These are actual-device results, distinct from the previous FL11_1 laboratory device.

| Same-process interval | Frames | Presents/s | Mean frame | CPU Busy | GPU Busy | Frame p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original before | 604 | 121.413 | 8.236 ms | 8.147 ms | 3.626 ms | 10.615 ms |
| Free-draw | 631 | 126.942 | 7.878 ms | 7.788 ms | 2.775 ms | 10.165 ms |
| Original after | 633 | 127.279 | 7.857 ms | 7.769 ms | 3.663 ms | 10.166 ms |

The valid free-draw counter window suppressed **1,610,926 indexed draws (83.319%)** and forwarded 322,509; the total diagnostic activation suppressed 1,929,611. The difference is window length, not missing draw accounting. Query fallback was zero. All three counter windows had zero capture/replay activity and zero adapter errors; all traces had zero lost events/buffers. Original mode and inactive suppression were explicitly confirmed after the test.

Free-draw is 0.359 ms faster than the first original interval, but **0.021 ms slower than the second**. Original-to-original drift is 0.380 ms, larger than the apparent improvement against the first reference. GPU Busy falls by about 0.85–0.89 ms. This triplet therefore does not demonstrate a CPU benefit from omitting the scoped draws beyond reference drift. It is not a statistical proof of no benefit, and is not a pure upper bound for a complete proxy: setters, preparation, uploads and outside-scope draws remain.

The screen argues against spending more time refining the existing draw-level capture/replay architecture without a stronger budget. It does not yet accept or reject whole-submission pipelining. Do not reinterpret a single short triplet's p99 as a robust 1%/0.1%-low benchmark. Validated live report (private artifact; excluded from publication).

Clipped main-thread attribution now identifies the thread from its executable entry point and render stacks. It is scheduled for 95.6–95.8% of one processor in all three windows. Direct instruction-pointer attribution places 10.5–11.5% of its samples inside the loaded bridge, revealing that the observed original reference has material diagnostic work. Those sample shares are not an overhead subtraction. Main-thread sampled placement is 99.0–99.7% on the higher-performance CPU class; placement does not explain a predominantly E-core main thread here. Whole-process NVIDIA percentages remain unsuitable for main-thread budgets. The next diagnostic compares cleaner original forwarding and inventories all context calls without source-upload snapshots. [V12 inventory and decision policy](CONTEXT-INVENTORY-012.md), private clipped attribution (private artifact; excluded from publication).

### Reproduction procedure

Use a fixed, loaded view, uncapped verification and one unchanged process. First record off, then a five-second internally armed free-draw interval, then off again. The recorder arms suppression after WPR starts, waits for confirmation, records PresentMon, and restores off in `finally`; the native deadline is an independent safeguard. Windows UAC and readiness happen before suppression.

```powershell
# Execute each capture only when the previous capture has completed.
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode off
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode free-draw
./tools/Start-Baseline.ps1 -Scene 'fixed-gate' -DurationSeconds 5 -RequireUncapped -BridgeMode off
```

Analyze each CSV with the existing frame summarizer, check its trace losses, then pass the three run directories to `tools/Analyze-SubmissionHeadroom.py --output <report.json>`. It requires a valid actual-device query, growing draw/render counters, positive suppression only in the middle interval, zero capture/replacement growth, draw conservation, active uncapping, one valid gameplay swapchain and verified restoration.

WPR's full trace includes original rendering before arming and after restoration. Stack attribution must be clipped to verified active PresentMon/bridge windows; whole-trace module percentages would mix modes. The report preserves bridge-window timestamps for this purpose. CPU Busy is not exclusive main-thread or driver CPU time.

**This is a mixed CPU/GPU ablation, not a pure Amdahl fraction.** Removing draws also removes GPU work and alters queue pressure, output-dependent rendering and possible query behavior. A large frame-time reduction establishes sensitivity to those draws, not a promised submission-thread saving. A small reduction does not exclude costs in setters/uploads, which a complete proxy would also move. The analyzer deliberately leaves `amdahlFraction` null.

## Pipelining as the larger architecture candidate

Wine's command stream provides a useful design precedent: its source has queued present operations, retained resource references, worker execution and frame-latency signaling. Its map implementation includes upload fast paths and otherwise synchronizes a map queue to obtain the pointer and result. This is implementation evidence, not a benchmark for native Skyrim or a guarantee of transferable performance. No Wine implementation code is copied into this project. [Wine command-stream source](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/cs.c).

The proposed design would own the complete immediate-context command path, rather than rebuild state by getters at each intercepted draw:

```text
engine thread: prepare game work -> maintain logical bindings -> enqueue owned commands
submission thread: consume ordered commands -> execute actual D3D calls -> Present
both threads: bounded acknowledgments for return data, ownership and queue pressure
```

Immediate-context operations and DXGI/Present must share the designated render owner; moving context calls while leaving concurrent Present on the engine thread is not a valid design. [Microsoft threading rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro).

Several contracts require explicit treatment:

1. **Complete interception and logical state:** context interface aliases, getters, implicit unbinding, all supported stages, resource updates, output/UAV operations and fallback calls. Existing generated forwarding covers 149 methods, but forwarding coverage is not command serialization or ownership coverage.
2. **Owned payloads and lifetimes:** copy arrays/data before their caller reuses storage; retain objects until consumption; model mapped writes, subresource pitches, ranges and content versions. A ring needs both byte limits and backpressure.
3. **Synchronous results:** map pointers, readbacks, query results, errors and device removal cannot be fabricated. Initially use conservative ordered acknowledgments. Measure whether these drains erase useful overlap before adding upload virtualization.
4. **Presentation:** `Present` returns an HRESULT and has swapchain/backbuffer effects. A synchronous present acknowledgment can limit across-frame overlap while still permitting overlap earlier within a frame. Returning a fabricated success to allow the next frame is not the initial design. [Present contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).
5. **Latency and correctness:** begin with a conservative queue cap, instrument queued versus consumed work and input-to-present delay, and validate multiple-frame resource reuse. Two queued frames is a policy to test, not a free latency guarantee.

Driver work remains part of system throughput. A rough idealized CPU model is the maximum of producer and consumer work, plus unhidden synchronization; actual frame pacing also depends on the GPU and presentation. Pipelining is therefore promising only when enough independent producer work overlaps submission. Higher-level render preparation parallelization remains another possible architecture, not inherently limited to original-plus-overhead.

## Decision gates

1. Completed: actual-device reporting and short draw-ablation screen. Reconstruct exclusive driver/engine and wait attribution in the same scene and time window. Do not extrapolate the 600-NPC trace.
2. Inventory context calls with synchronous results, mapped upload bytes and query/Present drains. A complete-proxy decision needs this broader budget, not just draw removal.
3. If the budget supports overlap, build a separate laboratory producer/consumer prototype with owned commands, bounded queues and conservative acknowledgments before game-wide context ownership transfer. If preparation dominates, investigate the earlier engine boundary instead.
4. Worker placement on the hybrid i5-14400F is worth a controlled check: it has six P-cores and four E-cores. Log actual scheduling before trying affinity; do not pin the whole game or raise global priority. Placement is currently unmeasured. [Intel specifications](https://www.intel.com/content/www/us/en/products/sku/236777/intel-core-i5-processor-14400f-20m-cache-up-to-4-70-ghz/specifications.html).
5. Treat fewer lists and `RestoreContextState=FALSE` as controlled renderer-ownership experiments. The latter leaves default immediate state and cannot be applied blindly beneath Skyrim's state assumptions. [ExecuteCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist).
6. Before a general performance/mod-capacity claim, add reproducible camera/state control, multiple trials and temporal image validation. Camera replay alone does not freeze animation, simulation or lighting; raw screenshot hashes can differ without a renderer defect. V10's reported live shadow flicker remains an open failure, despite V8's earlier positive visual report.

The current bridge is not accepted as a performance solution. This investigation tests whether a different cut or pipelined architecture has enough headroom to justify its complexity. The public repository is unchanged.
