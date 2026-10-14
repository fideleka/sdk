#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace displayFrame {
// GNU may_alias permits word access to RGB565 storage without violating strict
// aliasing. Xtensa requires aligned word loads; clipped/odd rows use halfwords.
typedef uint32_t PixelWord __attribute__((__may_alias__));
// Common aligned RGB565 rows: bounded inline word copies instead of a libc
// call per dirty row. The byte/halfword-aligned fallback preserves clipping.
inline void copyPixels(uint16_t* output, const uint16_t* source, size_t pixels) {
    if (!((reinterpret_cast<uintptr_t>(output) | reinterpret_cast<uintptr_t>(source)) & 3)) {
        auto* destination = reinterpret_cast<PixelWord*>(output);
        const auto* incoming = reinterpret_cast<const PixelWord*>(source);
        while (pixels >= 8) {
            destination[0] = incoming[0];
            destination[1] = incoming[1];
            destination[2] = incoming[2];
            destination[3] = incoming[3];
            destination += 4;
            incoming += 4;
            pixels -= 8;
        }
        while (pixels >= 2) {
            *destination++ = *incoming++;
            pixels -= 2;
        }
        if (pixels) *reinterpret_cast<uint16_t*>(destination) = *reinterpret_cast<const uint16_t*>(incoming);
    } else memcpy(output, source, pixels * sizeof(uint16_t));
}

inline bool copyCompareRow(
    uint16_t* output, const uint16_t* source, const uint16_t* previous, size_t pixels, bool force,
    bool outputMatchesPrevious = false
) {
    if (force) {
        copyPixels(output, source, pixels);
        return true;
    }
    const bool retainedPrefix = output == previous || outputMatchesPrevious;
    size_t x = 0;
    if (!((reinterpret_cast<uintptr_t>(output) | reinterpret_cast<uintptr_t>(source) |
           reinterpret_cast<uintptr_t>(previous)) &
          3)) {
        auto* destination = reinterpret_cast<PixelWord*>(output);
        const auto* incoming = reinterpret_cast<const PixelWord*>(source);
        const auto* retained = reinterpret_cast<const PixelWord*>(previous);
        const size_t words = pixels / 2;
        for (size_t i = 0; i < words; ++i) {
            const uint32_t next = incoming[i];
            if (next != retained[i]) {
                destination[i] = next;
                x = (i + 1) * 2;
                // Once a row is dirty, more comparisons serve no purpose. Copy
                // only the suffix, not the already compared/copy-written prefix.
                copyPixels(output + x, source + x, pixels - x);
                return true;
            }
            if (!retainedPrefix) destination[i] = next;
        }
        x = words * 2;
    }
    for (; x < pixels; ++x) {
        const uint16_t next = source[x];
        if (next != previous[x]) {
            output[x] = next;
            ++x;
            copyPixels(output + x, source + x, pixels - x);
            return true;
        }
        if (!retainedPrefix) output[x] = next;
    }
    return false;
}
} // namespace displayFrame
