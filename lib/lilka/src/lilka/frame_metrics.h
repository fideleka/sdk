#pragma once
#include <cstdint>

// Fixed-size interval statistics: no allocation or per-frame serial output.
struct DisplayFrameMetrics {
    uint32_t count = 0, maxUs = 0;
    uint64_t totalUs = 0;
    void record(uint32_t us) {
        ++count;
        totalUs += us;
        if (us > maxUs) maxUs = us;
    }
    uint32_t averageUs() const {
        return count ? static_cast<uint32_t>(totalUs / count) : 0;
    }
    uint32_t fpsX10(uint32_t elapsedMs) const {
        return elapsedMs ? static_cast<uint32_t>(uint64_t(count) * 10000 / elapsedMs) : 0;
    }
};
