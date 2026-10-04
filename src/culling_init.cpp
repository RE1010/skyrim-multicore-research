#include "multicore/culling_init.hpp"
#include <cstdint>
#include <cstring>

namespace multicore::culling_init {
bool seed_unpublished_free_ring(std::span<std::byte> object) noexcept {
    static_assert(sizeof(std::uintptr_t)==8);
    if(object.size()<extent || reinterpret_cast<std::uintptr_t>(object.data())%8) return false;
    // Fail closed if this is not the freshly zeroed constructor ring.
    for(std::size_t at=free_ring_offset;at<extent;at+=8) {
        std::uint64_t value=0;
        std::memcpy(&value,object.data()+at,sizeof(value));
        if(value) return false;
    }
    for(std::size_t index=0;index<node_count;++index) {
        const auto node=reinterpret_cast<std::uintptr_t>(object.data()+node_offset+index*node_stride);
        std::memcpy(object.data()+free_ring_offset+index*sizeof(node),&node,sizeof(node));
    }
    const std::uint32_t written=static_cast<std::uint32_t>(node_count);
    std::memcpy(object.data()+counters_offset+8,&written,sizeof(written));
    std::memcpy(object.data()+counters_offset+12,&written,sizeof(written));
    return true;
}
}
