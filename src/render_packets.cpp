#define NOMINMAX
#include "skyrim_mc/render_packets.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace skyrim_mc::render {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool v,const char* why) {if(!v) throw std::runtime_error(why);}
void checked(HRESULT h,const char* why) {if(FAILED(h)) throw std::runtime_error(why);}
double elapsed(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::uint64_t cycles() {ULONG64 n=0;require(QueryThreadCycleTime(GetCurrentThread(),&n)!=0,"worker CPU cycles unavailable");return n;}
void on_device(ID3D11DeviceChild* child,ID3D11Device* device) {
    if(!child) return;Ref<ID3D11Device> owner;child->GetDevice(&owner);require(owner.Get()==device,"cross-device packet resource");
}
void immutable(ID3D11Buffer* b,UINT flag,ID3D11Device* device) {
    require(b,"missing geometry");on_device(b,device);D3D11_BUFFER_DESC desc{};b->GetDesc(&desc);
    require(desc.Usage==D3D11_USAGE_IMMUTABLE && (desc.BindFlags&flag) && !desc.CPUAccessFlags,"geometry must be immutable");
}
struct Recorder {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> context;
    std::map<UINT,Ref<ID3D11Buffer>> uploads;
    Recorder(ID3D11Device* d,ID3D11DeviceContext* c):device(d),context(c) {}
    ID3D11Buffer* upload(std::span<const std::byte> bytes) {
        const auto size=static_cast<UINT>(bytes.size());auto& buffer=uploads[size];
        if(!buffer) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            checked(device->CreateBuffer(&desc,nullptr,&buffer),"worker constant buffer creation failed");
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(context->Map(buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"constant snapshot upload failed");
        std::memcpy(mapped.pData,bytes.data(),bytes.size());context->Unmap(buffer.Get(),0);return buffer.Get();
    }
    void draw(const Batch& batch,std::size_t first,std::size_t last) {
        // Every list starts with a complete supported pipeline, never inherited
        // engine state. ClearState also disables GS/HS/DS, SO and predication.
        context->ClearState();auto* target=batch.pass().target.Get();context->OMSetRenderTargets(1,&target,batch.pass().depth.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for(std::size_t i=first;i<last;++i) {
            const auto& p=batch.packets()[i];const auto& r=batch.resources(p);auto* vertex=r.vertices.Get();auto* texture=r.texture.Get();auto* sampler=r.sampler.Get();
            context->IASetInputLayout(r.layout.Get());context->IASetVertexBuffers(0,1,&vertex,&p.stride,&p.vertex_offset);
            context->IASetIndexBuffer(r.indices.Get(),p.index_format,p.index_offset);
            context->VSSetShader(r.vertex_shader.Get(),nullptr,0);context->PSSetShader(r.pixel_shader.Get(),nullptr,0);
            context->PSSetShaderResources(0,1,&texture);context->PSSetSamplers(0,1,&sampler);
            context->RSSetState(r.raster.Get());context->RSSetViewports(1,&p.viewport);context->RSSetScissorRects(1,&p.scissor);
            const FLOAT blend_factor[4]{1,1,1,1};context->OMSetBlendState(r.blend.Get(),blend_factor,0xffffffff);context->OMSetDepthStencilState(r.depth.Get(),0);
            auto* constants=upload(batch.constants(p));context->VSSetConstantBuffers(0,1,&constants);context->PSSetConstantBuffers(0,1,&constants);
            context->DrawIndexed(p.index_count,p.index_start,p.base_vertex);
        }
    }
};
}
Batch::Batch(Pass pass,std::vector<FrozenPacket> packets,std::vector<Resources> resources,std::vector<std::byte> constants)
    :pass_(std::move(pass)),packets_(std::move(packets)),resources_(std::move(resources)),constants_(std::move(constants)) {}
std::shared_ptr<const Batch> Batch::freeze(ID3D11Device* device,Pass pass,std::span<const Packet> packets) {
    require(device && pass.target && !packets.empty() && packets.size()<=65536,"invalid packet batch");
    on_device(pass.target.Get(),device);on_device(pass.depth.Get(),device);
    using Key=std::array<std::uintptr_t,10>;
    std::map<Key,std::uint32_t> resource_indices;
    std::vector<Resources> resources;
    std::size_t total_bytes=0;
    for(const auto& p:packets) {
        require(!p.constants.empty() && p.constants.size()<=65536 && p.constants.size()%16==0,"invalid constant snapshot size");
        total_bytes+=p.constants.size();
    }
    require(total_bytes<=UINT32_MAX,"constant arena exceeds offset range");
    std::vector<std::byte> constants(total_bytes);std::uint32_t offset=0;
    std::vector<FrozenPacket> frozen;frozen.reserve(packets.size());
    for(const auto& p:packets) {
        require(p.vertex_shader && p.pixel_shader && p.layout && p.raster && p.index_count && p.stride,"incomplete pipeline");
        require(!p.constants.empty() && p.constants.size()<=65536 && p.constants.size()%16==0,"invalid constant snapshot size");
        const Key key{reinterpret_cast<std::uintptr_t>(p.vertex_shader.Get()),reinterpret_cast<std::uintptr_t>(p.pixel_shader.Get()),reinterpret_cast<std::uintptr_t>(p.layout.Get()),
            reinterpret_cast<std::uintptr_t>(p.vertices.Get()),reinterpret_cast<std::uintptr_t>(p.indices.Get()),reinterpret_cast<std::uintptr_t>(p.texture.Get()),
            reinterpret_cast<std::uintptr_t>(p.sampler.Get()),reinterpret_cast<std::uintptr_t>(p.raster.Get()),reinterpret_cast<std::uintptr_t>(p.blend.Get()),reinterpret_cast<std::uintptr_t>(p.depth.Get())};
        const auto [entry,inserted]=resource_indices.try_emplace(key,static_cast<std::uint32_t>(resources.size()));
        if(inserted) {
            on_device(p.vertex_shader.Get(),device);on_device(p.pixel_shader.Get(),device);on_device(p.layout.Get(),device);
            on_device(p.raster.Get(),device);on_device(p.blend.Get(),device);on_device(p.depth.Get(),device);on_device(p.sampler.Get(),device);
            immutable(p.vertices.Get(),D3D11_BIND_VERTEX_BUFFER,device);immutable(p.indices.Get(),D3D11_BIND_INDEX_BUFFER,device);
            D3D11_BUFFER_DESC indices{},vertices{};p.indices->GetDesc(&indices);p.vertices->GetDesc(&vertices);
            if(p.texture) {
                require(p.sampler,"textured packet without sampler");on_device(p.texture.Get(),device);
                Ref<ID3D11Resource> resource;p.texture->GetResource(&resource);Ref<ID3D11Texture2D> texture;
                checked(resource.As(&texture),"only immutable Texture2D resources supported");D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
                require(desc.Usage==D3D11_USAGE_IMMUTABLE && !desc.CPUAccessFlags,"texture content must be immutable");
            }
            Resources own;own.vertex_shader=p.vertex_shader;own.pixel_shader=p.pixel_shader;own.layout=p.layout;
            own.vertices=p.vertices;own.indices=p.indices;own.texture=p.texture;own.sampler=p.sampler;own.raster=p.raster;own.blend=p.blend;own.depth=p.depth;
            own.vertex_bytes=vertices.ByteWidth;own.index_bytes=indices.ByteWidth;resources.push_back(std::move(own));
        }
        const auto& retained=resources[entry->second];
        const auto width=p.index_format==DXGI_FORMAT_R32_UINT?4u:p.index_format==DXGI_FORMAT_R16_UINT?2u:0u;
        require(width && p.index_offset%width==0 && static_cast<std::uint64_t>(p.index_offset)+(static_cast<std::uint64_t>(p.index_start)+p.index_count)*width<=retained.index_bytes,"invalid index range");
        require(p.vertex_offset<retained.vertex_bytes && p.stride<=retained.vertex_bytes-p.vertex_offset,"invalid vertex range");
        require(std::isfinite(p.viewport.TopLeftX) && std::isfinite(p.viewport.TopLeftY) && std::isfinite(p.viewport.Width) && std::isfinite(p.viewport.Height)
            && p.viewport.Width>0 && p.viewport.Height>0 && p.viewport.MinDepth>=0 && p.viewport.MaxDepth<=1 && p.viewport.MinDepth<=p.viewport.MaxDepth,"invalid viewport");
        const auto size=static_cast<std::uint32_t>(p.constants.size());std::memcpy(constants.data()+offset,p.constants.data(),size);
        frozen.push_back({entry->second,offset,size,p.stride,p.vertex_offset,p.index_offset,p.index_count,p.index_start,p.base_vertex,p.index_format,p.viewport,p.scissor});
        offset+=size;
    }
    return std::shared_ptr<const Batch>(new Batch(std::move(pass),std::move(frozen),std::move(resources),std::move(constants)));
}
struct Backend::Impl {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> immediate;DWORD owner=GetCurrentThreadId();
    Recorder serial_recorder;
    std::vector<std::unique_ptr<Recorder>> recorders;
    std::vector<Ref<ID3D11CommandList>> lists;
    std::vector<std::jthread> threads;std::vector<DWORD> ids;std::vector<std::uint64_t> cpu;
    std::vector<std::exception_ptr> errors;
    std::mutex mutex;std::condition_variable work,done;
    bool stopping=false;std::uint64_t generation=0;unsigned remaining=0;
    std::shared_ptr<const Batch> pending;
    Impl(ID3D11Device* d,ID3D11DeviceContext* c,unsigned count):device(d),immediate(c),serial_recorder(d,c),lists(count),ids(count),cpu(count),errors(count) {
        require(d && c && count>=1 && count<=16,"invalid backend");require(c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"immediate context required");on_device(c,d);
        require(!(d->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED),"single-threaded device unsupported");
        for(unsigned i=0;i<count;++i) {Ref<ID3D11DeviceContext> context;checked(d->CreateDeferredContext(0,&context),"deferred context creation failed");recorders.push_back(std::make_unique<Recorder>(d,context.Get()));}
        try {for(unsigned i=0;i<count;++i) threads.emplace_back([this,i,count] {
            std::uint64_t seen=0;
            for(;;) {
                std::unique_lock lock(mutex);work.wait(lock,[&]{return stopping || generation!=seen;});if(stopping) return;
                seen=generation;const auto batch=pending;lock.unlock();
                try {
                    ids[i]=GetCurrentThreadId();const auto before=cycles();const auto n=batch->packets().size();
                    recorders[i]->draw(*batch,n*i/count,n*(i+1)/count);
                    checked(recorders[i]->context->FinishCommandList(FALSE,lists[i].ReleaseAndGetAddressOf()),"FinishCommandList failed");
                    cpu[i]=cycles()-before;
                } catch(...) {
                    errors[i]=std::current_exception();
                    // Discard partially recorded work; a later submission must
                    // never inherit commands from a failed earlier batch.
                    Ref<ID3D11CommandList> discarded;
                    recorders[i]->context->FinishCommandList(FALSE,&discarded);
                    lists[i].Reset();
                }
                lock.lock();--remaining;if(!remaining) done.notify_one();
            }
        });} catch(...) {stop();throw;}
    }
    void stop() {{std::lock_guard lock(mutex);stopping=true;}work.notify_all();threads.clear();}
    ~Impl() {stop();}
    void check_owner() {require(GetCurrentThreadId()==owner,"submission must remain on owning thread");}
};
Backend::Backend(ID3D11Device* d,ID3D11DeviceContext* c,unsigned workers) {
    require(d && c && workers>=1 && workers<=16,"invalid backend");impl_=std::make_unique<Impl>(d,c,workers);
}
Backend::~Backend()=default;
Stats Backend::submit(std::shared_ptr<const Batch> batch) {
    auto& s=*impl_;s.check_owner();require(batch && batch->packets().size()>=s.recorders.size(),"batch smaller than worker pool");
    // Batch creation validates a device, but submission must validate that it
    // is THIS backend's device before any worker can touch its resources.
    on_device(batch->pass().target.Get(),s.device.Get());
    const auto start=Clock::now();
    {std::lock_guard lock(s.mutex);s.pending=std::move(batch);s.remaining=static_cast<unsigned>(s.recorders.size());
        std::fill(s.errors.begin(),s.errors.end(),nullptr);std::fill(s.cpu.begin(),s.cpu.end(),0);++s.generation;}
    s.work.notify_all();{std::unique_lock lock(s.mutex);s.done.wait(lock,[&]{return s.remaining==0;});}
    for(auto error:s.errors) if(error) {
        s.pending.reset();for(auto& list:s.lists) list.Reset();std::rethrow_exception(error);
    }
    Stats result;result.draws=s.pending->packets().size();result.lists=s.lists.size();result.worker_ids=s.ids;
    for(const auto& p:s.pending->packets()) result.constant_bytes+=p.constant_size;for(auto cpu:s.cpu) result.worker_cycles+=cpu;
    const auto replay=Clock::now();
    for(const auto& list:s.lists) s.immediate->ExecuteCommandList(list.Get(),TRUE);
    result.replay_ms=elapsed(replay);result.record_and_replay_ms=elapsed(start);s.pending.reset();
    // Release lists while on owner thread; D3D retains resources for GPU work.
    for(auto& list:s.lists) list.Reset();return result;
}
void Backend::serial(const Batch& batch) {
    impl_->check_owner();on_device(batch.pass().target.Get(),impl_->device.Get());
    impl_->serial_recorder.draw(batch,0,batch.packets().size());
}
}
