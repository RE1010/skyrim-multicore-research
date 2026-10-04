# Skyrim Multicore Research

Research into reducing Skyrim Special Edition's main-thread work and creating CPU headroom for mods. **No general multicore switch or accepted game performance improvement has been achieved.**

## Current status — October 4, 2026

The source now includes **V12 diagnostics** and the **Study 013 reference without project diagnostic hooks**. Draw-level worker replay remains an unsuccessful experimental branch: it adds substantial overhead, and later live testing reproduced shadow flicker. The original rendering path is the current reference.

The latest investigation, **Study 014**, identifies concrete engine boundaries before DirectX submission. A repeated `BSCullingProcess` initialization region accounts for 7.1–7.8% of raw main-thread CPU samples in two reference windows. An earlier rendering corridor connects shader selection, constant preparation, uploads and geometry submission. Both require ownership and lifetime proof before any worker implementation. These sample shares are not promised FPS gains.

| Completed step | Finding |
| --- | --- |
| V8–V10 worker replay | Slower than original; shadow correctness remains unresolved |
| V11 draw ablation | Native command lists supported; omitting most indexed draws did not demonstrate a CPU benefit beyond reference drift |
| V12 context inventory | Roughly 10,000 mostly WRITE_DISCARD Maps/frame; profiling and observer work materially perturb measurements |
| Study 013 hook-free reference | Three validated captures; normal view approximately 3.47–3.53 ms/frame; CPU/GPU Busy close |
| Study 014 engine audit | Concrete culling-initialization and render-preparation candidates, with shared-state blockers identified |

The next step is the culling initialization/lifetime audit and an owned-data equivalence prototype if its assumptions hold. A representative CPU-heavy mod workload is required before claiming mod-capacity relief. A complete DirectX submission proxy is not implemented or justified by the current evidence.

## Read the evidence

- [Current progress and next step](docs/PROJEKTSTAND-2026-10-04.md)
- [Engine boundaries and dependency audit](research/ENGINE-BOUNDARIES-014.md) · [curated census](research/ENGINE-BOUNDARIES-LIVE-014.json)
- [Reference without project hooks](research/PROJECT-HOOK-FREE-013.md) · [curated results](research/PROJECT-HOOK-FREE-LIVE-013.json)
- [V12 context inventory](research/CONTEXT-INVENTORY-012.md) · [curated results](research/CONTEXT-INVENTORY-LIVE-012.json)
- [Submission headroom decision](research/SUBMISSION-HEADROOM-001.md)
- [DirectX 11 research](research/D3D11-MULTITHREADING-2026-10-04.md)
- [Repository contents and build prerequisites](docs/REPOSITORY.md)

## Validation and distribution

V12 previously passed 22 checks and 952 laboratory image comparisons, including shadow/lighting cases. Those synthetic checks do not establish correctness in Skyrim. The hook-free report adds a targeted positive/negative fixture check; this update does not claim a newly completed full 23-test native run.

The prebuilt [V8 package](artifacts/render-worker-bridge-v8/README.md) is preserved as a **historical experiment**, not the current source version or a recommended performance mod. No new prebuilt V12 DLL is published in this update. Sources require a locally verified, user-owned SkyrimSE.exe 1.7.104.0 and matching SKSE 2.3.1; generated executable evidence and third-party binaries are not distributed.

Current reports and community-facing documentation are in English. Earlier detailed research notes remain historical. Raw traces, full frame captures, savegames, screenshots, machine paths and private installation records are excluded from this update.
