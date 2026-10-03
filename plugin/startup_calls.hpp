#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
namespace startup_calls {
struct Call {
    unsigned char* instruction;
    std::array<unsigned char,5> expected;
    void* callback;
    unsigned char* stub;
};
inline bool rel32(std::uintptr_t after,std::uintptr_t target,std::int32_t& value) {
    if(target>=after) {
        const auto distance=target-after;
        if(distance>static_cast<std::uintptr_t>(INT32_MAX)) return false;
        value=static_cast<std::int32_t>(distance);
    } else {
        const auto distance=after-target;
        if(distance>static_cast<std::uintptr_t>(INT32_MAX)+1) return false;
        value=distance==static_cast<std::uintptr_t>(INT32_MAX)+1?INT32_MIN:-static_cast<std::int32_t>(distance);
    }
    return true;
}
// Called ONLY during SKSEPlugin_Load before gameplay. Never patch executing calls
// to start/stop a capture. Callbacks remain pinned pass-throughs afterward.
template<std::size_t N>
inline bool install(const std::array<Call,N>& calls,bool& patched) {
    static_assert(N>0 && N<=2);
    patched=false;
    std::array<std::array<unsigned char,5>,N> replacements{};
    std::array<DWORD,N> protections{};
    for(std::size_t i=0;i!=calls.size();++i) {
        const auto& c=calls[i]; std::int32_t displacement=0;
        if(!c.instruction || !c.stub || !c.callback || c.expected[0]!=0xe8
            || std::memcmp(c.instruction,c.expected.data(),5)
            || !rel32(reinterpret_cast<std::uintptr_t>(c.instruction)+5,reinterpret_cast<std::uintptr_t>(c.stub),displacement)) return false;
        replacements[i][0]=0xe8; std::memcpy(replacements[i].data()+1,&displacement,4);
    }
    for(std::size_t i=0;i!=calls.size();++i) {
        if(!VirtualProtect(calls[i].instruction,5,PAGE_EXECUTE_READWRITE,&protections[i])) {
            for(std::size_t j=i;j>0;--j) {DWORD ignored=0;VirtualProtect(calls[j-1].instruction,5,protections[j-1],&ignored);}
            return false;
        }
    }
    for(const auto& c:calls) {
        // RIP-indirect tail jump preserves the original CALL return address and
        // Win64 shadow space. No displaced instructions or copied prologue.
        const std::array<unsigned char,6> jump{0xff,0x25,0,0,0,0};
        std::memcpy(c.stub,jump.data(),6); std::memcpy(c.stub+6,&c.callback,8);
        FlushInstructionCache(GetCurrentProcess(),c.stub,14);
    }
    patched=true;
    for(std::size_t i=0;i!=calls.size();++i) std::memcpy(calls[i].instruction,replacements[i].data(),5);
    bool ok=true;
    for(std::size_t j=calls.size();j>0;--j) {
        const auto i=j-1;
        DWORD ignored=0; ok=VirtualProtect(calls[i].instruction,5,protections[i],&ignored)!=0 && ok;
        ok=FlushInstructionCache(GetCurrentProcess(),calls[i].instruction,5)!=0 && ok;
    }
    // After any instruction is written, the owner MUST retain pinned callbacks,
    // even if restoring page protection failed. Never unload a patched target.
    return ok;
}
}
