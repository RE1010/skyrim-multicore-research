# Project Progress and Results — October 4, 2026

## Result so far

We have working, version-guarded experimental tools and a substantially better picture of Skyrim's main-thread workload. We do not yet have an accepted multicore performance mod. The goal remains CPU headroom for modded scenes, with correct rendering and bounded latency.

## Implemented and tested

- Owned-data visibility/render laboratories, SKSE version guards, bounded live controls and original-path fallback.
- V8–V10 deferred worker experiments, ordered replay, owned uploads and state-isolation variants. Later game shadow flicker remains unresolved; laboratory images did not guarantee live correctness.
- V11 native device capability checks and bounded draw omission. The tested device supports native command lists. The draw ablation did not demonstrate CPU savings beyond drift.
- V12 original-clean forwarding and typed inventory of 149 SDK context methods. Fourteen short captures showed significant observer/timer cost and approximately ten thousand mostly WRITE_DISCARD Maps/frame.
- Study 013: three hook-free reference captures, external read-only call/context verification, unchanged uncapping, zero ETW losses and exact-window main-thread attribution.
- Study 014: verified PE unwind-family census plus static inspection of engine initialization, shader lookup, constant preparation, shared render caches and TLS dependencies.

## Latest measured reference

Three five-second normal gate/wall captures average 3.526 / 3.473 / 3.471 ms/frame. Mean CPU Busy is 3.445 / 3.394 / 3.393 ms; GPU Busy is 3.197 / 3.202 / 3.211 ms. These are normal-scene observations, not a heavy-mod workload or a cross-version speedup.

The first and third exact frame windows contain 4,474 / 4,503 raw main-thread samples. Skyrim accounts for 60.06% / 58.07%, D3D11 for 16.23% / 17.17%, NVIDIA for 7.55% / 7.44%. These are disjoint instruction-location samples, not movable frame-time percentages. About 9.5% of wall time is blocked, but its API/job cause remains unknown.

## Concrete next implementation candidate

First audit repeated culling-object initialization at RVA `0xFED6E0`. Its class association is established by RTTI; two inspected callers construct large stack-resident objects. Determine complete initialization/destruction semantics and whether helpers publish those objects. If isolation is established, compare an owned scratch implementation against the original initial state before any live substitution.

The larger alternative is CPU-only material/constant preparation inside the rendering corridor beginning at RVA `0x1560340`. Shared cache mutations, engine TLS, mapped pointers and shader binding currently prevent moving the entire routine to a worker. Split immutable inputs and worker-owned outputs before considering parallel work.

Neither target is implemented as a live optimization. The 25%/40% full-proxy decision gates remain unclassified. See [Study 014](../research/ENGINE-BOUNDARIES-014.md) for exact ranges, counts, dependencies and limits.

## Validation and publication

V12's historical validation is 22 checks and 952 full images. The reference analyzer has additional targeted contamination-rejection tests. Synthetic rendering tests and tester observations do not prove correctness across Skyrim passes, weather, movement, save loads or mods.

This repository update includes current source, tools, English reports and explicitly curated results. Raw traces and personal/machine paths remain private. The packaged V8 binary remains historical; no new binary release is implied. [Setup](REPOSITORY.md).
