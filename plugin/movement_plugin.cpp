#define NOMINMAX
#include "movement_probe.hpp"
#include "startup_calls.hpp"
#include "runtime_guard.hpp"
#include "runtime_evidence.hpp"
#include "getter_evidence.hpp"
#include "report_file.hpp"
#include "skse_abi.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
namespace fs=std::filesystem;
struct State;
std::atomic<State*> state{nullptr};
bool hooked_search(void* controller);
void* hooked_lock(void* guard,void* lock_address);
bool init_helpers_match(std::uintptr_t base) {
    for(const auto& helper:getter_evidence::init_helpers)
        if(std::memcmp(reinterpret_cast<const void*>(base+helper.rva),helper.bytes.data(),helper.length)) return false;
    return true;
}
struct State {
    movement_fast::Table fast_table;
    movement_probe::Probe probe;
    fs::path base,session;
    std::uint32_t duration;
    LARGE_INTEGER frequency{};
    std::uint64_t capture_begin=0,capture_stop=0;
    std::ofstream log;
    std::jthread coordinator;
    std::atomic<bool> installed{false},ready{false};
    const std::size_t verified_getters;
    bool getter_code_valid=true;
    State(fs::path output,movement_probe::Search search,movement_probe::Acquire acquire,std::uint32_t sampling,std::uint32_t seconds,
        const std::vector<movement_fast::Getter>& getters,std::uintptr_t image_base):
        fast_table(getters),probe(search,acquire,sampling),base(std::move(output)),duration(seconds),verified_getters(getters.size()) {
        probe.fast_table=&fast_table;probe.module_base=image_base;
        probe.target_slot=reinterpret_cast<const std::uint32_t*>(image_base+0x3242fc4);
        if(!base.is_absolute()) throw std::runtime_error("Output must be absolute");
        session=base/("session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(session);QueryPerformanceFrequency(&frequency);
        log.open(session/"plugin.log");if(!log || !frequency.QuadPart) throw std::runtime_error("Output/QPC unavailable");
        std::ofstream current(base/"current-session.json");
        current<<"{\"processId\":"<<GetCurrentProcessId()<<",\"processStartFileTime\":";
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) throw std::runtime_error("Process start unavailable");
        const auto stamp=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        current<<stamp<<",\"sessionDirectory\":\""<<session.generic_string()<<"\"}\n";
        current.close();if(!current) throw std::runtime_error("Current session output");
        summary("loaded");
    }
    ~State() {coordinator.request_stop();if(coordinator.joinable()) coordinator.join();}
    void note(const char* message) {log<<message<<'\n';log.flush();}
    bool getter_code_matches() const {
        if(fast_table.find(probe.module_base+0x7a6b10) && !init_helpers_match(probe.module_base)) return false;
        for(const auto& entry:getter_evidence::entries) {
            const auto function=probe.module_base+entry.function_rva;
            if(fast_table.find(function) && (std::memcmp(reinterpret_cast<const void*>(probe.module_base+entry.body_rva),entry.bytes.data(),entry.length)
                || (entry.guard_rva && std::memcmp(reinterpret_cast<const void*>(function),entry.thunk.data(),entry.thunk.size())))) return false;
        }
        return true;
    }
    void summary(const char* status) {
        const auto temp=session/"summary.tmp";std::ofstream out(temp);
        std::uint64_t calls=0,positive=0,negative=0,aborted=0;
        for(const auto& c:probe.counters) {calls+=c.completed.load();positive+=c.positive.load();negative+=c.negative.load();aborted+=c.aborted.load();}
        const auto recorded=(std::min)(probe.record_attempts.load(),static_cast<std::uint64_t>(probe.records.size()));
        out<<"{\"schemaVersion\":2,\"status\":\""<<status<<"\",\"mode\":\"movement-fast-shadow\",\"originalResultsPreserved\":true,"
           <<"\"processId\":"<<GetCurrentProcessId()<<",\"runtimeVersion\":"<<runtime_evidence::version
           <<",\"exeSHA256\":\""<<runtime_evidence::sha256<<"\",\"scope\":\"caller-rva-673d68\",\"sampleEvery\":"<<probe.sample_every
           <<",\"durationSeconds\":"<<duration<<",\"qpcFrequency\":"<<frequency.QuadPart
           <<",\"captureStartQPC\":"<<capture_begin<<",\"captureStopQPC\":"<<capture_stop
           <<",\"completedCalls\":"<<calls<<",\"positive\":"<<positive<<",\"negative\":"<<negative<<",\"aborted\":"<<aborted
           <<",\"samples\":"<<recorded<<",\"sampleAttempts\":"<<probe.record_attempts.load()
           <<",\"threadCapacityOverflowCalls\":"<<probe.overflow_calls.load()<<",\"activeCalls\":"<<probe.active.load()
           <<",\"verifiedGetterBodies\":"<<verified_getters<<",\"fastCompared\":"<<probe.fast_compared.load()
           <<",\"getterCodeValidAtCapture\":"<<(getter_code_valid?"true":"false")
           <<",\"fastMismatches\":"<<probe.fast_mismatches.load()<<",\"fastUnsupported\":"<<probe.fast_unsupported.load()
           <<",\"hooksRetainedAsPassthrough\":true,\"threads\":[";
        bool first=true;
        for(const auto& c:probe.counters) if(c.thread_id.load()) {
            if(!first) out<<',';first=false;out<<"{\"threadId\":"<<c.thread_id.load()<<",\"completed\":"<<c.completed.load()
                <<",\"positive\":"<<c.positive.load()<<",\"negative\":"<<c.negative.load()<<",\"aborted\":"<<c.aborted.load()<<'}';
        }
        out<<"]}\n";out.close();if(!out) throw std::runtime_error("Summary write failed");
        report_file::publish(temp,session/"summary.json");
    }
    void write_records() {
        const auto count=(std::min)(probe.record_attempts.load(),static_cast<std::uint64_t>(probe.records.size()));
        const auto temp=session/"samples.tmp";std::ofstream out(temp);
        out<<"ThreadId,Sequence,ControllerToken,ListSize,LockObservations,Result,FunctionTicks,LockTicks,FastTicks,FastStatus,FastResult,Inspected,NonNull,UnsupportedGetterRVA\n";
        for(std::uint64_t i=0;i<count;++i) {
            const auto& r=probe.records[static_cast<std::size_t>(i)];
            out<<r.thread_id<<','<<r.sequence<<','<<r.controller_token<<','<<r.list_size<<','<<r.lock_observations<<','<<r.result<<','<<r.function_ticks<<','<<r.lock_ticks
                <<','<<r.fast_ticks<<','<<r.fast_status<<','<<r.fast_result<<','<<r.inspected<<','<<r.nonnull<<','<<r.unsupported_getter_rva<<'\n';
        }
        out.close();if(!out) throw std::runtime_error("Samples write failed");
        if(!report_file::publish(temp,session/"samples.csv")) throw std::runtime_error("Samples destination unexpectedly locked");
    }
    void run(std::stop_token stop) {
        try {
            bool started=false,finished=false;
            std::chrono::steady_clock::time_point begin{};
            while(!stop.stop_requested()) {
                if(ready && !started && fs::exists(session/"capture.trigger")) {
                    getter_code_valid=getter_code_matches();
                    if(!getter_code_valid) {probe.fast_allowed=false;note("Getter code changed after startup; shadow candidate disabled.");}
                    started=true;begin=std::chrono::steady_clock::now();capture_begin=movement_probe::ticks();probe.enabled=true;
                    note("Capture started. Fast candidate sampled under original lock; original search remains authoritative.");
                }
                if(started && !finished && (std::chrono::steady_clock::now()-begin>=std::chrono::seconds(duration)
                    || fs::exists(session/"capture-stop.trigger"))) {
                    probe.enabled=false;if(!capture_stop) capture_stop=movement_probe::ticks();
                    if(probe.active.load()==0) {write_records();finished=true;note("Capture complete. Startup call hooks retained as inactive original passthroughs.");}
                }
                summary(finished?"capture-complete":started?"capturing":ready?"waiting-for-trigger":"waiting-for-data");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        } catch(const std::exception& e) {probe.enabled=false;note(e.what());try {summary("diagnostic-error");} catch(...) {}}
    }
};
bool hooked_search(void* controller) {return state.load(std::memory_order_acquire)->probe.call(controller);}
void* hooked_lock(void* guard,void* lock_address) {return state.load(std::memory_order_acquire)->probe.lock(guard,lock_address);}
void message(skse_abi::Message* m) {if(m && m->type==skse_abi::data_loaded) if(auto* s=state.load()) s->ready=true;}
fs::path config_path() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&hooked_search),&module)) throw std::runtime_error("Module handle");
    wchar_t path[32768];if(!GetModuleFileNameW(module,path,32768)) throw std::runtime_error("Module path");
    return fs::path(path).replace_extension(L".ini");
}
}
extern "C" __declspec(dllexport) skse_abi::VersionData SKSEPlugin_Version={
    1,3,"MovementMessageProbe","Local research","",0,0,{runtime_evidence::version,0},0};
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse_abi::Interface* skse) {
    // Nothing after the first instruction write may unload callbacks or State.
    // Installation runs at SKSE startup, never in an already running save.
    try {
        if(!skse || skse->is_editor || skse->runtime_version!=runtime_evidence::version
            || !skse->query_interface || !skse->get_plugin_handle || !runtime_guard::hash_matches(runtime_evidence::sha256)) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if(std::memcmp(reinterpret_cast<void*>(base+runtime_evidence::movement_rva),runtime_evidence::movement_bytes.data(),runtime_evidence::movement_bytes.size())
            || std::memcmp(reinterpret_cast<void*>(base+runtime_evidence::movement_lock_rva),runtime_evidence::movement_lock_bytes.data(),runtime_evidence::movement_lock_bytes.size())
            || std::memcmp(reinterpret_cast<void*>(base+runtime_evidence::movement_caller_rva),runtime_evidence::movement_caller_bytes.data(),5)) return false;
        const auto* trampoline=static_cast<const skse_abi::Trampoline*>(skse->query_interface(skse_abi::trampoline_id));
        const auto* messaging=static_cast<const skse_abi::Messaging*>(skse->query_interface(skse_abi::messaging_id));
        if(!trampoline || trampoline->version<1 || !trampoline->allocate_branch || !messaging || messaging->version<2 || !messaging->register_listener) return false;
        const auto ini=config_path();wchar_t output[32768];
        if(!GetPrivateProfileStringW(L"Diagnostics",L"OutputDirectory",L"",output,32768,ini.c_str())) return false;
        const auto sampling=GetPrivateProfileIntW(L"Diagnostics",L"SampleEvery",64,ini.c_str());
        const auto seconds=GetPrivateProfileIntW(L"Diagnostics",L"DurationSeconds",20,ini.c_str());
        if(sampling<1 || sampling>65536 || seconds<5 || seconds>60) return false;
        const auto handle=skse->get_plugin_handle();
        auto* stubs=static_cast<unsigned char*>(trampoline->allocate_branch(handle,28));if(!stubs) return false;
        std::vector<movement_fast::Getter> getters;getters.reserve(getter_evidence::entries.size());
        const bool guarded_helpers_valid=init_helpers_match(base);
        for(const auto& entry:getter_evidence::entries) {
            if(entry.guard_rva && !guarded_helpers_valid) continue;
            const auto function=base+entry.function_rva;
            if(std::memcmp(reinterpret_cast<const void*>(base+entry.body_rva),entry.bytes.data(),entry.length)
                || (entry.guard_rva && std::memcmp(reinterpret_cast<const void*>(function),entry.thunk.data(),entry.thunk.size()))) continue;
            const auto mode=static_cast<movement_fast::Mode>(entry.mode);
            const auto operand=mode==movement_fast::Mode::CachedGlobal || mode==movement_fast::Mode::Global || mode==movement_fast::Mode::GuardedGlobal?base+entry.operand:entry.operand;
            getters.push_back({function,operand,mode,entry.guard_rva?base+entry.guard_rva:0});
        }
        auto instance=std::make_unique<State>(output,reinterpret_cast<movement_probe::Search>(base+runtime_evidence::movement_rva),
            reinterpret_cast<movement_probe::Acquire>(base+runtime_evidence::movement_lock_rva),sampling,seconds,getters,base);
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&hooked_search),&pinned)) return false;
        state=instance.get();
        if(!messaging->register_listener(handle,"SKSE",&message)) {state=nullptr;return false;}
        const std::array<startup_calls::Call,2> calls{{
            {reinterpret_cast<unsigned char*>(base+runtime_evidence::movement_caller_rva),runtime_evidence::movement_caller_bytes,reinterpret_cast<void*>(&hooked_search),stubs},
            {reinterpret_cast<unsigned char*>(base+runtime_evidence::movement_lock_call_rva),runtime_evidence::movement_lock_call_bytes,reinterpret_cast<void*>(&hooked_lock),stubs+14}}};
        bool patched=false;const auto installed=startup_calls::install(calls,patched);
        if(!patched) {state=nullptr;return false;}
        auto* retained=instance.release();retained->installed=installed;
        if(!installed) {retained->note("Startup hooks written; page protection/cache operation failed. Diagnostics disabled; callbacks retained.");retained->summary("installation-error");return true;}
        retained->note("Exact runtime/hash/function/callsite checks passed. Two startup CALL hooks; original search decisions and original lock implementation preserved.");
        retained->coordinator=std::jthread([retained](std::stop_token stop){retained->run(stop);});return true;
    } catch(...) {
        // state is null until construction succeeds; any retained pinned state
        // must stay alive and disabled if startup reporting/thread creation fails.
        if(auto* retained=state.load()) {retained->probe.enabled=false;return true;}
        return false;
    }
}
