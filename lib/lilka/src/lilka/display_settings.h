#pragma once

#include <stdint.h>

#define LILKA_DISPLAY_NVS_NAMESPACE "backlight"
#define LILKA_DISPLAY_NVS_BRIGHTNESS_KEY "level"
#define LILKA_DISPLAY_NVS_TIMEOUT_KEY "timeoutSeconds"
#define LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS 120

namespace lilka {

/// Shared settings: NVS namespace backlight, keys level and timeoutSeconds.
/// Apps opt into idle-off by calling begin() and serviceIdle() on the LCD owner task.
class DisplaySettings {
public:
    /// Load timeout (default 120 seconds, 0 disables). Creates a deferred-save task.
    static bool begin();
    static bool isAvailable();
    static uint32_t getTimeoutSeconds();
    /// RAM-only clamped update (0..3600); save after 600 ms quiet.
    static bool setTimeoutSeconds(uint32_t seconds);
    /// Called by the input scanner with physical levels, even for consumed chords.
    static void noteInput(uint16_t pressed, uint32_t now);
    static bool isSleeping();
    static bool wakePending();
    /// eligible means foreground is idle, not playing/loading/running a game.
    /// LCD owner task ONLY: may call LCD sleep/wake, including driver delays.
    /// Returns true when it woke this frame; callers should repaint retained layers.
    static bool serviceIdle(bool eligible);

private:
    static void servicePersistence();
};

extern DisplaySettings displaySettings;

} // namespace lilka
