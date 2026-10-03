#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include "multicore/engine_shadow.hpp"
#include "runtime_evidence.hpp"
#include "skse_abi.hpp"
#include "report_file.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
namespace fs=std::filesystem;
using Original=bool(*)(void*,const multicore::Sphere*);
constexpr std::size_t queue_capacity=8192, max_attempts=100000;
std::atomic<Original> original_function=nullptr;
std::array<std::atomic<void*>,2> slot_addresses{};
struct State;
std::atomic<State*> state=nullptr;

fs::path module_path() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_path),&module)) throw std::runtime_error("Module handle");
    wchar_t path[32768]; const auto length=GetModuleFileNameW(module,path,32768);
    if(!length || length>=32768) throw std::runtime_error("Module filename");
    return path;
}
bool matches_exe_hash() {
    wchar_t filename[32768]; if(!GetModuleFileNameW(nullptr,filename,32768)) return false;
    if(_wcsicmp(fs::path(filename).filename().c_str(),L"SkyrimSE.exe")) return false;
    std::ifstream file(fs::path(filename),std::ios::binary); if(!file) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    DWORD object_size=0,received=0;
    bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),4,&received,0)>=0;
    std::vector<unsigned char> object(object_size); unsigned char result[32]{};
    if(ok) ok=BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
    char buffer[65536];
    while(ok && file.read(buffer,sizeof(buffer))) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),sizeof(buffer),0)>=0;
    if(ok && file.gcount()) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),static_cast<ULONG>(file.gcount()),0)>=0;
    if(file.bad()) ok=false;
    if(ok) ok=BCryptFinishHash(hash,result,sizeof(result),0)>=0;
    if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
    const char digits[]="0123456789ABCDEF"; std::string hex;
    for(const auto byte:result) {hex+=digits[byte>>4];hex+=digits[byte&15];}
    return ok && hex==runtime_evidence::sha256;
}

struct State {
    fs::path base,session;
    multicore::VisibilityWorkers workers{4,0}; // force diagnostic comparisons onto workers
    std::mutex mutex,log_mutex;
    std::condition_variable_any wake;
    std::vector<multicore::EngineSnapshot> queue;
    std::atomic<bool> enabled=false,installed=false;
    std::atomic<std::size_t> attempts=0,active_writers=0,captured=0,dropped=0,hook_calls=0;
    multicore::ShadowStats stats;
    std::map<std::uint32_t,std::size_t> thread_counts;
    std::ofstream log,binary;
    std::jthread coordinator;
    explicit State(fs::path output):base(std::move(output)) {
        if(!base.is_absolute()) throw std::runtime_error("Output directory must be absolute");
        session=base/("session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(session);
        log.open(session/"plugin.log"); binary.open(session/"snapshots.bin",std::ios::binary);
        if(!log || !binary) throw std::runtime_error("Cannot open diagnostic output");
        multicore::SnapshotHeader header{}; std::memcpy(header.magic,"MCVS1710",8);
        header.record_size=sizeof(multicore::EngineSnapshot); header.runtime_version=runtime_evidence::version;
        std::memcpy(header.exe_sha256,runtime_evidence::sha256,64);
        binary.write(reinterpret_cast<const char*>(&header),sizeof(header)); binary.flush();
        queue.reserve(queue_capacity);
        write_current(); write_summary("loaded");
        coordinator=std::jthread([this](std::stop_token stop){run(stop);});
    }
    ~State() {coordinator.request_stop(); wake.notify_all(); if(coordinator.joinable()) coordinator.join();}
    void note(const char* text) {std::lock_guard lock(log_mutex);log<<text<<'\n';log.flush();}
    void write_current() {
        // Forward-slash path is valid on Windows and easy to escape in JSON.
        std::ofstream out(base/"current-session.json");
        out<<"{\"processId\":"<<GetCurrentProcessId()<<",\"sessionDirectory\":\""<<session.generic_string()<<"\"}\n";
        if(!out) throw std::runtime_error("Cannot publish session location");
    }
    void write_summary(const char* status) {
        const auto temporary=session/"summary.tmp"; std::ofstream out(temporary);
        out<<"{\"status\":\""<<status<<"\",\"mode\":\"shadow-only\",\"originalResultsPreserved\":true,"
           <<"\"processId\":"<<GetCurrentProcessId()<<",\"hookCalls\":"<<hook_calls.load()
           <<",\"attempted\":"<<attempts.load()<<",\"captured\":"<<captured.load()<<",\"dropped\":"<<dropped.load()
           <<",\"compared\":"<<stats.compared<<",\"unsupported\":"<<stats.unsupported
           <<",\"visibilityMismatches\":"<<stats.visibility_mismatches<<",\"maskMismatches\":"<<stats.mask_mismatches
           <<",\"serialParallelMismatches\":"<<stats.serial_parallel_mismatches<<",\"threads\":[";
        bool first=true; for(const auto& [id,count]:thread_counts) {if(!first) out<<',';first=false;
            out<<"{\"threadId\":"<<id<<",\"snapshots\":"<<count<<'}';}
        out<<"]}\n"; out.close();
        if(!out) throw std::runtime_error("Cannot write diagnostic summary");
        // Keep the prior complete report on transient locks and retry next update.
        report_file::publish(temporary,session/"summary.json");
    }
    void run(std::stop_token stop);
};

bool hooked_test(void* context,const multicore::Sphere* bound) {
    const auto original=original_function.load(std::memory_order_acquire);
    auto* s=state.load(std::memory_order_acquire);
    if(!s) return original(context,bound);
    ++s->hook_calls;
    if(!s->enabled.load(std::memory_order_acquire)) return original(context,bound);
    ++s->active_writers;
    const auto attempt=s->attempts.fetch_add(1);
    if(attempt>=max_attempts) {
        s->enabled=false; --s->active_writers; return original(context,bound);
    }
    multicore::EngineSnapshot snapshot{};
    const auto* bytes=static_cast<const unsigned char*>(context);
    std::memcpy(snapshot.engine_planes.data(),bytes+0x3c,96);
    std::memcpy(&snapshot.before_mask,bytes+0x9c,4);
    snapshot.optimize=bytes[0x120]; snapshot.bound=*bound; snapshot.thread_id=GetCurrentThreadId();
    const bool visible=original(context,bound); // always execute original, including its mask mutation
    snapshot.engine_visible=static_cast<std::uint8_t>(visible);
    std::memcpy(&snapshot.after_mask,bytes+0x9c,4);
    {
        std::unique_lock lock(s->mutex,std::try_to_lock);
        if(lock && s->queue.size()<queue_capacity) {s->queue.push_back(snapshot);++s->captured;}
        else ++s->dropped;
    }
    if(attempt+1==max_attempts) s->enabled=false;
    --s->active_writers;
    s->wake.notify_one();
    return visible;
}

bool replace_one_slot(void* address,void* expected,void* replacement) {
    auto* slot=static_cast<void* volatile*>(address);
    DWORD old=0; if(!slot || !VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&old)) return false;
    const auto previous=InterlockedCompareExchangePointer(slot,replacement,expected);
    DWORD ignored=0; const bool protection=VirtualProtect(const_cast<void**>(slot),sizeof(void*),old,&ignored)!=0;
    return previous==expected && protection;
}
bool replace_slot(void* expected,void* replacement) {
    bool success=true;
    for(auto& address:slot_addresses) {
        if(!replace_one_slot(address.load(),expected,replacement)) success=false;
    }
    return success;
}

void State::run(std::stop_token stop) {
    try {
        std::vector<multicore::EngineSnapshot> batch;batch.reserve(queue_capacity);
        bool started=false,finished=false;
        std::chrono::steady_clock::time_point started_at{};
        while(!stop.stop_requested()) {
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock,stop,std::chrono::milliseconds(500),[&]{return !queue.empty();});
                batch.clear(); batch.swap(queue);
            }
            if(!started && installed && fs::exists(session/"capture.trigger")) {
                started=true; started_at=std::chrono::steady_clock::now(); enabled=true;
                note("Capture started; original game results remain authoritative.");
            }
            if(started && !finished && (std::chrono::steady_clock::now()-started_at>=std::chrono::seconds(20)
                || fs::exists(session/"capture-stop.trigger"))) enabled=false;
            if(!batch.empty()) {
                binary.write(reinterpret_cast<const char*>(batch.data()),static_cast<std::streamsize>(batch.size()*sizeof(batch[0])));
                binary.flush(); if(!binary) throw std::runtime_error("Snapshot output failed");
                for(const auto& record:batch) ++thread_counts[record.thread_id];
                const auto part=multicore::compare_engine_snapshots(batch,workers);
                stats.compared+=part.compared;stats.unsupported+=part.unsupported;
                stats.visibility_mismatches+=part.visibility_mismatches;stats.mask_mismatches+=part.mask_mismatches;
                stats.serial_parallel_mismatches+=part.serial_parallel_mismatches;
            }
            if(started && !enabled && !finished && active_writers==0) {
                std::lock_guard lock(mutex);
                if(queue.empty()) {
                    finished=true;
                    const bool restored=replace_slot(reinterpret_cast<void*>(&hooked_test),reinterpret_cast<void*>(original_function.load()));
                    note(restored?"Capture complete; original vtable slot restored.":"Capture complete; hook restoration conflict (pass-through remains).");
                }
            }
            write_summary(finished?"capture-complete":started?"capturing":installed?"waiting-for-trigger":"loaded");
        }
    } catch(const std::exception& e) {
        enabled=false;note(e.what());
        replace_slot(reinterpret_cast<void*>(&hooked_test),reinterpret_cast<void*>(original_function.load()));
        try {write_summary("diagnostic-error");} catch(...) {}
    }
}

void on_message(skse_abi::Message* message) {
    if(!message || message->type!=skse_abi::data_loaded) return;
    auto* s=state.load(); if(!s || s->installed) return;
    if(!replace_slot(reinterpret_cast<void*>(original_function.load()),reinterpret_cast<void*>(&hooked_test))) {
        replace_slot(reinterpret_cast<void*>(&hooked_test),reinterpret_cast<void*>(original_function.load()));
        s->note("Hook refused: slot changed or page protection failed.");return;
    }
    s->installed=true;s->note("Verified BSCullingProcess and BSGeometryListCullingProcess::TestBaseVisibility3 bound; waiting for capture.trigger.");
}
}

extern "C" __declspec(dllexport) skse_abi::VersionData SKSEPlugin_Version={
    1,1,"MulticoreVisibilityShadow","Local research","",0,0,{runtime_evidence::version,0},0};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse_abi::Interface* skse) {
    try {
        if(!skse || skse->is_editor || skse->runtime_version!=runtime_evidence::version ||
            !skse->query_interface || !skse->get_plugin_handle || !matches_exe_hash()) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if(std::memcmp(reinterpret_cast<const void*>(base+runtime_evidence::function_rva),
                       runtime_evidence::function_bytes.data(),runtime_evidence::function_bytes.size())) return false;
        std::array<void*,2> slots{};
        for(std::size_t i=0;i!=slots.size();++i) {
            auto* slot=reinterpret_cast<void**>(base+runtime_evidence::vtable_rvas[i]+0x1c*sizeof(void*));
            if(*slot!=reinterpret_cast<void*>(base+runtime_evidence::function_rva)) return false;
            slots[i]=slot;
        }
        const auto* messaging=static_cast<const skse_abi::Messaging*>(skse->query_interface(skse_abi::messaging_id));
        if(!messaging || messaging->version<2 || !messaging->register_listener) return false;
        const auto ini=module_path().replace_extension(L".ini");
        wchar_t output[32768];
        if(!GetPrivateProfileStringW(L"Diagnostics",L"OutputDirectory",L"",output,32768,ini.c_str())) return false;
        auto instance=std::make_unique<State>(output);
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&hooked_test),&pinned)) return false;
        original_function=reinterpret_cast<Original>(base+runtime_evidence::function_rva);
        for(std::size_t i=0;i!=slots.size();++i) slot_addresses[i]=slots[i];
        state=instance.get();
        if(!messaging->register_listener(skse->get_plugin_handle(),"SKSE",&on_message)) {state=nullptr;return false;}
        instance->note("Runtime hash and code verified. Shadow mode; no visibility replacement.");
        // Successfully loaded SKSE plugins live until process exit. Pinning prevents
        // unloading code while engine threads hold the callback; no worker join in DllMain.
        instance.release();return true;
    } catch(...) {state=nullptr;return false;}
}
