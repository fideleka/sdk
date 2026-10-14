#pragma once

#include "volume_overlay.h"

namespace lilka {

/// Independent backlight control. Opt in with LILKA_INDEPENDENT_BACKLIGHT=1
/// ONLY after physically isolating amplifier SD on Lilka v2.
/// Reserves low-speed LEDC timer 3/channel 7; no other user may reconfigure them.
class Brightness {
public:
    /// Initialized by board.begin(). Stock boards remain unchanged.
    static bool begin();
    /// True only when opted-in PWM initialization succeeded.
    static bool isEnabled();
    /// Arduino LEDC channels 6/7 share the reserved low-speed timer.
    static bool isPWMChannelReserved(int channel);
    /// Native and attached external GPIO ownership for safe script wrappers.
    static bool isPinReserved(int pin);
    /// Attach an extension BLK input through a series resistor. Channel 6 shares
    /// the native 20 kHz timer; HIGH is released, never driven (open drain).
    static bool attachExternalBacklight(int pin);
    static void detachExternalBacklight();
    /// Route backlights without changing selected/saved brightness or overrides.
    static bool setActiveDisplays(bool native, bool external);
    /// RAM-only brightness query, 5..100. Returns 100 when disabled.
    static int getBrightness();
    /// Clamped setter; changes hardware immediately, saves after 600 ms idle.
    /// Returns false if unsupported or the hardware update failed.
    static bool setBrightness(int level);
    /// Bounded relative adjustment; no NVS or display work on this path.
    static bool changeBrightnessLive(int delta);
    /// Five percentage points per signed shortcut step, including limit feedback.
    static bool stepBrightnessShortcut(int steps);
    /// Coherent feedback snapshot, consumed by the SDK display renderer.
    static VolumeOverlaySnapshot getOverlay();
    /// Darken/restore without changing the requested or saved brightness.
    static bool suspend();
    static bool resume();
    /// Hardware-only idle override: selected brightness and NVS remain unchanged.
    static bool dim();
    static bool undim();
    static bool isDimmed();

private:
    static uint32_t servicePersistence();
};

extern Brightness brightness;

} // namespace lilka
