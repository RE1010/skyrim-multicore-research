#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace multicore {
// Owned numeric snapshots only. No engine pointers or virtual calls.
struct Sphere { float x, y, z, radius; };
struct Plane { float x, y, z, offset; }; // inside: dot(normal, center) + offset >= 0
enum class TestMode { Conservative, Skyrim17104, Skyrim17104PlaneOptimization };
class Frustum {
public:
    explicit Frustum(std::array<Plane, 6> planes, std::uint8_t active_mask = 0x3f);
    [[nodiscard]] bool may_be_visible(const Sphere& sphere) const noexcept;
    // Packed engine result: bit 7 = visible, low six bits = resulting active mask.
    [[nodiscard]] std::uint8_t skyrim17104_test(const Sphere& sphere, bool optimize) const noexcept;
private:
    std::array<Plane, 6> planes_;
    std::array<double, 6> normal_lengths_{};
    std::uint8_t active_mask_;
};

void classify_serial(std::span<const Sphere> input, const Frustum& frustum,
                     std::span<std::uint8_t> output, TestMode mode = TestMode::Conservative);

// One synchronous batch at a time. Workers persist across batches; call completion
// guarantees all output bytes are published. Callers keep input immutable until return.
class VisibilityWorkers {
public:
    explicit VisibilityWorkers(std::size_t worker_count, std::size_t serial_threshold = 4096);
    ~VisibilityWorkers();
    VisibilityWorkers(const VisibilityWorkers&) = delete;
    VisibilityWorkers& operator=(const VisibilityWorkers&) = delete;
    void classify(std::span<const Sphere> input, const Frustum& frustum,
                  std::span<std::uint8_t> output, TestMode mode = TestMode::Conservative);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
