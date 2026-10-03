# RenderWorkerBridge Version 8 — Validated Test Package

This fixed package contains the tested DLL and an INI with a neutral example path. It was installed and tested in-game on October 3, 2026; draw replacement was disabled again afterward. Version 8 passed 17 tests and 164 full-image comparisons in the laboratory.

Two short game comparison pairs measured 41.56–42.69 presents/s with shared groups versus 33.14–33.91 with the copy control, an average **25.7% improvement within the worker path**. The original reference remained substantially faster at 120.51 presents/s. An overall Skyrim FPS gain has not been achieved. The tester reported that the earlier flicker was gone even during movement in the current test; other scenes have not yet been checked. [Live report](../../measurements/20261003-v8-live-comparison/analysis.json).

V8 shares unchanged binding groups between draws. Changed groups receive separate immutable versions. Constant contents and draw arguments remain stored per draw. Batch boundaries, GPU barriers, and replay order are preserved. New timing counters separate group publication and queue release; publication is already included in capture time.

Compatibility is restricted to the verified **SkyrimSE.exe 1.7.104.0** and **SKSE 2.3.1**. The executable hash is checked strictly. Package hashes and lab provenance are recorded in `manifest.json`. The INI starts with `Enabled=0` and four workers. Adjust its `OutputDirectory` to your own project directory before using it on another machine.

## Installation or reinstallation

Close Skyrim and the SKSE loader normally. From your project directory, select this fixed package with the installation script. Writing to the Steam folder may require administrator rights. The following paths are examples:

```powershell
Set-Location 'C:\SkyrimMulticoreResearch'
.\tools\Install-RenderWorkerBridge.ps1 -SourceDirectory 'C:\SkyrimMulticoreResearch\artifacts\render-worker-bridge-v8'
```

The script refuses to replace the DLL while the game is running, checks the Skyrim executable, and creates a backup and manifest under `measurements/bridge-installation-...` before replacement. Start Skyrim through the existing SKSE loader afterward. The SKSE log must show `RenderWorkerBridge.dll` loaded correctly as version `00000008`.

## Short comparison

Load the same view outside Whiterun and close the console and menus. Begin with the original rendering path active. Use ten-second intervals for initial observations:

```powershell
# Control: copy every group again for each draw.
.\tools\Set-RenderBridgeMode.ps1 -Mode parallel-owned-snapshots -DurationSeconds 10

# Once the original path is active again: share groups.
.\tools\Set-RenderBridgeMode.ps1 -Mode parallel -DurationSeconds 10

# Return to the original path at any time.
.\tools\Set-RenderBridgeMode.ps1 -Mode off
```

The control uses the same new group representation and recorder rules but copies all 13 groups for every draw. It is not identical to V7's flat snapshot representation. This comparison measures reuse within V8. An improvement over Skyrim itself must also be measured against the original path. `parallel-full-bindings` remains the separate setter-reduction control; `parallel-uncached` checks the getter cache.

The precise live comparison used `Start-Baseline.ps1`, PresentMon, and saved bridge state histories. Further comparisons should measure `off`, `parallel-owned-snapshots`, and `parallel` in the same fixed scene. Do not change NPC load during a capture. Assess visual behavior and FPS separately; individual screenshots can miss rapid flicker.

`manifest.json` records the historical package creation state and the published INI cleanup. `live-test.json` documents the subsequent game test. If you edit the INI, its hash will differ from the packaged version; the DLL hash remains independently verifiable.

[Laboratory report](../../measurements/20261003-143229-005-render-bridge-ownership-tests/analysis.json) · [Project summary](../../docs/PROJEKTSTAND-2026-10-03.md) · [Detailed technical notes, in German](../../research/RENDER-BRIDGE-001.md)
