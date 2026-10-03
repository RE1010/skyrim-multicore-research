#define NOMINMAX
#include <Windows.h>
#include "multicore/visibility.hpp"
#include "multicore/engine_shadow.hpp"
#include "runtime_evidence.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

int main() {
    try {
        // Execute only this verified leaf function in the test process, not in Skyrim.
        // It has one RIP-relative sign constant and no calls or external dependencies.
        auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!code) throw std::runtime_error("VirtualAlloc");
        std::memcpy(code,runtime_evidence::function_bytes.data(),runtime_evidence::function_bytes.size());
        const std::int32_t displacement=512-13;
        std::memcpy(code+9,&displacement,4);
        const std::uint32_t sign=0x80000000;
        std::memcpy(code+512,&sign,4);
        DWORD previous=0;
        if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous)) throw std::runtime_error("VirtualProtect");
        FlushInstructionCache(GetCurrentProcess(),code,4096);
        using Original=bool(*)(void*,const multicore::Sphere*);
        const auto original=reinterpret_cast<Original>(code);
        std::mt19937 random(17104);
        std::uniform_real_distribution<float> values(-100,100), radius(0,10);
        multicore::VisibilityWorkers pool(4,0);
        std::size_t checked=0;
        std::vector<multicore::EngineSnapshot> snapshots;
        for(unsigned configuration=0;configuration!=128;++configuration) {
            std::array<multicore::Plane,6> engine_planes{};
            std::array<multicore::Plane,6> adapted{};
            for(std::size_t i=0;i!=6;++i) {
                engine_planes[i]={values(random),values(random),values(random),values(random)};
                adapted[i]=engine_planes[i]; adapted[i].offset=-engine_planes[i].offset;
            }
            const auto mask=static_cast<std::uint8_t>(configuration%64);
            const bool optimize=configuration>=64;
            const multicore::Frustum f(adapted,mask);
            std::vector<multicore::Sphere> inputs(1027);
            for(auto& s:inputs) s={values(random),values(random),values(random),radius(random)};
            inputs[0]={0,0,0,0};
            inputs[1]={std::numeric_limits<float>::quiet_NaN(),0,0,1};
            inputs[2]={0,0,0,-1};
            std::vector<std::uint8_t> expected(inputs.size()),serial(inputs.size()),parallel(inputs.size());
            for(std::size_t i=0;i!=inputs.size();++i) {
                alignas(16) std::array<unsigned char,0x128> object{};
                std::memcpy(object.data()+0x3c,engine_planes.data(),96);
                const std::uint32_t full_mask=mask;
                std::memcpy(object.data()+0x9c,&full_mask,4);
                object[0x120]=static_cast<unsigned char>(optimize);
                const bool visible=original(object.data(),&inputs[i]);
                std::uint32_t after=0; std::memcpy(&after,object.data()+0x9c,4);
                expected[i]=static_cast<std::uint8_t>((visible?0x80:0)|after);
                multicore::EngineSnapshot snapshot{};
                snapshot.engine_planes=engine_planes; snapshot.bound=inputs[i];
                snapshot.before_mask=full_mask; snapshot.after_mask=after;
                snapshot.optimize=static_cast<std::uint8_t>(optimize);
                snapshot.engine_visible=static_cast<std::uint8_t>(visible);
                snapshots.push_back(snapshot);
            }
            const auto mode=optimize?multicore::TestMode::Skyrim17104PlaneOptimization:multicore::TestMode::Skyrim17104;
            multicore::classify_serial(inputs,f,serial,mode); pool.classify(inputs,f,parallel,mode);
            if(serial!=expected || parallel!=expected) throw std::runtime_error("Mismatch against actual engine machine code");
            checked+=inputs.size();
        }
        // The lower tangency boundary is excluded by this build, unlike the lab mode.
        std::array<multicore::Plane,6> plane{}; plane[0]={1,0,0,0};
        const multicore::Frustum boundary(plane,1);
        if(boundary.skyrim17104_test({-1,0,0,1},false)&0x80) throw std::runtime_error("Tangency");
        const auto stats=multicore::compare_engine_snapshots(snapshots,pool);
        if(stats.compared!=checked || stats.unsupported || stats.visibility_mismatches || stats.mask_mismatches
            || stats.serial_parallel_mismatches) throw std::runtime_error("Snapshot replay mismatch");
        snapshots.resize(3);
        snapshots[0].engine_visible^=1; snapshots[1].after_mask^=1; snapshots[2].before_mask=64;
        const auto errors=multicore::compare_engine_snapshots(snapshots,pool);
        if(errors.compared!=2 || errors.unsupported!=1 || errors.visibility_mismatches!=1 || errors.mask_mismatches!=1)
            throw std::runtime_error("Snapshot diagnostics missed injected faults");
        VirtualFree(code,0,MEM_RELEASE);
        std::cout<<"PASS: "<<checked<<" comparisons against original 1.7.104 machine code; visibility and resulting masks, serial and 4 workers\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
