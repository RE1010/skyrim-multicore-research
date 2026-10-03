# Repository Contents and Setup

This repository contains the source code, tests, tools, research reports, and validated RenderWorkerBridge V8 test package. The current status is described in [Project Progress and Results](PROJEKTSTAND-2026-10-03.md).

V8 replaces eligible D3D11 draws with ordered recording on four workers. Two short game comparison pairs show a higher present rate than the internal V8 copy control. The original renderer remains substantially faster. This is a research prototype; an overall Skyrim FPS improvement has not been achieved. Replacement starts disabled.

## Included measurement evidence

Compact reports for the current V8 laboratory and game comparisons are under `measurements/`. The [live report](../measurements/20261003-v8-live-comparison/analysis.json) records provenance, rates, phase budgets, and limitations. Its `analyze.py` script reproduces the report from the five saved capture reports, state histories, trace statistics, and saved final state. Some earlier notes refer to captures that remain local.

Large raw ETL traces, complete frame CSV files, screenshots, saved games, installation backups, local builds, and downloaded third-party tools are excluded. The repository does not distribute a Skyrim installation or SKSE. Version-specific executable evidence is generated from your own verified game installation when building.

## Local prerequisites

- Windows, Visual Studio C++ Build Tools, Windows SDK with D3D11 headers, CMake, Ninja, and Python 3.
- Your own verified SkyrimSE.exe 1.7.104.0 matching the documented SHA256; SKSE 2.3.1 for game testing.
- Adjust paths in `tools/Build-Multicore.cmd`, CMake settings, and INI files for your machine. The build script retains the original development toolchain assumptions, with neutral example paths in this published copy.

The [fixed V8 package](../artifacts/render-worker-bridge-v8/README.md) contains the unchanged tested DLL and an INI with a neutral example path. Adjust `OutputDirectory` before using it on another machine. Editing the INI changes its hash relative to the published package manifest; the DLL hash remains independently verifiable. The manifest records package creation and INI cleanup; `live-test.json` records the subsequent game test.

CTest can run the laboratory checks in the build directory without a new game capture. V8 passed 17/17 tests and 164 full-image comparisons; the corresponding reports are included. The tester's report that flicker was gone during movement is documented as an observation from the current game test, not an automatic correctness check across all scenes.

The README, project summary, repository guide, and V8 instructions are in English. Earlier detailed research notes remain in German.
