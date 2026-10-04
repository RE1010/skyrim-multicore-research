# Study 015: Owned Culling-Initialization Prototype

October 4, 2026. This is an implemented and tested engine-work prototype, not a new Skyrim performance release. No DLL is installed and no game memory is patched by this step.

## Concrete result

The repeated initialization candidate from [Study 014](ENGINE-BOUNDARIES-014.md) now has a verified narrow algorithmic replacement. In the final owned-memory test, the original machine loop takes **79.556 microseconds** and guarded bulk initialization takes **4.588 microseconds**, median across ten alternating pairs. Both measurements include resetting the same empty-ring bytes. The measured ratio is approximately **17.3× for this isolated seam**, not Skyrim FPS or complete-constructor speed. Earlier checks yielded approximately 19–20×; that variation is retained as a microbenchmark limitation, not selected as the final result.

The native test passes **256 serial and 256 independent-worker whole-memory comparisons** against the exact extracted original instructions, plus **11 invalid-state rejection cases**. Four workers use separate owned storage. This establishes a reentrant owned-data algorithm; it does not move any live Skyrim object to a worker. [Curated results](CULLING-INITIALIZATION-LAB-015.json).

## Ownership and lifecycle audit

The independent audit inspects the complete pre-loop call closure and three observed caller prefixes. The base constructor at `0xF04740` writes local fields and invokes two scalar/subobject initialization leaves. Leaves at `0xCE0CB0` and `0xCE0970` only zero local fields. The two ring clears resolve to the imported CRT memset. No pre-loop object publication is observed in this closure.

The three verified caller returns are `0x15394DA`, `0x516992` and `0x43F32C`. Each passes a fresh stack object with `RDX=0`, and no earlier object-address publication is found in its inspected prefix. All three later call the derived destructor at `0xFED8E0`; it cleans owned members and containers and resets the base vtable. The embedded node/ring storage is not separately freed.

This proof is restricted to those construction paths. Traversal can subsequently pass the culling-process pointer to external virtual callbacks. It does not justify reusing whole objects, resetting live queues, removing traversal locks, or moving an entire culling pass between threads. The highest observed constructor write ends at offset `0x301F8`; exact source-level size/padding is not recovered.

## Replacement seam and invariant

Original instruction interval: **`0xFED7B0–0xFED844`** in the exact verified 1.7.104 executable. It inserts 4,096 node pointers into a freshly zeroed 8,192-slot free ring using the general concurrent enqueue machinery.

| State | Verified offset / value after the loop |
| --- | --- |
| Node base / stride | Object `+0x140`, 16 bytes per node |
| Free ring | Object `+0x20150`, 8,192 pointer slots |
| First 4,096 ring entries | `object + 0x140 + index * 16` |
| Remaining ring entries | Zero |
| Read counters | Object `+0x30150` / `+0x30154`: zero |
| Committed / reserved write counters | Object `+0x30158` / `+0x3015C`: 4,096 |

[The implementation](../src/culling_init.cpp) accepts an exclusively owned unpublished byte view. It checks minimum extent, alignment and every free-ring/counter byte for the fresh empty state before writing anything. Nonempty, misaligned or short inputs are rejected. Ownership itself remains a caller obligation; an empty queue is not proof of exclusive ownership.

The implementation writes the same pointer sequence and final counters directly. It preserves node payloads, the active ring, prefix fields, padding, tail fields and surrounding bytes. It performs no allocation, D3D calls, shared-state mutation or engine callbacks.

## Why the comparison is stronger than a copied model

[The evidence generator](../tools/Generate-CullingInitEvidence.py) first checks the exact executable SHA256, PE constructor interval, RTTI class and original loop shape. It generates a private header from the user's local executable. The test executes the extracted original loop in the test process, with its single conditional wait-import reference redirected to an abort sentinel. That wait must never be reached for a valid fresh ring.

The Win64 wrapper saves/restores the touched nonvolatile registers and supplies call shadow space. It registers its own dynamic unwind table; four native lookup/unwind checks recover the expected return address, stack pointer and saved registers from different body positions. This validates the standalone wrapper, not a live constructor splice. Internal branch displacements remain unchanged. Comparisons run at different buffer offsets with randomized untouched bytes, restore the same buffer address for the replacement, and compare the entire surrounding allocation. Tests cover dirty used/unused slots, all four counters, insufficient extent, misalignment, empty input and exact minimum extent.

Only the new targeted CTest check is claimed for this change. The earlier 22-check/952-image V12 validation remains historical. The CMake suite now registers additional reference and culling tests, but a new full-suite rendering result is not claimed.

## Next integration gate

Do not splice this function into a running constructor yet. A startup-only integration must preserve the original constructor/destructor lifecycle, restrict activation to the three verified callers, validate current-thread stack bounds and exact code bytes, and retain the original path on every failed precondition.

For a mid-function splice, the register poststate, return address, stack alignment, exception/unwind metadata and original fallback relocation require separate native validation. In particular, copying code into an unregistered trampoline would disrupt Windows stack walking even if simple byte comparisons passed. Those integration requirements are not completed here. Microsoft documents the requirement for dynamic function tables and unwind information for generated nonleaf code: [RtlAddFunctionTable](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable), [x64 exception handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170).

This step removes the need to use thousands of atomic enqueue operations for a private initialization task. Whether that yields useful CPU relief in Skyrim and modded workloads remains a live-test question. After reducing the seam to a few microseconds, adding one worker dispatch per object may be counterproductive; no such scheduling speedup is claimed.
