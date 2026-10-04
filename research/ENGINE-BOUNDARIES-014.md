# Study 014: Concrete Engine Boundaries Before D3D11

October 4, 2026. Offline analysis of the completed Study 013 traces and the verified local executable. No new game capture, patch, worker or performance gain is introduced.

## What changed

The investigation now identifies specific engine routines and their state dependencies rather than treating all Skyrim CPU samples as one bucket. An independent census uses the exact first and third reference frame windows from [Study 013](PROJECT-HOOK-FREE-013.md). Static inspection uses the executable with SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F` and preferred image base `0x140000000`. Addresses below are RVAs, not live process addresses.

PE exception-table ranges identify physical code intervals. Chained unwind entries are grouped into a canonical unwind family; this does not recover original source functions or names. Gaps and leaf functions without a matching range remain unassigned. Of 2,687 / 2,615 raw Skyrim leaf samples, 2,384 / 2,319 map to these intervals; 303 / 296 remain unassigned.

| Verified unwind-family start | Reference 1 exclusive samples | Reference 3 exclusive samples | Observed responsibility / dependency |
| --- | ---: | ---: | --- |
| `0xFED6E0` | 348 | 319 | Constructor-like initialization associated by RTTI with `BSCullingProcess`; large queue initialization and atomic operations |
| `0x1579120` | 196 | 184 | Two chained-table searches followed by shader binding; not a pure lookup routine |
| `0x1560340` | 177 | 187 | Earlier joint preparation/submission corridor; shared shader/material caches and engine TLS |
| `0x1549550` | 132 | 129 | Constant preparation with WRITE_DISCARD Maps, writes to mapped memory and shared globals |
| `0xF049C0` | 63 | 66 | Additional culling-related family; detailed ownership not yet established |
| `0x100BCD0` | 53 | 65 | Geometry/context submission wrapper |
| `0x155F050` | 40 | 50 | Geometry path reaching the preceding wrapper |
| `0x1548820` | 43 | 40 | Additional mapped-upload / binding family |

Denominators are **4,474 / 4,503 raw main-thread CPU samples**, not frame wall time. Exclusive rows are disjoint attribution; inclusive stack counts below overlap and must not be summed. Symbolized-stack denominators remain separate. [Curated census](ENGINE-BOUNDARIES-LIVE-014.json).

## Candidate A: repeated culling-object initialization

The physical interval `0xFED6E0–0xFED8CB` accounts for **7.78% / 7.08% of all raw main-thread samples** in the two windows. It writes vtable RVA `0x1A6EF90`. The vtable's complete-object locator points to a type descriptor containing `.?AVBSCullingProcess@@`, establishing the class association. Calling the observed code constructor-like describes its initialization behavior; it is not a recovered source symbol.

The code initializes large queue storage, including 64 KiB zeroing and a 4,096-iteration loop. The loop reserves queue positions and advances counters with locked compare/exchange operations, writes node pointers and advances nodes by 16 bytes. Hot instruction addresses are inside this loop. Atomic instruction samples alone do not prove contention or a measured waiting duration.

Captured nearest engine parents are three distinct families:

| Parent family | Return after initialization call | Samples, reference 1 / 3 |
| --- | --- | ---: |
| `0x1538FA0` | `0x15394DA` | 126 / 104 |
| `0x516960` | `0x516992` | 119 / 101 |
| `0x43F2B0` | `0x43F32C` | 103 / 114 |

Static inspection confirms the latter two callers allocate approximately `0x30230` / `0x30260` bytes of stack and pass addresses of stack storage to initialization. They subsequently pass the initialized object to other engine helpers. This makes repeated temporary-object initialization a concrete hypothesis, including work outside the previously hooked render scope.

**Next proof:** trace the complete constructor/base-helper and destruction paths, determine whether the object or its queues can escape or publish work during initialization, and check whether capacity and initial node/counter state are invariant. Stack allocation alone does not establish exclusive ownership. Do not remove atomic operations, reuse an object, or move these callers to another thread without that proof. A scratch-owned initialization-equivalence test is a possible next prototype; it would establish semantics, not a game-wide multicore gain.

## Candidate B: earlier render preparation boundary

`0x1560340–0x15606A1` is usually reached through return `0x15601D4` in family `0x155FF40`. Its captured descendants connect shader selection, constant preparation, Map/Unmap, constant-buffer binding, vertex/index binding and DrawIndexed. Its **2,086 / 2,143 inclusive raw-stack samples** establish a broad dependency corridor; they are not a transferable CPU budget.

Static inspection finds writes to shared shader/material cache globals at RVAs `0x36939D8`, `0x36939D4`, `0x36939E0` and technique state at `0x20D696C`. The routine also reads the engine TLS index at `0x369A278`, accesses the thread's block through `gs:[0x58]`, temporarily changes the field at offset `0x768`, then restores it. Copying the function to a worker would not preserve these engine assumptions.

Within this corridor, family `0x1549550` Maps through the original context at `0x3331F30`, uses WRITE_DISCARD, stores mapped pointers in shared buffer records and writes calculated constants directly into returned memory. Family `0x1579120` searches two tables, but successful lookups continue into VS/PS shader setters. Neither entire routine is currently a safe independent task.

**Next proof:** isolate CPU-only constant/material calculations from Map, binding and shared-cache mutation. Inputs must be immutable snapshots or retained objects with a validated lifetime; outputs must be worker-owned byte storage. The owner thread can then apply results in the original order. Resource renaming, camera/environment changes, shader reloads, TLS-dependent behavior and fallback must be explicit. This is an earlier potential split than intercepting DrawIndexed, not an implemented split.

## Decision

Prioritize Candidate A's lifetime and initialization audit because it has a narrow, stable, directly sampled cost and does not require replacing the entire DirectX context. Keep Candidate B as the larger preparation-boundary investigation. Before claiming relief for mods, validate any successful prototype in a representative CPU-heavy mod workload, with visual correctness and observer cost controlled.

No full submission proxy, accepted worker-render replacement, numerical movable-work fraction, or 25%/40% architecture gate is established. The old replay shadow defect remains unresolved. The value of this step is two concrete targets and specific blockers, not another claimed FPS improvement.
