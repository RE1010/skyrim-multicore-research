# Next Step: Startup Integration of the Verified Initialization Seam

Study 015 passes 512 original-machine whole-memory comparisons, 11 invalid-state rejections and four native standalone-wrapper unwind checks. Its isolated seam is approximately 17.3× faster in the final ten-pair run. No live game optimization or mod-capacity gain is established.

1. Preserve the complete constructor/destructor lifecycle and untouched bytes; do not reuse objects or move traversal to workers.
2. Restrict fast initialization to the three audited fresh stack callers. Check exact current caller/helper/constructor bytes and current-thread stack bounds; reject conflicts.
3. Validate original continuation register state, return address and stack alignment. The standalone wrapper's unwind proof is not proof for the engine constructor frame.
4. Retain the original loop/fallback with correct relocation and native exception/unwind behavior. No live splice is installed before these checks pass.
5. Then perform bounded game correctness and CPU measurements, followed by a representative CPU-heavy mod workload. Do not infer game speed from the laboratory ratio.

[Prototype](../research/CULLING-INITIALIZATION-015.md) · [Curated result](../research/CULLING-INITIALIZATION-LAB-015.json) · [Original boundary audit](../research/ENGINE-BOUNDARIES-014.md)
