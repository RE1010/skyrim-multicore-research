#pragma once
#include <cstdint>
namespace render_bridge_call {
using Original=std::uint8_t(*)(void*,std::uint32_t*,std::uint32_t*,void*,std::uint32_t);
template<class Scope> std::uint8_t invoke(Original original,Scope& scope,void* a,std::uint32_t* b,std::uint32_t* c,void* d,std::uint32_t e) {
    scope.begin();struct Finish {Scope& scope;~Finish(){scope.end();}} finish{scope};
    return original(a,b,c,d,e);
}
}
