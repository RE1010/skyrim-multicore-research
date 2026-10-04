// Share only the independent test scene/oracle with the packet backend test.
#define main packet_backend_unused_main
#include "render_packets.cpp"
#undef main
#include "skyrim_mc/render_stream.hpp"
#include "render_bridge_call.hpp"
#include "startup_calls.hpp"
#include "context_slots.hpp"
#include <functional>

namespace {
unsigned abi_calls=0;void* abi_a=nullptr;std::uint32_t* abi_b=nullptr;std::uint32_t* abi_c=nullptr;void* abi_d=nullptr;
std::uint8_t abi_original(void* a,std::uint32_t* b,std::uint32_t* c,void* d,std::uint32_t e) {
    require(a==abi_a && b==abi_b && c==abi_c && d==abi_d && e==0xaabbccdd,"five engine arguments changed");++abi_calls;return 0xa5;
}
struct AbiScope {unsigned began=0,ended=0;void begin(){++began;}void end(){++ended;}} abi_scope;
std::uint8_t abi_hook(void* a,std::uint32_t* b,std::uint32_t* c,void* d,std::uint32_t e) {
    return render_bridge_call::invoke(&abi_original,abi_scope,a,b,c,d,e);
}
void test_abi() {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));require(memory,"ABI memory");
    // Caller copies its fifth argument past its own Win64 shadow space.
    const unsigned char caller[]{0x48,0x83,0xec,0x28,0x8b,0x44,0x24,0x50,0x89,0x44,0x24,0x20,0xe8,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
    std::memcpy(memory,caller,sizeof(caller));const auto displacement=static_cast<std::int32_t>(128-17);std::memcpy(memory+13,&displacement,4);
    const unsigned char jump[]{0xff,0x25,0,0,0,0};std::memcpy(memory+128,jump,6);auto original=&abi_original;std::memcpy(memory+134,&original,8);
    std::array<unsigned char,5> expected{};std::memcpy(expected.data(),memory+12,5);
    bool patched=false;require(startup_calls::install(std::array<startup_calls::Call,1>{{{memory+12,expected,reinterpret_cast<void*>(&abi_hook),memory+256}}},patched) && patched,"startup bridge patch");
    std::uint32_t b=7,c=11;abi_a=&abi_calls;abi_b=&b;abi_c=&c;abi_d=&abi_scope;
    const auto result=reinterpret_cast<render_bridge_call::Original>(memory)(abi_a,&b,&c,abi_d,0xaabbccdd);
    require(result==0xa5 && abi_calls==1 && abi_scope.began==1 && abi_scope.ended==1,"AL/original once/scope ABI");VirtualFree(memory,0,MEM_RELEASE);
}
unsigned test_shadow_passes(Fixture& f,skyrim_mc::stream::Bridge& bridge,unsigned& images,bool debug) {
    // The lighting image consumes both raw depth and comparison-sampler results.
    // Color-only draws cannot detect missing writes in a PS-null shadow pass.
    D3D11_TEXTURE2D_DESC texture_desc{};texture_desc.Width=texture_desc.Height=256;
    texture_desc.MipLevels=texture_desc.ArraySize=1;texture_desc.Format=DXGI_FORMAT_R32_TYPELESS;
    texture_desc.SampleDesc.Count=1;texture_desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    Ref<ID3D11Texture2D> shadow;Ref<ID3D11DepthStencilView> shadow_target,shadow_readonly;Ref<ID3D11ShaderResourceView> shadow_view;
    checked(f.device->CreateTexture2D(&texture_desc,nullptr,&shadow),"shadow depth texture");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};dsv_desc.Format=DXGI_FORMAT_D32_FLOAT;dsv_desc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    checked(f.device->CreateDepthStencilView(shadow.Get(),&dsv_desc,&shadow_target),"shadow DSV");
    dsv_desc.Flags=D3D11_DSV_READ_ONLY_DEPTH;
    checked(f.device->CreateDepthStencilView(shadow.Get(),&dsv_desc,&shadow_readonly),"read-only shadow DSV");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};srv_desc.Format=DXGI_FORMAT_R32_FLOAT;srv_desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv_desc.Texture2D.MipLevels=1;
    checked(f.device->CreateShaderResourceView(shadow.Get(),&srv_desc,&shadow_view),"shadow SRV");
    const char* shader=R"(
cbuffer C:register(b0) {float4 parameters;float4 transform;};
Texture2D<float> shadow:register(t5);SamplerComparisonState comparison:register(s3);
struct Out {float4 position:SV_Position;};
Out vertex(float2 xy:POSITION) {Out o;o.position=float4(transform.xy+xy*transform.zw,parameters.x,1);return o;}
float4 lighting(Out p):SV_Target {
    float depth=shadow.Load(int3(int2(p.position.xy),0));
    float visible=shadow.SampleCmpLevelZero(comparison,p.position.xy/256,parameters.y);
    return float4(visible,depth,1-visible,1);
}
)";
    Ref<ID3DBlob> vs_code,ps_code,error;Ref<ID3D11VertexShader> vertex;Ref<ID3D11PixelShader> lighting;
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&vs_code,&error),"shadow VS compile");
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"lighting","ps_5_0",0,0,&ps_code,&error),"shadow lighting compile");
    checked(f.device->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&vertex),"shadow VS");
    checked(f.device->CreatePixelShader(ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&lighting),"shadow lighting PS");
    Ref<ID3D11Buffer> constants;D3D11_BUFFER_DESC cb_desc{};cb_desc.ByteWidth=64;cb_desc.Usage=D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb_desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    checked(f.device->CreateBuffer(&cb_desc,nullptr,&constants),"shadow constants");
    std::array<Ref<ID3D11DepthStencilState>,3> depths;
    for(unsigned i=0;i<depths.size();++i) {
        D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;
        state.DepthWriteMask=i==2?D3D11_DEPTH_WRITE_MASK_ZERO:D3D11_DEPTH_WRITE_MASK_ALL;
        state.DepthFunc=i==1?D3D11_COMPARISON_ALWAYS:D3D11_COMPARISON_LESS;
        checked(f.device->CreateDepthStencilState(&state,&depths[i]),"shadow depth state");
    }
    std::array<Ref<ID3D11RasterizerState>,4> rasters;
    for(unsigned i=0;i<rasters.size();++i) {
        D3D11_RASTERIZER_DESC state{};state.FillMode=D3D11_FILL_SOLID;state.CullMode=i==3?D3D11_CULL_FRONT:D3D11_CULL_NONE;
        state.DepthClipEnable=TRUE;state.ScissorEnable=i!=1;
        // A bounded, visible bias makes omitted rasterizer changes affect pixels.
        if(i==2) {state.DepthBias=4194304;state.DepthBiasClamp=0.0625f;}
        checked(f.device->CreateRasterizerState(&state,&rasters[i]),"shadow rasterizer state");
    }
    std::array<Ref<ID3D11SamplerState>,2> samplers;
    for(unsigned i=0;i<samplers.size();++i) {
        D3D11_SAMPLER_DESC state{};state.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        state.AddressU=state.AddressV=state.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        state.ComparisonFunc=i?D3D11_COMPARISON_GREATER_EQUAL:D3D11_COMPARISON_LESS_EQUAL;state.MaxLOD=D3D11_FLOAT32_MAX;
        checked(f.device->CreateSamplerState(&state,&samplers[i]),"shadow comparison sampler");
    }
    Ref<ID3D11InfoQueue> messages;if(debug) {f.messages();checked(f.device.As(&messages),"shadow debug queue");messages->ClearStoredMessages();}
    unsigned warnings=0;
    // Seven draws exercise serial/deferred and isolated direct replay; 257
    // crosses worker dispatch and capacity, then leaves a one-draw tail.
    for(const unsigned length:{7u,257u}) {
        std::array<std::vector<unsigned char>,2> expected;
        for(unsigned variant=0;variant<4;++variant) {
            bridge.direct_small_batches(variant==2);f.context->ClearState();
            auto* cb=constants.Get();auto* vb=f.source.vertices.Get();
            f.context->VSSetConstantBuffers(0,1,&cb);f.context->PSSetConstantBuffers(0,1,&cb);
            f.context->IASetVertexBuffers(0,1,&vb,&f.source.stride,&f.source.vertex_offset);
            f.context->IASetIndexBuffer(f.source.indices.Get(),f.source.index_format,f.source.index_offset);
            f.context->IASetInputLayout(f.source.layout.Get());f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            f.context->VSSetShader(vertex.Get(),nullptr,0);f.context->OMSetBlendState(nullptr,nullptr,~0u);
            f.context->RSSetViewports(1,&f.source.viewport);
            const auto before=bridge.statistics();
            for(unsigned frame=0;frame<2;++frame) {
                f.clear(f.rtv.Get());bridge.begin(variant!=0,variant==3?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers);
                // Frame two intentionally still has this depth SRV bound from
                // lighting. A writable DSV must implicitly null it before use.
                f.context->OMSetRenderTargets(0,nullptr,shadow_target.Get());
                Ref<ID3D11ShaderResourceView> unbound;f.context->PSGetShaderResources(5,1,&unbound);
                require(!unbound,"shadow DSV did not implicitly unbind its SRV");
                f.context->ClearDepthStencilView(shadow_target.Get(),D3D11_CLEAR_DEPTH,1,0);
                f.context->PSSetShader(nullptr,nullptr,0);
                auto draw=[&](unsigned i,float depth,bool shadow_pass) {
                    const unsigned tile=i%64,x=tile%8,y=tile/8;
                    const std::array<float,8> values{depth,0.5f,0,0,x/4.0f-1,1-y/4.0f,0.25f,-0.25f};
                    D3D11_MAPPED_SUBRESOURCE map{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"shadow upload");
                    std::memset(map.pData,0,64);std::memcpy(map.pData,values.data(),sizeof(values));f.context->Unmap(constants.Get(),0);
                    D3D11_RECT scissor{static_cast<LONG>(x*32),static_cast<LONG>(y*32),static_cast<LONG>((x+1)*32),static_cast<LONG>((y+1)*32)};
                    if(shadow_pass && i%5==0)scissor.left+=16;
                    f.context->RSSetScissorRects(1,&scissor);
                    if(shadow_pass) {
                        f.context->OMSetDepthStencilState(depths[(i/64+tile+frame)%3].Get(),0);
                        f.context->RSSetState(rasters[(tile+frame)%4].Get());
                    } else {
                        auto* sampler=samplers[(tile+frame)%2].Get();f.context->PSSetSamplers(3,1,&sampler);
                    }
                    f.context->DrawIndexed(6,0,0);
                };
                for(unsigned i=0;i<length;++i)draw(i,((i/64+i%64+frame)%2)?0.75f:0.25f,true);
                // A read-only DSV may overlap the sampled SRV. The second
                // frame also depth-tests receiver geometry against that view.
                f.context->OMSetDepthStencilState(frame?depths[2].Get():nullptr,0);
                auto* target=f.rtv.Get();f.context->OMSetRenderTargets(1,&target,frame?shadow_readonly.Get():nullptr);
                auto* view=shadow_view.Get();f.context->PSSetShaderResources(5,1,&view);
                f.context->PSSetShader(lighting.Get(),nullptr,0);f.context->RSSetState(f.source.raster.Get());
                for(unsigned i=0;i<length;++i)draw(i,frame && i%2?0.5f:0,false);
                bridge.end();auto output=f.pixels();
                if(!variant) {
                    bool written_depth=false,clear_depth=false,visible=false,occluded=false;
                    for(std::size_t p=0;p<output.size();p+=4)if(output[p+3]==255) {
                        written_depth|=output[p+1]>0 && output[p+1]<250;clear_depth|=output[p+1]==255;
                        visible|=output[p]==255;occluded|=output[p]==0;
                    }
                    require(written_depth && clear_depth && visible && occluded,"shadow oracle did not consume depth writes and comparison results");
                    expected[frame]=std::move(output);
                    if(frame)require(expected[0]!=expected[1],"changing shadow states did not affect lighting");
                } else require(output==expected[frame],"PS-null shadow depth or sampled lighting changed under replacement");
                ++images;
            }
            const auto after=bridge.statistics();const auto draws=4u*length;
            if(variant)require(after.replaced-before.replaced==draws,"shadow draws silently fell back instead of exercising replacement");
            if(variant==3)require(after.serial_draws-before.serial_draws==draws && after.worker_draws==before.worker_draws,"shadow serial control dispatched workers");
            if(variant==1 || variant==2)require(after.worker_draws-before.worker_draws==(length==257?1024u:0u),"shadow group did not exercise expected worker dispatch");
            const auto direct=variant==2?(length==7?28u:4u):0u;
            require(after.direct_small_draws-before.direct_small_draws==direct,"shadow direct-small control did not exercise its intended tail");
            if(messages) {
                unsigned hazards=0;
                for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
                    SIZE_T size=0;checked(messages->GetMessage(i,nullptr,&size),"shadow diagnostic size");
                    std::vector<std::byte> bytes(size);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());checked(messages->GetMessage(i,message,&size),"shadow diagnostic");
                    if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) {
                        const bool expected_hazard=message->ID==D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD || message->ID==D3D11_MESSAGE_ID_DEVICE_PSSETSHADERRESOURCES_HAZARD;
                        if(!expected_hazard)std::cerr<<"Shadow diagnostic ID "<<message->ID<<": "<<message->pDescription<<'\n';
                        require(expected_hazard,"unexpected diagnostic in shadow depth test");++hazards;
                    }
                }
                require(hazards==2,"shadow SRV/DSV transition did not produce exactly its intentional hazard diagnostics");warnings+=hazards;messages->ClearStoredMessages();
            }
        }
    }
    return warnings;
}
}

int main(int argc,char** argv) {
    try {
        bool debug=true,sharing=true,flat_lookup=true,direct_small=false;
        for(int a=1;a<argc;++a) {if(std::strcmp(argv[a],"--hardware")==0 || std::strcmp(argv[a],"--no-debug")==0)debug=false;else if(std::strcmp(argv[a],"--owned-snapshots")==0)sharing=false;else if(std::strcmp(argv[a],"--map-uploads")==0)flat_lookup=false;else if(std::strcmp(argv[a],"--direct-small")==0)direct_small=true;else throw std::runtime_error("unknown stream test option");}
        test_abi();Fixture f(debug);unsigned images=0;
        using Update=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
        const auto original_update=reinterpret_cast<Update>((*reinterpret_cast<void***>(f.context.Get()))[skyrim_mc::stream::slots::UpdateSubresource]);
        Ref<ID3D11Buffer> constants;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        checked(f.device->CreateBuffer(&desc,nullptr,&constants),"stream constants");
        skyrim_mc::stream::Bridge bridge(f.device.Get(),f.context.Get(),4);require(bridge.attach(),"context attachment");bridge.share_bindings(sharing);bridge.flat_upload_lookup(flat_lookup);
        const auto caps=bridge.capabilities();D3D11_FEATURE_DATA_THREADING direct_caps{};
        const auto direct_hr=f.device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&direct_caps,sizeof(direct_caps));
        require(caps.threading_query_result==direct_hr && caps.creation_flags==f.device->GetCreationFlags() && caps.feature_level==static_cast<std::uint32_t>(f.device->GetFeatureLevel()),"device capability report identity");
        if(SUCCEEDED(direct_hr))require(caps.driver_command_lists==(direct_caps.DriverCommandLists!=FALSE) && caps.driver_concurrent_creates==(direct_caps.DriverConcurrentCreates!=FALSE),"device threading flags differ");
        bridge.direct_small_batches(direct_small);require(bridge.statistics().direct_small_available,"D3D11 context-state isolation unavailable");
        std::function<void(const Packet&)> before_draw;
        auto issue=[&](const Packet& p,bool update=true) {
            if(update) {D3D11_MAPPED_SUBRESOURCE m{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m),"engine upload");
                std::memset(m.pData,0,64);std::memcpy(m.pData,p.constants.data(),p.constants.size());f.context->Unmap(constants.Get(),0);}
            auto* cb=constants.Get();f.context->VSSetConstantBuffers(0,1,&cb);f.context->PSSetConstantBuffers(0,1,&cb);
            auto* vb=p.vertices.Get();f.context->IASetVertexBuffers(0,1,&vb,&p.stride,&p.vertex_offset);
            f.context->IASetIndexBuffer(p.indices.Get(),p.index_format,p.index_offset);f.context->IASetInputLayout(p.layout.Get());f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            f.context->VSSetShader(p.vertex_shader.Get(),nullptr,0);f.context->PSSetShader(p.pixel_shader.Get(),nullptr,0);
            auto* view=p.texture.Get();auto* sampler=p.sampler.Get();f.context->PSSetShaderResources(0,1,&view);f.context->PSSetSamplers(0,1,&sampler);
            f.context->RSSetState(p.raster.Get());f.context->RSSetViewports(1,&p.viewport);f.context->RSSetScissorRects(1,&p.scissor);
            const float factor[]{1,1,1,1};f.context->OMSetBlendState(p.blend.Get(),factor,~0u);f.context->OMSetDepthStencilState(p.depth.Get(),0);
            if(before_draw) before_draw(p);
            f.context->DrawIndexed(p.index_count,p.index_start,p.base_vertex);
        };
        auto source=f.packets(3);auto* target=f.rtv.Get();
        std::uint64_t reduced_material_bindings=0,full_material_bindings=0;
        for(unsigned variant=0;variant<5;++variant) {
            f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            const auto recording=variant==2?skyrim_mc::stream::Recording::serial:variant==3?skyrim_mc::stream::Recording::inline_serial:skyrim_mc::stream::Recording::workers;
            const auto variant_before=bridge.statistics();bridge.begin(variant!=0,recording,variant!=4);
            for(std::size_t i=0;i<source.size();++i) {
                issue(source[i]);
                if(variant==2 && i%73==0) {
                    // Real GPU mutation/readback barrier while draws are queued.
                    f.context->CopyResource(f.staging.Get(),f.target.Get());
                }
                if(variant==3 && i%91==0) {f.context->Flush();}
            }
            bridge.end();require(f.pixels()==Fixture::oracle(3),"stream ordered replacement/original oracle");++images;
            const auto variant_after=bridge.statistics();if(variant==2 || variant==3)require(variant_after.worker_draws==variant_before.worker_draws && variant_after.serial_draws==variant_before.serial_draws+source.size(),"serial controls did worker work or lost original draws");
            if(variant==1)reduced_material_bindings=variant_after.recording_bindings-variant_before.recording_bindings;
            if(variant==4)full_material_bindings=variant_after.recording_bindings-variant_before.recording_bindings;
            Ref<ID3D11PixelShader> shader;f.context->PSGetShader(&shader,nullptr,nullptr);require(shader.Get()==source.back().pixel_shader.Get(),"engine state preservation");
        }
        require(reduced_material_bindings<full_material_bindings,"redundant material bindings were not reduced");
        // Free-draw removes only the actual scoped indexed draw. It must avoid
        // capture/replay, preserve outside-scope draws, and recover without a
        // control-thread response when its native deadline expires.
        f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);const auto blank=f.pixels();
        bridge.suppress_indexed_draws(GetTickCount64()+10000);const auto outside_before=bridge.statistics();issue(source[0]);
        require(bridge.statistics().suppressed_indexed_draws==outside_before.suppressed_indexed_draws,"free-draw affected outside-scope rendering");
        f.clear(target);const auto free_before=bridge.statistics();bridge.begin(false);
        for(unsigned i=0;i<32;++i)issue(source[i]);bridge.end();const auto free_after=bridge.statistics();
        require(f.pixels()==blank,"free-draw did not leave the cleared GPU output");++images;
        require(free_after.suppressed_indexed_draws-free_before.suppressed_indexed_draws==32 && free_after.capture_attempts==free_before.capture_attempts && free_after.replaced==free_before.replaced && free_after.batches==free_before.batches,"free-draw captured/replayed work or lost accounting");
        bridge.suppress_indexed_draws(0);f.clear(target);bridge.begin(false);for(const auto& packet:source)issue(packet);bridge.end();
        require(f.pixels()==Fixture::oracle(3),"free-draw explicit off did not restore original output");++images;
        const auto expiry=GetTickCount64()+32;bridge.suppress_indexed_draws(expiry);
        while(GetTickCount64()<expiry)Sleep(1);const auto expired_before=bridge.statistics();
        f.clear(target);bridge.begin(false);for(const auto& packet:source)issue(packet);bridge.end();
        require(f.pixels()==Fixture::oracle(3) && bridge.statistics().suppressed_indexed_draws==expired_before.suppressed_indexed_draws && !bridge.statistics().draw_suppression_active,"free-draw native expiry did not restore original output");++images;
        bridge.suppress_indexed_draws(GetTickCount64()+20000);require(!bridge.statistics().draw_suppression_active,"free-draw accepted an overlong deadline");
        // Inventory mode must keep original state, mapped pointers and GPU
        // results while eliminating observer payload copies. Check both timing
        // densities and a counter-only control against the independent oracle.
        for(const unsigned sampling:{0u,1u,16u}) {
            bridge.profile_original(true,sampling?GetTickCount64()+10000:0,sampling?sampling:16);
            const auto inventory_before=bridge.statistics();
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            bridge.begin(true);for(const auto& packet:source)issue(packet);bridge.end();
            require(f.pixels()==Fixture::oracle(3),"original inventory forwarding changed GPU image");++images;
            const auto inventory_after=bridge.statistics();
            require(inventory_after.clean_forwarding && inventory_after.upload_copies==inventory_before.upload_copies && inventory_after.upload_copy_bytes==inventory_before.upload_copy_bytes && inventory_after.capture_attempts==inventory_before.capture_attempts && inventory_after.replaced==inventory_before.replaced && inventory_after.worker_draws==inventory_before.worker_draws,"original inventory retained payloads or replayed");
            using namespace skyrim_mc::stream::slots;
            const auto& draw=inventory_after.context_methods[DrawIndexed];const auto& old_draw=inventory_before.context_methods[DrawIndexed];
            require(draw.calls-old_draw.calls==(sampling?source.size():0) && draw.scoped_calls-old_draw.scoped_calls==(sampling?source.size():0),"inventory draw/scoped coverage");
            if(sampling) {
                require(draw.samples>old_draw.samples && draw.samples-old_draw.samples<=draw.calls-old_draw.calls && draw.ticks>=old_draw.ticks,"inventory sample accounting");
                require(inventory_after.context_methods[Map].scoped_calls-inventory_before.context_methods[Map].scoped_calls==source.size() && inventory_after.context_methods[Unmap].scoped_calls-inventory_before.context_methods[Unmap].scoped_calls==source.size(),"inventory missed original maps");
                // f.pixels performs one READ Map outside the guarded scope.
                require(inventory_after.map_modes[0]-inventory_before.map_modes[0]==1 && inventory_after.map_modes[3]-inventory_before.map_modes[3]==source.size(),"inventory conflated readback/discard modes");
                if(sampling==1)require(draw.samples-old_draw.samples==source.size(),"full-density timer skipped real calls");
                Ref<ID3D11PixelShader> saved;f.context->PSGetShader(&saved,nullptr,nullptr);
                require(saved.Get()==source.back().pixel_shader.Get(),"inventory getter returned wrong COM object");
                Ref<ID3D11Query> event;D3D11_QUERY_DESC event_desc{D3D11_QUERY_EVENT,0};checked(f.device->CreateQuery(&event_desc,&event),"inventory event query");
                f.context->End(event.Get());const auto query_stats=bridge.statistics();
                const auto status=f.context->GetData(event.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                require(status==S_OK || status==S_FALSE,"inventory changed query HRESULT");
                const auto query_after=bridge.statistics();
                require(query_after.context_methods[GetData].calls==query_stats.context_methods[GetData].calls+1 && query_after.context_methods[GetData].pending-query_stats.context_methods[GetData].pending==(status==S_FALSE?1u:0u),"inventory lost pending query result");
            } else require(!inventory_after.context_profile_active,"clean control unexpectedly timed context calls");
        }
        const auto profile_expiry=GetTickCount64()+32;bridge.profile_original(true,profile_expiry,1);
        while(GetTickCount64()<profile_expiry)Sleep(1);const auto profile_expired_before=bridge.statistics();
        f.clear(target);bridge.begin(false);for(const auto& packet:source)issue(packet);bridge.end();
        const auto profile_expired_after=bridge.statistics();
        require(!profile_expired_after.context_profile_active && profile_expired_after.context_methods[skyrim_mc::stream::slots::DrawIndexed].calls==profile_expired_before.context_methods[skyrim_mc::stream::slots::DrawIndexed].calls && f.pixels()==Fixture::oracle(3),"inventory native expiry changed output or kept counting");++images;
        bridge.profile_original(false);require(!bridge.statistics().clean_forwarding,"inventory did not restore normal adapter mode");
        // Unsupported unknown constant must execute original, not be discarded.
        Ref<ID3D11Buffer> unknown;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
        const auto bytes=source[0].constants;std::array<std::byte,64> initial{};std::copy(bytes.begin(),bytes.end(),initial.begin());D3D11_SUBRESOURCE_DATA data{initial.data(),0,0};
        checked(f.device->CreateBuffer(&desc,&data,&unknown),"unknown constant");
        const auto before=bridge.statistics();std::swap(constants,unknown);
        f.clear(target);bridge.begin(true);issue(source[0],false);bridge.end();std::swap(constants,unknown);
        require(bridge.statistics().fallback==before.fallback+1,"unknown constant fallback");
        // Queries enclose original GPU work; worker recording is refused.
        Ref<ID3D11Query> query;D3D11_QUERY_DESC q{D3D11_QUERY_OCCLUSION,0};checked(f.device->CreateQuery(&q,&query),"query");
        const auto query_before=bridge.statistics();f.context->Begin(query.Get());bridge.begin(true);issue(source[0]);bridge.end();f.context->End(query.Get());
        require(bridge.statistics().replaced==query_before.replaced,"query scope offloaded");
        Ref<ID3D11Query> free_query;checked(f.device->CreateQuery(&q,&free_query),"free-draw query");
        const auto free_query_before=bridge.statistics();bridge.suppress_indexed_draws(GetTickCount64()+10000);
        f.context->Begin(free_query.Get());bridge.begin(false);issue(source[0]);bridge.end();f.context->End(free_query.Get());bridge.suppress_indexed_draws(0);
        require(bridge.statistics().suppressed_indexed_draws==free_query_before.suppressed_indexed_draws && bridge.statistics().suppression_query_fallbacks==free_query_before.suppression_query_fallbacks+1,"free-draw changed query-enclosed draws");
        // Distinct same-sized buffers at nonzero VS/PS slots. PS is a DEFAULT
        // buffer updated in-place; workers must see each draw's owned bytes.
        const char* multi_shader=R"(
cbuffer V:register(b3) {float4 ignored;float4 transform;};
cbuffer P:register(b7) {float4 color;float4 other;};
Texture2D texture0:register(t0);
struct Out {float4 position:SV_Position;};
Out vertex(float2 xy:POSITION) {Out o;o.position=float4(transform.xy+xy*transform.zw,0,1);return o;}
float4 pixel(Out p):SV_Target {return color*texture0.Load(int3(0,0,0));}
)";
        Ref<ID3DBlob> vs_code,ps_code,error;Ref<ID3D11VertexShader> multi_vs;Ref<ID3D11PixelShader> multi_ps;
        checked(D3DCompile(multi_shader,std::strlen(multi_shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&vs_code,&error),"multiple slot VS");
        checked(D3DCompile(multi_shader,std::strlen(multi_shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&ps_code,&error),"multiple slot PS");
        checked(f.device->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&multi_vs),"multi VS creation");
        checked(f.device->CreatePixelShader(ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&multi_ps),"multi PS creation");
        Ref<ID3D11Buffer> pixel_constants;checked(f.device->CreateBuffer(&desc,nullptr,&pixel_constants),"multi default CB");
        before_draw=[&](const Packet& p) {
            std::array<std::byte,64> copy{};std::memcpy(copy.data(),p.constants.data(),32);float changed=0.375f;std::memcpy(copy.data()+4,&changed,4);
            f.context->UpdateSubresource(pixel_constants.Get(),0,nullptr,copy.data(),0,0);
            auto* vcb=constants.Get();auto* pcb=pixel_constants.Get();ID3D11Buffer* empty=nullptr;
            f.context->VSSetConstantBuffers(0,1,&empty);f.context->PSSetConstantBuffers(0,1,&empty);
            f.context->VSSetConstantBuffers(3,1,&vcb);f.context->PSSetConstantBuffers(7,1,&pcb);
            f.context->VSSetShader(multi_vs.Get(),nullptr,0);f.context->PSSetShader(multi_ps.Get(),nullptr,0);
        };
        std::vector<unsigned char> multi_expected;
        for(unsigned i=0;i<2;++i) {
            f.clear(target);bridge.begin(i!=0);for(const auto& p:source) issue(p);bridge.end();
            auto output=f.pixels();if(!i) multi_expected=std::move(output);else require(output==multi_expected,"multi-slot DEFAULT buffer snapshots");++images;
        }
        // Frame/material constants often survive many geometry draws. Reuse
        // unchanged owned bytes, then update them across list boundaries.
        std::vector<unsigned char> reuse_expected;
        for(unsigned variant=0;variant<3;++variant) {
            std::size_t ordinal=0;
            before_draw=[&](const Packet& p) {
                if(ordinal%97==0) {
                    std::array<std::byte,64> copy{};std::memcpy(copy.data(),p.constants.data(),32);
                    const float green=(ordinal/97)%2?0.25f:0.75f;std::memcpy(copy.data()+4,&green,4);
                    f.context->UpdateSubresource(pixel_constants.Get(),0,nullptr,copy.data(),0,0);
                }
                ++ordinal;auto* vcb=constants.Get();auto* pcb=pixel_constants.Get();ID3D11Buffer* empty=nullptr;
                f.context->VSSetConstantBuffers(0,1,&empty);f.context->PSSetConstantBuffers(0,1,&empty);
                f.context->VSSetConstantBuffers(3,1,&vcb);f.context->PSSetConstantBuffers(7,1,&pcb);
                f.context->VSSetShader(multi_vs.Get(),nullptr,0);f.context->PSSetShader(multi_ps.Get(),nullptr,0);
            };
            f.clear(target);bridge.begin(variant!=0,variant==1?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers);
            for(const auto& p:source)issue(p);bridge.end();auto output=f.pixels();
            if(!variant)reuse_expected=std::move(output);else require(output==reuse_expected,"reused constants changed pixels across command lists");++images;
        }
        require(bridge.statistics().private_upload_reuses>1000,"unchanged frame constants were repeatedly uploaded");
        // Constants change without rebinding. The cache must refresh owned
        // bytes on each draw, preserve draw arguments, and avoid all getters
        // except the genuinely changing scissor state.
        before_draw={};std::vector<unsigned char> cache_expected;
        std::uint64_t cached_getters=0,uncached_getters=0,cached_ticks=0,uncached_ticks=0;
        std::array<std::uint64_t,2> ownership_capture{},ownership_publish{},ownership_release{},ownership_copies{},ownership_reuses{},ownership_bindings{},ownership_batches{};
        for(unsigned variant=0;variant<4;++variant) {
            bridge.share_bindings(variant==3?!sharing:sharing);bridge.cache_state(variant!=1);f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            const auto cache_before=bridge.statistics();bridge.begin(variant!=0);
            for(std::size_t i=0;i<source.size();++i) {
                auto p=source[i];p.pixel_shader=f.source.pixel_shader;p.texture=f.source.texture;p.blend=nullptr;
                if(!i)issue(p);
                else {
                    D3D11_MAPPED_SUBRESOURCE upload{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&upload),"cache version upload");
                    std::memset(upload.pData,0,64);std::memcpy(upload.pData,p.constants.data(),p.constants.size());f.context->Unmap(constants.Get(),0);
                    f.context->RSSetScissorRects(1,&p.scissor);
                    // Different arguments are part of each command, never cache
                    // state: the empty draw must not become a six-index draw.
                    f.context->DrawIndexed(i%113==0?0:p.index_count,p.index_start,p.base_vertex);
                }
                if(i==37) {
                    // A refresh must restore capacity, rather than reusing the
                    // previous one-viewport/scissor result as its capacity.
                    D3D11_VIEWPORT two[]{p.viewport,p.viewport};D3D11_RECT rects[]{p.scissor,p.scissor};
                    f.context->RSSetViewports(2,two);f.context->RSSetScissorRects(2,rects);
                }
            }
            bridge.end();auto output=f.pixels();if(!variant)cache_expected=std::move(output);else require(output==cache_expected,"cached bindings froze constants or draw arguments");++images;
            const auto cache_after=bridge.statistics();
            if(variant>=2) {const unsigned slot=(variant==3?!sharing:sharing)?1:0;
                ownership_capture[slot]=cache_after.capture_ticks-cache_before.capture_ticks;
                ownership_publish[slot]=cache_after.snapshot_publish_ticks-cache_before.snapshot_publish_ticks;
                ownership_release[slot]=cache_after.queue_release_ticks-cache_before.queue_release_ticks;
                ownership_copies[slot]=cache_after.snapshot_group_copies-cache_before.snapshot_group_copies;
                ownership_reuses[slot]=cache_after.snapshot_group_reuses-cache_before.snapshot_group_reuses;
                ownership_bindings[slot]=cache_after.recording_bindings-cache_before.recording_bindings;
                ownership_batches[slot]=cache_after.batches-cache_before.batches;
            }
            if(variant==1) {uncached_getters=cache_after.snapshot_getters-cache_before.snapshot_getters;uncached_ticks=cache_after.capture_ticks-cache_before.capture_ticks;}
            if(variant==2) {cached_getters=cache_after.snapshot_getters-cache_before.snapshot_getters;cached_ticks=cache_after.capture_ticks-cache_before.capture_ticks;
                require(cache_after.state_group_reuses-cache_before.state_group_reuses>30000,"unchanged binding groups were not reused");}
        }
        require(cached_getters*10<uncached_getters,"binding cache did not remove constant-only draw getters");
        require(ownership_copies[1]<ownership_copies[0] && ownership_reuses[1]>0 && !ownership_reuses[0],"immutable group reuse not exercised");
        require(ownership_bindings[0]==ownership_bindings[1] && ownership_batches[0]==ownership_batches[1],"ownership control changed bindings or flush boundaries");
        bridge.share_bindings(sharing);
        // Sparse material changes exercise selective invalidation while the
        // full independent CPU oracle still defines every final pixel.
        for(unsigned mode=0;mode<2;++mode) {
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);bridge.begin(mode!=0);
            for(std::size_t i=0;i<source.size();++i) {
                const auto& p=source[i];if(!i || i==513)issue(p);
                else {
                    D3D11_MAPPED_SUBRESOURCE upload{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&upload),"sparse cache upload");
                    std::memset(upload.pData,0,64);std::memcpy(upload.pData,p.constants.data(),p.constants.size());f.context->Unmap(constants.Get(),0);
                    if(p.pixel_shader!=source[i-1].pixel_shader)f.context->PSSetShader(p.pixel_shader.Get(),nullptr,0);
                    if(p.texture!=source[i-1].texture) {auto* view=p.texture.Get();f.context->PSSetShaderResources(0,1,&view);}
                    if(p.blend!=source[i-1].blend) {const float factor[]{1,1,1,1};f.context->OMSetBlendState(p.blend.Get(),factor,~0u);}
                    f.context->RSSetScissorRects(1,&p.scissor);f.context->DrawIndexed(p.index_count,p.index_start,p.base_vertex);
                }
                if(i==512) {f.context->ClearState();f.context->OMSetRenderTargets(1,&target,nullptr);}
            }
            bridge.end();auto output=f.pixels();
            require(output==Fixture::oracle(3),"selective state cache or ClearState invalidation changed pixels");++images;
        }
        // Binding an output implicitly unbinds the same subresource from PS.
        // A cache based only on explicit PS setters would retain stale SRVs.
        Ref<ID3D11Texture2D> hazard_texture;Ref<ID3D11ShaderResourceView> hazard_view;Ref<ID3D11RenderTargetView> hazard_target;
        D3D11_TEXTURE2D_DESC hazard_desc{};f.target->GetDesc(&hazard_desc);hazard_desc.BindFlags|=D3D11_BIND_SHADER_RESOURCE;
        checked(f.device->CreateTexture2D(&hazard_desc,nullptr,&hazard_texture),"hazard texture");
        checked(f.device->CreateShaderResourceView(hazard_texture.Get(),nullptr,&hazard_view),"hazard SRV");
        checked(f.device->CreateRenderTargetView(hazard_texture.Get(),nullptr,&hazard_target),"hazard RTV");
        Ref<ID3D11InfoQueue> hazard_messages;if(debug) {f.messages();checked(f.device.As(&hazard_messages),"hazard queue");hazard_messages->ClearStoredMessages();}
        unsigned expected_hazard_warnings=0;std::vector<unsigned char> hazard_expected;
        for(unsigned variant=0;variant<3;++variant) {
            bridge.cache_state(variant!=1);f.context->ClearState();f.clear(target);
            const float white[]{1,1,1,1};f.context->ClearRenderTargetView(hazard_target.Get(),white);f.context->OMSetRenderTargets(1,&target,nullptr);
            bridge.begin(variant!=0);auto first=source[0];first.pixel_shader=f.source.pixel_shader;first.texture=hazard_view;first.blend=nullptr;issue(first);
            auto* conflicting=hazard_target.Get();f.context->OMSetRenderTargets(1,&conflicting,nullptr);
            Ref<ID3D11ShaderResourceView> actual;f.context->PSGetShaderResources(0,1,&actual);require(!actual,"runtime did not perform expected implicit SRV unbinding");
            f.context->OMSetRenderTargets(1,&target,nullptr);
            for(std::size_t i=1;i<64;++i) {
                D3D11_MAPPED_SUBRESOURCE upload{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&upload),"hazard constants");
                std::memset(upload.pData,0,64);std::memcpy(upload.pData,source[i].constants.data(),source[i].constants.size());f.context->Unmap(constants.Get(),0);
                f.context->RSSetScissorRects(1,&source[i].scissor);f.context->DrawIndexed(6,0,0);
            }
            bridge.end();auto output=f.pixels();if(!variant)hazard_expected=std::move(output);else require(output==hazard_expected,"implicit output hazard left cached SRVs bound");++images;
            if(hazard_messages) {
                for(UINT64 i=0;i<hazard_messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
                    SIZE_T length=0;checked(hazard_messages->GetMessage(i,nullptr,&length),"hazard message size");
                    std::vector<std::byte> message(length);auto* entry=reinterpret_cast<D3D11_MESSAGE*>(message.data());checked(hazard_messages->GetMessage(i,entry,&length),"hazard message");
                    if(entry->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) {
                        const bool expected=entry->ID==D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD || entry->ID==D3D11_MESSAGE_ID_DEVICE_PSSETSHADERRESOURCES_HAZARD;
                        if(!expected)std::cerr<<"Hazard diagnostic ID "<<entry->ID<<": "<<entry->pDescription<<'\n';
                        require(expected,"unexpected diagnostic in deliberate output hazard test");++expected_hazard_warnings;
                    }
                }
                hazard_messages->ClearStoredMessages();
            }
        }
        if(debug)require(expected_hazard_warnings==6,"deliberate runtime hazards not diagnosed twice per variant");
        // Pixel-visible high-slot resources and samplers exercise explicit
        // nonnull/null/nonnull transitions, including repeated equal groups.
        const char* high_shader=R"(
cbuffer P:register(b7){float4 color;};
Texture2D a:register(t5);Texture2D b:register(t97);
SamplerState sa:register(s3);SamplerState sb:register(s11);
float4 shade(){return (a.SampleLevel(sa,float2(.37,.63),0)+b.SampleLevel(sb,float2(.61,.29),0))*.5;}
float4 pixel(float4 p:SV_Position):SV_Target{return shade()*color;}
float4 free_pixel(float4 p:SV_Position):SV_Target{return shade();}
)";
        Ref<ID3DBlob> high_code,free_code;Ref<ID3D11PixelShader> high_ps,free_ps;
        checked(D3DCompile(high_shader,std::strlen(high_shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&high_code,&error),"high slot PS compile");
        checked(D3DCompile(high_shader,std::strlen(high_shader),nullptr,nullptr,nullptr,"free_pixel","ps_5_0",0,0,&free_code,&error),"free PS compile");
        checked(f.device->CreatePixelShader(high_code->GetBufferPointer(),high_code->GetBufferSize(),nullptr,&high_ps),"high slot PS");
        checked(f.device->CreatePixelShader(free_code->GetBufferPointer(),free_code->GetBufferSize(),nullptr,&free_ps),"free PS");
        D3D11_TEXTURE2D_DESC sample_desc{};sample_desc.Width=sample_desc.Height=2;sample_desc.MipLevels=sample_desc.ArraySize=1;
        sample_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sample_desc.SampleDesc.Count=1;sample_desc.Usage=D3D11_USAGE_IMMUTABLE;sample_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const UINT sample_pixels[]{0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};D3D11_SUBRESOURCE_DATA sample_data{sample_pixels,8,0};
        Ref<ID3D11Texture2D> sample_texture;Ref<ID3D11ShaderResourceView> sample_view;
        checked(f.device->CreateTexture2D(&sample_desc,&sample_data,&sample_texture),"sample texture");checked(f.device->CreateShaderResourceView(sample_texture.Get(),nullptr,&sample_view),"sample view");
        D3D11_SAMPLER_DESC linear_desc{};f.source.sampler->GetDesc(&linear_desc);linear_desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        Ref<ID3D11SamplerState> linear_sampler;checked(f.device->CreateSamplerState(&linear_desc,&linear_sampler),"linear sampler");
        std::vector<unsigned char> high_expected;
        for(unsigned variant=0;variant<4;++variant) {
            std::size_t ordinal=0;before_draw=[&](const Packet&) {
                const auto phase=(ordinal++/8)%4;
                ID3D11ShaderResourceView* a=phase==1?nullptr:sample_view.Get();ID3D11ShaderResourceView* b=phase==3?nullptr:f.source.texture.Get();
                f.context->PSSetShaderResources(5,1,&a);f.context->PSSetShaderResources(97,1,&b);
                ID3D11SamplerState* sa=phase%2?linear_sampler.Get():f.source.sampler.Get();ID3D11SamplerState* sb=phase%2?f.source.sampler.Get():linear_sampler.Get();
                f.context->PSSetSamplers(3,1,&sa);f.context->PSSetSamplers(11,1,&sb);
                const float tint[16]{.75f,.5f,.25f,1};f.context->UpdateSubresource(pixel_constants.Get(),0,nullptr,tint,0,0);
                auto* cb=phase==2?nullptr:pixel_constants.Get();f.context->PSSetConstantBuffers(7,1,&cb);f.context->PSSetShader(phase==2?free_ps.Get():high_ps.Get(),nullptr,0);
            };
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            bridge.begin(variant!=0,variant==2?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers,variant!=3);
            for(const auto& p:source)issue(p);bridge.end();auto output=f.pixels();if(!variant)high_expected=std::move(output);else require(output==high_expected,"high slot/null/resource/sampler binding transition changed pixels");++images;
        }
        // Same objects with different numeric setter arguments: buffer layout,
        // index format/offset, viewport and blend factors must all be compared.
        before_draw={};std::array<std::byte,144> packed_vertices{};
        const float packed_xy[]{0,0,1,0,0,1,0,1,1,0,1,1};
        for(unsigned i=0;i<6;++i)std::memcpy(packed_vertices.data()+i*16,packed_xy+i*2,8);
        std::memcpy(packed_vertices.data()+96,packed_xy,sizeof(packed_xy));
        D3D11_BUFFER_DESC numeric_desc{};numeric_desc.ByteWidth=static_cast<UINT>(packed_vertices.size());numeric_desc.Usage=D3D11_USAGE_IMMUTABLE;numeric_desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA numeric_data{packed_vertices.data(),0,0};Ref<ID3D11Buffer> numeric_vertices,numeric_indices;
        checked(f.device->CreateBuffer(&numeric_desc,&numeric_data,&numeric_vertices),"numeric vertex buffer");
        std::array<std::byte,64> packed_indices{};const UINT wide_indices[]{0,1,2,3,4,5};const unsigned short narrow_indices[]{0,1,2,3,4,5};
        std::memcpy(packed_indices.data(),wide_indices,sizeof(wide_indices));std::memcpy(packed_indices.data()+32,narrow_indices,sizeof(narrow_indices));
        numeric_desc.ByteWidth=static_cast<UINT>(packed_indices.size());numeric_desc.BindFlags=D3D11_BIND_INDEX_BUFFER;numeric_data.pSysMem=packed_indices.data();
        checked(f.device->CreateBuffer(&numeric_desc,&numeric_data,&numeric_indices),"numeric index buffer");
        D3D11_BLEND_DESC factor_desc{};auto& factor_target=factor_desc.RenderTarget[0];factor_target.BlendEnable=TRUE;factor_target.SrcBlend=D3D11_BLEND_BLEND_FACTOR;
        factor_target.DestBlend=D3D11_BLEND_ZERO;factor_target.BlendOp=D3D11_BLEND_OP_ADD;factor_target.SrcBlendAlpha=D3D11_BLEND_ONE;factor_target.DestBlendAlpha=D3D11_BLEND_ZERO;factor_target.BlendOpAlpha=D3D11_BLEND_OP_ADD;factor_target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        Ref<ID3D11BlendState> factor_blend;checked(f.device->CreateBlendState(&factor_desc,&factor_blend),"factor blend");std::vector<unsigned char> numeric_expected;
        for(unsigned variant=0;variant<4;++variant) {
            std::size_t ordinal=0;before_draw=[&](const Packet& p) {
                const bool changed=(ordinal++/8)%2!=0;auto* vertices=numeric_vertices.Get();const UINT stride=changed?8:16,offset=changed?96:0;
                f.context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);f.context->IASetIndexBuffer(numeric_indices.Get(),changed?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,changed?32:0);
                const float factors[]{changed?.25f:.75f,changed?.8f:.4f,1,1};f.context->OMSetBlendState(factor_blend.Get(),factors,~0u);
                auto viewport=p.viewport;if(changed) {viewport.TopLeftX=8;viewport.Width=240;}f.context->RSSetViewports(1,&viewport);
            };
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            bridge.begin(variant!=0,variant==2?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers,variant!=3);
            for(const auto& p:source)issue(p);bridge.end();auto output=f.pixels();if(!variant)numeric_expected=std::move(output);else require(output==numeric_expected,"same-object numeric binding changes lost");++images;
        }
        // GPU mutation of DEFAULT geometry flushes earlier snapshots before
        // changing the resource. Unsupported nonindexed draws also flush first.
        before_draw={};f.context->ClearState();Ref<ID3D11Buffer> mutable_vertices;f.source.vertices->GetDesc(&desc);desc.Usage=D3D11_USAGE_DEFAULT;
        checked(f.device->CreateBuffer(&desc,nullptr,&mutable_vertices),"mutable stream geometry");
        const float positions[]{0,0,1,0,0,1,0,1,1,0,1,1};
        for(unsigned mode=0;mode<2;++mode) {
            f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);bridge.begin(mode!=0);
            for(std::size_t i=0;i<source.size();++i) {
                auto p=source[i];p.vertices=mutable_vertices;
                if(i%64==0) f.context->UpdateSubresource(mutable_vertices.Get(),0,nullptr,positions,0,0);
                issue(p);if(i%89==0) f.context->Draw(6,0);
            }
            bridge.end();require(f.pixels()==Fixture::oracle(3),"geometry mutation/nonindexed draw order");++images;
        }
        // Loading-time context calls can occur on another thread outside a
        // renderpass. The next complete pass may safely acquire that thread.
        std::jthread loading([&]{f.context->Flush();});loading.join();
        std::jthread migrated([&]{
            f.clear(target);bridge.begin(true);for(const auto& p:source) issue(p);bridge.end();
        });migrated.join();require(f.pixels()==Fixture::oracle(3),"renderpass owner migration");++images;
        // Foreign GPU operations during an active pass must flush and stop
        // capture only until that pass ends, without dropping queued draws.
        f.clear(target);bridge.begin(true);for(std::size_t i=0;i<64;++i) issue(source[i]);
        std::jthread collision([&]{f.context->CopyResource(f.staging.Get(),f.target.Get());});collision.join();
        const auto collision_before=bridge.statistics();for(std::size_t i=64;i<source.size();++i) issue(source[i]);bridge.end();
        require(bridge.statistics().replaced==collision_before.replaced && f.pixels()==Fixture::oracle(3),"foreign caller pass fallback");++images;
        f.clear(target);bridge.begin(true);for(const auto& p:source) issue(p);bridge.end();
        require(!bridge.statistics().disabled && f.pixels()==Fixture::oracle(3),"worker replacement recovery after loading thread");++images;
        // Streaming textures can carry a resource LOD clamp in addition to
        // sampler state. Compare native and recorded access to multiple mips.
        D3D11_TEXTURE2D_DESC lod_desc{};lod_desc.Width=lod_desc.Height=4;lod_desc.MipLevels=3;lod_desc.ArraySize=1;
        lod_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;lod_desc.SampleDesc.Count=1;lod_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;lod_desc.MiscFlags=D3D11_RESOURCE_MISC_RESOURCE_CLAMP;
        const std::array<UINT,16> red_pixels{0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff};
        const UINT green_pixels[]{0xff00ff00,0xff00ff00,0xff00ff00,0xff00ff00},blue_pixel=0xffff0000;
        const D3D11_SUBRESOURCE_DATA mips[]{{red_pixels.data(),16,0},{green_pixels,8,0},{&blue_pixel,4,0}};
        Ref<ID3D11Texture2D> lod_texture;Ref<ID3D11ShaderResourceView> lod_view;
        checked(f.device->CreateTexture2D(&lod_desc,mips,&lod_texture),"LOD texture");checked(f.device->CreateShaderResourceView(lod_texture.Get(),nullptr,&lod_view),"LOD view");
        const char* lod_shader="cbuffer P:register(b0){float4 color;float4 transform;}; Texture2D t:register(t0); SamplerState s:register(s0); float4 pixel(float4 p:SV_Position):SV_Target{return t.SampleLevel(s,float2(.5,.5),0)*color;}";
        Ref<ID3DBlob> lod_code;Ref<ID3D11PixelShader> lod_ps;
        checked(D3DCompile(lod_shader,std::strlen(lod_shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&lod_code,&error),"LOD shader");
        checked(f.device->CreatePixelShader(lod_code->GetBufferPointer(),lod_code->GetBufferSize(),nullptr,&lod_ps),"LOD PS");
        before_draw=[&](const Packet&) {auto* view=lod_view.Get();f.context->PSSetShaderResources(0,1,&view);f.context->PSSetShader(lod_ps.Get(),nullptr,0);};
        std::vector<unsigned char> lod_expected;
        f.context->SetResourceMinLOD(lod_texture.Get(),2);
        for(unsigned i=0;i<2;++i) {f.clear(target);bridge.begin(i!=0);for(const auto& p:source) issue(p);bridge.end();auto output=f.pixels();
            if(!i)lod_expected=std::move(output);else require(output==lod_expected,"resource LOD clamp omitted from worker state");++images;}
        before_draw={};
        // Earlier queued draws must own resources after both the application
        // and the mutable getter cache have relinquished their references.
        std::vector<unsigned char> lifetime_expected;
        for(unsigned variant=0;variant<3;++variant) {
            bridge.share_bindings(variant!=2);bridge.cache_state(true);
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            const auto lifetime_before=bridge.statistics();bridge.begin(variant!=0);
            for(std::size_t i=0;i<(direct_small?7u:64u);++i) {
                D3D11_TEXTURE2D_DESC temporary_desc{};temporary_desc.Width=temporary_desc.Height=temporary_desc.MipLevels=temporary_desc.ArraySize=1;
                temporary_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;temporary_desc.SampleDesc.Count=1;temporary_desc.Usage=D3D11_USAGE_IMMUTABLE;temporary_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                const UINT pixel=0xff000000u|static_cast<UINT>((i*37+19)%256)|static_cast<UINT>((i*71+43)%256)<<8|static_cast<UINT>((i*13+101)%256)<<16;
                D3D11_SUBRESOURCE_DATA temporary_data{&pixel,4,0};Ref<ID3D11Texture2D> temporary_texture;Ref<ID3D11ShaderResourceView> temporary_view;
                checked(f.device->CreateTexture2D(&temporary_desc,&temporary_data,&temporary_texture),"temporary texture");
                checked(f.device->CreateShaderResourceView(temporary_texture.Get(),nullptr,&temporary_view),"temporary view");
                auto p=source[i];p.pixel_shader=f.source.pixel_shader;p.texture=temporary_view;p.blend=nullptr;issue(p);
            }
            ID3D11ShaderResourceView* empty_view=nullptr;f.context->PSSetShaderResources(0,1,&empty_view);
            require(bridge.statistics().batches==lifetime_before.batches,"lifetime scene flushed before external owners were released");
            bridge.end();auto output=f.pixels();if(!variant)lifetime_expected=std::move(output);else require(output==lifetime_expected,"immutable groups lost temporary resources before replay");++images;
        }
        // Switching ownership while draws are queued must retain old versions.
        // At the final switch no setter re-dirties shader/geometry/CB/SRV state.
        std::vector<unsigned char> ownership_expected;
        for(unsigned variant=0;variant<4;++variant) {
            bridge.share_bindings(variant!=2);bridge.cache_state(true);
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            const auto ownership_before=bridge.statistics();bridge.begin(variant!=0);
            for(std::size_t i=0;i<128;++i) {
                if(variant==3 && (i==32 || i==80))bridge.share_bindings(i==80);
                auto p=source[i];p.pixel_shader=f.source.pixel_shader;p.texture=f.source.texture;p.blend=nullptr;
                if(!i)issue(p);
                else {
                    D3D11_MAPPED_SUBRESOURCE upload{};checked(f.context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&upload),"ownership version upload");
                    std::memset(upload.pData,0,64);std::memcpy(upload.pData,p.constants.data(),p.constants.size());f.context->Unmap(constants.Get(),0);
                    if(i==48 || i==96) {auto* view=i==48?sample_view.Get():f.source.texture.Get();f.context->PSSetShaderResources(0,1,&view);}
                    if(i!=80)f.context->RSSetScissorRects(1,&p.scissor);
                    f.context->DrawIndexed(p.index_count,p.index_start,p.base_vertex);
                }
            }
            require(bridge.statistics().batches==ownership_before.batches,"ownership mode switch unexpectedly flushed");
            bridge.end();auto output=f.pixels();if(!variant)ownership_expected=std::move(output);else require(output==ownership_expected,"ownership mode switch froze constants or mutated queued bindings");++images;
        }
        bridge.share_bindings(sharing);
        // Consume all 14 slots in each stage. Exercise the scratch table's
        // maximum 28 distinct buffers, cross-stage duplicates, and NULL holes.
        std::string wide_shader;
        for(unsigned i=0;i<14;++i)wide_shader+="cbuffer C"+std::to_string(i)+":register(b"+std::to_string(i)+") {float4 color"+std::to_string(i)+";float4 transform"+std::to_string(i)+";float4 padding"+std::to_string(i)+"[2];};\n";
        wide_shader+="struct Out {float4 position:SV_Position;};\nOut vertex(float2 xy:POSITION) {Out o;float4 t=(";
        for(unsigned i=0;i<14;++i)wide_shader+=(i?"+":"")+std::string("transform")+std::to_string(i);
        wide_shader+=")/14; o.position=float4(t.xy+xy*t.zw,0,1);return o;}\nfloat4 pixel(Out p):SV_Target {return (";
        for(unsigned i=0;i<14;++i)wide_shader+=(i?"+":"")+std::string("color")+std::to_string(i);
        wide_shader+=")/14;}";
        Ref<ID3DBlob> wide_vs_code,wide_ps_code;Ref<ID3D11VertexShader> wide_vs;Ref<ID3D11PixelShader> wide_ps;
        checked(D3DCompile(wide_shader.data(),wide_shader.size(),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&wide_vs_code,&error),"28-buffer VS");
        checked(D3DCompile(wide_shader.data(),wide_shader.size(),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&wide_ps_code,&error),"28-buffer PS");
        checked(f.device->CreateVertexShader(wide_vs_code->GetBufferPointer(),wide_vs_code->GetBufferSize(),nullptr,&wide_vs),"28-buffer VS creation");
        checked(f.device->CreatePixelShader(wide_ps_code->GetBufferPointer(),wide_ps_code->GetBufferSize(),nullptr,&wide_ps),"28-buffer PS creation");
        std::array<Ref<ID3D11Buffer>,28> wide_buffers;D3D11_BUFFER_DESC wide_desc{};
        wide_desc.ByteWidth=64;wide_desc.Usage=D3D11_USAGE_DEFAULT;wide_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        for(auto& buffer:wide_buffers)checked(f.device->CreateBuffer(&wide_desc,nullptr,&buffer),"28-buffer allocation");
        for(unsigned scene=0;scene<3;++scene) {
            std::vector<unsigned char> wide_expected;
            for(unsigned variant=0;variant<5;++variant) {
                bridge.flat_upload_lookup(variant==1 || variant==3);
                f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
                before_draw=[&](const Packet& p) {
                    std::array<ID3D11Buffer*,14> vs_buffers{},ps_buffers{};
                    for(unsigned slot=0;slot<14;++slot) {
                        std::array<std::byte,64> copy{};std::memcpy(copy.data(),p.constants.data(),32);
                        // Each stage has its own changing version in the 28-buffer
                        // case. Shared buffers must be uploaded only once per draw.
                        const float tint=static_cast<float>(slot)/255;std::memcpy(copy.data(),&tint,4);
                        auto* vb=wide_buffers[slot].Get();auto* pb=wide_buffers[scene==0?slot+14:slot].Get();
                        f.context->UpdateSubresource(vb,0,nullptr,copy.data(),0,0);
                        if(pb!=vb) {const float other=1-tint;std::memcpy(copy.data(),&other,4);f.context->UpdateSubresource(pb,0,nullptr,copy.data(),0,0);}
                        if(scene!=2 || slot%2==0) {vs_buffers[slot]=vb;ps_buffers[slot]=pb;}
                    }
                    f.context->VSSetConstantBuffers(0,14,vs_buffers.data());f.context->PSSetConstantBuffers(0,14,ps_buffers.data());
                    f.context->VSSetShader(wide_vs.Get(),nullptr,0);f.context->PSSetShader(wide_ps.Get(),nullptr,0);
                };
                const auto wide_before=bridge.statistics();
                bridge.begin(variant!=0,variant>=3?skyrim_mc::stream::Recording::serial:skyrim_mc::stream::Recording::workers);
                for(unsigned i=0;i<64;++i)issue(source[i]);
                require(bridge.statistics().batches==wide_before.batches,"28-buffer scene flushed before its scope ended");
                bridge.end();auto output=f.pixels();if(!variant)wide_expected=std::move(output);else require(output==wide_expected,"upload lookup changed high slots, shared versions or NULL bindings");++images;
                const auto wide_after=bridge.statistics();if(variant) {
                    const auto entries=(scene==0?28u:scene==1?14u:7u)*64u;
                    const bool flat=variant==1 || variant==3;
                    require(wide_after.flat_upload_entries-wide_before.flat_upload_entries==(flat?entries:0),"flat lookup distinct buffer accounting");
                    require(wide_after.temporary_upload_map_entries-wide_before.temporary_upload_map_entries==(flat?0:entries),"map control distinct buffer accounting");
                    require(wide_after.draw_upload_duplicates-wide_before.draw_upload_duplicates==(scene==0?0:entries),"cross-stage upload duplicate accounting");
                    require(wide_after.private_uploads-wide_before.private_uploads==entries,"changed distinct buffer uploaded zero or multiple times");
                }
            }
        }
        before_draw={};bridge.flat_upload_lookup(flat_lookup);
        // Exact small/worker and queue-capacity boundaries. Direct replay must
        // preserve the independent output and untouched compute-stage bindings.
        const char* compute_source="[numthreads(1,1,1)] void compute(uint3 index:SV_DispatchThreadID) {}";
        Ref<ID3DBlob> compute_code;Ref<ID3D11ComputeShader> compute_shader;
        checked(D3DCompile(compute_source,std::strlen(compute_source),nullptr,nullptr,nullptr,"compute","cs_5_0",0,0,&compute_code,&error),"compute preservation shader");
        checked(f.device->CreateComputeShader(compute_code->GetBufferPointer(),compute_code->GetBufferSize(),nullptr,&compute_shader),"compute shader creation");
        for(const unsigned length:{1u,2u,3u,4u,15u,16u,17u,63u,64u,255u,256u,257u}) {
            std::vector<unsigned char> boundary_expected;
            for(unsigned variant=0;variant<3;++variant) {
                bridge.direct_small_batches(variant==2);f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
                auto* compute_buffer=constants.Get();auto* compute_view=f.source.texture.Get();auto* compute_sampler=f.source.sampler.Get();
                f.context->CSSetShader(compute_shader.Get(),nullptr,0);f.context->CSSetConstantBuffers(13,1,&compute_buffer);
                f.context->CSSetShaderResources(127,1,&compute_view);f.context->CSSetSamplers(15,1,&compute_sampler);
                const auto boundary_before=bridge.statistics();bridge.begin(variant!=0);
                for(unsigned i=0;i<length;++i)issue(source[i]);bridge.end();
                auto output=f.pixels();if(!variant)boundary_expected=std::move(output);else require(output==boundary_expected,"direct small batch changed pixels at a threshold");++images;
                Ref<ID3D11ComputeShader> saved_shader;Ref<ID3D11Buffer> saved_buffer;Ref<ID3D11ShaderResourceView> saved_view;Ref<ID3D11SamplerState> saved_sampler;
                f.context->CSGetShader(&saved_shader,nullptr,nullptr);f.context->CSGetConstantBuffers(13,1,&saved_buffer);
                f.context->CSGetShaderResources(127,1,&saved_view);f.context->CSGetSamplers(15,1,&saved_sampler);
                require(saved_shader.Get()==compute_shader.Get() && saved_buffer.Get()==compute_buffer && saved_view.Get()==compute_view && saved_sampler.Get()==compute_sampler,"direct small replay failed to restore untouched compute state");
                const auto boundary_after=bridge.statistics();const unsigned tail=length%256,expected_direct=variant==2 && tail<16?tail:0;
                require(boundary_after.direct_small_draws-boundary_before.direct_small_draws==expected_direct,"direct small threshold or capacity accounting");
                require(boundary_after.direct_small_batch_count-boundary_before.direct_small_batch_count==(expected_direct?1u:0u),"direct batch count changed");
                if(expected_direct && length<16)require(boundary_after.serial_finish_ticks==boundary_before.serial_finish_ticks,"direct small batch still finished a command list");
            }
        }
        // Repeated small scopes retain private uploads across state swaps.
        // Alternating direct/deferred scopes and unchanged constant versions
        // must neither change output nor leave private buffers bound to Skyrim.
        std::vector<unsigned char> mixed_expected;
        for(unsigned variant=0;variant<4;++variant) {
            f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
            unsigned ordinal=0;
            for(std::size_t first=0;first<source.size();) {
                const unsigned lengths[]{1,2,3,7,15,16,17};const auto last=std::min(source.size(),first+lengths[ordinal%7]);
                bridge.direct_small_batches(variant==2 || (variant==3 && ordinal%2==0));bridge.begin(variant!=0);
                for(auto i=first;i<last;++i)issue(source[i],i%3==0);
                bridge.end();Ref<ID3D11Buffer> engine_vertex,engine_pixel;
                f.context->VSGetConstantBuffers(0,1,&engine_vertex);f.context->PSGetConstantBuffers(0,1,&engine_pixel);
                require(engine_vertex.Get()==constants.Get() && engine_pixel.Get()==constants.Get(),"small scope left private constant buffers bound");
                first=last;++ordinal;
            }
            auto output=f.pixels();if(!variant)mixed_expected=std::move(output);else require(output==mixed_expected,"repeated mixed small scopes changed reused upload versions or ordering");++images;
        }
        const auto shadow_before=images;const auto shadow_warnings=test_shadow_passes(f,bridge,images,debug);const auto shadow_images=images-shadow_before;
        bridge.direct_small_batches(direct_small);f.context->ClearState();f.clear(target);f.context->OMSetRenderTargets(1,&target,nullptr);
        bridge.verify_constants(true);bridge.begin(false);for(std::size_t i=0;i<64;++i) issue(source[i]);bridge.end();bridge.verify_constants(false);
        require(bridge.statistics().constant_checks==64 && !bridge.statistics().constant_mismatches,"actual GPU constant bytes differed from CPU upload snapshots");
        before_draw=[&](const Packet& p) {
            std::array<std::byte,64> copy{};std::memcpy(copy.data(),p.constants.data(),p.constants.size());
            f.context->UpdateSubresource(pixel_constants.Get(),0,nullptr,copy.data(),0,0);
            // Deliberately bypass observation to emulate an untracked writer.
            const float changed=0.99f;std::memcpy(copy.data(),&changed,4);original_update(f.context.Get(),pixel_constants.Get(),0,nullptr,copy.data(),0,0);
            auto* pcb=pixel_constants.Get();f.context->PSSetConstantBuffers(0,1,&pcb);
        };
        bridge.verify_constants(true);bridge.begin(false);issue(source[0]);bridge.end();bridge.verify_constants(false);
        const auto stats=bridge.statistics();require(stats.constant_checks==66 && stats.constant_mismatches==1 && stats.first_mismatch_slot==14,"unobserved GPU update not detected");
        require(stats.worker_draws>6000 && stats.errors==0 && stats.unknown_constants>0,"actual replacement counters");
        for(const auto id:stats.worker_ids) require(id && id!=GetCurrentThreadId(),"real non-main workers");
        std::uint64_t bucket_batches=0,bucket_draws=0,reason_batches=0;
        for(const auto& bucket:stats.batch_sizes) {bucket_batches+=bucket.batches;bucket_draws+=bucket.draws;require(bucket.batches==bucket.worker_batches+bucket.serial_batches && bucket.draws==bucket.worker_draws+bucket.serial_draws,"batch bucket recording accounting");}
        for(const auto value:stats.flush_reasons)reason_batches+=value;
        require(bucket_batches==stats.batches && reason_batches==stats.batches && bucket_draws==stats.worker_draws+stats.serial_draws,"batch histogram conservation");
        require(stats.serial_finish_ticks<=stats.serial_record_ticks,"serial finish timing exceeds inclusive recording");
        for(unsigned i=0;i<4;++i)require(stats.worker_finish_ticks[i]<=stats.worker_record_ticks[i],"worker finish timing exceeds inclusive recording");
        const auto messages=debug?f.messages():0;
        std::cout<<"{\"kind\":\"immediate-stream-replacement-test\",\"fullImages\":"<<images<<",\"replacedDraws\":"<<stats.replaced<<",\"workerDraws\":"<<stats.worker_draws
            <<",\"originalDraws\":"<<stats.fallback<<",\"batches\":"<<stats.batches<<",\"errors\":"<<stats.errors<<",\"debugMessages\":"<<messages
            <<",\"suppressedIndexedDraws\":"<<stats.suppressed_indexed_draws<<",\"suppressionQueryFallbacks\":"<<stats.suppression_query_fallbacks
            <<",\"driverCommandLists\":"<<(caps.driver_command_lists?"true":"false")<<",\"driverConcurrentCreates\":"<<(caps.driver_concurrent_creates?"true":"false")<<",\"threadingQueryHRESULT\":"<<caps.threading_query_result
            <<",\"GPUConstantChecks\":"<<stats.constant_checks<<",\"deliberateGPUChangesDetected\":"<<stats.constant_mismatches
            <<",\"qpcFrequency\":"<<stats.qpc_frequency<<",\"captureTicks\":"<<stats.capture_ticks<<",\"uploadCopyTicks\":"<<stats.upload_copy_ticks
            <<",\"serialRecordTicks\":"<<stats.serial_record_ticks<<",\"workerWaitTicks\":"<<stats.worker_wait_ticks<<",\"executeTicks\":"<<stats.execute_ticks
            <<",\"captureAttempts\":"<<stats.capture_attempts<<",\"uploadCopies\":"<<stats.upload_copies<<",\"uploadCopyBytes\":"<<stats.upload_copy_bytes
            <<",\"privateUploads\":"<<stats.private_uploads<<",\"privateUploadReuses\":"<<stats.private_upload_reuses<<",\"privateUploadBytes\":"<<stats.private_upload_bytes
            <<",\"snapshotGetters\":"<<stats.snapshot_getters<<",\"stateGroupRefreshes\":"<<stats.state_group_refreshes<<",\"stateGroupReuses\":"<<stats.state_group_reuses
            <<",\"recordingBindings\":"<<stats.recording_bindings<<",\"recordingBindingsSkipped\":"<<stats.recording_bindings_skipped
            <<",\"reducedMaterialBindings\":"<<reduced_material_bindings<<",\"fullMaterialBindings\":"<<full_material_bindings
            <<",\"constantOnlyCachedGetters\":"<<cached_getters<<",\"constantOnlyUncachedGetters\":"<<uncached_getters
            <<",\"constantOnlyCachedCaptureTicks\":"<<cached_ticks<<",\"constantOnlyUncachedCaptureTicks\":"<<uncached_ticks
            <<",\"snapshotOwnershipSharing\":"<<(sharing?"true":"false")
            <<",\"snapshotGroupCopies\":"<<stats.snapshot_group_copies<<",\"snapshotGroupReuses\":"<<stats.snapshot_group_reuses
            <<",\"snapshotPublishTicks\":"<<stats.snapshot_publish_ticks<<",\"queueReleaseTicks\":"<<stats.queue_release_ticks
            <<",\"constantOnlyOwnedCaptureTicks\":"<<ownership_capture[0]<<",\"constantOnlySharedCaptureTicks\":"<<ownership_capture[1]
            <<",\"constantOnlyOwnedPublishTicks\":"<<ownership_publish[0]<<",\"constantOnlySharedPublishTicks\":"<<ownership_publish[1]
            <<",\"constantOnlyOwnedReleaseTicks\":"<<ownership_release[0]<<",\"constantOnlySharedReleaseTicks\":"<<ownership_release[1]
            <<",\"constantOnlyOwnedGroupCopies\":"<<ownership_copies[0]<<",\"constantOnlySharedGroupCopies\":"<<ownership_copies[1]
            <<",\"flatUploadLookup\":"<<(flat_lookup?"true":"false")
            <<",\"temporaryUploadMapEntries\":"<<stats.temporary_upload_map_entries<<",\"flatUploadEntries\":"<<stats.flat_upload_entries<<",\"drawUploadDuplicates\":"<<stats.draw_upload_duplicates
            <<",\"serialFinishTicks\":"<<stats.serial_finish_ticks
            <<",\"directSmallMode\":"<<(direct_small?"true":"false")<<",\"directSmallDraws\":"<<stats.direct_small_draws<<",\"directSmallBatches\":"<<stats.direct_small_batch_count<<",\"directSmallTicks\":"<<stats.direct_small_ticks
            <<",\"expectedHazardWarnings\":"<<expected_hazard_warnings
            <<",\"shadowFullImages\":"<<shadow_images<<",\"expectedShadowHazardWarnings\":"<<shadow_warnings
            <<",\"mismatches\":0}\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
