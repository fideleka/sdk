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
        int brightnessSteps;
        bool selectConsumed;
    };

    Result scan(
        uint16_t pressed, uint32_t now, bool enabled, bool selectAdjusting = true, bool brightnessEnabled = false
    ) {
        constexpr uint16_t select = 1 << 8;
        constexpr uint16_t start = 1 << 9;
        // Cancellation survives missed release scans until the next physical press.
        if ((pressed & select) && !(previous & select)) selectConsumed = false;
        Result result = {suppressed, 0, 0, selectConsumed};
        // Include the release scan, so no unmatched callback/release leaks out.
        suppressed &= pressed;
        active &= pressed;
        if (!selectAdjusting || !enabled || !(pressed & select) || (pressed & start)) active = 0;
        if (!brightnessEnabled) active &= ~((1 << 2) | (1 << 3));
        for (int direction = 0; direction < (brightnessEnabled ? 4 : 2); ++direction) {
            const uint16_t bit = 1 << direction;
            const bool capture = selectAdjusting && enabled && (previous & select) && (pressed & select) &&
                                 !(pressed & start) && (pressed & bit) && !(previous & bit);
            if (capture) {
                suppressed |= bit | select;
                selectConsumed = true;
                active |= bit;
                nextRepeat[direction] = now + 400;
            }
            const bool repeat = (active & bit) && static_cast<int32_t>(now - nextRepeat[direction]) >= 0;
            if (capture || repeat) {
                // At most one step per scan: no bursts after a delayed task.
                if (repeat) nextRepeat[direction] = now + 100;
                if (direction == 0) ++result.volumeSteps;
                if (direction == 1) --result.volumeSteps;
                if (direction == 2) --result.brightnessSteps;
                if (direction == 3) ++result.brightnessSteps;
            }
        }
        // Opposing brightness directions cancel; no out-of-phase repeat drift.
        if (brightnessEnabled && (pressed & (1 << 2)) && (pressed & (1 << 3))) {
            result.brightnessSteps = 0;
            active &= ~((1 << 2) | (1 << 3));
        }
        if (selectConsumed && (pressed & select)) suppressed |= select;
        result.selectConsumed = selectConsumed;
        result.suppressed |= suppressed;
        previous = pressed;
        return result;
    }

private:
    bool selectConsumed = false;
    uint16_t previous = 0;
    uint16_t suppressed = 0;
    uint16_t active = 0;
    uint32_t nextRepeat[4] = {};
};

} // namespace detail
} // namespace lilka
