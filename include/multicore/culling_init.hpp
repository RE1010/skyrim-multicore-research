#pragma once
#include <cstddef>
#include <span>

namespace multicore::culling_init {
// Exact 1.7.104 constructor seam, not a general concurrent queue reset.
inline constexpr std::size_t node_offset=0x140, node_stride=16, node_count=4096;
inline constexpr std::size_t free_ring_offset=0x20150, ring_slots=8192;
inline constexpr std::size_t counters_offset=0x30150, extent=0x30160;
// Caller must own unpublished storage exclusively. This checks the empty-state
// precondition without changing invalid storage; it cannot prove ownership.
[[nodiscard]] bool seed_unpublished_free_ring(std::span<std::byte> object) noexcept;
}
