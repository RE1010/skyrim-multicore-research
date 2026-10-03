#define NOMINMAX
#include <Windows.h>
#include "skse_abi.hpp"
#include "runtime_evidence.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 1;
    const auto module=LoadLibraryW(argv[1]); if(!module) return 2;
    using Load=bool(*)(const skse_abi::Interface*);
    const auto load=reinterpret_cast<Load>(GetProcAddress(module,"SKSEPlugin_Load"));
    const auto* version=reinterpret_cast<const skse_abi::VersionData*>(GetProcAddress(module,"SKSEPlugin_Version"));
    if(!load || !version || version->data_version!=1 || version->compatible[0]!=runtime_evidence::version
        || version->compatible[1]!=0 || load(nullptr)) return 3;
    skse_abi::Interface fake{}; fake.runtime_version=0x01060640;
    if(load(&fake)) return 4;
    fake.runtime_version=runtime_evidence::version;
    fake.query_interface=[](std::uint32_t)->void* {return nullptr;};
    fake.get_plugin_handle=[]()->std::uint32_t{return 1;};
    if(load(&fake)) return 5; // exact runtime in a different executable must still be rejected
    FreeLibrary(module);
    std::cout<<"PASS: DLL exports, version allowlist, null interface, wrong runtime, wrong executable guards\n";
    return 0;
}
