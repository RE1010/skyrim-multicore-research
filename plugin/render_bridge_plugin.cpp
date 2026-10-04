#define NOMINMAX
#include "skyrim_mc/render_stream.hpp"
#include "render_bridge_call.hpp"
#include "startup_calls.hpp"
#include "runtime_guard.hpp"
#include "bridge_evidence.hpp"
#include "context_slots.hpp"
#include "report_file.hpp"
#include "skse_abi.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <wrl/client.h>
namespace {
namespace fs=std::filesystem;
struct State;std::atomic<State*> state{nullptr};
enum class Mode {off,parallel,parallel_uncached,parallel_full_bindings,parallel_owned_snapshots,parallel_map_uploads,parallel_direct_small,verify,serial,inline_draws,free_draw,original_clean,profile_context};
bool replacing(Mode m) {return m==Mode::parallel || m==Mode::parallel_uncached || m==Mode::parallel_full_bindings || m==Mode::parallel_owned_snapshots || m==Mode::parallel_map_uploads || m==Mode::parallel_direct_small || m==Mode::serial || m==Mode::inline_draws;}
const char* name(Mode m) {switch(m){case Mode::parallel:return "parallel";case Mode::parallel_uncached:return "parallel-uncached";case Mode::parallel_full_bindings:return "parallel-full-bindings";case Mode::parallel_owned_snapshots:return "parallel-owned-snapshots";case Mode::parallel_map_uploads:return "parallel-map-uploads";case Mode::parallel_direct_small:return "parallel-direct-small";case Mode::verify:return "verify-constants";case Mode::serial:return "serial";case Mode::inline_draws:return "inline";case Mode::free_draw:return "free-draw";case Mode::original_clean:return "original-clean";case Mode::profile_context:return "profile-context";default:return "off";}}
std::uint8_t hooked_pass(void*,std::uint32_t*,std::uint32_t*,void*,std::uint32_t);
struct State {
    const std::uintptr_t image;const unsigned workers;
    std::atomic<Mode> mode;std::atomic<bool> world{false},attempted{false},init_error{false};
    std::atomic<std::uint64_t> mode_deadline{0};
    std::atomic<unsigned> profile_sample_every{16};std::uint64_t process_start=0;
    std::atomic<skyrim_mc::stream::Bridge*> bridge{nullptr};
    std::atomic<std::uint64_t> calls{0};
    fs::path session;std::ofstream log;std::jthread reporter;
    State(std::uintptr_t base,fs::path output,unsigned n,bool on):image(base),workers(n),mode(on?Mode::parallel:Mode::off) {
        if(!output.is_absolute()) throw std::runtime_error("Absolute output required");
        session=output/("session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(session);log.open(session/"plugin.log");
        FILETIME created{},exit{},kernel{},user{};
        if(!log || !GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) throw std::runtime_error("Session identity");
        process_start=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        std::ofstream current(output/"current-session.json");
        current<<"{\"processId\":"<<GetCurrentProcessId()<<",\"processStartFileTime\":"<<((static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime)
            <<",\"sessionDirectory\":\""<<session.generic_string()<<"\"}\n";
        current.close();if(!current) throw std::runtime_error("Session output");
    }
    void initialize() noexcept {
        if(attempted.exchange(true)) return;
        try {
            auto* d=*reinterpret_cast<ID3D11Device**>(image+bridge_evidence::device_rva);
            auto* c=*reinterpret_cast<ID3D11DeviceContext**>(image+bridge_evidence::context_rva);
            const auto renderer=*reinterpret_cast<std::uintptr_t*>(image+bridge_evidence::renderer_rva);
            if(!d || !c || !renderer || c!=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x40)) throw std::runtime_error("Verified context identity unavailable");
            Microsoft::WRL::ComPtr<ID3D11Device> owner;c->GetDevice(&owner);if(owner.Get()!=d) throw std::runtime_error("Context/device mismatch");
            auto instance=std::make_unique<skyrim_mc::stream::Bridge>(d,c,workers);
            if(!instance->attach()) throw std::runtime_error("Context4 object alias/attachment unsupported");
            bridge=instance.release();
        } catch(const std::exception& e) {log<<"Bridge initialization refused: "<<e.what()<<'\n';log.flush();init_error=true;mode=Mode::off;}
          catch(...) {init_error=true;mode=Mode::off;}
    }
    void report() {
        const auto temp=session/"summary.tmp";std::ofstream out(temp);auto* b=bridge.load();const auto s=b?b->statistics():skyrim_mc::stream::Statistics{};const auto selected=mode.load();const auto caps=b?b->capabilities():skyrim_mc::stream::DeviceCapabilities{};
        out<<"{\"schemaVersion\":1,\"mode\":\"actual-indexed-draw-worker-replacement\",\"requestedMode\":\""<<name(selected)<<"\",\"processId\":"<<GetCurrentProcessId()<<",\"enabled\":"<<(replacing(selected)?"true":"false")
            <<",\"worldLoaded\":"<<(world?"true":"false")<<",\"contextAttached\":"<<(b?"true":"false")<<",\"initializationError\":"<<(init_error?"true":"false")
            <<",\"processStartFileTime\":"<<process_start<<",\"offloadDisabled\":"<<(s.disabled?"true":"false")
            <<",\"deviceCapabilities\":{\"available\":"<<(b?"true":"false")<<",\"creationFlags\":"<<caps.creation_flags<<",\"featureLevel\":"<<caps.feature_level
            <<",\"threadingQueryHRESULT\":"<<caps.threading_query_result<<",\"threadingQuerySucceeded\":"<<(b && SUCCEEDED(caps.threading_query_result)?"true":"false")
            <<",\"driverCommandLists\":"<<(caps.driver_command_lists?"true":"false")<<",\"driverConcurrentCreates\":"<<(caps.driver_concurrent_creates?"true":"false")<<'}'
            <<",\"drawSuppressionRequested\":"<<(selected==Mode::free_draw?"true":"false")<<",\"drawSuppressionActive\":"<<(s.draw_suppression_active?"true":"false")
            <<",\"suppressedIndexedDraws\":"<<s.suppressed_indexed_draws<<",\"suppressionQueryFallbacks\":"<<s.suppression_query_fallbacks
            <<",\"engineWorkOffloaded\":"<<(s.worker_draws?"true":"false")<<",\"renderPassCalls\":"<<calls<<",\"scopes\":"<<s.scopes<<",\"drawIndexedCalls\":"<<s.draws
            <<",\"replacedDraws\":"<<s.replaced<<",\"workerRecordedDraws\":"<<s.worker_draws<<",\"serialRecordedDraws\":"<<s.serial_draws<<",\"originalDraws\":"<<s.fallback
            <<",\"batches\":"<<s.batches<<",\"errors\":"<<s.errors<<",\"unknownConstants\":"<<s.unknown_constants<<",\"unsupportedDraws\":"<<s.unsupported
            <<",\"foreignContextCalls\":"<<s.foreign_calls<<",\"workerIds\":[";
        for(unsigned i=0;i<workers;++i){if(i)out<<',';out<<s.worker_ids[i];}
        out<<"],\"constantVerificationMode\":"<<(selected==Mode::verify?"true":"false")<<",\"constantChecks\":"<<s.constant_checks<<",\"constantMismatches\":"<<s.constant_mismatches
            <<",\"differentConstantBytes\":"<<s.different_constant_bytes<<",\"firstMismatchSlot\":"<<s.first_mismatch_slot<<",\"firstMismatchOffset\":"<<s.first_mismatch_offset
            <<",\"firstExpectedByte\":"<<s.first_expected_byte<<",\"firstGPUByte\":"<<s.first_gpu_byte
            <<",\"qpcFrequency\":"<<s.qpc_frequency<<",\"captureTicks\":"<<s.capture_ticks<<",\"uploadCopyTicks\":"<<s.upload_copy_ticks
            <<",\"serialRecordTicks\":"<<s.serial_record_ticks<<",\"workerWaitTicks\":"<<s.worker_wait_ticks<<",\"executeTicks\":"<<s.execute_ticks
            <<",\"captureAttempts\":"<<s.capture_attempts<<",\"uploadCopies\":"<<s.upload_copies<<",\"uploadCopyBytes\":"<<s.upload_copy_bytes<<",\"workerRecordTicks\":[";
        for(unsigned i=0;i<workers;++i){if(i)out<<',';out<<s.worker_record_ticks[i];}
        out<<"],\"privateUploads\":"<<s.private_uploads<<",\"privateUploadReuses\":"<<s.private_upload_reuses<<",\"privateUploadBytes\":"<<s.private_upload_bytes
            <<",\"snapshotGetters\":"<<s.snapshot_getters<<",\"stateGroupRefreshes\":"<<s.state_group_refreshes<<",\"stateGroupReuses\":"<<s.state_group_reuses
            <<",\"recordingBindings\":"<<s.recording_bindings<<",\"recordingBindingsSkipped\":"<<s.recording_bindings_skipped
            <<",\"sharedSnapshotBindings\":"<<(s.snapshot_binding_sharing?"true":"false")
            <<",\"snapshotPublishTicks\":"<<s.snapshot_publish_ticks<<",\"queueReleaseTicks\":"<<s.queue_release_ticks
            <<",\"snapshotGroupCopies\":"<<s.snapshot_group_copies<<",\"snapshotGroupReuses\":"<<s.snapshot_group_reuses
            <<",\"flatUploadLookup\":"<<(s.flat_upload_lookup?"true":"false")
            <<",\"temporaryUploadMapEntries\":"<<s.temporary_upload_map_entries<<",\"flatUploadEntries\":"<<s.flat_upload_entries<<",\"drawUploadDuplicates\":"<<s.draw_upload_duplicates
            <<",\"serialFinishTicks\":"<<s.serial_finish_ticks<<",\"workerFinishTicks\":[";
        for(unsigned i=0;i<workers;++i){if(i)out<<',';out<<s.worker_finish_ticks[i];}
        out<<"],\"batchSizeBuckets\":[";
        for(unsigned i=0;i<6;++i) {if(i)out<<',';const auto& bucket=s.batch_sizes[i];out<<"{\"batches\":"<<bucket.batches<<",\"draws\":"<<bucket.draws<<",\"workerBatches\":"<<bucket.worker_batches<<",\"serialBatches\":"<<bucket.serial_batches<<",\"workerDraws\":"<<bucket.worker_draws<<",\"serialDraws\":"<<bucket.serial_draws<<'}';}
        out<<"],\"flushReasons\":{";
        const char* reasons[]{"scopeEnd","capacity","unsupportedDraw","gpuBarrier","foreignCall","shutdown"};
        for(unsigned i=0;i<static_cast<unsigned>(skyrim_mc::stream::FlushReason::count);++i) {if(i)out<<',';out<<'"'<<reasons[i]<<"\":"<<s.flush_reasons[i];}
        out<<"},\"directSmallRequested\":"<<(s.direct_small_batches?"true":"false")<<",\"directSmallAvailable\":"<<(s.direct_small_available?"true":"false")
            <<",\"directSmallDraws\":"<<s.direct_small_draws<<",\"directSmallBatches\":"<<s.direct_small_batch_count<<",\"directSmallTicks\":"<<s.direct_small_ticks
            <<",\"contextProfile\":{\"active\":"<<(s.context_profile_active?"true":"false")<<",\"cleanForwarding\":"<<(s.clean_forwarding?"true":"false")
            <<",\"sampleEvery\":"<<s.profile_sample_every<<",\"qpcFrequency\":"<<s.qpc_frequency
            <<",\"snapshotQPC\":"<<s.profile_snapshot_qpc<<",\"ownerThreadId\":"<<s.context_owner_thread_id
            <<",\"timingSemantics\":\"sampled-owner-thread-original-call-wall-time-includes-driver-and-waits-excludes-adapter-callbacks-lock-and-payload-copy\",\"methods\":[";
        for(unsigned i=0;i<skyrim_mc::stream::context_method_count;++i) {
            const auto& m=s.context_methods[i];if(i)out<<',';
            out<<"{\"slot\":"<<i<<",\"name\":\""<<skyrim_mc::stream::slots::names[i]<<"\",\"category\":\""<<skyrim_mc::stream::slots::categories[i]
                <<"\",\"calls\":"<<m.calls<<",\"samples\":"<<m.samples<<",\"ticks\":"<<m.ticks<<",\"failed\":"<<m.failed<<",\"pending\":"<<m.pending
                <<",\"scopedCalls\":"<<m.scoped_calls<<",\"scopedSamples\":"<<m.scoped_samples<<",\"scopedTicks\":"<<m.scoped_ticks<<'}';
        }
        out<<"],\"mapModes\":{";
        const char* map_names[]{"read","write","readWrite","discard","noOverwrite","doNotWait"};
        for(unsigned i=0;i<6;++i) {if(i)out<<',';out<<'"'<<map_names[i]<<"\":"<<s.map_modes[i];}
        out<<"}},\"pluginVersion\":12,\"scopeRVA\":\"1521289\",\"returnALPreserved\":true}\n";
        out.close();if(out) report_file::publish(temp,session/"summary.json");
    }
    void run(std::stop_token stop) {
        while(!stop.stop_requested()) {
            try {
                std::ifstream file(session/"mode.txt");std::string requested;
                if(file>>requested) {
                    Mode selected=Mode::off;
                    if(requested=="parallel")selected=Mode::parallel;else if(requested=="parallel-uncached")selected=Mode::parallel_uncached;else if(requested=="parallel-full-bindings")selected=Mode::parallel_full_bindings;else if(requested=="parallel-owned-snapshots")selected=Mode::parallel_owned_snapshots;else if(requested=="parallel-map-uploads")selected=Mode::parallel_map_uploads;else if(requested=="parallel-direct-small")selected=Mode::parallel_direct_small;else if(requested=="serial")selected=Mode::serial;else if(requested=="inline")selected=Mode::inline_draws;else if(requested=="verify-constants")selected=Mode::verify;
                    if(requested=="free-draw")selected=Mode::free_draw;
                    if(requested=="original-clean")selected=Mode::original_clean;
                    if(requested=="profile-context")selected=Mode::profile_context;
                    std::uint64_t deadline=0;
                    if(replacing(selected) || selected==Mode::free_draw || selected==Mode::profile_context) {const auto now=GetTickCount64();const auto maximum=selected==Mode::free_draw || selected==Mode::profile_context?10000ull:120000ull;if(!(file>>deadline) || deadline<=now || deadline-now>maximum) {selected=Mode::off;deadline=0;}}
                    unsigned every=16;if(selected==Mode::profile_context && (file>>every) && every!=1 && every!=16){selected=Mode::off;deadline=0;}
                    profile_sample_every=every;
                    mode_deadline=deadline;
                    mode=selected;
                }
                report();
            }catch(...) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    void begin() noexcept {initialize();if(auto* b=bridge.load()){
        const auto selected=mode.load();b->profile_original(world && (selected==Mode::original_clean || selected==Mode::profile_context),selected==Mode::profile_context && world?mode_deadline.load():0,profile_sample_every.load());
        b->verify_constants(selected==Mode::verify && world);
        b->suppress_indexed_draws(selected==Mode::free_draw && world?mode_deadline.load():0);
        b->cache_state(selected!=Mode::parallel_uncached && selected!=Mode::verify);
        b->share_bindings(selected!=Mode::parallel_owned_snapshots);
        b->flat_upload_lookup(selected!=Mode::parallel_map_uploads);
        b->direct_small_batches(selected==Mode::parallel_direct_small);
        const auto recording=selected==Mode::inline_draws?skyrim_mc::stream::Recording::inline_serial:selected==Mode::serial?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers;
        b->begin(replacing(selected) && world,recording,selected!=Mode::parallel_full_bindings);}++calls;}
    void end() noexcept {if(auto* b=bridge.load()) b->end();}
};
std::uint8_t hooked_pass(void* a,std::uint32_t* b,std::uint32_t* c,void* d,std::uint32_t e) {
    auto& s=*state.load();return render_bridge_call::invoke(reinterpret_cast<render_bridge_call::Original>(s.image+bridge_evidence::function_rva),s,a,b,c,d,e);
}
void message(skse_abi::Message* m) {
    if(!m) return;auto* s=state.load();if(!s) return;
    if(m->type==2) s->world=false;if(m->type==3)s->world=m->data!=nullptr;if(m->type==7)s->world=true;
}
}
extern "C" __declspec(dllexport) skse_abi::VersionData SKSEPlugin_Version={1,12,"RenderWorkerBridge","Local research","",0,0,{bridge_evidence::version,0},0};
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse_abi::Interface* skse) {
    try {
        if(!skse || skse->is_editor || skse->runtime_version!=bridge_evidence::version || !skse->query_interface || !skse->get_plugin_handle || !runtime_guard::hash_matches(bridge_evidence::sha256)) return false;
        const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto matches=[image](std::uintptr_t rva,const auto& bytes){return !std::memcmp(reinterpret_cast<const void*>(image+rva),bytes.data(),bytes.size());};
        if(!matches(bridge_evidence::caller_rva,bridge_evidence::caller_bytes) || !matches(bridge_evidence::function_rva,bridge_evidence::function_bytes) || !matches(bridge_evidence::device_creation_rva,bridge_evidence::device_creation_bytes)) return false;
        const auto* t=static_cast<const skse_abi::Trampoline*>(skse->query_interface(7));const auto* m=static_cast<const skse_abi::Messaging*>(skse->query_interface(5));
        if(!t || t->version<1 || !t->allocate_branch || !m || m->version<2 || !m->register_listener) return false;
        HMODULE module=nullptr;wchar_t path[32768],output[32768];
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&hooked_pass),&module) || !GetModuleFileNameW(module,path,32768)) return false;
        const auto ini=fs::path(path).replace_extension(L".ini");if(!GetPrivateProfileStringW(L"Bridge",L"OutputDirectory",L"",output,32768,ini.c_str())) return false;
        const unsigned n=GetPrivateProfileIntW(L"Bridge",L"Workers",4,ini.c_str());if(n<1 || n>4) return false;
        const auto handle=skse->get_plugin_handle();auto* stub=static_cast<unsigned char*>(t->allocate_branch(handle,14));if(!stub) return false;
        auto instance=std::make_unique<State>(image,output,n,GetPrivateProfileIntW(L"Bridge",L"Enabled",0,ini.c_str())!=0);
        HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&hooked_pass),&pin)) return false;
        state=instance.get();if(!m->register_listener(handle,"SKSE",&message)){state=nullptr;return false;}
        const std::array<startup_calls::Call,1> calls{{{reinterpret_cast<unsigned char*>(image+bridge_evidence::call_rva),bridge_evidence::call_bytes,reinterpret_cast<void*>(&hooked_pass),stub}}};
        bool patched=false;const auto okay=startup_calls::install(calls,patched);if(!patched){state=nullptr;return false;}
        auto* retained=instance.release();if(!okay){retained->mode=Mode::off;retained->init_error=true;retained->attempted=true;}
        retained->log<<"Exact EXE, full caller/callee/device creation checked. Five arguments and AL return retained. Per-object immediate context adapter; unsupported draws forward once.\n";retained->log.flush();
        retained->reporter=std::jthread([retained](std::stop_token stop){retained->run(stop);});return true;
    }catch(...) {if(auto* retained=state.load()){retained->mode=Mode::off;return true;}return false;}
}
