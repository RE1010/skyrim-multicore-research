# Next Step: Engine Initialization and Ownership

No new broad FPS comparison or full submission proxy is scheduled from the current normal-scene result.

1. Audit the complete initialization/destruction and base-helper paths associated with `BSCullingProcess` at RVA `0xFED6E0` in the exact verified runtime.
2. Determine queue/object escape, capacity invariants, node layout and counter initial state. Stack-resident callers are useful evidence, not an ownership guarantee.
3. If those assumptions hold, create a private owned-data equivalence prototype; compare initial bytes, counter/queue behavior and failure/cleanup behavior before a bounded live substitution.
4. For a subsequent threading experiment, retain immutable inputs, worker-owned outputs, original-order commit and fallback. The larger render-preparation corridor has shared globals, engine TLS and Map/binding dependencies that must be separated.
5. Evaluate any accepted implementation in a representative CPU-heavy mod scene, controlling observer cost and checking rendering during motion. No mod-capacity improvement is established yet.

[Engine audit](../research/ENGINE-BOUNDARIES-014.md) · [Reference](../research/PROJECT-HOOK-FREE-013.md) · [Progress](PROJEKTSTAND-2026-10-04.md)
