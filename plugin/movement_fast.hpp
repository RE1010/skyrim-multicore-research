#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
namespace movement_fast {
enum class Mode:std::uint8_t {CachedGlobal=0,Global=1,Object=2,Constant=3,GuardedGlobal=4};
struct Getter {std::uintptr_t function=0;std::uintptr_t operand=0;Mode mode=Mode::CachedGlobal;std::uintptr_t guard=0;};
struct Table {
    std::vector<Getter> slots;
    explicit Table(std::span<const Getter> entries) {
        std::size_t size=4;while(size<entries.size()*4) size*=2;slots.resize(size);
        for(const auto& entry:entries) {
            auto at=hash(entry.function)&(size-1);
            while(slots[at].function && slots[at].function!=entry.function) at=(at+1)&(size-1);
            slots[at]=entry;
        }
    }
    static std::size_t hash(std::uintptr_t key) noexcept {return static_cast<std::size_t>((key>>4)*0x9e3779b97f4a7c15ULL);}
    const Getter* find(std::uintptr_t function) const noexcept {
        auto at=hash(function)&(slots.size()-1);
        while(slots[at].function) {if(slots[at].function==function) return &slots[at];at=(at+1)&(slots.size()-1);}
        return nullptr;
    }
};
enum class Status:std::uint32_t {Complete=0,UnknownGetter=1,ColdGetter=2,ColdTarget=3};
struct Result {
    Status status=Status::Complete;
    bool found=false;
    std::uint32_t inspected=0,nonnull=0;
    std::uintptr_t unsupported_getter=0;
};
inline std::uint32_t read32(const void* address) noexcept {
    // Match one original aligned/unaligned x64 32-bit load. Never cache a value
    // across entries or calls; object fields and global slots are reread.
    return *static_cast<const volatile std::uint32_t*>(address);
}
// Caller MUST already hold the original controller lock. No engine function is
// called and no object/list lifetime is retained after this invocation.
inline Result scan(void* controller,const Table& table,const std::uint32_t* target_slot) noexcept {
    Result result{};const auto* bytes=static_cast<unsigned char*>(controller);
    void** items=nullptr;std::memcpy(&items,bytes+0x158,8);
    const auto count=read32(bytes+0x168);
    // Invocation-local cache of immutable code descriptions, never type values
    // or search results. Exact function equality is checked on every hit.
    std::array<const Getter*,64> recent{};
    for(std::uint32_t i=0;i<count;++i) {
        ++result.inspected;auto* item=items[i];if(!item) continue;++result.nonnull;
        const auto target=read32(target_slot);
        if(!target) {result.status=Status::ColdTarget;return result;}
        void** vtable=nullptr;std::memcpy(&vtable,item,8);
        const auto function=reinterpret_cast<std::uintptr_t>(vtable[1]);
        auto& cached=recent[(function>>4)&63];
        const auto* getter=cached && cached->function==function?cached:table.find(function);
        cached=getter;
        if(!getter) {result.status=Status::UnknownGetter;result.unsupported_getter=function;return result;}
        if(getter->mode==Mode::GuardedGlobal && std::bit_cast<std::int32_t>(read32(reinterpret_cast<const void*>(getter->guard)))>=-1) {
            result.status=Status::ColdGetter;result.unsupported_getter=function;return result;
        }
        const auto value=getter->mode==Mode::Constant?static_cast<std::uint32_t>(getter->operand):
            read32(getter->mode==Mode::Object?static_cast<unsigned char*>(item)+getter->operand:reinterpret_cast<const void*>(getter->operand));
        if((getter->mode==Mode::CachedGlobal || getter->mode==Mode::GuardedGlobal) && !value) {result.status=Status::ColdGetter;result.unsupported_getter=function;return result;}
        if(value==target) {result.found=true;return result;}
    }
    return result;
}
}
