#pragma once
#include "multicore/visibility.hpp"
#include <span>

namespace multicore {
// File and queue format contains numbers only; no engine addresses survive a call.
struct EngineSnapshot {
    std::array<Plane,6> engine_planes; // NiPlane.constant, not the lab offset convention
    Sphere bound;
    std::uint32_t before_mask, after_mask, thread_id;
    std::uint8_t optimize, engine_visible;
    std::uint16_t reserved = 0;
};
static_assert(sizeof(EngineSnapshot)==128);
struct SnapshotHeader {
    char magic[8];
    std::uint32_t record_size, runtime_version;
    char exe_sha256[64];
};
static_assert(sizeof(SnapshotHeader)==80);
struct ShadowStats {
    std::size_t compared=0, unsupported=0, visibility_mismatches=0, mask_mismatches=0,
        serial_parallel_mismatches=0;
};
ShadowStats compare_engine_snapshots(std::span<const EngineSnapshot> snapshots, VisibilityWorkers& workers);
}
