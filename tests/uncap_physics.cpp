// Executes this build's original physics-budget machine code in an isolated
// image with private globals. Also tests the plugin controller and Win64 float ABI.
#include "../plugin/uncap_plugin.cpp"
#include <iostream>
#include <limits>
namespace {
void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
bool close(float a,float b) {return std::abs(a-b)<0.000001f;}
std::uint64_t forwarded_physics=0,forwarded_present=0;
float received_time=0;std::uint8_t received_complex=0,received_unknown=0;
void original(float t,std::uint8_t c,std::uint8_t u) {++forwarded_physics;received_time=t;received_complex=c;received_unknown=u;}
void present_original() {++forwarded_present;}
}
int main() {
    try {
        const auto normal=1.0f/60,complex=1.0f/30;
        for(const auto fps:{60.0f,120.0f,240.0f,500.0f}) {
            const auto t=uncap_policy::scale(1/fps,normal,complex);
            const auto physical_fps=(std::min)(fps,240.0f);
            require(t.valid && close(t.normal,1/physical_fps) && close(t.complex,1/(physical_fps-30)),"frame-dependent physics budgets");
        }
        require(uncap_policy::scale(0,normal,complex).valid,"paused time");
        for(const auto delta:{-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::denorm_min()})
            require(!uncap_policy::scale(delta,normal,complex).valid,"invalid timer fallback");
        const auto custom=uncap_policy::scale(1.0f/120,1.0f/240,1.0f/180);
        require(custom.valid && close(custom.normal,1.0f/240) && close(custom.complex,1.0f/180),"custom tighter budgets retained");
        require(close(uncap_policy::scale(1.0f/20,normal,complex).normal,normal),"slow frames retain original maximum budget");
        auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3340000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        require(image,"private test image");
        const auto base=reinterpret_cast<std::uintptr_t>(image);
        std::memcpy(image+uncap_evidence::physics_function_rva,uncap_evidence::physics_function_bytes.data(),uncap_evidence::physics_function_bytes.size());
        std::memcpy(image+uncap_evidence::physics_threshold_rva,uncap_evidence::physics_threshold_bytes.data(),4);
        DWORD protect=0;require(VirtualProtect(image+uncap_evidence::physics_function_rva,uncap_evidence::physics_function_bytes.size(),PAGE_EXECUTE_READ,&protect)!=0,"private original code protection");
        FlushInstructionCache(GetCurrentProcess(),image+uncap_evidence::physics_function_rva,uncap_evidence::physics_function_bytes.size());
        const auto output=fs::temp_directory_path()/("skyrim-uncap-test-"+std::to_string(GetCurrentProcessId()));
        {
            State s(base,output,true);
            s.value<std::uint8_t>(uncap_evidence::lock_rva)=1;s.value<std::int32_t>(uncap_evidence::clamp_rva)=72;
            s.value<float>(uncap_evidence::max_time_rva)=normal;s.value<float>(uncap_evidence::complex_time_rva)=complex;
            s.value<std::uint32_t>(uncap_evidence::steps_rva)=3;s.value<std::uint32_t>(uncap_evidence::complex_steps_rva)=1;
            s.initialize_settings();require(s.settings_ready,"settings initialized");
            auto& delta=*reinterpret_cast<float*>(image+uncap_evidence::unscaled_delta_rva);
            auto& time_scale=*reinterpret_cast<float*>(image+0x20ccac8);
            auto& budget=*reinterpret_cast<float*>(image+0x20d1750);
            auto& elapsed=*reinterpret_cast<float*>(image+0x20d174c);
            auto& carry=*reinterpret_cast<float*>(image+0x3336518);
            auto& consumed=*reinterpret_cast<float*>(image+0x20d1748);
            s.world=true;
            std::uint64_t cases=0;
            for(const auto scale:{0.25f,1.0f,2.0f}) for(const auto fps:{60.0f,120.0f,240.0f,500.0f}) for(const std::uint8_t c:{std::uint8_t{0},std::uint8_t{1}}) {
                time_scale=scale;delta=1/fps;carry=0;
                for(int frame=0;frame<1000;++frame) {
                    const auto previous=carry;
                    s.physics_call(delta*scale,c,0xa5);
                    const auto desired=uncap_policy::scale(delta,normal,complex);
                    require(close(budget,(c?desired.complex:desired.normal)*scale),"original code scales physics budget exactly once");
                    require(close(elapsed,delta*scale),"original code receives scaled frame time unchanged");
                    require(std::isfinite(consumed) && std::isfinite(carry) && close(consumed+carry,elapsed+previous),"original physics-budget time accounting");
                    ++cases;
                }
            }
            s.original_physics=&original;s.original_present=&present_original;
            time_scale=1;
            std::array<unsigned char,64> renderer{};std::uint32_t sync=1;std::memcpy(renderer.data()+0x30,&sync,4);
            *reinterpret_cast<std::uintptr_t*>(image+uncap_evidence::renderer_rva)=reinterpret_cast<std::uintptr_t>(renderer.data());
            delta=1.0f/120;s.physics_call(0.125f,0x79,0xa5);s.present_call();
            require(s.uncapped_active && s.value<std::uint8_t>(uncap_evidence::lock_rva)==0 && s.value<std::int32_t>(uncap_evidence::clamp_rva)==0,"enable engine limiter removal");
            std::memcpy(&sync,renderer.data()+0x30,4);require(sync==0,"disable DXGI sync interval");
            s.enabled=false;s.present_call();std::memcpy(&sync,renderer.data()+0x30,4);
            require(!s.uncapped_active && sync==1 && s.value<std::uint8_t>(uncap_evidence::lock_rva)==1 && s.value<std::int32_t>(uncap_evidence::clamp_rva)==72,"off mode restores limiter, sync and clamp");
            require(close(s.value<float>(uncap_evidence::max_time_rva),normal),"off restores physics settings");
            s.enabled=true;s.world=false;s.physics_call(0.375f,0x79,0xa5);s.present_call();require(!s.uncapped_active,"menu/loading remains capped");
            s.world=true;delta=std::numeric_limits<float>::quiet_NaN();s.physics_call(0.125f,0x79,0xa5);s.present_call();require(!s.uncapped_active && s.invalid_times==1,"invalid timer retains original limiter");
            require(std::isfinite(s.latest_delta.load()),"report remains valid JSON after bad timer");
            time_scale=0.1f;delta=1.0f/240;s.physics_call(delta*time_scale,1,0);s.present_call();
            require(!s.uncapped_active && close(s.value<float>(uncap_evidence::complex_time_rva),complex),"extreme slow motion falls back before shrinking below original minimum step");
            time_scale=1;
            // A real CALL and SKSE-style tail-jump stub exercise mixed XMM0/DL/R8B arguments.
            auto* region=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));require(region,"test CALL region");
            const std::array<unsigned char,14> caller{0x48,0x83,0xec,0x28,0xe8,0x37,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
            std::memcpy(region,caller.data(),caller.size());const std::array<unsigned char,6> jump{0xff,0x25,0,0,0,0};
            std::memcpy(region+64,jump.data(),6);auto* address=reinterpret_cast<void*>(&original);std::memcpy(region+70,&address,8);
            const std::array<startup_calls::Call,1> calls{{{region+4,{0xe8,0x37,0,0,0},reinterpret_cast<void*>(&physics),region+128}}};
            bool patched=false;state=&s;require(startup_calls::install(calls,patched) && patched,"float CALL hook install");
            const auto before=forwarded_physics;delta=1.0f/240;reinterpret_cast<Physics>(region)(0.375f,0x79,0xa5);
            require(forwarded_physics==before+1 && received_time==0.375f && received_complex==0x79 && received_unknown==0xa5,"original called once with intact float and byte arguments");
            state=nullptr;VirtualFree(region,0,MEM_RELEASE);s.report();
            std::cout<<"PASS: "<<cases<<" original-engine physics-budget calls; slow motion; invalid timer fallback; mode/menu restoration; exact float CALL forwarding\n";
        }
        VirtualFree(image,0,MEM_RELEASE);return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
