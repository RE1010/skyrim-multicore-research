# Repository Contents and Setup

Current source is the V12 diagnostic research branch, plus the read-only Study 013 reference tools. The prebuilt V8 package remains a historical experiment. There is no accepted performance release. [Current progress](PROJEKTSTAND-2026-10-04.md).

## Included and excluded

Source, tests, local build/recording tools, English current reports and curated numerical results are included. Study 014 reports engine RVAs and dependencies rather than distributing game code or disassembly. Historical V8 compact evidence remains available.

Raw ETL traces, complete frame CSVs, screenshots, savegames, private installation backups, local paths, builds and third-party executables are excluded. The repository does not distribute Skyrim or SKSE. Executable evidence is generated locally from the user's verified installation.

## Prerequisites

- Windows, Visual Studio C++ tools, Windows SDK including D3D11 headers, CMake, Ninja and Python 3 discoverable by CMake.
- A user-owned SkyrimSE.exe 1.7.104.0 matching SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`, and matching SKSE 2.3.1 for game testing.
- Review the toolchain/version assumptions in `tools/Build-Multicore.cmd`. Set `CONTEXT_SDK`, `SKYRIM_EXE` and optionally `Python3_EXECUTABLE` for your machine when configuring CMake. Generated INI output paths depend on the local source directory.

Use a separate experimental game configuration. Replacement defaults to disabled. Native tests can run through CTest without a game capture. V12's historical 22 checks and 952 images remain separate from the later hook-free report fixture check; there is no claimed new full 23-test native result.

`Set-ProjectHookReference.ps1` only handles the exact known diagnostic DLL hashes in its guard. It refuses unrelated or changed binaries and requires Skyrim to be closed for disable/restore. It preserves the existing uncapping files. `Get-ProjectHookReference.ps1` is an external read-only verifier, not an injector or live hook-detach tool.

The fixed V8 DLL and its sanitized INI are preserved unchanged. They do not correspond to the latest source and are not recommended as a performance mod. Later live shadow failures prevent treating the earlier V8 visual observation as general correctness. No new V12 binary is included.
