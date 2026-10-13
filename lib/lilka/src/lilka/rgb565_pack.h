#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lilka {

// ESP32 framebuffer words are little-endian; LCD wire pixels are big-endian.
// memcpy preserves strict aliasing and lets the compiler use aligned word
// loads/stores. Never read past the final pixel (including odd-sized rows).
inline void packRGB565(uint8_t* output, const uint16_t* pixels, size_t count) {
    if (!(reinterpret_cast<uintptr_t>(pixels) & 3) && !(reinterpret_cast<uintptr_t>(output) & 3)) {
        // The runtime check alone is not enough for Xtensa GCC: without the
        // explicit alignment fact, a 4-byte memcpy expands into byte accesses.
        pixels = static_cast<const uint16_t*>(__builtin_assume_aligned(pixels, 4));
        output = static_cast<uint8_t*>(__builtin_assume_aligned(output, 4));
        while (count >= 2) {
            uint32_t word;
            memcpy(&word, pixels, sizeof(word));
            word = ((word & 0x00ff00ffu) << 8) | ((word & 0xff00ff00u) >> 8);
            memcpy(output, &word, sizeof(word));
            pixels += 2;
            output += 4;
            count -= 2;
        }
    }
    while (count--) {
        const uint16_t pixel = *pixels++;
        *output++ = pixel >> 8;
        *output++ = pixel & 255;
    }
}

} // namespace lilka
