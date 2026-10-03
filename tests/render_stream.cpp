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
}

int main(int argc,char** argv) {
    try {
        bool debug=true,sharing=true;
        for(int a=1;a<argc;++a) {if(std::strcmp(argv[a],"--hardware")==0 || std::strcmp(argv[a],"--no-debug")==0)debug=false;else if(std::strcmp(argv[a],"--owned-snapshots")==0)sharing=false;else throw std::runtime_error("unknown stream test option");}
        test_abi();Fixture f(debug);unsigned images=0;
        using Update=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
        const auto original_update=reinterpret_cast<Update>((*reinterpret_cast<void***>(f.context.Get()))[skyrim_mc::stream::slots::UpdateSubresource]);
        Ref<ID3D11Buffer> constants;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        checked(f.device->CreateBuffer(&desc,nullptr,&constants),"stream constants");
        skyrim_mc::stream::Bridge bridge(f.device.Get(),f.context.Get(),4);require(bridge.attach(),"context attachment");bridge.share_bindings(sharing);
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
            for(std::size_t i=0;i<64;++i) {
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
        const auto messages=debug?f.messages():0;
        std::cout<<"{\"kind\":\"immediate-stream-replacement-test\",\"fullImages\":"<<images<<",\"replacedDraws\":"<<stats.replaced<<",\"workerDraws\":"<<stats.worker_draws
            <<",\"originalDraws\":"<<stats.fallback<<",\"batches\":"<<stats.batches<<",\"errors\":"<<stats.errors<<",\"debugMessages\":"<<messages
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
            <<",\"expectedHazardWarnings\":"<<expected_hazard_warnings
            <<",\"mismatches\":0}\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
