#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
namespace skyrim_mc::stream {
enum class Recording {workers,serial,inline_serial};
struct Statistics {
    bool disabled=false,snapshot_binding_sharing=true;
    std::uint64_t scopes=0,draws=0,replaced=0,worker_draws=0,serial_draws=0,fallback=0,batches=0,errors=0,unknown_constants=0,unsupported=0,foreign_calls=0;
    std::uint64_t constant_checks=0,constant_mismatches=0,different_constant_bytes=0;
    // Elapsed CPU-side wall time, in QueryPerformanceCounter ticks. Worker
    // totals can overlap; execution measures submission, not GPU completion.
    std::uint64_t qpc_frequency=0,capture_ticks=0,upload_copy_ticks=0,serial_record_ticks=0,worker_wait_ticks=0,execute_ticks=0;
    std::uint64_t capture_attempts=0,upload_copies=0,upload_copy_bytes=0,worker_record_ticks[4]{};
    std::uint64_t private_uploads=0,private_upload_reuses=0,private_upload_bytes=0;
    std::uint64_t snapshot_getters=0,state_group_refreshes=0,state_group_reuses=0;
    std::uint64_t recording_bindings=0,recording_bindings_skipped=0;
    // Publication is included in capture time. Queue release runs after replay.
    std::uint64_t snapshot_publish_ticks=0,queue_release_ticks=0,snapshot_group_copies=0,snapshot_group_reuses=0;
    unsigned first_mismatch_slot=0,first_mismatch_offset=0,first_expected_byte=0,first_gpu_byte=0;
    std::uint32_t worker_ids[4]{};
};
class Bridge {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Bridge(ID3D11Device*,ID3D11DeviceContext*,unsigned workers=4);
    ~Bridge();Bridge(const Bridge&)=delete;Bridge& operator=(const Bridge&)=delete;
    bool attach();
    void begin(bool enabled,Recording=Recording::workers,bool reduce_bindings=true) noexcept;
    void end() noexcept;
    // Bounded GPU readback in the original path, never worker replacement.
    void verify_constants(bool) noexcept;
    // Binding cache only. Owned constant bytes are resolved anew on each draw.
    void cache_state(bool) noexcept;
    // Control changes ownership reuse only; draw/byte versions and barriers stay.
    void share_bindings(bool) noexcept;
    Statistics statistics() const noexcept;
};
}
