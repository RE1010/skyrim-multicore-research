#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
namespace skyrim_mc::stream {
struct DeviceCapabilities {
    std::uint32_t creation_flags=0,feature_level=0;
    std::int32_t threading_query_result=static_cast<std::int32_t>(0x80004005u);
    bool driver_command_lists=false,driver_concurrent_creates=false;
};
DeviceCapabilities query_device_capabilities(ID3D11Device*) noexcept;
enum class Recording {workers,serial,inline_serial};
enum class FlushReason : unsigned {scope_end,capacity,unsupported_draw,gpu_barrier,foreign_call,shutdown,count};
// SDK ABI coverage is checked against this storage bound in the implementation.
inline constexpr unsigned context_method_count=149;
struct MethodProfile {
    std::uint64_t calls=0,samples=0,ticks=0,failed=0,pending=0;
    std::uint64_t scoped_calls=0,scoped_samples=0,scoped_ticks=0;
};
struct BatchBucket {
    std::uint64_t batches=0,draws=0,worker_batches=0,serial_batches=0,worker_draws=0,serial_draws=0;
};
struct Statistics {
    bool disabled=false,snapshot_binding_sharing=true,flat_upload_lookup=true,direct_small_batches=false,direct_small_available=false;
    std::uint64_t scopes=0,draws=0,replaced=0,worker_draws=0,serial_draws=0,fallback=0,batches=0,errors=0,unknown_constants=0,unsupported=0,foreign_calls=0;
    // Diagnostic only: skip scoped DrawIndexed without snapshot/replay. Other
    // draw kinds and query-enclosed draws remain on the original path.
    std::uint64_t suppressed_indexed_draws=0,suppression_query_fallbacks=0;
    bool draw_suppression_active=false;
    bool clean_forwarding=false,context_profile_active=false;
    unsigned profile_sample_every=16;
    std::uint64_t profile_snapshot_qpc=0;std::uint32_t context_owner_thread_id=0;
    MethodProfile context_methods[context_method_count]{};
    std::uint64_t map_modes[6]{}; // READ, WRITE, READ_WRITE, DISCARD, NO_OVERWRITE, DO_NOT_WAIT
    std::uint64_t constant_checks=0,constant_mismatches=0,different_constant_bytes=0;
    // Elapsed CPU-side wall time, in QueryPerformanceCounter ticks. Worker
    // totals can overlap; execution measures submission, not GPU completion.
    std::uint64_t qpc_frequency=0,capture_ticks=0,upload_copy_ticks=0,serial_record_ticks=0,worker_wait_ticks=0,execute_ticks=0;
    std::uint64_t capture_attempts=0,upload_copies=0,upload_copy_bytes=0,worker_record_ticks[4]{};
    std::uint64_t private_uploads=0,private_upload_reuses=0,private_upload_bytes=0;
    std::uint64_t temporary_upload_map_entries=0,flat_upload_entries=0,draw_upload_duplicates=0;
    // Direct small-batch time is INCLUDED in serial_record_ticks. These draws
    // already ran serially; the experiment removes their temporary command list.
    std::uint64_t direct_small_draws=0,direct_small_batch_count=0,direct_small_ticks=0;
    // Finish time is INCLUDED in record time; wait includes worker recording.
    std::uint64_t serial_finish_ticks=0,worker_finish_ticks[4]{};
    // Draw ranges: 1, 2-3, 4-15, 16-63, 64-255, 256+. Counts are cumulative.
    BatchBucket batch_sizes[6]{};
    std::uint64_t flush_reasons[static_cast<unsigned>(FlushReason::count)]{};
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
    // Per-draw deduplication only; retained buffer/version caches are unchanged.
    void flat_upload_lookup(bool) noexcept;
    void direct_small_batches(bool) noexcept;
    // Absolute GetTickCount64 deadline, at most ten seconds ahead. Checked on
    // every indexed draw even if the reporting/control thread stops running.
    void suppress_indexed_draws(std::uint64_t deadline_ms) noexcept;
    // Diagnostics: full original rendering, without retaining upload snapshots.
    // Sampling times only real context calls, excluding adapter callbacks/locks.
    // Profiling is independently bounded by a native deadline <= ten seconds.
    void profile_original(bool clean,std::uint64_t deadline_ms=0,unsigned sample_every=16) noexcept;
    DeviceCapabilities capabilities() const noexcept;
    Statistics statistics() const noexcept;
};
}
