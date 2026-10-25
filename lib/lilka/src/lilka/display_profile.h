#pragma once
#include <stdint.h>
#ifndef LILKA_MEASURE_DISPLAY
#    define LILKA_MEASURE_DISPLAY 0
#endif
#if LILKA_MEASURE_DISPLAY
#    include <atomic>
#    include <esp_timer.h>
#endif

namespace lilka {
namespace displayProfile {
// Cumulative wall-time stages, not pure CPU time or exact on-wire duration.
enum class Stage { NativePack, NativeFeed, NativeWait, ExternalPack, ExternalFeed, ExternalWait, Capture, Count };
struct Snapshot {
    uint32_t n = 0, totalUs = 0, maxUs = 0, bytes = 0;
};
#if LILKA_MEASURE_DISPLAY
struct Counter {
    std::atomic<uint32_t> n{0}, total{0}, maximum{0}, bytes{0};
};
inline Counter* counters() {
    static Counter values[unsigned(Stage::Count)];
    return values;
}
inline void record(Stage stage, uint32_t us, uint32_t bytes = 0) {
    auto& value = counters()[unsigned(stage)];
    value.n.fetch_add(1, std::memory_order_relaxed);
    value.total.fetch_add(us, std::memory_order_relaxed);
    value.bytes.fetch_add(bytes, std::memory_order_relaxed);
    uint32_t old = value.maximum.load(std::memory_order_relaxed);
    while (us > old && !value.maximum.compare_exchange_weak(old, us, std::memory_order_relaxed)) {
    }
}
inline Snapshot sample(Stage stage) {
    auto& value = counters()[unsigned(stage)];
    Snapshot result;
    result.n = value.n.load(std::memory_order_relaxed);
    result.totalUs = value.total.load(std::memory_order_relaxed);
    result.maxUs = value.maximum.load(std::memory_order_relaxed);
    result.bytes = value.bytes.load(std::memory_order_relaxed);
    return result;
}
class Scope {
public:
    explicit Scope(Stage stage, uint32_t bytes = 0) : stage(stage), bytes(bytes), start(esp_timer_get_time()) {
    }
    ~Scope() {
        record(stage, uint32_t(esp_timer_get_time() - start), bytes);
    }

private:
    Stage stage;
    uint32_t bytes;
    int64_t start;
};
#else
class Scope {
public:
    explicit Scope(Stage, uint32_t = 0) {
    }
};
#endif
} // namespace displayProfile
} // namespace lilka
