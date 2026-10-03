#define NOMINMAX
#include "render_probe.hpp"
#include "startup_calls.hpp"
#include "runtime_guard.hpp"
#include "render_evidence.hpp"
#include "report_file.hpp"
#include "skse_abi.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
namespace {
namespace fs=std::filesystem;
struct State;
std::atomic<State*> state{nullptr};
void hooked_pass(void*,std::uint32_t,std::uint32_t,std::uint32_t);
struct State {
    render_probe::Probe probe;
    fs::path base,session;
    const std::uintptr_t image;
    const std::uint32_t seconds;
    LARGE_INTEGER frequency{};
    std::uint64_t begin=0,end=0;
    std::array<unsigned char,5> owned_call{};
    std::atomic<bool> ready{false};
    std::ofstream log;
    std::jthread coordinator;
    bool code_valid=false;
    State(fs::path output,std::uintptr_t address,std::uint32_t sampling,std::uint32_t duration):
        probe(reinterpret_cast<render_probe::Original>(address+render_evidence::function_rva),sampling),base(std::move(output)),image(address),seconds(duration) {
        if(!base.is_absolute()) throw std::runtime_error("Absolute output path required");
        probe.shader_slot=reinterpret_cast<const std::uintptr_t*>(image+render_evidence::shader_rva);
        probe.material_slot=reinterpret_cast<const std::uintptr_t*>(image+render_evidence::material_rva);
        probe.technique_slot=reinterpret_cast<const std::uint32_t*>(image+render_evidence::technique_rva);
        session=base/("session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(session);log.open(session/"plugin.log");
        if(!log || !QueryPerformanceFrequency(&frequency)) throw std::runtime_error("Output/QPC unavailable");
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) throw std::runtime_error("Process identity");
        std::ofstream current(base/"current-session.json");
        current<<"{\"processId\":"<<GetCurrentProcessId()<<",\"processStartFileTime\":"<<((static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime)
            <<",\"sessionDirectory\":\""<<session.generic_string()<<"\"}\n";
        current.close();if(!current) throw std::runtime_error("Current session output");
        summary("loaded");
    }
    void note(const char* message) {log<<message<<'\n';log.flush();}
    bool code_matches() const {
        constexpr auto offset=render_evidence::call_rva-render_evidence::caller_rva;
        const auto* caller=reinterpret_cast<const unsigned char*>(image+render_evidence::caller_rva);
        return !std::memcmp(caller,render_evidence::caller_bytes.data(),offset)
            && !std::memcmp(caller+offset,owned_call.data(),5)
            && !std::memcmp(caller+offset+5,render_evidence::caller_bytes.data()+offset+5,render_evidence::caller_bytes.size()-offset-5)
            && !std::memcmp(reinterpret_cast<const void*>(image+render_evidence::function_rva),render_evidence::function_bytes.data(),render_evidence::function_bytes.size());
    }
    void summary(const char* status) {
        const auto temp=session/"summary.tmp";std::ofstream out(temp);
        std::uint64_t attempted=0,completed=0,aborted=0;
        for(const auto& c:probe.counters) {attempted+=c.attempted.load();completed+=c.completed.load();aborted+=c.aborted.load();}
        out<<"{\"schemaVersion\":1,\"mode\":\"render-pass-diagnostic\",\"status\":\""<<status<<"\",\"processId\":"<<GetCurrentProcessId()
            <<",\"runtimeVersion\":"<<render_evidence::version<<",\"exeSHA256\":\""<<render_evidence::sha256
            <<"\",\"scope\":\"caller-rva-15601cf\",\"originalRendererPreserved\":true,\"engineWorkOffloaded\":false,\"hooksRetainedAsPassthrough\":true"
            <<",\"sampleEvery\":"<<probe.every<<",\"durationSeconds\":"<<seconds<<",\"qpcFrequency\":"<<frequency.QuadPart
            <<",\"captureStartQPC\":"<<begin<<",\"captureStopQPC\":"<<end<<",\"codeValidAtCapture\":"<<(code_valid?"true":"false")
            <<",\"attemptedCalls\":"<<attempted<<",\"completedCalls\":"<<completed<<",\"abortedCalls\":"<<aborted
            <<",\"sampleAttempts\":"<<probe.reserved.load()<<",\"completedSamples\":"<<probe.completed_samples.load()<<",\"droppedSamples\":"<<probe.dropped.load()
            <<",\"threadCapacityOverflowCalls\":"<<probe.overflow_threads.load()<<",\"activeCalls\":"<<probe.active.load()<<",\"threads\":[";
        bool first=true;
        for(const auto& c:probe.counters) if(c.id.load()) {
            if(!first) out<<',';first=false;
            out<<"{\"threadId\":"<<c.id.load()<<",\"attempted\":"<<c.attempted.load()<<",\"completed\":"<<c.completed.load()<<",\"aborted\":"<<c.aborted.load()<<'}';
        }
        out<<"]}\n";out.close();if(!out) throw std::runtime_error("Summary output");
        report_file::publish(temp,session/"summary.json");
    }
    void write_records() {
        const auto temp=session/"samples.tmp";std::ofstream out(temp);
        out<<"ThreadId,Sequence,BeginQPC,EndQPC,PacketToken,ShaderToken,PropertyToken,GeometryToken,Technique,Arg3,Arg4,Flags,ShaderBefore,ShaderAfter,MaterialBefore,MaterialAfter,TechniqueBefore,TechniqueAfter\n";
        const auto count=(std::min)(probe.reserved.load(),static_cast<std::uint64_t>(probe.records.size()));
        for(std::uint64_t i=0;i<count;++i) {
            const auto& r=probe.records[static_cast<std::size_t>(i)];if(!r.end) continue;
            out<<r.thread_id<<','<<r.sequence<<','<<r.begin<<','<<r.end<<','<<r.packet<<','<<r.shader<<','<<r.property<<','<<r.geometry
                <<','<<r.technique<<','<<r.arg3<<','<<r.arg4<<','<<r.flags<<','<<r.shader_before<<','<<r.shader_after<<','<<r.material_before<<','<<r.material_after
                <<','<<r.technique_before<<','<<r.technique_after<<'\n';
        }
        out.close();if(!out || !report_file::publish(temp,session/"samples.csv")) throw std::runtime_error("Samples output/lock");
    }
    void run(std::stop_token stop) {
        try {
            bool started=false,finished=false;std::chrono::steady_clock::time_point start;
            while(!stop.stop_requested()) {
                if(ready && !started && fs::exists(session/"capture.trigger")) {
                    code_valid=code_matches();
                    if(!code_valid) {note("Render code changed after installation; capture refused.");summary("code-conflict");return;}
                    started=true;start=std::chrono::steady_clock::now();begin=render_probe::ticks();probe.enabled=true;
                    note("Capture started. Original render calls remain synchronous and execute exactly once. No worker dereferences.");
                }
                if(started && !finished && (std::chrono::steady_clock::now()-start>=std::chrono::seconds(seconds) || fs::exists(session/"capture-stop.trigger"))) {
                    probe.enabled=false;if(!end) end=render_probe::ticks();
                    if(!probe.active.load()) {write_records();finished=true;note("Capture finished; original forwarding remains, without sampled work.");}
                }
                summary(finished?"capture-complete":started?"capturing":ready?"waiting-for-trigger":"waiting-for-data");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        } catch(const std::exception& e) {probe.enabled=false;note(e.what());try{summary("diagnostic-error");}catch(...){}}
    }
};
void hooked_pass(void* packet,std::uint32_t a,std::uint32_t b,std::uint32_t c) {state.load(std::memory_order_acquire)->probe.call(packet,a,b,c);}
void message(skse_abi::Message* m) {if(m && m->type==skse_abi::data_loaded) if(auto* s=state.load()) s->ready=true;}
fs::path config_path() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&hooked_pass),&module)) throw std::runtime_error("Module handle");
    wchar_t path[32768];if(!GetModuleFileNameW(module,path,32768)) throw std::runtime_error("Module path");
    return fs::path(path).replace_extension(L".ini");
}
}
extern "C" __declspec(dllexport) skse_abi::VersionData SKSEPlugin_Version={1,1,"RenderPassProbe","Local research","",0,0,{render_evidence::version,0},0};
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse_abi::Interface* skse) {
    try {
        if(!skse || skse->is_editor || skse->runtime_version!=render_evidence::version || !skse->query_interface || !skse->get_plugin_handle || !runtime_guard::hash_matches(render_evidence::sha256)) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if(std::memcmp(reinterpret_cast<const void*>(base+render_evidence::caller_rva),render_evidence::caller_bytes.data(),render_evidence::caller_bytes.size())
            || std::memcmp(reinterpret_cast<const void*>(base+render_evidence::function_rva),render_evidence::function_bytes.data(),render_evidence::function_bytes.size())) return false;
        const auto* trampoline=static_cast<const skse_abi::Trampoline*>(skse->query_interface(skse_abi::trampoline_id));
        const auto* messaging=static_cast<const skse_abi::Messaging*>(skse->query_interface(skse_abi::messaging_id));
        if(!trampoline || trampoline->version<1 || !trampoline->allocate_branch || !messaging || messaging->version<2 || !messaging->register_listener) return false;
        const auto ini=config_path();wchar_t output[32768];
        if(!GetPrivateProfileStringW(L"Diagnostics",L"OutputDirectory",L"",output,32768,ini.c_str())) return false;
        const auto sampling=GetPrivateProfileIntW(L"Diagnostics",L"SampleEvery",16,ini.c_str());
        const auto seconds=GetPrivateProfileIntW(L"Diagnostics",L"DurationSeconds",20,ini.c_str());
        if(sampling<1 || sampling>65536 || seconds<5 || seconds>60) return false;
        const auto handle=skse->get_plugin_handle();auto* stub=static_cast<unsigned char*>(trampoline->allocate_branch(handle,14));if(!stub) return false;
        auto instance=std::make_unique<State>(output,base,sampling,seconds);
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&hooked_pass),&pinned)) return false;
        state=instance.get();
        if(!messaging->register_listener(handle,"SKSE",&message)) {state=nullptr;return false;}
        const std::array<startup_calls::Call,1> calls{{{reinterpret_cast<unsigned char*>(base+render_evidence::call_rva),render_evidence::call_bytes,reinterpret_cast<void*>(&hooked_pass),stub}}};
        bool patched=false;const bool installed=startup_calls::install(calls,patched);
        if(!patched) {state=nullptr;return false;}
        auto* retained=instance.release();
        std::memcpy(retained->owned_call.data(),reinterpret_cast<const void*>(base+render_evidence::call_rva),5);
        if(!installed) {retained->note("Installation protection/cache failure; callbacks pinned, diagnostics inactive.");retained->summary("installation-error");return true;}
        retained->note("Exact EXE hash, full caller/callee bytes and integer-argument CALL checked. Original renderer retained; diagnostic integration only.");
        retained->coordinator=std::jthread([retained](std::stop_token stop){retained->run(stop);});return true;
    } catch(...) {
        if(auto* retained=state.load()) {retained->probe.enabled=false;return true;}
        return false;
    }
}
