#define NOMINMAX
#include "render_probe.hpp"
#include "startup_calls.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>
namespace {
struct Packet {std::uintptr_t shader=0x1234,property=0x2345,geometry=0x3456;std::array<unsigned char,8> tail{};std::uint64_t touches=0;};
static_assert(offsetof(Packet,tail)==0x18);
std::atomic<std::uint64_t> original_calls{0},bad_arguments{0};
std::uintptr_t shader_state=0,material_state=0;
std::uint32_t technique_state=0;
bool update_globals=false;
void original(void* raw,std::uint32_t a,std::uint32_t b,std::uint32_t c) {
    ++original_calls;
    if(a!=0xfedcba98 || b!=0x123456ab || c!=0x89abcdef) ++bad_arguments;
    auto* p=static_cast<Packet*>(raw);++p->touches;
    if(update_globals) {shader_state=p->shader;material_state=p->property;technique_state=a;}
}
void throwing(void*,std::uint32_t,std::uint32_t,std::uint32_t) {throw std::runtime_error("original failure");}
render_probe::Probe* forwarded=nullptr;
void callback(void* p,std::uint32_t a,std::uint32_t b,std::uint32_t c) {forwarded->call(p,a,b,c);}
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void call(render_probe::Probe& p,Packet& packet) {p.call(&packet,0xfedcba98,0x123456ab,0x89abcdef);}
}
int main() {
    try {
        Packet packet;packet.tail[6]=0x5a;
        render_probe::Probe probe(&original,16,8);
        probe.shader_slot=&shader_state;probe.material_slot=&material_state;probe.technique_slot=&technique_state;
        for(int i=0;i<22;++i) call(probe,packet);
        require(original_calls==22 && probe.reserved==0,"inactive forwarding");
        update_globals=true;probe.enabled=true;
        for(int i=0;i<1000;++i) call(probe,packet);
        probe.enabled=false;update_globals=false;
        require(original_calls==1022 && packet.touches==1022 && !bad_arguments,"original exactly once, all integer arguments retained");
        require(probe.reserved==63 && probe.completed_samples==8 && probe.dropped==55 && !probe.active,"sampling/capacity/quiescence");
        const auto& first=probe.records[0];
        require(first.sequence==1 && first.flags==0x5a && first.technique==0xfedcba98 && first.arg3==0x123456ab && first.arg4==0x89abcdef && first.begin && first.end>=first.begin,"record fields and clock");
        require(!first.shader_before && first.shader_after==render_probe::token(packet.shader,probe.salt) && first.material_after==render_probe::token(packet.property,probe.salt),"before/after copied state");
        call(probe,packet);require(probe.reserved==63 && original_calls==1023,"post-stop original forwarding");
        render_probe::Probe failed(&throwing,1,8);failed.enabled=true;
        try {call(failed,packet);require(false,"original exception lost");} catch(const std::runtime_error&) {}
        require(!failed.active && failed.completed_samples==0 && failed.counters[0].aborted==1,"exception cleanup without false completed record");
        render_probe::Probe parallel(&original,16,1024);parallel.enabled=true;
        std::array<Packet,4> packets;std::vector<std::jthread> threads;
        for(std::size_t i=0;i<packets.size();++i) threads.emplace_back([&,i]{for(int j=0;j<1000;++j) call(parallel,packets[i]);});
        threads.clear();parallel.enabled=false;
        require(!parallel.active && parallel.completed_samples==252 && parallel.reserved==252 && !parallel.dropped && !parallel.overflow_threads && !bad_arguments,"concurrent records");
        for(const auto& p:packets) require(p.touches==1000,"thread original count");
        for(std::size_t i=0;i<252;++i) require(parallel.records[i].end>=parallel.records[i].begin && parallel.records[i].begin && parallel.records[i].thread_id,"record published before active zero");
        // Exercise a real Win64 CALL rewrite and tail-jump stub in executable
        // memory, preserving all four arguments and the caller's shadow space.
        auto* region=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));require(region,"executable test memory");
        const std::array<unsigned char,14> caller{0x48,0x83,0xec,0x28,0xe8,0x37,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        std::memcpy(region,caller.data(),caller.size());
        const std::array<unsigned char,6> jump{0xff,0x25,0,0,0,0};std::memcpy(region+64,jump.data(),6);
        auto* original_address=reinterpret_cast<void*>(&original);std::memcpy(region+70,&original_address,8);FlushInstructionCache(GetCurrentProcess(),region,4096);
        render_probe::Probe hooked(&original,1,8);forwarded=&hooked;
        const std::array<unsigned char,5> expected{0xe8,0x37,0,0,0};
        auto incorrect=expected;incorrect[1]++;
        const std::array<startup_calls::Call,1> wrong{{{region+4,incorrect,reinterpret_cast<void*>(&callback),region+128}}};
        bool patched=false;require(!startup_calls::install(wrong,patched) && !patched && !std::memcmp(region,caller.data(),caller.size()),"conflict must not patch");
        const std::array<startup_calls::Call,1> correct{{{region+4,expected,reinterpret_cast<void*>(&callback),region+128}}};
        require(startup_calls::install(correct,patched) && patched,"single CALL install");
        const auto entry=reinterpret_cast<render_probe::Original>(region);const auto calls_before=original_calls.load();
        entry(&packet,0xfedcba98,0x123456ab,0x89abcdef);
        hooked.enabled=true;entry(&packet,0xfedcba98,0x123456ab,0x89abcdef);hooked.enabled=false;
        require(original_calls==calls_before+2 && hooked.completed_samples==1 && !bad_arguments,"patched CALL synchronous exact forwarding");
        VirtualFree(region,0,MEM_RELEASE);
        std::cout<<"PASS: original exactly once; all integer arguments; sampled state; bounded drops; exception cleanup; 4 concurrent callers; real single CALL patch/conflict checks\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
