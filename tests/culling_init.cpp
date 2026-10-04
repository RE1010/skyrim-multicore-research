#define NOMINMAX
#include <Windows.h>
#include "multicore/culling_init.hpp"
#include "culling_init_evidence.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ci=multicore::culling_init;
[[noreturn]] void unexpected_wait(DWORD) {std::abort();}
using Loop=void(*)(void*);

class OriginalLoop {
    unsigned char* memory{};
    RUNTIME_FUNCTION function{};
    bool registered=false;
public:
    OriginalLoop() {
        memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!memory) throw std::runtime_error("VirtualAlloc");
        // Preserve all touched Win64 nonvolatile registers and provide shadow
        // space. Original branches stay within the unchanged loop bytes.
        constexpr unsigned char prefix[]{0x53,0x55,0x56,0x41,0x56,0x48,0x83,0xec,0x28,
          0x48,0x8d,0xb1,0x40,0x01,0,0,0x48,0x8d,0x99,0x50,0x01,0x02,0,
          0xbd,0,0x10,0,0,0x45,0x33,0xf6};
        constexpr unsigned char suffix[]{0x48,0x83,0xc4,0x28,0x41,0x5e,0x5e,0x5d,0x5b,0xc3};
        std::memcpy(memory,prefix,sizeof(prefix));
        auto* loop=memory+sizeof(prefix);
        std::memcpy(loop,culling_init_evidence::loop_bytes.data(),culling_init_evidence::loop_bytes.size());
        const std::int32_t import_displacement=512-static_cast<std::int32_t>(sizeof(prefix)+32);
        std::memcpy(loop+28,&import_displacement,4);
        const auto wait=&unexpected_wait;
        std::memcpy(memory+512,&wait,sizeof(wait));
        std::memcpy(loop+culling_init_evidence::loop_bytes.size(),suffix,sizeof(suffix));
        // The generated test wrapper is nonleaf. Supply its own unwind data;
        // this is not the metadata required for a live constructor splice.
        constexpr unsigned char unwind[]{1,9,5,0,9,0x42,5,0xe0,3,0x60,2,0x50,1,0x30,0,0};
        std::memcpy(memory+528,unwind,sizeof(unwind));
        function={0,static_cast<DWORD>(sizeof(prefix)+culling_init_evidence::loop_bytes.size()+sizeof(suffix)),528};
        DWORD previous=0;
        if(!VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&previous) || !FlushInstructionCache(GetCurrentProcess(),memory,4096))
            throw std::runtime_error("Executable loop protection");
        if(!RtlAddFunctionTable(&function,1,reinterpret_cast<DWORD64>(memory)))
            throw std::runtime_error("Generated wrapper unwind registration");
        registered=true;
        for(const auto at:std::array<unsigned,4>{30,74,141,172}) {
            alignas(16) std::array<std::uint64_t,32> stack{};
            stack[5]=0x14abcd;stack[6]=0x51abcd;stack[7]=0xbfabcd;stack[8]=0xb3abcd;stack[9]=0x180abcdef;
            CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
            context.Rsp=reinterpret_cast<DWORD64>(stack.data());context.Rip=reinterpret_cast<DWORD64>(memory+at);
            DWORD64 lookup_base=0,frame=0;void* handler=nullptr;
            const auto* entry=RtlLookupFunctionEntry(context.Rip,&lookup_base,nullptr);
            if(!entry || lookup_base!=reinterpret_cast<DWORD64>(memory))throw std::runtime_error("Wrapper unwind lookup");
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,lookup_base,context.Rip,const_cast<PRUNTIME_FUNCTION>(entry),&context,&handler,&frame,nullptr);
            if(context.Rsp!=reinterpret_cast<DWORD64>(stack.data()+10) || context.Rip!=stack[9]
                || context.R14!=stack[5] || context.Rsi!=stack[6] || context.Rbp!=stack[7] || context.Rbx!=stack[8])
                throw std::runtime_error("Wrapper nonvolatile/stack unwind mismatch");
        }
    }
    ~OriginalLoop(){if(registered)RtlDeleteFunctionTable(&function);if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    void operator()(std::span<std::byte> object) const {reinterpret_cast<Loop>(memory)(object.data());}
};

void reset(std::span<std::byte> object) {
    std::fill(object.begin()+ci::free_ring_offset,object.begin()+ci::extent,std::byte{});
}
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}

int main() {
    try {
        OriginalLoop original;
        std::mt19937 random(17104);
        std::vector<std::uint64_t> storage((ci::extent+512)/8);
        auto all=std::as_writable_bytes(std::span(storage));
        unsigned comparisons=0,rejections=0;
        for(unsigned iteration=0;iteration<256;++iteration) {
            for(auto& value:storage)value=(static_cast<std::uint64_t>(random())<<32)|random();
            auto object=all.subspan((iteration%16)*8);
            reset(object);
            const std::vector<std::byte> before(all.begin(),all.end());
            original(object);
            const std::vector<std::byte> expected(all.begin(),all.end());
            std::copy(before.begin(),before.end(),all.begin());
            check(ci::seed_unpublished_free_ring(object),"Valid initialized ring rejected");
            check(std::equal(expected.begin(),expected.end(),all.begin()),"Original-machine-loop byte mismatch");
            ++comparisons;
        }
        // Independent worker-owned storage: no game objects, resource handles,
        // queues shared between workers, or publication during initialization.
        std::array<std::exception_ptr,4> failures{};
        std::array<unsigned,4> worker_comparisons{};
        {
            std::vector<std::jthread> workers;
            for(unsigned index=0;index<4;++index)workers.emplace_back([&,index]{
                try {
                    std::mt19937 worker_random(17104+index);
                    std::vector<std::uint64_t> worker_storage((ci::extent+256)/8);
                    auto bytes=std::as_writable_bytes(std::span(worker_storage));
                    for(unsigned iteration=0;iteration<64;++iteration) {
                        for(auto& value:worker_storage)value=(static_cast<std::uint64_t>(worker_random())<<32)|worker_random();
                        auto object=bytes.subspan((iteration%16)*8);
                        reset(object);
                        const std::vector<std::byte> before(bytes.begin(),bytes.end());
                        original(object);
                        const std::vector<std::byte> expected(bytes.begin(),bytes.end());
                        std::copy(before.begin(),before.end(),bytes.begin());
                        check(ci::seed_unpublished_free_ring(object),"Worker-owned ring rejected");
                        check(std::equal(expected.begin(),expected.end(),bytes.begin()),"Worker original-machine mismatch");
                        ++worker_comparisons[index];
                    }
                }catch(...){failures[index]=std::current_exception();}
            });
        }
        unsigned parallel_comparisons=0;
        for(unsigned index=0;index<4;++index) {
            if(failures[index])std::rethrow_exception(failures[index]);
            parallel_comparisons+=worker_comparisons[index];
        }
        // Reject dirty slots throughout the ring, and every counter, without
        // changing any byte. The live original fallback must remain possible.
        for(const auto at:std::vector<std::size_t>{ci::free_ring_offset,ci::free_ring_offset+4095*8,
            ci::free_ring_offset+4096*8,ci::counters_offset-8,ci::counters_offset,
            ci::counters_offset+4,ci::counters_offset+8,ci::counters_offset+12}) {
            reset(all);all[at]=std::byte{1};
            const std::vector<std::byte> before(all.begin(),all.end());
            check(!ci::seed_unpublished_free_ring(all),"Dirty ring accepted");
            check(std::equal(before.begin(),before.end(),all.begin()),"Rejected ring mutated");
            ++rejections;
        }
        reset(all);
        check(!ci::seed_unpublished_free_ring(all.first(ci::extent-1)),"Short buffer accepted");
        check(!ci::seed_unpublished_free_ring(all.subspan(1)),"Unaligned buffer accepted");
        check(!ci::seed_unpublished_free_ring({}),"Empty buffer accepted");
        reset(all);
        check(ci::seed_unpublished_free_ring(all.first(ci::extent)),"Exact-size ring rejected");
        // Compare steady-state seam throughput, including empty-state checking.
        // Both sides reset the same bytes; this is not a Skyrim frame benchmark.
        std::vector<double> engine,bulk;
        constexpr unsigned repeats=2000;
        for(unsigned pair=0;pair<10;++pair) {
            auto timing=[&](bool fast){
                const auto begin=std::chrono::steady_clock::now();
                for(unsigned i=0;i<repeats;++i) {reset(all);if(fast)check(ci::seed_unpublished_free_ring(all),"Bench precondition");else original(all);}
                return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/repeats;
            };
            if(pair%2) {bulk.push_back(timing(true));engine.push_back(timing(false));}
            else {engine.push_back(timing(false));bulk.push_back(timing(true));}
        }
        std::sort(engine.begin(),engine.end());std::sort(bulk.begin(),bulk.end());
        const auto engine_us=(engine[4]+engine[5])/2,bulk_us=(bulk[4]+bulk[5])/2;
        std::cout<<std::setprecision(9)<<"{\"originalMachineComparisons\":"<<comparisons
          <<",\"workerOwnedOriginalMachineComparisons\":"<<parallel_comparisons<<",\"independentWorkers\":4"
          <<",\"standaloneWrapperUnwindChecks\":4"
          <<",\"invalidStateRejections\":"<<rejections+3<<",\"includesRingReset\":true,\"pairs\":10"
          <<",\"originalMedianUs\":"<<engine_us<<",\"guardedBulkMedianUs\":"<<bulk_us
          <<",\"seamSpeedRatio\":"<<engine_us/bulk_us<<",\"gamePerformanceMeasured\":false}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
