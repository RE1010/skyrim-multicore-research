# RenderWorkerBridge V10: Direct Replay of Small Serial Groups

Date: October 3, 2026. Status: built, locally validated, installed, and loaded correctly by SKSE; rejected in the live visual test because shadows flicker.

## Why this change

V9's live worker-path capture averaged 49.05 presents/s. It recorded 107,460 batches, of which 80.00% had fewer than 16 draws. With four workers, these groups already use serial recording: a deferred context records a temporary command list, finishes it, and submits it through the immediate context. Finishing alone accounted for 410.82 ms of the 1,313.09 ms inclusive serial recording interval. This is an overhead candidate, not proof that command-list finishing dominates the whole frame. [V9 evidence and limitations](RENDER-BRIDGE-009.md).

V10 adds the optional `parallel-direct-small` mode. For an eligible group below the existing `workerCount * 4` threshold, the calling thread plays the immutable snapshots directly through the immediate context. This avoids a temporary command list and its finish/execute calls. Larger groups retain worker recording and ordered command-list execution. `parallel` remains the deferred-path control. With four workers, groups of 1–15 draws use the new option and groups of 16 or more remain eligible for workers.

This change reduces overhead on a path that was already serial. It does not move additional engine preparation to workers, prove a solution to Skyrim's main-thread bottleneck, or assume that greater total CPU utilization improves frame time.

## State and ownership rules

The immediate replay runs only on the bridge's owner thread, within the existing context lock and hook bypass. A D3D11.1 context-state object isolates replay from the application's bindings. The bridge uses the same device feature level and D3D11 interface behavior, saves application state, draws using retained immutable snapshots and private constant-buffer uploads, clears the scratch state, and restores application state on exit, including exception exits. The scratch state must not retain the last render targets or shader resources between batches.

State isolation is optional. If the device cannot create the context-state object, the bridge retains deferred recording. A live report must confirm `directSmallAvailable`, `directSmallRequested`, and growing direct counters before it can count as a direct-path test. Foreign-thread flushes use deferred recording. Failures after suppressed or partially executed draws remain fatal rather than silently dropping or duplicating draws.

No supported-state eligibility check, resource barrier, engine scope boundary, queue limit, resource retention rule, constant-byte version, or replay order was relaxed. The new direct recorder has its own persistent private upload cache; shared source buffers still preserve upload-change flags across VS and PS.

Microsoft documents [CreateDeviceContextState](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-createdevicecontextstate) and [SwapDeviceContextState](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate). The latter operates on an immediate context and must not race other immediate-context calls. The bridge's lock and owner-thread restriction preserve that condition for intercepted calls.

## Validation

The final Release build compiles with `/W4 /WX`. **20/20 CTest checks pass.** Eight separate renderer runs cover flat upload lookup, temporary-map lookup, owned snapshots, and direct-small mode, each with and without the D3D11 debug layer: **96 full-image checks per run, 768 total**. All report zero image mismatches, bridge errors, and unexpected debug messages. Deliberate resource-hazard checks and untracked constant writers remain exercised.

The new cases compare original rendering, deferred replay, and direct replay at 1, 2, 3, 4, 15, 16, 17, 63, 64, 255, 256, and 257 draws. They verify dispatch boundaries, exact direct counts, and the absence of command-list finishing for wholly direct groups. State-restoration checks bind a compute shader, constant buffer slot 13, shader-resource slot 127, and sampler slot 15, then verify their preservation. Short resource-lifetime cases release resources before queued replay. Additional mixed groups repeatedly switch between deferred and direct behavior, reuse private uploads across state swaps, and verify that original application constant buffers are restored after every scope.

The live report validator checks direct-mode activation, availability, growing counters, monotonic times, and direct work being a subset of serial work. Seven new deliberately invalid direct-path records are rejected alongside the existing mode and batch-accounting checks. These are hardware correctness tests; their timings are not Skyrim performance evidence.

## Live visual rejection

After the user returned and closed Skyrim normally, the installer backed up V9 and installed the fixed V10 package. SKSE reports version `0000000A` loaded correctly, the installed DLL hash matches the package, and the live summary reports state isolation available, replacement `off`, and zero errors. The local INI keeps output inside the chosen development project. The installation/live record (private artifact; excluded from publication) records this later state; the package manifest and lab report retain their creation-time state before installation.

The ten-second `parallel-direct-small` interval recorded 188,843 direct small-group draws and 1,146,904 worker draws within its validated counter window, with zero adapter errors. The user reported flickering lighting effects and shadows. Two sparse static screenshots showed no gross error; they missed the user's temporal observation.

A repeated, announced ten-second `parallel` control disabled direct-small replay and retained deferred serial fallback. It recorded 1,138,336 worker draws with zero errors, but the user again reported shadow flicker. The user then confirmed that shadows remain quiet in `off` at the same view and camera movement. The failure therefore affects a shared bridge path; direct-small state swapping alone does not explain it. The root cause remains undetermined.

Replacement is confirmed `off`. The planned PresentMon/CPU-trace capture of the new path was deferred after visual rejection. Approximate rates in the short counter-only files are not accepted as a performance result. V10 is **not accepted as a correct optimization**, despite its passing laboratory tests. Two existing agents reviewed common rendering rules and added real depth/shadow coverage: the expanded suite passes 20/20 tests and 896 images, including 128 shadow images, but still does not reproduce the live failure. Separate uncached and full-binding game controls also flicker. [Shadow investigation](RENDER-SHADOW-001.md).

After correcting the visual failure, the requested frame capture will still measure only the new path. It cannot by itself isolate the change's FPS effect, and historical V9/V8 values will not be presented as paired improvement measurements. Direct timing is included in serial recording; worker wait overlaps worker recording; submission does not measure GPU completion.

V10 package (private artifact; excluded from publication) · Validation report (private artifact; excluded from publication) · [Next game test](../docs/NEXT-TEST.md)
