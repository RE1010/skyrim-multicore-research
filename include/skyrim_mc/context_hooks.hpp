#pragma once
#include <cstdint>
namespace skyrim_mc::stream {
struct CallTiming {std::uint64_t started=0;bool active=false,scoped=false;};
struct Callbacks {
    void (*enter)(void*) noexcept;
    void (*leave)(void*) noexcept;
    bool (*before)(void*,unsigned,const std::uintptr_t*) noexcept;
    void (*after)(void*,unsigned,const std::uintptr_t*,std::intptr_t) noexcept;
    CallTiming (*start_original)(void*,unsigned,const std::uintptr_t*) noexcept;
    void (*finish_original)(void*,unsigned,CallTiming,std::intptr_t) noexcept;
};
// One immediate context per process. Only this object's vtable is replaced.
bool attach_context(void*,void*,Callbacks) noexcept;
void detach_context() noexcept;
}
