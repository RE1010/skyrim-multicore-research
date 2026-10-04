# Shadow Flicker Investigation

Date: October 3, 2026. Status: reproduced by user in two bridge modes; original path quiet; root cause pending.

V10's passing hardware image suite did not cover all conditions encountered in Skyrim's shadow renderer. A short test at Whiterun's gate exposed this gap. Adapter error counters remained zero, so those counters do not establish visual correctness.

| Observation | Mode | Result |
| --- | --- | --- |
| First announced 10-second interval | `parallel-direct-small` | User reports flickering lighting effects and shadows. 188,843 direct draws and 1,146,904 worker draws in the validated counter interval; zero errors. |
| First deferred control | `parallel` | Counter capture completed, but user requested a repeat after missing the visual interval. Do not treat this as a visual verdict. |
| Repeated control, announced with five-second lead-in | `parallel` | User reports shadow flicker. 1,138,336 worker draws; zero errors; new direct-small path disabled. |
| Same view/movement after returning to original | `off` | User confirms shadows remain quiet. |
| State-cache control | `parallel-uncached` | User reports continued shadow flicker. 950,868 worker draws; zero reused getter-state groups; zero errors. |
| Full-binding control | `parallel-full-bindings` | User reports continued shadow flicker. 1,106,397 worker draws; zero skipped recording bindings; zero errors. |
| Serial packet replay | `serial` | User reports a brief initial flicker followed by quiet shadows. Zero worker draws, 1,343,678 serial draws; zero errors. This is not an unqualified flicker-free verdict. |

The direct-small feature alone cannot account for the failure: it also occurs in the deferred worker control. This does not prove which shared state, resource, upload, ordering rule, or driver behavior is responsible. Stationary screenshots missed the temporal error. No V10 PresentMon/CPU trace recording was taken after rejection; short independently published counter rates are not performance evidence.

The installed V10 remains available for diagnosis, with replacement disabled. The original V9 installation has a recoverable installer backup. Public sources have not been updated with this rejected experiment.

Two existing agents were assigned focused tasks: a read-only review of common render/state/resource rules, and test coverage using an actual depth-only shadow pass followed by depth sampling and comparison-sampler lighting. The latter must make depth differences visible in the GPU-readback image and exercise small and larger groups, state changes, and output/input transitions. Merely testing another color-only scene would not address this coverage gap.

## Initial code review

The review found a conditional omission: capture checks only output-merger UAV slots 0–7, while feature level 11_1 can expose 64 slots. An active UAV above slot 7 would be missed, and replay does not recreate UAV bindings. The test device uses feature level 11_0, where the limit is eight; the presence of high-slot UAVs in the Skyrim scene has not been established. This is a separate guard issue, not a proven explanation of the shadows. [Microsoft feature-level table](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro), [OM UAV binding API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews).

The review found no concrete omission in private-upload deduplication or hook argument forwarding. It confirmed that the original suite lacked DSV creation, depth readback/sampling, and null-PS shadow passes. Dynamic vertex/index-buffer discard updates also lack dedicated coverage; current nonconstant Map handling appears to flush before mutation, so that is a test gap rather than a confirmed ordering defect.

## Additional hardware depth coverage

The test agent added 16 actual shadow/lighting images to each complete renderer run, increasing its image count from 96 to 112. A null pixel shader writes a typeless R32 depth texture through a D32 DSV; the lighting pass consumes raw depth and comparison-sampler visibility in its readback image. Changed depth comparison/write masks, bias/culling/scissors, high-slot depth SRVs and samplers, implicit writable DSV/SRV unbinding, and legal read-only DSV/SRV overlap affect the oracle. Small groups of seven draws and groups of 257 exercise serial fallback, direct replay, worker dispatch, capacity, and one-draw tails. Exact counters reject accidental fallback that could mask a replacement failure.

All **20/20 CTest checks pass**. Eight complete runs across flat/map/owned/direct modes, with and without the D3D11 debug layer, yield **896 full images, including 128 new shadow/lighting images**, with zero mismatches, bridge errors, or unexpected debug messages. These are later, expanded tests: the fixed V10 package retains its original 768-image creation-time report. Expanded laboratory report (private artifact; excluded from publication).

The new tests still do **not** reproduce the Skyrim flicker. Independently disabling direct-small replay, cached getter results, or reduced recording setters did not eliminate it in the game. Serial packet replay produced only brief initial flicker according to the user, then quiet shadows. This points toward a difference in worker execution or its interaction with frame state, but does not establish a cause or fully validate serial replay. A passing new lab test does not erase the live failure; the same game scene must pass a later announced movement/shadow check before the path can be accepted.

## Bounded live constant verification

An initial verification interval had no growing render calls and zero checked constants; it provides no correctness evidence. A repeat in the rendering scene checked **64 eligible constant-buffer snapshots against GPU-read source bytes**, with zero mismatches, zero differing bytes, zero adapter errors, and no replaced draws. This checks the capture-side byte assumption for that bounded sample. It does not verify every frame, textures, geometry, private worker-buffer execution, or frame-history resources. GPU readback stalls make this unsuitable as a frame-rate measurement. Replacement was returned to `off`.

The next investigation should capture the state/resources of the actual failing game shadow passes, particularly their transitions between frames and worker command lists. Source-byte verification and the synthetic depth oracle have not identified the defect. Any pass-specific diagnostic exclusion must be reported as isolation, not as a demonstrated fix or broader multicore solution. The public repository remains unchanged by this diagnostic round.

## D3D11 research follow-up

The [October 4 primary-source research](D3D11-MULTITHREADING-2026-10-04.md) distinguishes pipeline bindings from resource contents at command-list execution. It proposes actual-game capability reporting, a bounded pass/resource timeline, sequential versus concurrent multi-context controls, and a fresh-per-list upload control. These are diagnostic proposals; resource-version or concurrency faults have not been demonstrated. No DLL or game mode was changed for the research.

V10 live-test record (private artifact; excluded from publication) · [V10 implementation and live finding](RENDER-BRIDGE-010.md) · [Next-test handoff](../docs/NEXT-TEST.md)
