#include "display_settings.h"
#include "settings_persistence.h"
#include "board.h"
#include "brightness.h"
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

namespace lilka {
namespace {
std::atomic<bool> ready{false}, sleeping{false}, pendingWake{false};
std::atomic<uint32_t> timeout{LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS}, lastInput{0}, dimTimeout{0};
portMUX_TYPE settingsMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t revision = 0, changedAt = 0, savedRevision = 0, retryAt = 0;
bool saveFailed = false;
} // namespace

bool DisplaySettings::begin() {
    if (ready.load()) return true;
    Preferences prefs;
    uint32_t seconds = LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS, dimSeconds = 0;
    if (prefs.begin(LILKA_DISPLAY_NVS_NAMESPACE, true)) {
        seconds = prefs.getUInt(LILKA_DISPLAY_NVS_TIMEOUT_KEY, LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS);
        dimSeconds = prefs.getUInt(LILKA_DISPLAY_NVS_DIM_KEY, 0);
        prefs.end();
    }
    dimTimeout.store(dimSeconds > 3600 ? 0 : dimSeconds);
    timeout.store(seconds > 3600 ? LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS : seconds);
    lastInput.store(millis());
    if (!detail::SettingsPersistence::begin(2, &DisplaySettings::servicePersistence)) return false;
    ready.store(true);
    return true;
}

bool DisplaySettings::isAvailable() {
    return ready.load();
}

uint32_t DisplaySettings::getTimeoutSeconds() {
    return timeout.load();
}

bool DisplaySettings::setTimeoutSeconds(uint32_t seconds) {
    if (!ready.load()) return false;
    if (seconds > 3600) seconds = 3600;
    lastInput.store(millis()); // Renew activity before publishing a shorter timeout.
    bool dirty = false;
    portENTER_CRITICAL(&settingsMux);
    if (seconds != timeout.load()) {
        timeout.store(seconds);
        changedAt = millis();
        ++revision;
        dirty = true;
    }
    portEXIT_CRITICAL(&settingsMux);
    if (dirty) detail::SettingsPersistence::notify();
    if (!seconds && sleeping.load()) pendingWake.store(true);
    return true;
}

uint32_t DisplaySettings::getDimTimeoutSeconds() {
    return dimTimeout.load();
}

bool DisplaySettings::setDimTimeoutSeconds(uint32_t seconds) {
    if (!ready.load()) return false;
    if (seconds > 3600) seconds = 3600;
    lastInput.store(millis());
    bool dirty = false;
    portENTER_CRITICAL(&settingsMux);
    if (seconds != dimTimeout.load()) {
        dimTimeout.store(seconds);
        changedAt = millis();
        ++revision;
        dirty = true;
    }
    portEXIT_CRITICAL(&settingsMux);
    if (dirty) detail::SettingsPersistence::notify();
    if (brightness.isDimmed()) pendingWake.store(true);
    return true;
}

void DisplaySettings::noteInput(uint16_t pressed, uint32_t now) {
    if (!ready.load() || !pressed) return;
    lastInput.store(now);
    if (sleeping.load() || brightness.isDimmed()) pendingWake.store(true);
}

bool DisplaySettings::isSleeping() {
    return sleeping.load();
}

bool DisplaySettings::wakePending() {
    return pendingWake.load();
}

bool DisplaySettings::serviceIdle(bool eligible) {
    if (!ready.load()) return false;
    const uint32_t now = millis();
    const uint32_t seconds = timeout.load(), dimSeconds = dimTimeout.load();
    const int32_t elapsed = static_cast<int32_t>(now - lastInput.load());
    if (sleeping.load()) {
        if (pendingWake.load() || !eligible || !seconds) {
            board.disablePowerSavingMode();
            lastInput.store(millis());
            pendingWake.store(false);
            sleeping.store(false);
            return true;
        }
    } else if (pendingWake.load() || !eligible ||
               (brightness.isDimmed() && (!dimSeconds || elapsed < static_cast<int32_t>(dimSeconds * 1000)))) {
        const bool wasDimmed = brightness.isDimmed();
        if (wasDimmed && !brightness.undim()) return false;
        pendingWake.store(false);
        lastInput.store(now);
        return wasDimmed;
    } else if (seconds && elapsed >= static_cast<int32_t>(seconds * 1000)) {
        // Publish sleep before the LCD transition so a concurrent first button
        // is retained as a wake request and never dispatched to the application.
        sleeping.store(true);
        board.enablePowerSavingMode();
    } else if (dimSeconds && brightness.isEnabled() && !brightness.isDimmed() &&
               elapsed >= static_cast<int32_t>(dimSeconds * 1000)) {
        brightness.dim();
    }
    return false;
}

uint32_t DisplaySettings::servicePersistence() {
    portENTER_CRITICAL(&settingsMux);
    const uint32_t pending = revision, when = changedAt, value = timeout.load(), dimValue = dimTimeout.load();
    portEXIT_CRITICAL(&settingsMux);
    if (pending == savedRevision) return 0;
    const uint32_t now = millis(), elapsed = now - when;
    uint32_t wait = elapsed < 600 ? 600 - elapsed : 0;
    if (saveFailed && static_cast<int32_t>(now - retryAt) < 0 && retryAt - now > wait) wait = retryAt - now;
    if (wait) return wait;
    Preferences prefs;
    const bool opened = prefs.begin(LILKA_DISPLAY_NVS_NAMESPACE, false);
    const bool saved = opened && prefs.putUInt(LILKA_DISPLAY_NVS_TIMEOUT_KEY, value) == sizeof(uint32_t) &&
                       prefs.putUInt(LILKA_DISPLAY_NVS_DIM_KEY, dimValue) == sizeof(uint32_t);
    if (opened) prefs.end();
    if (saved) savedRevision = pending;
    saveFailed = !saved;
    retryAt = millis() + 1000;
    return saved ? 1 : 1000;
}

DisplaySettings displaySettings;
} // namespace lilka
