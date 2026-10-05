#pragma once

#include <stdint.h>

namespace lilka {
namespace detail {

// Hardware-independent chord arbitration. Bit positions match Controller::Button.
class SystemShortcuts {
public:
    struct Result {
        uint16_t suppressed;
        int volumeSteps;
    };

    Result scan(uint16_t pressed, uint32_t now, bool enabled) {
        constexpr uint16_t select = 1 << 8;
        constexpr uint16_t start = 1 << 9;
        Result result = {suppressed, 0};
        // Include the release scan, so no unmatched callback/release leaks out.
        suppressed &= pressed;
        active &= pressed;
        if (!enabled || !(pressed & select) || (pressed & start)) active = 0;
        for (int direction = 0; direction < 2; ++direction) {
            const uint16_t bit = 1 << direction;
            const bool capture = enabled && (previous & select) && (pressed & select) && !(pressed & start) &&
                                 (pressed & bit) && !(previous & bit);
            if (capture) {
                suppressed |= bit;
                active |= bit;
                nextRepeat[direction] = now + 400;
            }
            const bool repeat = (active & bit) && static_cast<int32_t>(now - nextRepeat[direction]) >= 0;
            if (capture || repeat) {
                // At most one step per scan: no bursts after a delayed task.
                if (repeat) nextRepeat[direction] = now + 100;
                if (direction == 0) ++result.volumeSteps;
                if (direction == 1) --result.volumeSteps;
            }
        }
        result.suppressed |= suppressed;
        previous = pressed;
        return result;
    }

private:
    uint16_t previous = 0;
    uint16_t suppressed = 0;
    uint16_t active = 0;
    uint32_t nextRepeat[2] = {};
};

} // namespace detail
} // namespace lilka
