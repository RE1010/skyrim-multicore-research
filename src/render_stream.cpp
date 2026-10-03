#define NOMINMAX
#include "skyrim_mc/render_stream.hpp"
#include "skyrim_mc/context_hooks.hpp"
#include "context_slots.hpp"
#include <d3d11_3.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
namespace skyrim_mc::stream {
namespace {
template<class T> using Ref=Microsoft::WRL::ComPtr<T>;
void check(HRESULT h) {if(FAILED(h)) throw std::runtime_error("D3D stream operation failed");}
using Bytes=std::shared_ptr<const std::vector<std::byte>>;
struct PhaseTimer {
    std::uint64_t& total;LARGE_INTEGER start{};
    explicit PhaseTimer(std::uint64_t& output):total(output) {QueryPerformanceCounter(&start);}
    ~PhaseTimer() {LARGE_INTEGER end{};QueryPerformanceCounter(&end);total+=static_cast<std::uint64_t>(end.QuadPart-start.QuadPart);}
};
template<class T,std::size_t N,class F> void get(std::array<Ref<T>,N>& refs,F f) {
    std::array<T*,N> raw{};f(raw.data());for(std::size_t i=0;i<N;++i) {refs[i].Reset();refs[i].Attach(raw[i]);}
}
template<class T,std::size_t N> auto pointers(const std::array<Ref<T>,N>& refs) {
    std::array<T*,N> raw{};for(std::size_t i=0;i<N;++i) raw[i]=refs[i].Get();return raw;
}
template<class T,std::size_t N> bool same_refs(const std::array<Ref<T>,N>& a,const std::array<Ref<T>,N>& b) {
    for(std::size_t i=0;i<N;++i)if(a[i].Get()!=b[i].Get())return false;return true;
}
struct ShaderBindings {Ref<ID3D11VertexShader> vs;Ref<ID3D11PixelShader> ps;};
struct GeometryBindings {
    Ref<ID3D11InputLayout> layout;Ref<ID3D11Buffer> index;
    std::array<Ref<ID3D11Buffer>,32> vertices;std::array<UINT,32> strides{},offsets{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};DXGI_FORMAT index_format{};UINT index_offset=0;
};
struct VertexConstants {std::array<Ref<ID3D11Buffer>,14> vcb;};
struct PixelConstants {std::array<Ref<ID3D11Buffer>,14> pcb;};
struct VertexViews {std::array<Ref<ID3D11ShaderResourceView>,128> vviews;};
struct PixelViews {std::array<Ref<ID3D11ShaderResourceView>,128> pviews;};
struct VertexSamplers {std::array<Ref<ID3D11SamplerState>,16> vsamplers;};
struct PixelSamplers {std::array<Ref<ID3D11SamplerState>,16> psamplers;};
struct OutputBindings {std::array<Ref<ID3D11RenderTargetView>,8> targets;Ref<ID3D11DepthStencilView> depth_view;};
struct BlendDepthBindings {
    Ref<ID3D11BlendState> blend;FLOAT blend_factor[4]{};UINT sample_mask=0,stencil_ref=0;
    Ref<ID3D11DepthStencilState> depth;
};
struct RasterBindings {Ref<ID3D11RasterizerState> raster;};
struct ViewportBindings {std::array<D3D11_VIEWPORT,16> viewports{};UINT viewport_count=16;};
struct ScissorBindings {std::array<D3D11_RECT,16> scissors{};UINT scissor_count=16;};
bool same_bindings(const ShaderBindings& a,const ShaderBindings& b) {return a.vs.Get()==b.vs.Get() && a.ps.Get()==b.ps.Get();}
bool same_bindings(const GeometryBindings& a,const GeometryBindings& b) {
    return a.layout.Get()==b.layout.Get() && a.index.Get()==b.index.Get() && same_refs(a.vertices,b.vertices)
        && a.strides==b.strides && a.offsets==b.offsets && a.topology==b.topology && a.index_format==b.index_format && a.index_offset==b.index_offset;
}
bool same_bindings(const VertexConstants& a,const VertexConstants& b) {return same_refs(a.vcb,b.vcb);}
bool same_bindings(const PixelConstants& a,const PixelConstants& b) {return same_refs(a.pcb,b.pcb);}
bool same_bindings(const VertexViews& a,const VertexViews& b) {return same_refs(a.vviews,b.vviews);}
bool same_bindings(const PixelViews& a,const PixelViews& b) {return same_refs(a.pviews,b.pviews);}
bool same_bindings(const VertexSamplers& a,const VertexSamplers& b) {return same_refs(a.vsamplers,b.vsamplers);}
bool same_bindings(const PixelSamplers& a,const PixelSamplers& b) {return same_refs(a.psamplers,b.psamplers);}
bool same_bindings(const OutputBindings& a,const OutputBindings& b) {return same_refs(a.targets,b.targets) && a.depth_view.Get()==b.depth_view.Get();}
bool same_bindings(const BlendDepthBindings& a,const BlendDepthBindings& b) {
    return a.blend.Get()==b.blend.Get() && a.depth.Get()==b.depth.Get() && a.sample_mask==b.sample_mask && a.stencil_ref==b.stencil_ref
        && std::memcmp(a.blend_factor,b.blend_factor,sizeof(a.blend_factor))==0;
}
bool same_bindings(const RasterBindings& a,const RasterBindings& b) {return a.raster.Get()==b.raster.Get();}
bool same_bindings(const ViewportBindings& a,const ViewportBindings& b) {
    return a.viewport_count==b.viewport_count && std::memcmp(a.viewports.data(),b.viewports.data(),a.viewport_count*sizeof(D3D11_VIEWPORT))==0;
}
bool same_bindings(const ScissorBindings& a,const ScissorBindings& b) {
    return a.scissor_count==b.scissor_count && std::memcmp(a.scissors.data(),b.scissors.data(),a.scissor_count*sizeof(D3D11_RECT))==0;
}
// Mutable getter results never escape to workers. Each published group owns
// its references and is immutable for every draw that retains its version.
struct CachedBindings : ShaderBindings,GeometryBindings,VertexConstants,PixelConstants,VertexViews,PixelViews,
    VertexSamplers,PixelSamplers,OutputBindings,BlendDepthBindings,RasterBindings,ViewportBindings,ScissorBindings {};
struct BindingPacket {
    std::shared_ptr<const ShaderBindings> shader;
    std::shared_ptr<const GeometryBindings> geometry;
    std::shared_ptr<const VertexConstants> vertex_constants;
    std::shared_ptr<const PixelConstants> pixel_constants;
    std::shared_ptr<const VertexViews> vertex_views;
    std::shared_ptr<const PixelViews> pixel_views;
    std::shared_ptr<const VertexSamplers> vertex_samplers;
    std::shared_ptr<const PixelSamplers> pixel_samplers;
    std::shared_ptr<const OutputBindings> outputs;
    std::shared_ptr<const BlendDepthBindings> blend_depth;
    std::shared_ptr<const RasterBindings> rasterizer;
    std::shared_ptr<const ViewportBindings> viewport;
    std::shared_ptr<const ScissorBindings> scissor;
};
struct Snapshot : BindingPacket {
    std::array<Bytes,14> vbytes,pbytes;
    UINT count=0,start=0;INT base=0;
};
enum StateGroup : unsigned {
    shaders=1u<<0,geometry=1u<<1,vertex_constants=1u<<2,pixel_constants=1u<<3,
    vertex_views=1u<<4,pixel_views=1u<<5,vertex_samplers=1u<<6,pixel_samplers=1u<<7,
    outputs=1u<<8,blend_depth=1u<<9,rasterizer=1u<<10,viewports=1u<<11,scissors=1u<<12,
    all_state=(1u<<13)-1
};
unsigned invalidated_state(unsigned s) {
    using namespace slots;
    // Read authoritative getters after setters, rather than predicting the
    // runtime's implicit resource-hazard unbindings from setter arguments.
    switch(s) {
        case VSSetConstantBuffers:case VSSetConstantBuffers1:return vertex_constants;
        case PSSetConstantBuffers:case PSSetConstantBuffers1:return pixel_constants;
        case VSSetSamplers:return vertex_samplers;
        case PSSetSamplers:return pixel_samplers;
        case VSSetShader:case PSSetShader:case GSSetShader:case HSSetShader:case DSSetShader:return shaders;
        case IASetInputLayout:case IASetPrimitiveTopology:return geometry;
        case IASetVertexBuffers:case IASetIndexBuffer:return geometry|shaders;
        case OMSetBlendState:case OMSetDepthStencilState:return blend_depth;
        case RSSetState:return rasterizer;
        case RSSetViewports:return viewports;
        case RSSetScissorRects:return scissors;
        case VSSetShaderResources:case PSSetShaderResources:case GSSetShaderResources:
        case HSSetShaderResources:case DSSetShaderResources:case CSSetShaderResources:
            return vertex_views|pixel_views|outputs|shaders;
        // Uploads change resource contents, not bindings. Constant byte versions
        // are resolved from the upload map on every draw, including cache hits.
        case Map:case Unmap:case UpdateSubresource:case UpdateSubresource1:return 0;
        default:break;
    }
    if(s<=SetPrivateDataInterface || (s>=VSGetConstantBuffers && s<=CSGetConstantBuffers)
       || (s>=VSGetConstantBuffers1 && s<=CSGetConstantBuffers1)
       || s==GetResourceMinLOD || s==GetType || s==GetContextFlags || s==IsAnnotationEnabled
       || s==GetHardwareProtectionState) return 0;
    // Includes output/UAV/SO setters, predication, queries, ClearState,
    // external command lists, context swaps and unknown mutating methods.
    return all_state;
}
struct Recorder {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> context;
    // Per original buffer, not just size: different same-sized VS/PS constants
    // must never alias. References prevent address reuse while cached.
    struct Upload {Ref<ID3D11Buffer> source,buffer;Bytes bytes;};
    std::map<ID3D11Buffer*,Upload> buffers;
    std::uint64_t uploads=0,reuses=0,uploaded_bytes=0,bindings=0,bindings_skipped=0;
    Recorder(ID3D11Device* d):device(d) {check(d->CreateDeferredContext(0,&context));}
    ID3D11Buffer* upload(ID3D11Buffer* source,const Bytes& bytes,bool& changed) {
        if(!source) return nullptr;
        auto& u=buffers[source];
        // Identity refers to an owned immutable upload version. Each recorder
        // owns its buffers; a different version is always uploaded again.
        if(u.buffer && u.bytes==bytes) {++reuses;return u.buffer.Get();}
        if(!u.buffer) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(bytes->size());desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&desc,nullptr,&u.buffer));u.source=source;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(u.buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
        std::memcpy(mapped.pData,bytes->data(),bytes->size());context->Unmap(u.buffer.Get(),0);
        changed=true;u.bytes=bytes;++uploads;uploaded_bytes+=bytes->size();return u.buffer.Get();
    }
    void record(const std::vector<Snapshot>& draws,std::size_t first,std::size_t last,bool reduce_bindings) {
        context->ClearState();if(buffers.size()>256) buffers.clear();
        // This cache is local to one command list. FinishCommandList(FALSE)
        // resets deferred state; no list may inherit a previous list's bindings.
        const Snapshot* previous=nullptr;
        std::array<ID3D11Buffer*,14> previous_vcb{},previous_pcb{};
        auto bind=[&](bool changed,auto command) {if(changed){command();++bindings;}else ++bindings_skipped;};
        for(auto i=first;i<last;++i) {
            const auto& d=draws[i];const bool full=!reduce_bindings || !previous;
            const bool output_changed=full || !same_refs(d.outputs->targets,previous->outputs->targets) || d.outputs->depth_view.Get()!=previous->outputs->depth_view.Get();
            // Outputs may implicitly null inputs. Replay the coupled output/SRV
            // group together, in output-before-input order, after any change.
            const bool resources_changed=output_changed || !same_refs(d.vertex_views->vviews,previous->vertex_views->vviews) || !same_refs(d.pixel_views->pviews,previous->pixel_views->pviews);
            bind(resources_changed,[&]{auto targets=pointers(d.outputs->targets);context->OMSetRenderTargets(8,targets.data(),d.outputs->depth_view.Get());});
            bind(full || d.blend_depth->blend.Get()!=previous->blend_depth->blend.Get() || d.blend_depth->sample_mask!=previous->blend_depth->sample_mask || std::memcmp(d.blend_depth->blend_factor,previous->blend_depth->blend_factor,sizeof(d.blend_depth->blend_factor)),[&]{context->OMSetBlendState(d.blend_depth->blend.Get(),d.blend_depth->blend_factor,d.blend_depth->sample_mask);});
            bind(full || d.blend_depth->depth.Get()!=previous->blend_depth->depth.Get() || d.blend_depth->stencil_ref!=previous->blend_depth->stencil_ref,[&]{context->OMSetDepthStencilState(d.blend_depth->depth.Get(),d.blend_depth->stencil_ref);});
            bind(full || d.geometry->layout.Get()!=previous->geometry->layout.Get(),[&]{context->IASetInputLayout(d.geometry->layout.Get());});
            bind(full || d.geometry->topology!=previous->geometry->topology,[&]{context->IASetPrimitiveTopology(d.geometry->topology);});
            bind(output_changed || !same_refs(d.geometry->vertices,previous->geometry->vertices) || d.geometry->strides!=previous->geometry->strides || d.geometry->offsets!=previous->geometry->offsets,[&]{auto vertices=pointers(d.geometry->vertices);context->IASetVertexBuffers(0,32,vertices.data(),d.geometry->strides.data(),d.geometry->offsets.data());});
            bind(output_changed || d.geometry->index.Get()!=previous->geometry->index.Get() || d.geometry->index_format!=previous->geometry->index_format || d.geometry->index_offset!=previous->geometry->index_offset,[&]{context->IASetIndexBuffer(d.geometry->index.Get(),d.geometry->index_format,d.geometry->index_offset);});
            bind(full || d.shader->vs.Get()!=previous->shader->vs.Get(),[&]{context->VSSetShader(d.shader->vs.Get(),nullptr,0);});
            bind(full || d.shader->ps.Get()!=previous->shader->ps.Get(),[&]{context->PSSetShader(d.shader->ps.Get(),nullptr,0);});
            bind(resources_changed,[&]{auto views=pointers(d.vertex_views->vviews);context->VSSetShaderResources(0,128,views.data());});
            bind(resources_changed,[&]{auto views=pointers(d.pixel_views->pviews);context->PSSetShaderResources(0,128,views.data());});
            bind(full || !same_refs(d.vertex_samplers->vsamplers,previous->vertex_samplers->vsamplers),[&]{auto samplers=pointers(d.vertex_samplers->vsamplers);context->VSSetSamplers(0,16,samplers.data());});
            bind(full || !same_refs(d.pixel_samplers->psamplers,previous->pixel_samplers->psamplers),[&]{auto samplers=pointers(d.pixel_samplers->psamplers);context->PSSetSamplers(0,16,samplers.data());});
            std::array<ID3D11Buffer*,14> vb{},pb{};
            struct Uploaded {ID3D11Buffer* buffer=nullptr;bool changed=false;};
            std::map<ID3D11Buffer*,Uploaded> uploaded;
            bool vertex_uploaded=false,pixel_uploaded=false;
            auto one=[&](ID3D11Buffer* source,const Bytes& bytes,bool& changed) {
                if(!source) return static_cast<ID3D11Buffer*>(nullptr);
                auto [entry,inserted]=uploaded.try_emplace(source);if(inserted)entry->second.buffer=upload(source,bytes,entry->second.changed);
                changed|=entry->second.changed;return entry->second.buffer;
            };
            for(unsigned s=0;s<14;++s) {vb[s]=one(d.vertex_constants->vcb[s].Get(),d.vbytes[s],vertex_uploaded);pb[s]=one(d.pixel_constants->pcb[s].Get(),d.pbytes[s],pixel_uploaded);}
            // WRITE_DISCARD may rename storage behind the same buffer pointer.
            // Rebind every affected stage after an upload, including shared VS/PS.
            bind(full || vertex_uploaded || vb!=previous_vcb,[&]{context->VSSetConstantBuffers(0,14,vb.data());});
            bind(full || pixel_uploaded || pb!=previous_pcb,[&]{context->PSSetConstantBuffers(0,14,pb.data());});
            bind(full || d.rasterizer->raster.Get()!=previous->rasterizer->raster.Get(),[&]{context->RSSetState(d.rasterizer->raster.Get());});
            bind(full || d.viewport->viewport_count!=previous->viewport->viewport_count || std::memcmp(d.viewport->viewports.data(),previous->viewport->viewports.data(),d.viewport->viewport_count*sizeof(D3D11_VIEWPORT)),[&]{context->RSSetViewports(d.viewport->viewport_count,d.viewport->viewports.data());});
            bind(full || d.scissor->scissor_count!=previous->scissor->scissor_count || std::memcmp(d.scissor->scissors.data(),previous->scissor->scissors.data(),d.scissor->scissor_count*sizeof(D3D11_RECT)),[&]{context->RSSetScissorRects(d.scissor->scissor_count,d.scissor->scissors.data());});
            context->DrawIndexed(d.count,d.start,d.base);
            previous=&d;previous_vcb=vb;previous_pcb=pb;
        }
    }
};
bool harmless(unsigned s) {
    using namespace slots;
    // All getters except query results are pure observations. All binding
    // setters are CPU state only; GPU output/mutation methods remain barriers.
    return s<=SetPrivateDataInterface || (s>=VSGetConstantBuffers && s<=GetPredication)
        || (s>=GSGetShaderResources && s<=CSGetConstantBuffers)
        || s==GetType || s==GetContextFlags
        || s==VSSetConstantBuffers || s==PSSetConstantBuffers || s==VSSetShader || s==PSSetShader
        || s==VSSetShaderResources || s==PSSetShaderResources || s==VSSetSamplers || s==PSSetSamplers
        || s==IASetInputLayout || s==IASetVertexBuffers || s==IASetIndexBuffer || s==IASetPrimitiveTopology
        || s==GSSetConstantBuffers || s==GSSetShader || s==GSSetShaderResources || s==GSSetSamplers
        || s==HSSetConstantBuffers || s==HSSetShader || s==HSSetShaderResources || s==HSSetSamplers
        || s==DSSetConstantBuffers || s==DSSetShader || s==DSSetShaderResources || s==DSSetSamplers
        || s==CSSetConstantBuffers || s==CSSetShader || s==CSSetShaderResources || s==CSSetSamplers
        || s==OMSetBlendState || s==OMSetDepthStencilState || s==RSSetState || s==RSSetViewports || s==RSSetScissorRects
        || (s>=VSSetConstantBuffers1 && s<=CSGetConstantBuffers1);
}
}
struct Bridge::Impl {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> context;Ref<ID3D11DeviceContext1> context1;
    DWORD owner=GetCurrentThreadId();unsigned count;bool attached=false,enabled=false,disabled=false,scope_conflict=false,verify_requested=false;unsigned scope=0,bypass=0,verify_remaining=0;
    Recording recording=Recording::workers;
    bool reduce_bindings=true;
    bool cache_enabled=true,cached_eligible=false;unsigned dirty_state=all_state;
    CachedBindings cached_state;BindingPacket published_bindings;
    bool share_bindings=true;unsigned unpublished_state=all_state;
    std::array<UINT,14> vertex_cb_offsets{},pixel_cb_offsets{},vertex_cb_lengths{},pixel_cb_lengths{};
    mutable std::recursive_mutex api_mutex;Statistics stats{};
    struct Constant {Ref<ID3D11Buffer> buffer;Bytes bytes;};
    struct Mapped {Ref<ID3D11Buffer> buffer;void* pointer;UINT size;};
    std::map<ID3D11Buffer*,Constant> constants;std::map<ID3D11Resource*,Mapped> mapped;
    std::set<ID3D11Asynchronous*> queries;
    std::vector<Snapshot> queued;
    Recorder serial;std::vector<std::unique_ptr<Recorder>> recorders;std::vector<Ref<ID3D11CommandList>> lists;
    std::vector<std::jthread> workers;std::vector<std::exception_ptr> failures;
    std::mutex work_mutex;std::condition_variable work,done;bool stop=false;std::uint64_t generation=0;unsigned remaining=0;
    Impl(ID3D11Device* d,ID3D11DeviceContext* c,unsigned n):device(d),context(c),count(n),serial(d),lists(n),failures(n) {
        if(n<1 || n>4 || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || (d->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)) throw std::runtime_error("unsupported stream device");
        LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) throw std::runtime_error("performance counter unavailable");stats.qpc_frequency=static_cast<std::uint64_t>(frequency.QuadPart);
        check(c->QueryInterface(IID_PPV_ARGS(&context1)));queued.reserve(256);
        for(unsigned i=0;i<n;++i) recorders.push_back(std::make_unique<Recorder>(d));
        try {for(unsigned i=0;i<n;++i) workers.emplace_back([this,i] {
            std::uint64_t seen=0;
            for(;;) {
                std::unique_lock lock(work_mutex);work.wait(lock,[&]{return stop || generation!=seen;});if(stop) return;
                seen=generation;lock.unlock();
                try {PhaseTimer timing(stats.worker_record_ticks[i]);recorders[i]->record(queued,queued.size()*i/count,queued.size()*(i+1)/count,reduce_bindings);
                    check(recorders[i]->context->FinishCommandList(FALSE,lists[i].ReleaseAndGetAddressOf()));
                } catch(...) {failures[i]=std::current_exception();Ref<ID3D11CommandList> discard;recorders[i]->context->FinishCommandList(FALSE,&discard);lists[i].Reset();}
                lock.lock();--remaining;if(!remaining) done.notify_one();
            }
        });} catch(...) {shutdown();throw;}
    }
    void shutdown() {{std::lock_guard lock(work_mutex);stop=true;}work.notify_all();workers.clear();}
    ~Impl() {if(attached) {std::lock_guard lock(api_mutex);flush(false);detach_context();}shutdown();}
    struct Bypass {Impl& s;explicit Bypass(Impl& v):s(v){++s.bypass;}~Bypass(){--s.bypass;}};
    void flush(bool parallel=true) noexcept {
        if(queued.empty()) return;Bypass guard(*this);const auto n=queued.size();bool used=false;
        try {
            if(parallel && recording==Recording::workers && !disabled && n>=count*4) {
                {PhaseTimer timing(stats.worker_wait_ticks);
                    {std::lock_guard lock(work_mutex);remaining=count;std::fill(failures.begin(),failures.end(),nullptr);++generation;}
                    work.notify_all();{std::unique_lock lock(work_mutex);done.wait(lock,[&]{return !remaining;});}}
                bool okay=true;for(const auto& error:failures) if(error) okay=false;
                if(okay) {PhaseTimer timing(stats.execute_ticks);for(const auto& list:lists) context->ExecuteCommandList(list.Get(),TRUE);used=true;}
                else {++stats.errors;disabled=true;}
                for(auto& list:lists) list.Reset();
            }
            if(!used) {
                Ref<ID3D11CommandList> list;
                {PhaseTimer timing(stats.serial_record_ticks);serial.record(queued,0,n,reduce_bindings);check(serial.context->FinishCommandList(FALSE,&list));}
                {PhaseTimer timing(stats.execute_ticks);context->ExecuteCommandList(list.Get(),TRUE);}stats.serial_draws+=n;
            } else stats.worker_draws+=n;
            ++stats.batches;{PhaseTimer timing(stats.queue_release_ticks);queued.clear();}
        } catch(...) {
            // A suppressed draw must never be silently dropped. Failure of both
            // recording paths means the device cannot execute this stream.
            ++stats.errors;disabled=true;RaiseFailFastException(nullptr,nullptr,0);
        }
    }
    static Ref<ID3D11Buffer> constant_buffer(ID3D11Resource* resource,UINT subresource=0) {
        Ref<ID3D11Buffer> buffer;if(!resource || subresource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&buffer)))) return {};
        D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);
        if(d.BindFlags!=D3D11_BIND_CONSTANT_BUFFER || !d.ByteWidth || d.ByteWidth>65536 || d.ByteWidth%16) return {};return buffer;
    }
    void save(ID3D11Buffer* b,const void* pointer,UINT size) {
        PhaseTimer timing(stats.upload_copy_ticks);++stats.upload_copies;stats.upload_copy_bytes+=size;
        if(constants.size()>=512 && !constants.contains(b)) constants.clear();
        auto copy=std::make_shared<std::vector<std::byte>>(size);std::memcpy(copy->data(),pointer,size);constants[b]={b,std::move(copy)};
    }
    bool capture(Snapshot& d) {
        PhaseTimer timing(stats.capture_ticks);++stats.capture_attempts;
        Bypass guard(*this);
        if(!cache_enabled) dirty_state=all_state;
        auto refresh=[&](unsigned group,unsigned getters,auto update) {
            if(dirty_state&group) {stats.snapshot_getters+=getters;++stats.state_group_refreshes;update();dirty_state&=~group;unpublished_state|=group;}
            else ++stats.state_group_reuses;
        };
        auto& state=cached_state;
        refresh(shaders,8,[&] {
            UINT vc=0,pc=0;context->VSGetShader(state.vs.ReleaseAndGetAddressOf(),nullptr,&vc);
            context->PSGetShader(state.ps.ReleaseAndGetAddressOf(),nullptr,&pc);
            Ref<ID3D11GeometryShader> gs;Ref<ID3D11HullShader> hs;Ref<ID3D11DomainShader> ds;
            context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
            Ref<ID3D11Predicate> predicate;BOOL value=FALSE;context->GetPredication(&predicate,&value);
            std::array<Ref<ID3D11Buffer>,4> so;get(so,[&](auto raw){context->SOGetTargets(4,raw);});
            std::array<Ref<ID3D11UnorderedAccessView>,8> uavs;get(uavs,[&](auto raw){context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,raw);});
            cached_eligible=state.vs && !vc && !pc && !gs && !hs && !ds && !predicate;
            for(const auto& b:so)if(b)cached_eligible=false;for(const auto& u:uavs)if(u)cached_eligible=false;
        });
        if(!cached_eligible || !queries.empty())return false;
        refresh(geometry,4,[&] {
            context->IAGetInputLayout(state.layout.ReleaseAndGetAddressOf());context->IAGetPrimitiveTopology(&state.topology);
            context->IAGetIndexBuffer(state.index.ReleaseAndGetAddressOf(),&state.index_format,&state.index_offset);
            get(state.vertices,[&](auto raw){context->IAGetVertexBuffers(0,32,raw,state.strides.data(),state.offsets.data());});
        });
        if(!state.index)return false;
        refresh(vertex_constants,1,[&]{get(state.vcb,[&](auto raw){context1->VSGetConstantBuffers1(0,14,raw,vertex_cb_offsets.data(),vertex_cb_lengths.data());});});
        refresh(pixel_constants,1,[&]{get(state.pcb,[&](auto raw){context1->PSGetConstantBuffers1(0,14,raw,pixel_cb_offsets.data(),pixel_cb_lengths.data());});});
        refresh(vertex_views,1,[&]{get(state.vviews,[&](auto raw){context->VSGetShaderResources(0,128,raw);});});
        refresh(pixel_views,1,[&]{get(state.pviews,[&](auto raw){context->PSGetShaderResources(0,128,raw);});});
        refresh(vertex_samplers,1,[&]{get(state.vsamplers,[&](auto raw){context->VSGetSamplers(0,16,raw);});});
        refresh(pixel_samplers,1,[&]{get(state.psamplers,[&](auto raw){context->PSGetSamplers(0,16,raw);});});
        refresh(outputs,1,[&]{get(state.targets,[&](auto raw){context->OMGetRenderTargets(8,raw,state.depth_view.ReleaseAndGetAddressOf());});});
        refresh(blend_depth,2,[&]{context->OMGetBlendState(state.blend.ReleaseAndGetAddressOf(),state.blend_factor,&state.sample_mask);context->OMGetDepthStencilState(state.depth.ReleaseAndGetAddressOf(),&state.stencil_ref);});
        refresh(rasterizer,1,[&]{context->RSGetState(state.raster.ReleaseAndGetAddressOf());});
        refresh(viewports,1,[&]{state.viewport_count=16;context->RSGetViewports(&state.viewport_count,state.viewports.data());});
        refresh(scissors,1,[&]{state.scissor_count=16;context->RSGetScissorRects(&state.scissor_count,state.scissors.data());});
        {
            PhaseTimer publication(stats.snapshot_publish_ticks);
            auto publish=[&](unsigned group,auto& output,const auto& value) {
                if(!share_bindings || !output || ((unpublished_state&group) && !same_bindings(*output,value))) {
                    using Group=std::remove_cvref_t<decltype(value)>;
                    output=std::make_shared<const Group>(value);++stats.snapshot_group_copies;
                } else ++stats.snapshot_group_reuses;
                unpublished_state&=~group;
            };
            publish(shaders,published_bindings.shader,static_cast<const ShaderBindings&>(state));
            publish(geometry,published_bindings.geometry,static_cast<const GeometryBindings&>(state));
            publish(vertex_constants,published_bindings.vertex_constants,static_cast<const VertexConstants&>(state));
            publish(pixel_constants,published_bindings.pixel_constants,static_cast<const PixelConstants&>(state));
            publish(vertex_views,published_bindings.vertex_views,static_cast<const VertexViews&>(state));
            publish(pixel_views,published_bindings.pixel_views,static_cast<const PixelViews&>(state));
            publish(vertex_samplers,published_bindings.vertex_samplers,static_cast<const VertexSamplers&>(state));
            publish(pixel_samplers,published_bindings.pixel_samplers,static_cast<const PixelSamplers&>(state));
            publish(outputs,published_bindings.outputs,static_cast<const OutputBindings&>(state));
            publish(blend_depth,published_bindings.blend_depth,static_cast<const BlendDepthBindings&>(state));
            publish(rasterizer,published_bindings.rasterizer,static_cast<const RasterBindings&>(state));
            publish(viewports,published_bindings.viewport,static_cast<const ViewportBindings&>(state));
            publish(scissors,published_bindings.scissor,static_cast<const ScissorBindings&>(state));
            static_cast<BindingPacket&>(d)=published_bindings;
        }
        auto bytes=[&](const auto& buffers,auto& snapshots,const auto& offsets,const auto& lengths) {
            for(unsigned i=0;i<14;++i) if(buffers[i]) {
                const auto it=constants.find(buffers[i].Get());
                if(it==constants.end() || mapped.contains(buffers[i].Get())) {++stats.unknown_constants;return false;}
                if(offsets[i] || lengths[i]*16<it->second.bytes->size()) return false;
                snapshots[i]=it->second.bytes;
            }return true;
        };
        if(!bytes(d.vertex_constants->vcb,d.vbytes,vertex_cb_offsets,vertex_cb_lengths) || !bytes(d.pixel_constants->pcb,d.pbytes,pixel_cb_offsets,pixel_cb_lengths)) return false;
        return true;
    }
    void verify(const Snapshot& draw) {
        Bypass guard(*this);std::set<ID3D11Buffer*> seen;
        auto stage=[&](const auto& buffers,const auto& bytes,unsigned stage_offset) {
            for(unsigned i=0;i<14 && verify_remaining;++i) if(buffers[i] && seen.insert(buffers[i].Get()).second) {
                D3D11_BUFFER_DESC desc{};buffers[i]->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
                Ref<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging));context->CopyResource(staging.Get(),buffers[i].Get());
                D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
                const auto* gpu=static_cast<const std::byte*>(map.pData);std::uint64_t differences=0;
                for(unsigned j=0;j<desc.ByteWidth;++j) if(gpu[j]!=(*bytes[i])[j]) {
                    if(!stats.constant_mismatches && !differences) {stats.first_mismatch_slot=stage_offset+i;stats.first_mismatch_offset=j;stats.first_expected_byte=std::to_integer<unsigned>((*bytes[i])[j]);stats.first_gpu_byte=std::to_integer<unsigned>(gpu[j]);}
                    ++differences;
                }
                context->Unmap(staging.Get(),0);++stats.constant_checks;if(differences) ++stats.constant_mismatches;stats.different_constant_bytes+=differences;--verify_remaining;
            }
        };
        stage(draw.vertex_constants->vcb,draw.vbytes,0);stage(draw.pixel_constants->pcb,draw.pbytes,14);
    }
    bool before(unsigned s,const std::uintptr_t* args) {
        using namespace slots;
        if(bypass) return false;
        if(GetCurrentThreadId()!=owner && !harmless(s)) {
            ++stats.foreign_calls;
            // Loading uses different context callers. Outside a captured pass
            // there is no suppressed GPU work. Within a pass a foreign caller
            // flushes before proceeding and prevents further capture in it.
            if(scope) {flush(false);scope_conflict=true;}
        }
        if(s==DrawIndexed) {
            ++stats.draws;
            if(scope && !enabled && verify_remaining && !scope_conflict && GetCurrentThreadId()==owner) {Snapshot d;if(capture(d)) verify(d);}
            if(scope && enabled && !disabled && !scope_conflict && GetCurrentThreadId()==owner) {
                Snapshot d;
                if(capture(d)) {d.count=static_cast<UINT>(args[0]);d.start=static_cast<UINT>(args[1]);d.base=static_cast<INT>(args[2]);queued.push_back(std::move(d));++stats.replaced;
                    if(recording==Recording::inline_serial)flush(false);else if(queued.size()>=256)flush();return true;}
                ++stats.unsupported;
            }
            flush();++stats.fallback;return false;
        }
        dirty_state|=invalidated_state(s);
        if(s==Map) {
            auto b=constant_buffer(reinterpret_cast<ID3D11Resource*>(args[0]),static_cast<UINT>(args[1]));
            if(b && (args[2]==D3D11_MAP_WRITE_DISCARD || args[2]==D3D11_MAP_WRITE_NO_OVERWRITE || args[2]==D3D11_MAP_WRITE)) return false;
        }
        if(s==Unmap) {
            const auto resource=reinterpret_cast<ID3D11Resource*>(args[0]);auto it=mapped.find(resource);
            if(it!=mapped.end()) {save(it->second.buffer.Get(),it->second.pointer,it->second.size);mapped.erase(it);return false;}
        }
        if(s==UpdateSubresource || s==UpdateSubresource1) {
            auto b=constant_buffer(reinterpret_cast<ID3D11Resource*>(args[0]),static_cast<UINT>(args[1]));
            if(b) {D3D11_BUFFER_DESC d{};b->GetDesc(&d);
                if(!args[2]) save(b.Get(),reinterpret_cast<const void*>(args[3]),d.ByteWidth);else constants.erase(b.Get());return false;}
        }
        if(harmless(s)) return false;
        flush();
        if(s==Begin) queries.insert(reinterpret_cast<ID3D11Asynchronous*>(args[0]));
        if(s==End) queries.erase(reinterpret_cast<ID3D11Asynchronous*>(args[0]));
        if(s==CopyResource || s==CopySubresourceRegion || s==CopySubresourceRegion1) {
            auto b=constant_buffer(reinterpret_cast<ID3D11Resource*>(args[0]));if(b) constants.erase(b.Get());
        }
        // An external command list may update buffers behind the adapter.
        if(s==ExecuteCommandList || s==SwapDeviceContextState || s==CopyStructureCount || s==DiscardResource || s==DiscardView || s==DiscardView1) constants.clear();
        return false;
    }
    void after(unsigned s,const std::uintptr_t* args,std::intptr_t result) {
        if(bypass || s!=slots::Map || FAILED(static_cast<HRESULT>(result))) return;
        auto* resource=reinterpret_cast<ID3D11Resource*>(args[0]);auto b=constant_buffer(resource,static_cast<UINT>(args[1]));
        if(b && (args[2]==D3D11_MAP_WRITE_DISCARD || args[2]==D3D11_MAP_WRITE_NO_OVERWRITE || args[2]==D3D11_MAP_WRITE)) {
            D3D11_BUFFER_DESC d{};b->GetDesc(&d);auto* map=reinterpret_cast<D3D11_MAPPED_SUBRESOURCE*>(args[4]);if(map) mapped[resource]={std::move(b),map->pData,d.ByteWidth};
        }
    }
    static void enter(void* p) noexcept {static_cast<Impl*>(p)->api_mutex.lock();}
    static void leave(void* p) noexcept {static_cast<Impl*>(p)->api_mutex.unlock();}
    static bool before_call(void* p,unsigned s,const std::uintptr_t* args) noexcept {
        auto& self=*static_cast<Impl*>(p);try {return self.before(s,args);}catch(...) {++self.stats.errors;self.flush(false);self.disabled=true;return false;}
    }
    static void after_call(void* p,unsigned s,const std::uintptr_t* args,std::intptr_t result) noexcept {
        auto& self=*static_cast<Impl*>(p);try {self.after(s,args,result);}catch(...) {++self.stats.errors;self.constants.clear();self.disabled=true;}
    }
};
Bridge::Bridge(ID3D11Device* d,ID3D11DeviceContext* c,unsigned n):impl_(std::make_unique<Impl>(d,c,n)) {}
Bridge::~Bridge()=default;
bool Bridge::attach() {auto& s=*impl_;std::lock_guard lock(s.api_mutex);s.attached=attach_context(s.context.Get(),&s,{&Impl::enter,&Impl::leave,&Impl::before_call,&Impl::after_call});return s.attached;}
void Bridge::begin(bool enabled,Recording recording,bool reduce_bindings) noexcept {
    auto& s=*impl_;std::lock_guard lock(s.api_mutex);
    if(!s.scope) {s.owner=GetCurrentThreadId();s.scope_conflict=false;}
    else if(GetCurrentThreadId()!=s.owner) {++s.stats.foreign_calls;s.flush(false);s.scope_conflict=true;}
    ++s.scope;s.enabled=enabled;s.recording=recording;s.reduce_bindings=reduce_bindings;++s.stats.scopes;
}
void Bridge::end() noexcept {auto& s=*impl_;std::lock_guard lock(s.api_mutex);s.flush();if(s.scope) --s.scope;}
void Bridge::verify_constants(bool on) noexcept {auto& s=*impl_;std::lock_guard lock(s.api_mutex);if(on && !s.verify_requested)s.verify_remaining=64;if(!on)s.verify_remaining=0;s.verify_requested=on;}
void Bridge::cache_state(bool on) noexcept {auto& s=*impl_;std::lock_guard lock(s.api_mutex);if(s.cache_enabled!=on) {s.cache_enabled=on;s.dirty_state=all_state;}}
void Bridge::share_bindings(bool on) noexcept {auto& s=*impl_;std::lock_guard lock(s.api_mutex);if(s.share_bindings!=on) {s.share_bindings=on;s.unpublished_state=all_state;s.published_bindings={};}}
Statistics Bridge::statistics() const noexcept {auto& s=*impl_;std::lock_guard lock(s.api_mutex);auto result=s.stats;result.disabled=s.disabled;
    result.snapshot_binding_sharing=s.share_bindings;
    result.private_uploads=s.serial.uploads;result.private_upload_reuses=s.serial.reuses;result.private_upload_bytes=s.serial.uploaded_bytes;
    result.recording_bindings=s.serial.bindings;result.recording_bindings_skipped=s.serial.bindings_skipped;
    for(const auto& recorder:s.recorders) {result.private_uploads+=recorder->uploads;result.private_upload_reuses+=recorder->reuses;result.private_upload_bytes+=recorder->uploaded_bytes;result.recording_bindings+=recorder->bindings;result.recording_bindings_skipped+=recorder->bindings_skipped;}
    for(unsigned i=0;i<s.count;++i) result.worker_ids[i]=GetThreadId(s.workers[i].native_handle());return result;}
}
