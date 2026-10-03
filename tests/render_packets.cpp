#define NOMINMAX
#include "skyrim_mc/render_packets.hpp"
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>
using namespace skyrim_mc::render;
namespace {
void require(bool v,const char* why) {if(!v) throw std::runtime_error(why);}
void checked(HRESULT h,const char* why) {if(FAILED(h)) throw std::runtime_error(why);}
template<class F> void rejects(F&& f) {bool rejected=false;try {f();}catch(const std::runtime_error&){rejected=true;}require(rejected,"unsafe request accepted");}
struct Fixture {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> context;
    Ref<ID3D11Texture2D> target,staging;Ref<ID3D11RenderTargetView> rtv;
    Packet source;Ref<ID3D11PixelShader> alternate;Ref<ID3D11ShaderResourceView> masked;
    Ref<ID3D11BlendState> red_only;
    explicit Fixture(bool debug=true) {
        D3D_FEATURE_LEVEL level{};checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,debug?D3D11_CREATE_DEVICE_DEBUG:0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"hardware device unavailable");
        D3D11_TEXTURE2D_DESC tex{};tex.Width=tex.Height=256;tex.MipLevels=tex.ArraySize=1;tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;tex.BindFlags=D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&tex,nullptr,&target),"target");checked(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"target view");
        tex.BindFlags=0;tex.Usage=D3D11_USAGE_STAGING;tex.CPUAccessFlags=D3D11_CPU_ACCESS_READ;checked(device->CreateTexture2D(&tex,nullptr,&staging),"staging");
        const char* shader=R"(
cbuffer Constants:register(b0) {float4 color;float4 transform;};
Texture2D texture0:register(t0);
struct Out {float4 position:SV_Position;};
Out vertex(float2 xy:POSITION) {Out o;o.position=float4(transform.xy+xy*transform.zw,0,1);return o;}
float4 pixel(Out p):SV_Target {return color*texture0.Load(int3(0,0,0));}
float4 swapped(Out p):SV_Target {return (color*texture0.Load(int3(0,0,0))).bgra;}
)";
        Ref<ID3DBlob> vs,ps,ps2,error;
        checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error),"VS compile");
        checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error),"PS compile");
        checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"swapped","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps2,&error),"alternate PS compile");
        checked(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&source.vertex_shader),"VS");
        checked(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&source.pixel_shader),"PS");
        checked(device->CreatePixelShader(ps2->GetBufferPointer(),ps2->GetBufferSize(),nullptr,&alternate),"alternate PS");
        const D3D11_INPUT_ELEMENT_DESC layout{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        checked(device->CreateInputLayout(&layout,1,vs->GetBufferPointer(),vs->GetBufferSize(),&source.layout),"layout");
        const float positions[]{0,0,1,0,0,1,0,1,1,0,1,1};const UINT indices[]{0,1,2,3,4,5};
        D3D11_BUFFER_DESC b{};b.Usage=D3D11_USAGE_IMMUTABLE;b.ByteWidth=sizeof(positions);b.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{positions,0,0};checked(device->CreateBuffer(&b,&data,&source.vertices),"vertices");
        b.ByteWidth=sizeof(indices);b.BindFlags=D3D11_BIND_INDEX_BUFFER;data.pSysMem=indices;checked(device->CreateBuffer(&b,&data,&source.indices),"indices");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
        checked(device->CreateRasterizerState(&raster,&source.raster),"raster");
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_ALPHA;
        checked(device->CreateBlendState(&blend,&red_only),"write-mask blend state");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;checked(device->CreateSamplerState(&sampler,&source.sampler),"sampler");
        tex={};tex.Width=tex.Height=tex.MipLevels=tex.ArraySize=1;tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;tex.Usage=D3D11_USAGE_IMMUTABLE;tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const UINT white=0xffffffff,magenta=0xffff00ff;
        for(int i=0;i<2;++i) {data={i?&magenta:&white,4,0};Ref<ID3D11Texture2D> texture;checked(device->CreateTexture2D(&tex,&data,&texture),"immutable texture");
            checked(device->CreateShaderResourceView(texture.Get(),nullptr,i?masked.GetAddressOf():source.texture.GetAddressOf()),"texture view");}
        source.stride=8;source.index_count=6;source.viewport={0,0,256,256,0,1};
    }
    std::vector<Packet> packets(unsigned layers) const {
        std::vector<Packet> packets;packets.reserve(layers*1024);
        for(unsigned i=0;i<layers*1024;++i) {
            const auto tile=i%1024,x=tile%32,y=tile/32;Packet p=source;
            const std::array<float,8> constants{static_cast<float>((i*13+17)&255)/255,static_cast<float>((i*7+29)&255)/255,static_cast<float>((i*3+43)&255)/255,1,
                static_cast<float>(x)/16-1,1-static_cast<float>(y)/16,1.0f/16,-1.0f/16};
            p.constants.resize(i%2?64:32);std::memcpy(p.constants.data(),constants.data(),sizeof(constants));
            if(i%3==0) p.pixel_shader=alternate;if(i%2) p.texture=masked;if(i%3==1) p.blend=red_only;
            p.scissor={static_cast<LONG>(x*8),static_cast<LONG>(y*8),static_cast<LONG>((x+1)*8),static_cast<LONG>((y+1)*8)};
            if(i/1024==layers-1 && tile%5==0) p.scissor.left+=4;
            packets.push_back(std::move(p));
        }
        return packets;
    }
    void clear(ID3D11RenderTargetView* view) {const float zero[]{0,0,0,0};context->ClearRenderTargetView(view,zero);}
    std::vector<unsigned char> pixels() {
        context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"readback");
        std::vector<unsigned char> pixels(256*256*4);
        for(std::size_t y=0;y<256;++y) std::memcpy(pixels.data()+y*256*4,static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,256*4);
        context->Unmap(staging.Get(),0);return pixels;
    }
    static std::vector<unsigned char> oracle(unsigned layers) {
        std::vector<unsigned char> pixels(256*256*4,0);
        for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) for(unsigned layer=0;layer<layers;++layer) {
            const auto tile=(y/8)*32+x/8,i=layer*1024+tile;
            if(layer==layers-1 && tile%5==0 && x%8<4) continue;
            std::array<unsigned char,4> color{static_cast<unsigned char>((i*13+17)&255),static_cast<unsigned char>((i*7+29)&255),static_cast<unsigned char>((i*3+43)&255),255};
            if(i%2) color[1]=0;if(i%3==0) std::swap(color[0],color[2]);
            const auto offset=(y*256+x)*4;
            if(i%3==1) {pixels[offset]=color[0];pixels[offset+3]=255;}
            else std::copy(color.begin(),color.end(),pixels.begin()+offset);
        }
        return pixels;
    }
    std::uint64_t messages() {
        Ref<ID3D11InfoQueue> queue;checked(device.As(&queue),"debug queue");const auto count=queue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for(std::uint64_t i=0;i<count;++i) {SIZE_T size=0;checked(queue->GetMessage(i,nullptr,&size),"message size");std::vector<std::byte> bytes(size);auto* m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());checked(queue->GetMessage(i,m,&size),"message");if(m->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){std::cerr<<m->pDescription<<'\n';throw std::runtime_error("D3D11 warning or error");}}
        return count;
    }
};
using Clock=std::chrono::steady_clock;
std::uint64_t cpu_ticks() {ULONG64 c=0;require(QueryThreadCycleTime(GetCurrentThread(),&c)!=0,"main cycles");return c;}
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double median(std::vector<double> v) {std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])/2;}
void benchmark() {
    Fixture f(false);unsigned images=0;
    struct Row {unsigned workers,draws;double serial,parallel,freeze,replay,serial_cycles,parallel_cycles,serial_frozen;std::uint64_t worker_cycles;};
    std::vector<Row> rows;
    for(const auto workers:{1u,2u,4u,6u}) {
        Backend backend(f.device.Get(),f.context.Get(),workers);
        for(const auto layers:{1u,3u}) {
            const auto source=f.packets(layers);const auto expected=Fixture::oracle(layers);
            const auto prepared=Batch::freeze(f.device.Get(),{f.rtv,nullptr},source);
            std::vector<double> serial,parallel,freeze,replay,serial_cycles,parallel_cycles,serial_frozen;std::uint64_t worker_cycles=0;
            for(unsigned iteration=0;iteration<15;++iteration) {
                auto run_serial=[&] {
                    f.clear(f.rtv.Get());const auto before=cpu_ticks();const auto start=Clock::now();
                    // Both paths receive producer packets, validate and freeze
                    // them. Constant uploads and full pipeline work are equal.
                    const auto batch=Batch::freeze(f.device.Get(),{f.rtv,nullptr},source);backend.serial(*batch);
                    const auto duration=ms(start),cycles=static_cast<double>(cpu_ticks()-before);
                    require(f.pixels()==expected,"benchmark serial oracle");++images;
                    if(iteration>=3) {serial.push_back(duration);serial_cycles.push_back(cycles);}
                };
                auto run_parallel=[&] {
                    f.clear(f.rtv.Get());const auto before=cpu_ticks();const auto start=Clock::now();
                    const auto batch=Batch::freeze(f.device.Get(),{f.rtv,nullptr},source);const auto freeze_time=ms(start);
                    const auto result=backend.submit(batch);const auto duration=ms(start),cycles=static_cast<double>(cpu_ticks()-before);
                    require(f.pixels()==expected,"benchmark parallel oracle");++images;
                    if(iteration>=3) {parallel.push_back(duration);parallel_cycles.push_back(cycles);freeze.push_back(freeze_time);replay.push_back(result.replay_ms);worker_cycles+=result.worker_cycles;}
                };
                auto run_frozen=[&] {
                    f.clear(f.rtv.Get());const auto start=Clock::now();backend.serial(*prepared);const auto duration=ms(start);
                    require(f.pixels()==expected,"already-owned serial oracle");++images;if(iteration>=3) serial_frozen.push_back(duration);
                };
                // Rotate all three to avoid always placing the no-copy serial
                // control after a particular variant.
                if(iteration%3==0) {run_serial();run_parallel();run_frozen();}
                else if(iteration%3==1) {run_parallel();run_frozen();run_serial();}
                else {run_frozen();run_serial();run_parallel();}
            }
            rows.push_back({workers,layers*1024,median(serial),median(parallel),median(freeze),median(replay),median(serial_cycles),median(parallel_cycles),median(serial_frozen),worker_cycles});
        }
    }
    std::cout<<std::setprecision(9)<<"{\"kind\":\"owned-render-packet-end-to-end-benchmark\",\"skyrimEngineWorkOffloaded\":false,\"debugLayerEnabled\":false,\"validatedFullImages\":"<<images<<",\"mismatches\":0,\"rows\":[";
    for(std::size_t i=0;i<rows.size();++i) {if(i) std::cout<<',';const auto& r=rows[i];std::cout<<"{\"draws\":"<<r.draws<<",\"workers\":"<<r.workers<<",\"measuredPairs\":12,\"serialFreezeUploadSubmitMedianMs\":"<<r.serial
        <<",\"parallelFreezeRecordReplayMedianMs\":"<<r.parallel<<",\"serialAlreadyOwnedSubmitMedianMs\":"<<r.serial_frozen<<",\"parallelFreezeMedianMs\":"<<r.freeze<<",\"replayMedianMs\":"<<r.replay<<",\"mainSerialMedianCycles\":"<<r.serial_cycles<<",\"mainParallelMedianCycles\":"<<r.parallel_cycles<<",\"workerMeasuredCycles\":"<<r.worker_cycles<<"}";}
    std::cout<<"],\"limitations\":[\"Own device and synthetic materials/geometry; no Skyrim FPS result.\",\"Both CPU paths include packet validation, copying and uploads; parallel also includes dispatch, join and ordered replay with context restoration.\",\"GPU readback and producer packet generation are outside timings; GPU completion is not a frame performance metric.\",\"Main cycles are not elapsed time and vary with frequency.\"]}\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--benchmark") {benchmark();return 0;}
        Fixture f;unsigned images=0;std::uint64_t uploaded=0,worker_cycles=0;std::vector<Stats> stats;
        for(const auto workers:{1u,2u,4u,6u}) {
            Backend backend(f.device.Get(),f.context.Get(),workers);
            for(const auto layers:{1u,3u}) {
                auto source=f.packets(layers);const auto batch=Batch::freeze(f.device.Get(),{f.rtv,nullptr},source);const auto expected=Fixture::oracle(layers);
                const auto snapshot=batch->constants(batch->packets()[0]);
                require(batch->resource_sets()==6 && std::equal(snapshot.begin(),snapshot.end(),source[0].constants.begin(),source[0].constants.end()),"resource interning and copied constants");
                f.clear(f.rtv.Get());backend.serial(*batch);require(f.pixels()==expected,"serial output differs from independent full-image oracle");++images;
                // Freeze MUST own its bytes, not views into the producer's storage.
                for(auto& p:source) {std::fill(p.constants.begin(),p.constants.end(),std::byte{0xff});p.pixel_shader.Reset();p.texture.Reset();}source.clear();source.shrink_to_fit();
                for(int repeat=0;repeat<3;++repeat) {
                    f.context->ClearState();const D3D11_VIEWPORT sentinel{7,11,101,103,0.2f,0.8f};f.context->RSSetViewports(1,&sentinel);
                    f.context->PSSetShader(f.alternate.Get(),nullptr,0);auto* sentinel_texture=f.masked.Get();f.context->PSSetShaderResources(0,1,&sentinel_texture);
                    f.clear(f.rtv.Get());const auto result=backend.submit(batch);
                    require(result.draws==layers*1024 && result.lists==workers && result.worker_cycles>0,"worker work accounting");
                    require(result.worker_ids.size()==workers,"worker count");
                    auto ids=result.worker_ids;std::sort(ids.begin(),ids.end());require(std::adjacent_find(ids.begin(),ids.end())==ids.end() && std::find(ids.begin(),ids.end(),GetCurrentThreadId())==ids.end(),"recording did not execute on distinct non-main workers");
                    D3D11_VIEWPORT restored{};UINT count=1;f.context->RSGetViewports(&count,&restored);require(count==1 && !std::memcmp(&restored,&sentinel,sizeof(sentinel)),"immediate viewport state was not restored");
                    Ref<ID3D11PixelShader> ps;f.context->PSGetShader(&ps,nullptr,nullptr);require(ps.Get()==f.alternate.Get(),"immediate shader state was not restored");
                    Ref<ID3D11ShaderResourceView> texture;f.context->PSGetShaderResources(0,1,&texture);require(texture.Get()==f.masked.Get(),"immediate resource state was not restored");
                    require(f.pixels()==expected,"parallel output/order/material/constant snapshot mismatch");++images;uploaded+=result.constant_bytes;worker_cycles+=result.worker_cycles;stats.push_back(result);
                }
                std::atomic<bool> wrong_thread_rejected=false;std::jthread wrong([&]{try {backend.submit(batch);}catch(const std::runtime_error&){wrong_thread_rejected=true;}});wrong.join();require(wrong_thread_rejected,"immediate submission escaped owner thread");
            }
        }
        auto p=f.packets(1);p.resize(1);
        auto bad=p;bad[0].constants.resize(17);rejects([&]{Batch::freeze(f.device.Get(),{f.rtv,nullptr},bad);});
        bad=p;bad[0].index_count=7;rejects([&]{Batch::freeze(f.device.Get(),{f.rtv,nullptr},bad);});
        bad=p;D3D11_BUFFER_DESC desc{};bad[0].vertices->GetDesc(&desc);desc.Usage=D3D11_USAGE_DYNAMIC;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;bad[0].vertices.Reset();checked(f.device->CreateBuffer(&desc,nullptr,&bad[0].vertices),"mutable test buffer");rejects([&]{Batch::freeze(f.device.Get(),{f.rtv,nullptr},bad);});
        bad=p;D3D11_TEXTURE2D_DESC tex{};tex.Width=tex.Height=tex.MipLevels=tex.ArraySize=1;tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Ref<ID3D11Texture2D> mutable_texture;checked(f.device->CreateTexture2D(&tex,nullptr,&mutable_texture),"writable test texture");bad[0].texture.Reset();checked(f.device->CreateShaderResourceView(mutable_texture.Get(),nullptr,&bad[0].texture),"writable test view");rejects([&]{Batch::freeze(f.device.Get(),{f.rtv,nullptr},bad);});
        rejects([&]{Batch::freeze(f.device.Get(),{f.rtv,nullptr},{});});
        // Drop every producer reference to geometry, shaders, textures and RTV.
        // The frozen batch must keep them alive, independently of producer data.
        const auto lifetime=Batch::freeze(f.device.Get(),{f.rtv,nullptr},p);
        p.clear();bad.clear();f.context->ClearState();f.source={};f.alternate.Reset();f.masked.Reset();f.red_only.Reset();f.rtv.Reset();
        Backend backend(f.device.Get(),f.context.Get(),1);f.clear(lifetime->pass().target.Get());backend.submit(lifetime);
        const auto all=Fixture::oracle(1);std::vector<unsigned char> expected(256*256*4,0);
        for(unsigned y=0;y<8;++y) for(unsigned x=4;x<8;++x) std::copy_n(all.data()+(y*256+x)*4,4,expected.data()+(y*256+x)*4);
        require(f.pixels()==expected,"batch COM resource lifetime/full image");++images;
        const auto messages=f.messages();
        std::cout<<"{\"kind\":\"owned-render-packet-hardware-test\",\"skyrimEngineWorkOffloaded\":false,\"validatedFullImages\":"<<images<<",\"mismatches\":0,\"debugStoredMessages\":"<<messages
            <<",\"uploadedConstantBytes\":"<<uploaded<<",\"workerCycles\":"<<worker_cycles<<",\"immediateStateRestored\":true,\"producerMutationTest\":true,\"producerReleaseTest\":true,\"unsupportedWritableGeometryRejected\":true,\"rows\":[";
        for(std::size_t i=0;i<stats.size();++i) {if(i) std::cout<<',';const auto& s=stats[i];std::cout<<"{\"draws\":"<<s.draws<<",\"workers\":"<<s.lists<<",\"constantBytes\":"<<s.constant_bytes<<",\"recordAndReplayMs\":"<<s.record_and_replay_ms<<",\"replayMs\":"<<s.replay_ms<<",\"workerIds\":[";for(std::size_t j=0;j<s.worker_ids.size();++j){if(j)std::cout<<',';std::cout<<s.worker_ids[j];}std::cout<<"]}";}
        std::cout<<"],\"timingLimitation\":\"Debug-layer correctness runs, excludes freezing/readback; not a performance comparison or Skyrim FPS result.\"}\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
