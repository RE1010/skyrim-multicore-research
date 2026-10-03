#include "multicore/engine_shadow.hpp"
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace multicore {
ShadowStats compare_engine_snapshots(std::span<const EngineSnapshot> snapshots, VisibilityWorkers& workers) {
    ShadowStats stats;
    // Group by the actual numeric plane state, never by object pointer or frame identity.
    // This is diagnostic replay only; it cannot supply delayed decisions to the engine.
    using Key=std::array<unsigned char,98>;
    std::map<Key,std::vector<std::size_t>> groups;
    for(std::size_t i=0;i!=snapshots.size();++i) {
        const auto& s=snapshots[i];
        if((s.before_mask|s.after_mask)&~0x3fu || s.optimize>1 || s.engine_visible>1) {++stats.unsupported;continue;}
        Key key{}; std::memcpy(key.data(),s.engine_planes.data(),96);
        key[96]=static_cast<unsigned char>(s.before_mask); key[97]=s.optimize;
        groups[key].push_back(i);
    }
    for(const auto& [key,indices]:groups) {
        auto planes=snapshots[indices.front()].engine_planes;
        for(auto& p:planes) p.offset=-p.offset;
        try {
            const Frustum frustum(planes,key[96]);
            const auto mode=key[97]?TestMode::Skyrim17104PlaneOptimization:TestMode::Skyrim17104;
            std::vector<Sphere> input; input.reserve(indices.size());
            for(const auto index:indices) input.push_back(snapshots[index].bound);
            std::vector<std::uint8_t> serial(input.size()),parallel(input.size());
            classify_serial(input,frustum,serial,mode);
            workers.classify(input,frustum,parallel,mode);
            for(std::size_t i=0;i!=input.size();++i) {
                const auto& expected=snapshots[indices[i]];
                ++stats.compared;
                if(serial[i]!=parallel[i]) ++stats.serial_parallel_mismatches;
                if(bool(parallel[i]&0x80)!=bool(expected.engine_visible)) ++stats.visibility_mismatches;
                if(static_cast<std::uint32_t>(parallel[i]&0x3f)!=expected.after_mask) ++stats.mask_mismatches;
            }
        } catch(const std::invalid_argument&) {stats.unsupported+=indices.size();}
    }
    return stats;
}
}
