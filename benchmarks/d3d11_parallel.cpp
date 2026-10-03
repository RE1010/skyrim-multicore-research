#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
namespace {
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void checked(HRESULT result,const char* message) {if(FAILED(result)) throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string(result));}
double milliseconds(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::uint64_t cpu_ticks(HANDLE thread) {
    ULONG64 cycles=0;require(QueryThreadCycleTime(thread,&cycles)!=0,"thread cycle count");return cycles;
}
struct Renderer {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Buffer> indices;
    D3D11_FEATURE_DATA_THREADING threading{};D3D_FEATURE_LEVEL feature{};DXGI_ADAPTER_DESC adapter{};
    explicit Renderer(bool debug) {
        const std::array<D3D_FEATURE_LEVEL,2> levels{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,debug?D3D11_CREATE_DEVICE_DEBUG:0,
            levels.data(),static_cast<UINT>(levels.size()),D3D11_SDK_VERSION,&device,&feature,&immediate),"hardware D3D11 device");
        checked(device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading)),"threading caps");
        ComPtr<IDXGIDevice> dxgi;checked(device.As(&dxgi),"DXGI device");ComPtr<IDXGIAdapter> dxgi_adapter;
        checked(dxgi->GetAdapter(&dxgi_adapter),"adapter");checked(dxgi_adapter->GetDesc(&adapter),"adapter description");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=512;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&desc,nullptr,&target),"target");checked(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&desc,nullptr,&staging),"staging");
        const char* shader=R"(
struct V {float4 p:SV_Position; nointerpolation uint id:TEXCOORD0;};
V vertex(uint v:SV_VertexID) {
    uint id=v/6,corner=v%6;
    float2 points[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    float2 cell=float2(id&63,(id>>6)&63)+points[corner];
    V o;o.p=float4(cell.x/32-1,1-cell.y/32,0,1);o.id=id;return o;
}
float4 pixel(V p):SV_Target {return float4(p.id&255,(p.id>>3)&255,(p.id>>7)&255,255)/255.0;}
)";
        ComPtr<ID3DBlob> vertex,pixel,errors;
        checked(D3DCompile(shader,std::char_traits<char>::length(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vertex,&errors),"vertex compile");
        checked(D3DCompile(shader,std::char_traits<char>::length(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&pixel,&errors),"pixel compile");
        checked(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs),"vertex shader");
        checked(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps),"pixel shader");
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        checked(device->CreateRasterizerState(&rs,&raster),"rasterizer");
        std::vector<std::uint32_t> index_data(32768*6);std::iota(index_data.begin(),index_data.end(),0u);
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=static_cast<UINT>(index_data.size()*sizeof(std::uint32_t));
        buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_INDEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA initial{index_data.data(),0,0};checked(device->CreateBuffer(&buffer,&initial,&indices),"index buffer");
    }
    void prepare(ID3D11DeviceContext* context) const {
        context->ClearState();auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);
        const D3D11_VIEWPORT viewport{0,0,512,512,0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    }
    void draw(ID3D11DeviceContext* context,std::uint32_t start,std::uint32_t end) const {
        prepare(context);for(auto i=start;i<end;++i) context->DrawIndexed(6,i*6,0);
    }
    void clear() const {const float color[]{0,0,0,0};immediate->ClearRenderTargetView(rtv.Get(),color);}
    std::vector<unsigned char> pixels(std::uint32_t draws) const {
        immediate->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback");
        std::vector<unsigned char> bytes(512*512*4);
        for(std::size_t y=0;y<512;++y) std::copy_n(static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,512*4,bytes.data()+y*512*4);
        immediate->Unmap(staging.Get(),0);
        // Independently verify each tile, including the final overwrite layer.
        // Matching blank/incorrect serial and deferred images cannot pass.
        for(std::uint32_t tile=0;tile<4096;++tile) {
            const auto id=draws-4096+tile;const auto at=((tile/64*8+4)*512+(tile%64*8+4))*4;
            if(bytes[at]!=(id&255) || bytes[at+1]!=((id>>3)&255) || bytes[at+2]!=((id>>7)&255) || bytes[at+3]!=255) {
                std::cerr<<"draws="<<draws<<" tile="<<tile<<" expected id="<<id<<" pixel="<<static_cast<unsigned>(bytes[at])<<','<<static_cast<unsigned>(bytes[at+1])<<','<<static_cast<unsigned>(bytes[at+2])<<','<<static_cast<unsigned>(bytes[at+3])<<'\n';
                throw std::runtime_error("independent tile oracle mismatch");
            }
        }
        return bytes;
    }
    std::uint64_t debug_messages() const {
        ComPtr<ID3D11InfoQueue> queue;if(FAILED(device.As(&queue))) return 0;
        const auto count=queue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for(std::uint64_t i=0;i<count;++i) {
            SIZE_T size=0;checked(queue->GetMessage(i,nullptr,&size),"debug message size");
            std::vector<unsigned char> storage(size);auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            checked(queue->GetMessage(i,message,&size),"debug message");
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) {std::cerr<<message->pDescription<<'\n';throw std::runtime_error("D3D11 debug warning/error");}
        }
        return count;
    }
};
struct Workers {
    Renderer& renderer;
    std::vector<ComPtr<ID3D11DeviceContext>> contexts;
    std::vector<ComPtr<ID3D11CommandList>> lists;
    std::vector<std::jthread> threads;
    std::vector<DWORD> ids;
    std::vector<std::uint64_t> cpu;
    std::vector<HRESULT> errors;
    std::mutex mutex;std::condition_variable work,done;
    bool stopping=false;std::uint64_t generation=0;std::uint32_t draw_count=0,remaining=0;
    explicit Workers(Renderer& r,std::uint32_t count):renderer(r),contexts(count),lists(count),ids(count),cpu(count),errors(count,S_OK) {
        for(auto& context:contexts) checked(renderer.device->CreateDeferredContext(0,&context),"deferred context");
        threads.reserve(count);
        try {for(std::uint32_t i=0;i<count;++i) threads.emplace_back([this,i,count]{
            std::uint64_t seen=0;
            for(;;) {
                std::unique_lock lock(mutex);work.wait(lock,[&]{return stopping || generation!=seen;});if(stopping) return;
                seen=generation;const auto n=draw_count;lock.unlock();ids[i]=GetCurrentThreadId();const auto before=cpu_ticks(GetCurrentThread());
                renderer.draw(contexts[i].Get(),n*i/count,n*(i+1)/count);
                errors[i]=contexts[i]->FinishCommandList(FALSE,lists[i].ReleaseAndGetAddressOf());
                cpu[i]+=cpu_ticks(GetCurrentThread())-before;
                lock.lock();--remaining;if(!remaining) done.notify_one();
            }
        });} catch(...) {stop();throw;}
    }
    void stop() {{std::lock_guard lock(mutex);stopping=true;}work.notify_all();threads.clear();}
    ~Workers() {stop();}
    double submit(std::uint32_t draws,double& replay_ms) {
        const auto start=Clock::now();
        {std::lock_guard lock(mutex);draw_count=draws;remaining=static_cast<std::uint32_t>(contexts.size());++generation;}work.notify_all();
        {std::unique_lock lock(mutex);done.wait(lock,[&]{return remaining==0;});}
        const auto replay=Clock::now();
        for(std::size_t i=0;i<lists.size();++i) {checked(errors[i],"finish commands");renderer.immediate->ExecuteCommandList(lists[i].Get(),FALSE);}
        replay_ms=milliseconds(replay);return milliseconds(start);
    }
};
double median(std::vector<double> values) {std::sort(values.begin(),values.end());return (values[(values.size()-1)/2]+values[values.size()/2])/2;}
std::uint64_t hash(const std::vector<unsigned char>& bytes) {std::uint64_t h=14695981039346656037ULL;for(auto b:bytes){h^=b;h*=1099511628211ULL;}return h;}
struct Result {std::uint32_t draws,workers;double serial_ms,parallel_ms,replay_ms,serial_main_cycles,parallel_main_cycles;std::uint64_t main_cycles,worker_cycles,image_hash;std::vector<DWORD> ids;};
}
int main(int argc,char** argv) {
    try {
        const bool debug=argc==2 && std::string(argv[1])=="--debug";
        Renderer renderer(debug);std::vector<Result> results;
        for(const auto worker_count:{1u,2u,4u,6u}) {
            Workers workers(renderer,worker_count);
            for(const auto draws:{4096u,16384u,32768u}) {
                renderer.clear();renderer.draw(renderer.immediate.Get(),0,draws);const auto reference=renderer.pixels(draws);
                std::vector<double> serial,parallel,replay,serial_cycles,parallel_cycles;const auto main_before=cpu_ticks(GetCurrentThread());
                const auto worker_before=std::accumulate(workers.cpu.begin(),workers.cpu.end(),std::uint64_t{0});
                for(int iteration=0;iteration<15;++iteration) {
                    auto run_serial=[&]{renderer.clear();const auto before=cpu_ticks(GetCurrentThread());const auto start=Clock::now();renderer.draw(renderer.immediate.Get(),0,draws);
                        const auto elapsed=milliseconds(start);const auto cycles=cpu_ticks(GetCurrentThread())-before;
                        require(renderer.pixels(draws)==reference,"serial image changed");if(iteration>=3){serial.push_back(elapsed);serial_cycles.push_back(static_cast<double>(cycles));}};
                    auto run_parallel=[&]{renderer.clear();double replay_time=0;const auto before=cpu_ticks(GetCurrentThread());const auto elapsed=workers.submit(draws,replay_time);
                        const auto cycles=cpu_ticks(GetCurrentThread())-before;
                        require(renderer.pixels(draws)==reference,"deferred image mismatch");if(iteration>=3){parallel.push_back(elapsed);replay.push_back(replay_time);parallel_cycles.push_back(static_cast<double>(cycles));}};
                    if(iteration%2){run_parallel();run_serial();}else{run_serial();run_parallel();}
                }
                const auto worker_after=std::accumulate(workers.cpu.begin(),workers.cpu.end(),std::uint64_t{0});
                results.push_back({draws,worker_count,median(serial),median(parallel),median(replay),median(serial_cycles),median(parallel_cycles),
                    cpu_ticks(GetCurrentThread())-main_before,worker_after-worker_before,hash(reference),workers.ids});
            }
        }
        const auto messages=renderer.debug_messages();
        std::cout<<std::setprecision(9)<<"{\"kind\":\"standalone-d3d11-parallel-recording\",\"debugStoredMessages\":"<<messages<<",\"adapterVendor\":"<<renderer.adapter.VendorId
            <<",\"adapterDevice\":"<<renderer.adapter.DeviceId<<",\"featureLevel\":"<<renderer.feature<<",\"deviceFlags\":"<<renderer.device->GetCreationFlags()
            <<",\"driverConcurrentCreates\":"<<(renderer.threading.DriverConcurrentCreates?"true":"false")
            <<",\"driverCommandLists\":"<<(renderer.threading.DriverCommandLists?"true":"false")
            <<",\"debugLayerEnabled\":"<<(debug?"true":"false")<<",\"validatedImages\":372,\"mismatches\":0,\"rows\":[";
        bool first=true;for(const auto& r:results){if(!first) std::cout<<',';first=false;
            std::cout<<"{\"draws\":"<<r.draws<<",\"workers\":"<<r.workers<<",\"measuredPairs\":12,\"serialSubmitMedianMs\":"<<r.serial_ms
                <<",\"parallelRecordAndReplayMedianMs\":"<<r.parallel_ms<<",\"replayMedianMs\":"<<r.replay_ms
                <<",\"mainSerialSubmissionMedianCycles\":"<<r.serial_main_cycles<<",\"mainParallelSubmissionMedianCycles\":"<<r.parallel_main_cycles
                <<",\"serialToParallelRatio\":"<<r.serial_ms/r.parallel_ms<<",\"mainCyclesWholeCase\":"<<r.main_cycles
                <<",\"workerRecordingCyclesWholeCase\":"<<r.worker_cycles<<",\"pixelFNV64\":\""<<std::hex<<r.image_hash<<std::dec<<"\",\"workerThreadIds\":[";
            for(std::size_t i=0;i<r.ids.size();++i){if(i)std::cout<<',';std::cout<<r.ids[i];}std::cout<<"]}";
        }
        std::cout<<"],\"limitations\":[\"Own hardware device and synthetic draw packets, not Skyrim engine work.\",\"CPU submission timings include dispatch, wait and ordered replay; GPU readback is outside timed submission.\",\"Main cycle totals include both variants, validation and readback; worker totals contain recording including warmup. Cycles are not time and vary with CPU frequency.\",\"No Skyrim FPS gain or runtime compatibility claim.\"]}\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
