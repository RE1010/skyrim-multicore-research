#define NOMINMAX
#include "movement_probe.hpp"
#include "startup_calls.hpp"
#include "runtime_evidence.hpp"
#include "getter_evidence.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <string>
#include <iomanip>
#include <intrin.h>
extern "C" unsigned long _tls_index;

namespace {
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
movement_probe::Probe* current=nullptr;
__declspec(thread) int test_epoch=INT32_MIN;
void init_header(int* guard) {if(*guard==0) *guard=-1;else test_epoch=*guard;}
void init_footer(int* guard) {*guard=-2;test_epoch=-2;}
bool search_hook(void* object) {return current->call(object);}
void* lock_hook(void* guard,void* lock) {return current->lock(guard,lock);}
struct Message {void** vtable;std::uint32_t type;};
std::uint32_t get_type(void* item) {return static_cast<Message*>(item)->type;}
std::uint32_t resolve_type(std::uint32_t* id,const char*,std::uint32_t) {InterlockedExchange(reinterpret_cast<volatile LONG*>(id),77);return 77;}
struct Fixture {
    unsigned char* code;
    alignas(8) std::array<unsigned char,0x170> controller{};
    std::array<void*,2> vtable{nullptr,reinterpret_cast<void*>(&get_type)};
    Message other{vtable.data(),11},wanted{vtable.data(),77};
    std::vector<Message*> items;
    movement_probe::Search original,caller;
    movement_probe::Acquire acquire;
    std::unique_ptr<movement_probe::Probe> probe;
    explicit Fixture(std::uint32_t every=1,std::size_t capacity=65536) {
        code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        check(code!=nullptr,"allocation");
        std::memcpy(code,runtime_evidence::movement_bytes.data(),runtime_evidence::movement_bytes.size());
        std::memcpy(code+0x1000,runtime_evidence::movement_lock_bytes.data(),runtime_evidence::movement_lock_bytes.size());
        auto rip=[&](std::size_t instruction,std::size_t displacement,std::size_t length,const void* target) {
            std::int32_t value=0;check(startup_calls::rel32(reinterpret_cast<std::uintptr_t>(code+instruction+length),reinterpret_cast<std::uintptr_t>(target),value),"test relocation");
            std::memcpy(code+instruction+displacement,&value,4);
        };
        auto slot=[&](std::size_t offset,void* target) {std::memcpy(code+offset,&target,8);};
        slot(0x2010,reinterpret_cast<void*>(&GetCurrentThreadId));slot(0x2018,reinterpret_cast<void*>(&Sleep));
        *reinterpret_cast<std::uint32_t*>(code+0x2000)=77;
        const unsigned char jump[]{0xff,0x25,0,0,0,0};std::memcpy(code+0x3100,jump,6);slot(0x3106,reinterpret_cast<void*>(&resolve_type));
        rip(0x2b,1,5,code+0x1000); // original lock constructor
        rip(0x5d,2,6,code+0x2000);rip(0x6a,3,7,code+0x2040);rip(0x71,3,7,code+0x2000);
        rip(0x78,1,5,code+0x3100);rip(0x7d,2,6,code+0x2000);rip(0xa4,2,6,code+0x2010);
        rip(0x1018,2,6,code+0x2010);rip(0x1074,2,6,code+0x2018);
        const unsigned char caller_code[]{0x48,0x83,0xec,0x28,0xe8,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        std::memcpy(code+0x400,caller_code,sizeof(caller_code));rip(0x404,1,5,code);
        original=reinterpret_cast<movement_probe::Search>(code);caller=reinterpret_cast<movement_probe::Search>(code+0x400);
        acquire=reinterpret_cast<movement_probe::Acquire>(code+0x1000);
        probe=std::make_unique<movement_probe::Probe>(original,acquire,every,capacity);current=probe.get();
        DWORD old=0;check(VirtualProtect(code,0x2000,PAGE_EXECUTE_READ,&old)!=0,"code protection");
        FlushInstructionCache(GetCurrentProcess(),code,0x4000);
    }
    ~Fixture() {current=nullptr;probe.reset();VirtualFree(code,0,MEM_RELEASE);}
    void list(std::vector<Message*> values) {
        items=std::move(values);auto* pointer=items.data();const auto size=static_cast<std::uint32_t>(items.size());
        std::memcpy(controller.data()+0x158,&pointer,8);std::memcpy(controller.data()+0x168,&size,4);
    }
    std::array<startup_calls::Call,2> calls() {
        std::array<unsigned char,5> outer{},lock{};std::memcpy(outer.data(),code+0x404,5);std::memcpy(lock.data(),code+0x2b,5);
        return {{{code+0x404,outer,reinterpret_cast<void*>(&search_hook),code+0x3000},
            {code+0x2b,lock,reinterpret_cast<void*>(&lock_hook),code+0x300e}}};
    }
    void install() {auto sites=calls();bool patched=false;check(startup_calls::install(sites,patched) && patched,"startup installation");}
    void assert_unlocked() {
        std::uint64_t value=1;std::memcpy(&value,controller.data()+0x150,8);check(value==0,"original lock not released");
    }
    void* cached_getter(std::size_t offset,std::size_t slot_offset) {
        const auto it=std::find_if(getter_evidence::entries.begin(),getter_evidence::entries.end(),[](const auto& e){return e.function_rva==0x11d5d30;});
        check(it!=getter_evidence::entries.end(),"actual NewPath getter missing");
        DWORD old=0;check(VirtualProtect(code+offset,47,PAGE_EXECUTE_READWRITE,&old)!=0,"getter protection");
        std::memcpy(code+offset,it->bytes.data(),47);
        auto rip=[&](std::size_t at,std::size_t disp,std::size_t length,unsigned char* target) {
            std::int32_t value=0;check(startup_calls::rel32(reinterpret_cast<std::uintptr_t>(code+offset+at+length),reinterpret_cast<std::uintptr_t>(target),value),"getter relocation");
            std::memcpy(code+offset+at+disp,&value,4);
        };
        rip(4,2,6,code+slot_offset);rip(17,3,7,code+0x2040);rip(24,3,7,code+slot_offset);
        rip(31,1,5,code+0x3100);rip(36,2,6,code+slot_offset);
        check(VirtualProtect(code+offset,47,old,&old)!=0,"getter restore");FlushInstructionCache(GetCurrentProcess(),code+offset,47);return code+offset;
    }
    void* leaf(std::size_t offset,std::span<const unsigned char> bytes) {
        DWORD old=0;check(VirtualProtect(code+offset,bytes.size(),PAGE_EXECUTE_READWRITE,&old)!=0,"leaf protection");
        std::memcpy(code+offset,bytes.data(),bytes.size());check(VirtualProtect(code+offset,bytes.size(),old,&old)!=0,"leaf restore");
        FlushInstructionCache(GetCurrentProcess(),code+offset,bytes.size());return code+offset;
    }
};
bool throwing_search(void*) {throw std::runtime_error("expected-original-exception");}
bool error_search(void*) {SetLastError(321);return true;}
void* dummy_acquire(void* guard,void*) {return guard;}
struct FastFixture {
    Fixture f;
    std::array<std::array<void*,2>,5> tables{};
    std::array<Message,5> messages{};
    std::unique_ptr<movement_fast::Table> table;
    explicit FastFixture(std::uint32_t sample=1,std::size_t capacity=65536):f(sample,capacity) {
        tables[0][1]=f.cached_getter(0x600,0x2020);tables[1][1]=f.cached_getter(0x680,0x2000);
        *reinterpret_cast<std::uint32_t*>(f.code+0x2020)=11;
        const std::array<unsigned char,6> constant{0xb8,11,0,0,0,0xc3};
        const std::array<unsigned char,4> field{0x8b,0x41,8,0xc3};
        tables[2][1]=f.leaf(0x700,constant);tables[3][1]=f.leaf(0x720,field);
        std::array<unsigned char,7> global{0x8b,0x05,0,0,0,0,0xc3};
        const std::int32_t disp=0x2020-0x740-6;std::memcpy(global.data()+2,&disp,4);
        tables[4][1]=f.leaf(0x740,global);
        for(std::size_t i=0;i<messages.size();++i) messages[i]={tables[i].data(),11};
        const std::array<movement_fast::Getter,5> entries{{
            {reinterpret_cast<std::uintptr_t>(tables[0][1]),reinterpret_cast<std::uintptr_t>(f.code+0x2020),movement_fast::Mode::CachedGlobal},
            {reinterpret_cast<std::uintptr_t>(tables[1][1]),reinterpret_cast<std::uintptr_t>(f.code+0x2000),movement_fast::Mode::CachedGlobal},
            {reinterpret_cast<std::uintptr_t>(tables[2][1]),11,movement_fast::Mode::Constant},
            {reinterpret_cast<std::uintptr_t>(tables[3][1]),8,movement_fast::Mode::Object},
            {reinterpret_cast<std::uintptr_t>(tables[4][1]),reinterpret_cast<std::uintptr_t>(f.code+0x2020),movement_fast::Mode::Global}}};
        table=std::make_unique<movement_fast::Table>(entries);f.probe->fast_table=table.get();
        f.probe->target_slot=reinterpret_cast<std::uint32_t*>(f.code+0x2000);
        f.probe->module_base=reinterpret_cast<std::uintptr_t>(f.code);f.install();f.probe->enabled=true;
    }
    void list(std::uint32_t size,bool mixed) {
        std::vector<Message*> values(size,&messages[0]);
        if(mixed) for(std::size_t i=0;i<values.size();++i) values[i]=i%5==4?nullptr:&messages[std::array<std::size_t,4>{0,2,3,4}[i%5]];
        f.list(std::move(values));
    }
    void guarded_getter() {
        const auto it=std::find_if(getter_evidence::entries.begin(),getter_evidence::entries.end(),[](const auto& e){return e.function_rva==0x7a6b10;});
        check(it!=getter_evidence::entries.end(),"live guarded getter missing");
        f.leaf(0x800,std::span(it->bytes.data(),it->length));
        DWORD old=0;check(VirtualProtect(f.code+0x800,it->length,PAGE_EXECUTE_READWRITE,&old)!=0,"guarded getter protection");
        auto rip=[&](std::size_t at,std::size_t displacement,std::size_t length,void* target) {
            std::int32_t value=0;check(startup_calls::rel32(reinterpret_cast<std::uintptr_t>(f.code+0x800+at+length),reinterpret_cast<std::uintptr_t>(target),value),"guarded relocation");
            std::memcpy(f.code+0x800+at+displacement,&value,4);
        };
        // Preserve the actual GS/TLS fast branch; relocate TLS index and offset
        // to this test executable's own thread-local epoch. Cold helper calls
        // are controlled fixtures, not the game's global CRT runtime.
        const auto tls=reinterpret_cast<void**>(__readgsqword(0x58));
        const auto offset=reinterpret_cast<std::uintptr_t>(&test_epoch)-reinterpret_cast<std::uintptr_t>(tls[_tls_index]);
        check(offset<=UINT32_MAX,"test TLS epoch offset");
        const auto epoch_offset=static_cast<std::uint32_t>(offset);
        std::memcpy(f.code+0x800+0x1d,&epoch_offset,4);
        *reinterpret_cast<std::uint32_t*>(f.code+0x2028)=_tls_index;
        const unsigned char jump[]{0xff,0x25,0,0,0,0};
        std::memcpy(f.code+0x3140,jump,6);auto* header=reinterpret_cast<void*>(&init_header);std::memcpy(f.code+0x3146,&header,8);
        std::memcpy(f.code+0x3150,jump,6);auto* footer=reinterpret_cast<void*>(&init_footer);std::memcpy(f.code+0x3156,&footer,8);
        rip(0x0d,2,6,f.code+0x2028);rip(0x28,2,6,f.code+0x2024);rip(0x30,2,6,f.code+0x2020);
        rip(0x3b,3,7,f.code+0x2024);rip(0x42,1,5,f.code+0x3140);rip(0x47,2,7,f.code+0x2024);
        rip(0x53,3,7,f.code+0x2040);rip(0x5a,3,7,f.code+0x2020);rip(0x61,1,5,f.code+0x3100);
        rip(0x66,2,6,f.code+0x2020);rip(0x6c,3,7,f.code+0x2024);rip(0x73,1,5,f.code+0x3150);rip(0x78,2,6,f.code+0x2020);
        check(VirtualProtect(f.code+0x800,it->length,old,&old)!=0,"guarded getter restore");
        FlushInstructionCache(GetCurrentProcess(),f.code+0x800,it->length);
        tables[0][1]=f.code+0x800;messages[0].vtable=tables[0].data();
        const std::array<movement_fast::Getter,1> entries{{{reinterpret_cast<std::uintptr_t>(tables[0][1]),
            reinterpret_cast<std::uintptr_t>(f.code+0x2020),movement_fast::Mode::GuardedGlobal,reinterpret_cast<std::uintptr_t>(f.code+0x2024)}}};
        table=std::make_unique<movement_fast::Table>(entries);f.probe->fast_table=table.get();
        *reinterpret_cast<std::int32_t*>(f.code+0x2024)=-2;test_epoch=INT32_MIN;
    }
};
int fast_equivalence() {
    std::size_t cases=0;
    {
        FastFixture test;
        for(const auto size:{0u,1u,2u,33u,1000u,10000u,50755u}) for(int where:{-1,0,1,2}) {
            test.list(size,true);const bool expected=size && where>=0;
            if(expected) test.f.items[where==0?0:where==1?size/2:size-1]=&test.messages[1];
            const auto original=test.f.original(test.f.controller.data());
            check(original==expected && test.f.caller(test.f.controller.data())==original,"fast/original result mismatch");
            const auto& r=test.f.probe->records[cases++];
            check(r.fast_status==0 && r.fast_result==r.result && r.function_ticks>=r.fast_ticks+r.lock_ticks,"fast record/timing invalid");
            test.f.assert_unlocked();
        }
        test.f.list({&test.messages[3]});
        for(const auto type:{11u,77u,11u}) {
            test.messages[3].type=type;check(test.f.caller(test.f.controller.data())==(type==77),"same-size same-address type change stale");++cases;
        }
        check(test.f.probe->fast_mismatches.load()==0,"unexpected shadow mismatch");
        test.f.list({&test.f.other});check(!test.f.caller(test.f.controller.data()),"unknown getter changed original result");
        check(test.f.probe->records[cases++].fast_status==1,"unknown getter not rejected");
        *reinterpret_cast<std::uint32_t*>(test.f.code+0x2020)=0;test.f.list({&test.messages[0]});
        check(test.f.caller(test.f.controller.data()),"cold getter original resolver missing");
        check(test.f.probe->records[cases++].fast_status==2,"cold getter not rejected");
        *reinterpret_cast<std::uint32_t*>(test.f.code+0x2020)=11;
        *reinterpret_cast<std::uint32_t*>(test.f.code+0x2000)=0;
        check(!test.f.caller(test.f.controller.data()),"cold target original resolver changed");
        check(test.f.probe->records[cases++].fast_status==3,"cold target not rejected");
        *reinterpret_cast<std::uint32_t*>(test.f.code+0x2000)=0;test.f.list({nullptr,nullptr});
        check(!test.f.caller(test.f.controller.data()) && movement_fast::read32(test.f.code+0x2000)==0,"all-null lazy-init side effect");++cases;
        test.f.assert_unlocked();
    }
    {
        FastFixture test;test.guarded_getter();test.list(33,false);
        std::size_t guarded_record=0;
        for(const auto type:{11u,77u}) {
            *reinterpret_cast<std::uint32_t*>(test.f.code+0x2020)=type;
            check(test.f.caller(test.f.controller.data())==(type==77),"guarded getter result mismatch");
            check(test.f.probe->records[guarded_record++].fast_status==0,"guarded getter unsupported");++cases;
        }
        *reinterpret_cast<std::int32_t*>(test.f.code+0x2024)=0;test_epoch=INT32_MIN;
        check(test.f.caller(test.f.controller.data()),"guarded cold resolver missing");
        check(test.f.probe->records[2].fast_status==2,"guarded cold path accepted");++cases;
        check(test.f.caller(test.f.controller.data()) && test.f.probe->records[3].fast_status==0,"initialized guarded path unsupported");++cases;
        test.f.assert_unlocked();
        check(test.f.probe->fast_mismatches.load()==0,"guarded original comparison failed");
    }
    {
        FastFixture test(7,128);test.list(257,true);test.f.items.back()=&test.messages[1];
        std::atomic<bool> wrong=false;std::vector<std::jthread> threads;
        for(int t=0;t<8;++t) threads.emplace_back([&]{for(int i=0;i<500;++i) if(!test.f.caller(test.f.controller.data())) wrong=true;});
        threads.clear();test.f.probe->enabled=false;
        check(!wrong && test.f.probe->fast_compared.load()==576 && !test.f.probe->fast_mismatches.load(),"contended fast shadow failed");
        test.f.assert_unlocked();cases+=4000;
    }
    {
        FastFixture test;test.f.list({&test.messages[2]});
        const std::array<movement_fast::Getter,1> bad{{{reinterpret_cast<std::uintptr_t>(test.tables[2][1]),77,movement_fast::Mode::Constant}}};
        movement_fast::Table bad_table(bad);test.f.probe->fast_table=&bad_table;
        check(!test.f.caller(test.f.controller.data()),"injected prediction changed engine return");
        check(test.f.probe->fast_mismatches.load()==1 && !test.f.probe->fast_allowed.load(),"mismatch did not disable candidate");
        check(!test.f.caller(test.f.controller.data()) && test.f.probe->records[1].fast_status==4,"disabled candidate still ran");
        cases+=2;
    }
    std::cout<<"PASS: "<<cases<<" original-machine-code decisions; verified getter modes, same-length mutations, cold/unknown fallback, contended original lock, mismatch disables candidate\n";return 0;
}
int fast_benchmark() {
    std::cout<<std::setprecision(10)<<"{\"kind\":\"paired-lab-shadow-timings\",\"rows\":[";bool first=true;
    for(int kind:{0,1,2}) for(const auto size:{1024u,10000u,50755u}) {
        const bool mixed=kind==1;
        FastFixture test(1,500);if(kind==2) test.guarded_getter();test.list(size,mixed);
        for(int i=0;i<300;++i) check(!test.f.caller(test.f.controller.data()),"benchmark original mismatch");
        double fast=0,original=0;LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        for(std::size_t i=50;i<300;++i) {
            const auto& r=test.f.probe->records[i];check(r.fast_status==0 && r.fast_result==r.result,"benchmark unsupported");
            fast+=static_cast<double>(r.fast_ticks);original+=static_cast<double>(r.function_ticks-r.fast_ticks);
        }
        fast=fast/250/static_cast<double>(frequency.QuadPart)*1e6;original=original/250/static_cast<double>(frequency.QuadPart)*1e6;
        if(!first) std::cout<<',';first=false;std::cout<<"{\"listSize\":"<<size<<",\"getterKind\":"<<kind<<",\"mixed\":"<<(mixed?"true":"false")
            <<",\"samples\":250,\"candidateMeanMicroseconds\":"<<fast<<",\"originalEstimateMeanMicroseconds\":"<<original<<",\"ratio\":"<<original/fast<<'}';
    }
    std::cout<<"],\"limitation\":\"Original estimate includes lock and instrumentation; candidate runs first and affects cache state; no game FPS claim.\"}\n";return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--fast-equivalence") return fast_equivalence();
        if(argc==2 && std::string(argv[1])=="--fast-benchmark") return fast_benchmark();
        std::int32_t value=0;
        check(!startup_calls::rel32(0x1000,0x90000000,value),"out-of-range branch accepted");
        check(startup_calls::rel32(0x80001000,0x1000,value) && value==INT32_MIN,"negative branch boundary");
        {
            Fixture f;auto sites=f.calls();sites[1].expected[0]=0x90;bool patched=true;
            check(!startup_calls::install(sites,patched) && !patched,"mismatched second call accepted");
            check(!std::memcmp(f.code+0x404,sites[0].expected.data(),5),"partial write on refusal");
            f.list({&f.other});check(!f.caller(f.controller.data()),"original baseline");f.assert_unlocked();
        }
        std::size_t cases=0;
        {
            Fixture f;f.install();f.probe->enabled=true;
            for(std::uint32_t size: {0u,1u,2u,17u,1000u,10000u}) {
                for(int position: {-1,0,1,2}) {
                    std::vector<Message*> list(size,&f.other);
                    if(size>1) list[1]=nullptr;
                    const bool expected=position>=0 && size>0;
                    if(expected) list[position==0?0:position==1?size/2:size-1]=&f.wanted;
                    f.list(std::move(list));
                    const bool baseline=f.original(f.controller.data());
                    check(baseline==expected,"machine-code baseline unexpected");
                    const bool measured=f.caller(f.controller.data());
                    check(measured==baseline,"original decision changed");f.assert_unlocked();
                    const auto& r=f.probe->records[cases];
                    check(r.result==static_cast<std::uint32_t>(expected) && r.list_size==size && r.lock_observations==1,"under-lock metadata incorrect");
                    check(r.function_ticks>=r.lock_ticks,"timing range");++cases;
                }
            }
            // Exercise the original lazy type resolver branch too.
            *reinterpret_cast<std::uint32_t*>(f.code+0x2000)=0;
            f.list({nullptr,&f.wanted});check(f.caller(f.controller.data()),"lazy type path changed");++cases;f.assert_unlocked();
            // Original lock constructor is recursive: outer ownership survives.
            void* guard=nullptr;f.acquire(&guard,f.controller.data()+0x150);
            check(f.caller(f.controller.data()),"recursive original call changed");++cases;
            auto* lock=reinterpret_cast<volatile LONG*>(f.controller.data()+0x150);
            check(lock[0]==static_cast<LONG>(GetCurrentThreadId()) && lock[1]==1,"recursive ownership changed");
            lock[0]=0;MemoryBarrier();check(InterlockedCompareExchange(lock+1,0,1)==1,"test outer unlock");f.assert_unlocked();
            f.probe->enabled=false;
            check(f.caller(f.controller.data()) && f.probe->record_attempts.load()==cases,"inactive passthrough recorded");
        }
        {
            Fixture f(7,128);f.install();f.list(std::vector<Message*>(257,&f.other));f.items.back()=&f.wanted;
            f.probe->enabled=true;std::atomic<bool> wrong=false;std::vector<std::jthread> threads;
            for(int t=0;t<8;++t) threads.emplace_back([&]{for(int i=0;i<500;++i) if(!f.caller(f.controller.data())) wrong=true;});
            threads.clear();f.probe->enabled=false;f.assert_unlocked();
            check(!wrong && !f.probe->active.load(),"concurrent original call failed");
            std::uint64_t completed=0,positive=0;
            for(const auto& c:f.probe->counters) {completed+=c.completed.load();positive+=c.positive.load();check(c.aborted.load()==0,"unexpected abort");}
            check(completed==4000 && positive==4000,"all-call counters");
            check(f.probe->record_attempts.load()==8*72,"per-thread sampling count");
            for(const auto& r:f.probe->records) check(r.list_size==257 && r.lock_observations==1 && r.result==1,"bounded concurrent record invalid");
            cases+=4000;
        }
        {
            movement_probe::Probe p(throwing_search,dummy_acquire);p.enabled=true;bool propagated=false;
            try {p.call(nullptr);} catch(const std::runtime_error&) {propagated=true;}
            check(propagated && !p.active.load() && movement_probe::current_frame==nullptr,"exception cleanup/propagation");
            p.search=error_search;check(p.call(nullptr) && GetLastError()==321,"original last-error changed");
        }
        std::cout<<"PASS: "<<cases<<" original-machine-code searches, real recursive/contended lock, startup branch guards, bounded sampling, exception/last-error preservation\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
