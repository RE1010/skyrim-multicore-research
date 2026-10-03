#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include "movement_fast.hpp"
namespace movement_probe {
using Search=bool(*)(void*);
using Acquire=void*(*)(void*,void*);
inline std::uint64_t ticks() noexcept {LARGE_INTEGER value{};QueryPerformanceCounter(&value);return static_cast<std::uint64_t>(value.QuadPart);}
inline std::uint64_t token(std::uintptr_t address,std::uint64_t salt) noexcept {
    auto x=static_cast<std::uint64_t>(address)^salt;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
struct Record {
    std::uint64_t sequence,controller_token,function_ticks,lock_ticks,fast_ticks;
    std::uint32_t thread_id,list_size,lock_observations,result,fast_status,fast_result,inspected,nonnull,unsupported_getter_rva,reserved;
};
static_assert(sizeof(Record)==80);
struct alignas(64) Counter {
    std::atomic<std::uint32_t> thread_id{0};
    std::uint32_t reserved=0;
    std::atomic<std::uint64_t> completed{0},positive{0},negative{0},aborted{0};
    std::array<unsigned char,24> padding{};
};
static_assert(sizeof(Counter)==64);
struct Frame {
    void* controller;std::uint32_t size=0,locks=0;std::uint64_t lock_ticks=0,fast_ticks=0;
    movement_fast::Result fast{};std::uint32_t fast_status=4;
};
inline thread_local Frame* current_frame=nullptr;
struct FrameScope {
    Frame* previous;
    explicit FrameScope(Frame* frame):previous(current_frame) {current_frame=frame;}
    ~FrameScope() {current_frame=previous;}
};
struct Probe {
    Search search;Acquire acquire;
    const std::uint32_t sample_every;
    const std::uint64_t salt;
    std::array<Counter,128> counters{};
    std::vector<Record> records;
    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> active{0},record_attempts{0},overflow_calls{0};
    const movement_fast::Table* fast_table=nullptr;
    const std::uint32_t* target_slot=nullptr;
    std::uintptr_t module_base=0;
    std::atomic<bool> fast_allowed{true};
    std::atomic<std::uint64_t> fast_compared{0},fast_mismatches{0},fast_unsupported{0};
    Probe(Search original_search,Acquire original_acquire,std::uint32_t sampling=64,std::size_t capacity=65536):
        search(original_search),acquire(original_acquire),sample_every(sampling),salt(ticks()),records(capacity) {}
    Counter* counter(std::uint32_t id) noexcept {
        for(auto& c:counters) {
            auto owner=c.thread_id.load(std::memory_order_relaxed);
            if(owner==id) return &c;
            if(!owner && c.thread_id.compare_exchange_strong(owner,id)) return &c;
        }
        return nullptr;
    }
    bool call(void* controller) {
        if(!enabled.load(std::memory_order_acquire)) return search(controller);
        active.fetch_add(1);
        if(!enabled.load(std::memory_order_acquire)) {active.fetch_sub(1);return search(controller);}
        struct Exit {
            Probe& probe;Counter* count=nullptr;bool completed=false;
            ~Exit() {if(count && !completed) count->aborted.fetch_add(1,std::memory_order_relaxed);probe.active.fetch_sub(1);}
        } exit{*this};
        const auto incoming_error=GetLastError();
        struct Cached {Probe* probe=nullptr;Counter* counter=nullptr;std::uint64_t sequence=0;std::uint32_t id=0;std::uint64_t salt=0;};
        static thread_local Cached cached;
        if(cached.probe!=this || cached.salt!=salt) {cached={this,nullptr,0,GetCurrentThreadId(),salt};cached.counter=counter(cached.id);}
        exit.count=cached.counter;
        if(!cached.counter) {overflow_calls.fetch_add(1,std::memory_order_relaxed);SetLastError(incoming_error);return search(controller);}
        const auto sequence=++cached.sequence;
        Frame frame{controller};
        const bool sampled=(sequence-1)%sample_every==0;
        bool result=false;std::uint64_t start=0,elapsed=0;DWORD resulting_error=0;
        {
            FrameScope scope(sampled?&frame:nullptr);
            if(sampled) start=ticks();
            SetLastError(incoming_error);
            result=search(controller); // exactly one authoritative original call
            resulting_error=GetLastError();
            if(sampled) elapsed=ticks()-start;
        }
        cached.counter->completed.fetch_add(1,std::memory_order_relaxed);
        (result?cached.counter->positive:cached.counter->negative).fetch_add(1,std::memory_order_relaxed);
        if(sampled) {
            if(frame.fast_status==0) {
                fast_compared.fetch_add(1,std::memory_order_relaxed);
                if(frame.fast.found!=result) {fast_mismatches.fetch_add(1,std::memory_order_relaxed);fast_allowed=false;}
            } else if(frame.fast_status!=4) fast_unsupported.fetch_add(1,std::memory_order_relaxed);
            const auto index=record_attempts.fetch_add(1);
            const auto address=frame.fast.unsupported_getter;
            const auto rva=address==0?0u:address>=module_base && address-module_base<0x03929000?
                static_cast<std::uint32_t>(address-module_base):UINT32_MAX;
            if(index<records.size()) records[static_cast<std::size_t>(index)]={sequence,
                token(reinterpret_cast<std::uintptr_t>(controller),salt),elapsed,frame.lock_ticks,frame.fast_ticks,
                cached.id,frame.size,frame.locks,static_cast<std::uint32_t>(result),frame.fast_status,
                static_cast<std::uint32_t>(frame.fast.found),frame.fast.inspected,frame.fast.nonnull,rva,0};
        }
        exit.completed=true;SetLastError(resulting_error);return result;
    }
    void* lock(void* guard,void* lock_address) {
        auto* frame=current_frame;
        if(!frame || static_cast<unsigned char*>(frame->controller)+0x150!=lock_address) return acquire(guard,lock_address);
        const auto incoming_error=GetLastError();const auto start=ticks();SetLastError(incoming_error);
        auto* result=acquire(guard,lock_address); // original recursion/ownership semantics
        const auto resulting_error=GetLastError();frame->lock_ticks+=ticks()-start;
        // Constructor has returned with the ORIGINAL lock held. The original
        // search will release it. The sampled shadow scan uses this same lock;
        // no additional lock, allocation or virtual message call is introduced.
        std::memcpy(&frame->size,static_cast<unsigned char*>(lock_address)+0x18,sizeof(frame->size));
        if(fast_table && target_slot && fast_allowed.load(std::memory_order_relaxed)) {
            const auto begin=ticks();frame->fast=movement_fast::scan(frame->controller,*fast_table,target_slot);
            frame->fast_ticks=ticks()-begin;frame->fast_status=static_cast<std::uint32_t>(frame->fast.status);
        }
        ++frame->locks;SetLastError(resulting_error);return result;
    }
};
}
