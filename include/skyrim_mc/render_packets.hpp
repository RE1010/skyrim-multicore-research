#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace skyrim_mc::render {
template<class T> using Ref=Microsoft::WRL::ComPtr<T>;
// Deliberately limited to owned static indexed geometry and VS/PS slot 0.
// This is a backend API, not a layout for Skyrim's mutable engine packets.
struct Packet {
    Ref<ID3D11VertexShader> vertex_shader;Ref<ID3D11PixelShader> pixel_shader;
    Ref<ID3D11InputLayout> layout;Ref<ID3D11Buffer> vertices,indices;
    Ref<ID3D11ShaderResourceView> texture;Ref<ID3D11SamplerState> sampler;
    Ref<ID3D11RasterizerState> raster;Ref<ID3D11BlendState> blend;Ref<ID3D11DepthStencilState> depth;
    UINT stride=0,vertex_offset=0,index_offset=0,index_count=0,index_start=0;INT base_vertex=0;
    DXGI_FORMAT index_format=DXGI_FORMAT_R32_UINT;
    D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};
    std::vector<std::byte> constants;
};
struct Pass {Ref<ID3D11RenderTargetView> target;Ref<ID3D11DepthStencilView> depth;};
struct Resources {
    Ref<ID3D11VertexShader> vertex_shader;Ref<ID3D11PixelShader> pixel_shader;
    Ref<ID3D11InputLayout> layout;Ref<ID3D11Buffer> vertices,indices;
    Ref<ID3D11ShaderResourceView> texture;Ref<ID3D11SamplerState> sampler;
    Ref<ID3D11RasterizerState> raster;Ref<ID3D11BlendState> blend;Ref<ID3D11DepthStencilState> depth;
    UINT vertex_bytes=0,index_bytes=0;
};
struct FrozenPacket {
    std::uint32_t resource_index=0,constant_offset=0,constant_size=0;
    UINT stride=0,vertex_offset=0,index_offset=0,index_count=0,index_start=0;INT base_vertex=0;
    DXGI_FORMAT index_format=DXGI_FORMAT_R32_UINT;
    D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};
};
class Batch {
    Pass pass_;std::vector<FrozenPacket> packets_;
    std::vector<Resources> resources_;std::vector<std::byte> constants_;
    Batch(Pass,std::vector<FrozenPacket>,std::vector<Resources>,std::vector<std::byte>);
public:
    // Copies constant bytes and holds COM references; rejects writable geometry
    // and textures rather than treating AddRef as an immutable snapshot.
    static std::shared_ptr<const Batch> freeze(ID3D11Device*,Pass,std::span<const Packet>);
    const Pass& pass() const noexcept {return pass_;}
    std::span<const FrozenPacket> packets() const noexcept {return packets_;}
    std::size_t resource_sets() const noexcept {return resources_.size();}
    const Resources& resources(const FrozenPacket& p) const noexcept {return resources_[p.resource_index];}
    std::span<const std::byte> constants(const FrozenPacket& p) const noexcept {return std::span<const std::byte>(constants_).subspan(p.constant_offset,p.constant_size);}
};
struct Stats {
    std::size_t draws=0,constant_bytes=0,lists=0;
    std::uint64_t worker_cycles=0;
    double record_and_replay_ms=0,replay_ms=0;
    std::vector<DWORD> worker_ids;
};
class Backend {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Backend(ID3D11Device*,ID3D11DeviceContext*,unsigned workers);
    ~Backend();Backend(const Backend&)=delete;Backend& operator=(const Backend&)=delete;
    // Only the creating/main thread may submit; workers never use immediate.
    // No GPU commands are replayed until ALL lists have recorded successfully.
    Stats submit(std::shared_ptr<const Batch>);
    void serial(const Batch&); // reference path, same packet workload
};
}
