#define NOMINMAX
#include "uncap_policy.hpp"
#include "startup_calls.hpp"
#include "runtime_guard.hpp"
#include "uncap_evidence.hpp"
#include "skse_abi.hpp"
#include "report_file.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
namespace {
namespace fs=std::filesystem;
using Physics=void(*)(float,std::uint8_t,std::uint8_t);
using Present=void(*)();
struct State;
std::atomic<State*> state=nullptr;
void physics(float,std::uint8_t,std::uint8_t);
void present();
struct State {
    const std::uintptr_t image;
    fs::path session;
    std::ofstream log;
    std::jthread reporter;
    Physics original_physics;Present original_present;
    std::atomic<bool> enabled,world=false,settings_ready=false,valid_physics=false;
    std::atomic<std::uint64_t> physics_calls=0,presents=0,invalid_times=0;
    std::atomic<float> latest_delta=0,latest_normal=0,latest_complex=0;
    std::atomic<bool> uncapped_active=false;
    std::uint8_t original_lock=1;std::int32_t original_clamp=0;
    float original_normal=1.0f/60.0f,original_complex=1.0f/30.0f;
    std::uintptr_t renderer=0;std::uint32_t original_sync=0;
    explicit State(std::uintptr_t base,fs::path output,bool on):image(base),enabled(on) {
        if(!output.is_absolute()) throw std::runtime_error("Absolute output required");
        session=output/("session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(session);log.open(session/"plugin.log");if(!log) throw std::runtime_error("Output unavailable");
        original_physics=reinterpret_cast<Physics>(image+uncap_evidence::physics_function_rva);
        original_present=reinterpret_cast<Present>(image+uncap_evidence::present_function_rva);
        FILETIME start{},end{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&start,&end,&kernel,&user)) throw std::runtime_error("Process identity");
        std::ofstream current(output/"current-session.json");current<<"{\"processId\":"<<GetCurrentProcessId()<<",\"processStartFileTime\":"<<((static_cast<std::uint64_t>(start.dwHighDateTime)<<32)|start.dwLowDateTime)
            <<",\"sessionDirectory\":\""<<session.generic_string()<<"\"}\n";
        current.close();if(!current) throw std::runtime_error("Session publication");
    }
    template<class T>T& value(std::uintptr_t rva) {return *reinterpret_cast<T*>(image+rva+8);}
    void initialize_settings() {
        original_lock=value<std::uint8_t>(uncap_evidence::lock_rva);original_clamp=value<std::int32_t>(uncap_evidence::clamp_rva);
        original_normal=value<float>(uncap_evidence::max_time_rva);original_complex=value<float>(uncap_evidence::complex_time_rva);
        const auto valid=uncap_policy::scale(1.0f/60.0f,original_normal,original_complex).valid
            && original_lock<=1 && value<std::uint32_t>(uncap_evidence::steps_rva)==3 && value<std::uint32_t>(uncap_evidence::complex_steps_rva)==1;
        settings_ready=valid;
    }
    void restore_times() {
        value<float>(uncap_evidence::max_time_rva)=original_normal;value<float>(uncap_evidence::complex_time_rva)=original_complex;
        value<std::int32_t>(uncap_evidence::clamp_rva)=original_clamp;
    }
    void physics_call(float time,std::uint8_t complex,std::uint8_t unknown) {
        if(settings_ready) {
            if(enabled && world) {
                const auto delta=*reinterpret_cast<const float*>(image+uncap_evidence::unscaled_delta_rva);
                auto t=uncap_policy::scale(delta,original_normal,original_complex);latest_delta=std::isfinite(delta)?delta:0;
                const auto time_scale=*reinterpret_cast<const float*>(image+0x20ccac8);
                const auto minimum_step=*reinterpret_cast<const float*>(image+uncap_evidence::physics_threshold_rva);
                t.valid=t.valid && std::isfinite(time) && std::isfinite(time_scale) && time_scale>=0
                    && (time_scale==0 || (t.normal*time_scale*3>=minimum_step && t.complex*time_scale>=minimum_step));
                valid_physics=t.valid;
                if(t.valid) {
                    value<float>(uncap_evidence::max_time_rva)=t.normal;value<float>(uncap_evidence::complex_time_rva)=t.complex;
                    value<std::int32_t>(uncap_evidence::clamp_rva)=0;
                    latest_normal=t.normal;latest_complex=t.complex;
                } else {++invalid_times;restore_times();}
            } else {restore_times();valid_physics=false;}
        }
        original_physics(time,complex,unknown);++physics_calls;
    }
    void present_call() {
        if(settings_ready) {
            const auto current=*reinterpret_cast<const std::uintptr_t*>(image+uncap_evidence::renderer_rva);
            if(current && current!=renderer) {renderer=current;original_sync=*reinterpret_cast<const std::uint32_t*>(current+0x30);}
            const bool on=enabled && world && valid_physics && current;
            if(!on) restore_times();
            value<std::uint8_t>(uncap_evidence::lock_rva)=on?0:original_lock;
            if(current) *reinterpret_cast<std::uint32_t*>(current+0x30)=on?0:original_sync;
            uncapped_active=on;
        }
        original_present();++presents;
    }
    void report() {
        const auto temp=session/"summary.tmp";std::ofstream out(temp);
        out<<"{\"mode\":\"uncapped-benchmark-with-dynamic-havok-budgets\",\"processId\":"<<GetCurrentProcessId()<<",\"engineWorkOffloaded\":false"
            <<",\"enabled\":"<<(enabled?"true":"false")<<",\"worldLoaded\":"<<(world?"true":"false")<<",\"settingsReady\":"<<(settings_ready?"true":"false")
            <<",\"uncappedActive\":"<<(uncapped_active?"true":"false")<<",\"physicsCalls\":"<<physics_calls<<",\"presents\":"<<presents<<",\"invalidTimes\":"<<invalid_times
            <<",\"lastUnscaledDelta\":"<<latest_delta<<",\"lastMaxTime\":"<<latest_normal<<",\"lastComplexMaxTime\":"<<latest_complex<<"}\n";
        out.close();if(out) report_file::publish(temp,session/"summary.json");
    }
    void run(std::stop_token stop) {
        while(!stop.stop_requested()) {
            try {
                std::error_code error;
                if(fs::exists(session/"mode.txt",error)) {std::ifstream file(session/"mode.txt");std::string mode;file>>mode;if(mode=="off") enabled=false;else if(mode=="uncapped") enabled=true;}
                report();
            } catch(...) { /* A report-file failure must never terminate Skyrim. */ }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
};
void physics(float time,std::uint8_t complex,std::uint8_t unknown) {state.load()->physics_call(time,complex,unknown);}
void present() {state.load()->present_call();}
void message(skse_abi::Message* m) {
    auto* s=state.load();if(!s || !m) return;
    if(m->type==8) s->initialize_settings();
    if(m->type==2) {s->world=false;s->valid_physics=false;}
    if(m->type==3) {s->world=m->data!=nullptr;s->valid_physics=false;}
    if(m->type==7) {s->world=true;s->valid_physics=false;}
}
bool setting_matches(std::uintptr_t base,std::uintptr_t rva,std::uintptr_t name_rva,const char* expected) {
    std::uintptr_t name=0;std::memcpy(&name,reinterpret_cast<const void*>(base+rva+16),8);
    return name==base+name_rva && !std::strcmp(reinterpret_cast<const char*>(name),expected);
}
}
extern "C" __declspec(dllexport) skse_abi::VersionData SKSEPlugin_Version={1,1,"UncappedBenchmark","Local research","",0,0,{uncap_evidence::version,0},0};
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse_abi::Interface* skse) {
    try {
        if(!skse || skse->is_editor || skse->runtime_version!=uncap_evidence::version || !skse->query_interface || !skse->get_plugin_handle || !runtime_guard::hash_matches(uncap_evidence::sha256)) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto matches=[base](std::uintptr_t rva,const auto& bytes){return !std::memcmp(reinterpret_cast<const void*>(base+rva),bytes.data(),bytes.size());};
        if(!matches(uncap_evidence::physics_caller_rva,uncap_evidence::physics_caller_bytes) || !matches(uncap_evidence::physics_function_rva,uncap_evidence::physics_function_bytes)
            || !matches(uncap_evidence::present_function_rva,uncap_evidence::present_function_bytes) || !matches(uncap_evidence::present_call_rva,uncap_evidence::present_call_bytes)
            || !matches(uncap_evidence::timer_rva,uncap_evidence::timer_bytes)) return false;
        if(!setting_matches(base,uncap_evidence::lock_rva,uncap_evidence::lock_name_rva,uncap_evidence::lock_name)
            || !setting_matches(base,uncap_evidence::clamp_rva,uncap_evidence::clamp_name_rva,uncap_evidence::clamp_name)
            || !setting_matches(base,uncap_evidence::max_time_rva,uncap_evidence::max_time_name_rva,uncap_evidence::max_time_name)
            || !setting_matches(base,uncap_evidence::complex_time_rva,uncap_evidence::complex_time_name_rva,uncap_evidence::complex_time_name)
            || !setting_matches(base,uncap_evidence::steps_rva,uncap_evidence::steps_name_rva,uncap_evidence::steps_name)
            || !setting_matches(base,uncap_evidence::complex_steps_rva,uncap_evidence::complex_steps_name_rva,uncap_evidence::complex_steps_name)) return false;
        const auto* trampoline=static_cast<const skse_abi::Trampoline*>(skse->query_interface(7));
        const auto* messaging=static_cast<const skse_abi::Messaging*>(skse->query_interface(5));
        if(!trampoline || trampoline->version<1 || !trampoline->allocate_branch || !messaging || !messaging->register_listener || messaging->version<2) return false;
        HMODULE module=nullptr;wchar_t path[32768],output[32768];
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&physics),&module) || !GetModuleFileNameW(module,path,32768)) return false;
        const auto ini=fs::path(path).replace_extension(L".ini");
        if(!GetPrivateProfileStringW(L"Benchmark",L"OutputDirectory",L"",output,32768,ini.c_str())) return false;
        const auto handle=skse->get_plugin_handle();auto* stub=static_cast<unsigned char*>(trampoline->allocate_branch(handle,28));if(!stub) return false;
        auto instance=std::make_unique<State>(base,output,GetPrivateProfileIntW(L"Benchmark",L"Enabled",1,ini.c_str())!=0);
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&physics),&pinned)) return false;
        state=instance.get();if(!messaging->register_listener(handle,"SKSE",&message)){state=nullptr;return false;}
        const std::array<startup_calls::Call,2> calls{{
            {reinterpret_cast<unsigned char*>(base+uncap_evidence::physics_call_rva),uncap_evidence::physics_call_bytes,reinterpret_cast<void*>(&physics),stub},
            {reinterpret_cast<unsigned char*>(base+uncap_evidence::present_call_rva),uncap_evidence::present_call_bytes,reinterpret_cast<void*>(&present),stub+14}}};
        bool patched=false;const auto ok=startup_calls::install(calls,patched);if(!patched){state=nullptr;return false;}
        auto* retained=instance.release();
        if(!ok){retained->enabled=false;retained->log<<"Hook installation failed; forwarding pinned, uncapping disabled.\n";retained->log.flush();return true;}
        retained->log<<"Exact hash/settings/code checked. Dynamic unscaled Havok budgets before original physics; uncap only after successful world load. Main-frame Present forwarding retained.\n";retained->log.flush();
        retained->reporter=std::jthread([retained](std::stop_token stop){retained->run(stop);});return true;
    } catch(...) {if(auto* s=state.load()){s->enabled=false;return true;}return false;}
}
