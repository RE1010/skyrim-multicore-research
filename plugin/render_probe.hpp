#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace render_probe {
using Original=void(*)(void*,std::uint32_t,std::uint32_t,std::uint32_t);
inline std::uint64_t ticks() noexcept {LARGE_INTEGER t{};QueryPerformanceCounter(&t);return static_cast<std::uint64_t>(t.QuadPart);}
inline std::uint64_t token(std::uintptr_t value,std::uint64_t salt) noexcept {
    if(!value) return 0;
    auto x=static_cast<std::uint64_t>(value)^salt;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
struct Snapshot {std::uintptr_t shader=0,material=0;std::uint32_t technique=0;};
struct Record {
    std::uint64_t sequence=0,begin=0,end=0,packet=0,shader=0,property=0,geometry=0;
    std::uint64_t shader_before=0,shader_after=0,material_before=0,material_after=0;
    std::uint32_t thread_id=0,technique=0,arg3=0,arg4=0,flags=0,technique_before=0,technique_after=0;
};
struct alignas(64) Counter {
    std::atomic<std::uint32_t> id{0};
    std::uint32_t reserved=0;
    std::atomic<std::uint64_t> attempted{0},completed{0},aborted{0};
    std::array<unsigned char,32> padding{};
};
static_assert(sizeof(Counter)==64);
struct Probe {
    Original original;
    const std::uint32_t every;
    const std::uint64_t salt=ticks();
    const std::uintptr_t* shader_slot=nullptr;
    const std::uintptr_t* material_slot=nullptr;
    const std::uint32_t* technique_slot=nullptr;
    std::array<Counter,128> counters{};
    std::vector<Record> records;
    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> active{0},reserved{0},completed_samples{0},overflow_threads{0},dropped{0};
    explicit Probe(Original fn,std::uint32_t sampling=16,std::size_t capacity=262144):original(fn),every(sampling),records(capacity) {
        if(!fn || !sampling || !capacity) throw std::invalid_argument("Render probe configuration");
    }
    Counter* counter(std::uint32_t id) noexcept {
        for(auto& c:counters) {
            auto owner=c.id.load();
            if(owner==id || (!owner && c.id.compare_exchange_strong(owner,id))) return &c;
        }
        return nullptr;
    }
    Snapshot snapshot() const noexcept {
        Snapshot s;
        if(shader_slot) std::memcpy(&s.shader,shader_slot,sizeof(s.shader));
        if(material_slot) std::memcpy(&s.material,material_slot,sizeof(s.material));
        if(technique_slot) std::memcpy(&s.technique,technique_slot,sizeof(s.technique));
        return s;
    }
    void call(void* packet,std::uint32_t technique,std::uint32_t arg3,std::uint32_t arg4) {
        if(!enabled.load()) {original(packet,technique,arg3,arg4);return;}
        ++active;
        struct Active {Probe& probe;~Active(){--probe.active;}} lifetime{*this};
        if(!enabled.load()) {original(packet,technique,arg3,arg4);return;}
        const auto id=GetCurrentThreadId();auto* c=counter(id);
        if(!c) {++overflow_threads;original(packet,technique,arg3,arg4);return;}
        const auto sequence=c->attempted.fetch_add(1)+1;
        const bool sampled=(sequence-1)%every==0;
        const auto index=sampled?reserved.fetch_add(1):UINT64_MAX;
        const bool recording=sampled && index<records.size();
        if(sampled && !recording) ++dropped;
        Record r;
        if(recording) {
            r.sequence=sequence;r.thread_id=id;r.technique=technique;r.arg3=arg3;r.arg4=arg4;
            r.packet=token(reinterpret_cast<std::uintptr_t>(packet),salt);
            // Read only fields the inspected callee itself uses. These copied
            // tokens are identities, not retained engine pointers or worker data.
            const auto* bytes=static_cast<const unsigned char*>(packet);
            std::uintptr_t shader=0,property=0,geometry=0;
            std::memcpy(&shader,bytes,8);std::memcpy(&property,bytes+8,8);std::memcpy(&geometry,bytes+0x10,8);
            r.shader=token(shader,salt);r.property=token(property,salt);r.geometry=token(geometry,salt);r.flags=bytes[0x1e];
            const auto before=snapshot();r.shader_before=token(before.shader,salt);r.material_before=token(before.material,salt);r.technique_before=before.technique;
            r.begin=ticks();
        }
        try {original(packet,technique,arg3,arg4);} catch(...) {++c->aborted;throw;}
        if(recording) {
            r.end=ticks();const auto after=snapshot();r.shader_after=token(after.shader,salt);r.material_after=token(after.material,salt);r.technique_after=after.technique;
            records[static_cast<std::size_t>(index)]=r;++completed_samples;
        }
        ++c->completed;
    }
};
}
