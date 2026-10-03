#include "multicore/visibility.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace multicore {
Frustum::Frustum(std::array<Plane, 6> planes, std::uint8_t active_mask)
    : planes_(planes), active_mask_(active_mask) {
    if (active_mask & ~0x3f) throw std::invalid_argument("Unknown active plane bits");
    for (std::size_t i = 0; i != planes_.size(); ++i) {
        if (!(active_mask_ & (1u << i))) continue;
        const auto p = planes_[i];
        const double length = std::sqrt(double(p.x)*p.x + double(p.y)*p.y + double(p.z)*p.z);
        if (!std::isfinite(length) || length == 0 || !std::isfinite(p.offset))
            throw std::invalid_argument("Invalid active plane");
        normal_lengths_[i] = length;
    }
}
bool Frustum::may_be_visible(const Sphere& s) const noexcept {
    // Invalid bounds must not make objects disappear. Engine-equivalent semantics
    // and boundary tolerances still require verification by the future adapter.
    if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z) ||
        !std::isfinite(s.radius) || s.radius < 0) return true;
    for (std::size_t i = 0; i != planes_.size(); ++i) {
        if (!(active_mask_ & (1u << i))) continue;
        const auto p = planes_[i];
        const double distance = double(p.x)*s.x + double(p.y)*s.y + double(p.z)*s.z + p.offset;
        if (distance < -double(s.radius)*normal_lengths_[i]) return false;
    }
    return true;
}
std::uint8_t Frustum::skyrim17104_test(const Sphere& s, bool optimize) const noexcept {
    auto mask = active_mask_;
    for (std::size_t i = 0; i != planes_.size(); ++i) {
        const auto bit = static_cast<std::uint8_t>(1u << i);
        if (!(mask & bit)) continue;
        const auto p = planes_[i];
        // Match the verified scalar SSE operation order, precision and strict lower
        // boundary. offset is the negative of NiPlane.constant. No normalization.
        const float x = p.x*s.x, y = p.y*s.y, z = p.z*s.z;
        const float distance = ((y+x)+z)+p.offset;
        if (!(distance > -s.radius)) return mask; // includes unordered (NaN)
        if (optimize && distance >= s.radius) mask = static_cast<std::uint8_t>(mask & ~bit);
    }
    return static_cast<std::uint8_t>(0x80 | mask);
}
namespace {
void check_size(std::span<const Sphere> input, std::span<std::uint8_t> output) {
    if (input.size() != output.size()) throw std::invalid_argument("Output size differs from input");
}
void classify_range(std::span<const Sphere> input, const Frustum& frustum,
                    std::span<std::uint8_t> output, std::size_t begin, std::size_t end, TestMode mode) noexcept {
    for (auto i = begin; i != end; ++i) {
        output[i] = mode == TestMode::Conservative ? static_cast<std::uint8_t>(frustum.may_be_visible(input[i])) :
            frustum.skyrim17104_test(input[i], mode == TestMode::Skyrim17104PlaneOptimization);
    }
}
}
void classify_serial(std::span<const Sphere> input, const Frustum& frustum,
                     std::span<std::uint8_t> output, TestMode mode) {
    check_size(input, output);
    classify_range(input, frustum, output, 0, input.size(), mode);
}
struct VisibilityWorkers::State {
    std::mutex dispatch_mutex, mutex;
    std::condition_variable_any start;
    std::condition_variable done;
    std::span<const Sphere> input;
    std::span<std::uint8_t> output;
    const Frustum* frustum = nullptr;
    TestMode mode = TestMode::Conservative;
    std::uint64_t generation = 0;
    std::size_t remaining = 0, count, threshold;
    std::vector<std::jthread> workers;
    State(std::size_t n, std::size_t serial_threshold) : count(n), threshold(serial_threshold) {
        if (n > 64) throw std::invalid_argument("Worker count exceeds 64");
        workers.reserve(n);
        for (std::size_t id = 0; id != n; ++id) {
            workers.emplace_back([this, id](std::stop_token stop) {
                std::uint64_t seen = 0;
                for (;;) {
                    std::unique_lock lock(mutex);
                    if (!start.wait(lock, stop, [&]{ return generation != seen; })) return;
                    seen = generation;
                    const auto local_input = input;
                    const auto local_output = output;
                    const auto* local_frustum = frustum;
                    const auto local_mode = mode;
                    const auto size = local_input.size();
                    const auto base = size/count, extra = size%count;
                    const auto begin = id*base + std::min(id, extra);
                    const auto end = begin + base + (id < extra ? 1 : 0);
                    lock.unlock();
                    classify_range(local_input, *local_frustum, local_output, begin, end, local_mode);
                    lock.lock();
                    if (--remaining == 0) done.notify_one();
                }
            });
        }
    }
    ~State() {
        for (auto& worker : workers) worker.request_stop();
        start.notify_all();
        workers.clear(); // joins before synchronization objects and batch storage are destroyed
    }
};
VisibilityWorkers::VisibilityWorkers(std::size_t count, std::size_t threshold)
    : state_(std::make_unique<State>(count, threshold)) {}
VisibilityWorkers::~VisibilityWorkers() = default;
void VisibilityWorkers::classify(std::span<const Sphere> input, const Frustum& frustum,
                                 std::span<std::uint8_t> output, TestMode mode) {
    check_size(input, output);
    const std::lock_guard batch_lock(state_->dispatch_mutex);
    if (state_->count == 0 || input.size() < state_->threshold || input.empty()) {
        classify_range(input, frustum, output, 0, input.size(), mode);
        return;
    }
    std::unique_lock lock(state_->mutex);
    state_->input = input;
    state_->output = output;
    state_->frustum = &frustum;
    state_->mode = mode;
    state_->remaining = state_->count;
    ++state_->generation;
    state_->start.notify_all();
    state_->done.wait(lock, [&]{return state_->remaining == 0;});
    state_->input = {};
    state_->output = {};
    state_->frustum = nullptr;
}
}
