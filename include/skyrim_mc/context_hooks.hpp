#pragma once
#include <cstdint>
namespace skyrim_mc::stream {
struct Callbacks {
    void (*enter)(void*) noexcept;
    void (*leave)(void*) noexcept;
    bool (*before)(void*,unsigned,const std::uintptr_t*) noexcept;
    void (*after)(void*,unsigned,const std::uintptr_t*,std::intptr_t) noexcept;
};
// One immediate context per process. Only this object's vtable is replaced.
bool attach_context(void*,void*,Callbacks) noexcept;
void detach_context() noexcept;
}
