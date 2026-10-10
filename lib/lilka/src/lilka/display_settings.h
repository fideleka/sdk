#pragma once

#include <stdint.h>

#define LILKA_DISPLAY_NVS_NAMESPACE           "backlight"
#define LILKA_DISPLAY_NVS_BRIGHTNESS_KEY      "level"
#define LILKA_DISPLAY_NVS_TIMEOUT_KEY         "timeoutSeconds"
#define LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS 0
#define LILKA_DISPLAY_NVS_DIM_KEY             "dimSeconds"

namespace lilka {

/// Shared settings: NVS namespace backlight, keys level, timeoutSeconds and dimSeconds.
/// Apps opt into idle-off by calling begin() and serviceIdle() on the LCD owner task.
class DisplaySettings {
public:
    /// Load timeout (default Never (0), 0 disables). Registers with the shared deferred-save worker.
    static bool begin();
    static bool isAvailable();
    static uint32_t getTimeoutSeconds();
    /// RAM-only clamped update (0..3600); save after 600 ms quiet.
    static bool setTimeoutSeconds(uint32_t seconds);
    /// Optional idle dim timer (Never by default); uses the same preset/range.
    static uint32_t getDimTimeoutSeconds();
    static bool setDimTimeoutSeconds(uint32_t seconds);
    /// Called by the input scanner with physical levels, even for consumed chords.
    static void noteInput(uint16_t pressed, uint32_t now);
    static bool isSleeping();
    static bool wakePending();
    /// eligible means foreground is idle, not playing/loading/running a game.
    /// LCD owner task ONLY: may call LCD sleep/wake, including driver delays.
    /// Returns true when it woke/undimmed this frame; repaint retained layers.
    static bool serviceIdle(bool eligible);

private:
    static uint32_t servicePersistence();
};

extern DisplaySettings displaySettings;

} // namespace lilka
