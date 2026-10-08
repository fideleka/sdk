#include "display_settings.h"
#include "board.h"
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

namespace lilka {
namespace {
std::atomic<bool> ready{false}, sleeping{false}, pendingWake{false};
std::atomic<uint32_t> timeout{LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS}, lastInput{0};
portMUX_TYPE settingsMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t revision = 0, changedAt = 0, savedRevision = 0;
} // namespace

bool DisplaySettings::begin() {
    if (ready.load()) return true;
    Preferences prefs;
    uint32_t seconds = LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS;
    if (prefs.begin(LILKA_DISPLAY_NVS_NAMESPACE, true)) {
        seconds = prefs.getUInt(LILKA_DISPLAY_NVS_TIMEOUT_KEY, LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS);
        prefs.end();
    }
    timeout.store(seconds > 3600 ? LILKA_DISPLAY_DEFAULT_TIMEOUT_SECONDS : seconds);
    lastInput.store(millis());
    TaskHandle_t task = nullptr;
    if (xTaskCreate(
            [](void*) {
                while (1) {
                    DisplaySettings::servicePersistence();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            },
            "displaySettings", 3072, nullptr, 1, &task
        ) != pdPASS) return false;
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
    portENTER_CRITICAL(&settingsMux);
    if (seconds != timeout.load()) {
        timeout.store(seconds);
        changedAt = millis();
        ++revision;
    }
    portEXIT_CRITICAL(&settingsMux);
    if (!seconds && sleeping.load()) pendingWake.store(true);
    return true;
}

void DisplaySettings::noteInput(uint16_t pressed, uint32_t now) {
    if (!ready.load() || !pressed) return;
    lastInput.store(now);
    if (sleeping.load()) pendingWake.store(true);
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
    const uint32_t seconds = timeout.load();
    if (sleeping.load()) {
        if (pendingWake.load() || !eligible || !seconds) {
            board.disablePowerSavingMode();
            lastInput.store(millis());
            pendingWake.store(false);
            sleeping.store(false);
            return true;
        }
    } else if (!eligible || !seconds) {
        lastInput.store(now);
    } else if (static_cast<int32_t>(now - lastInput.load()) >= static_cast<int32_t>(seconds * 1000)) {
        // Publish sleep before the LCD transition so a concurrent first button
        // is retained as a wake request and never dispatched to the application.
        sleeping.store(true);
        board.enablePowerSavingMode();
    }
    return false;
}

void DisplaySettings::servicePersistence() {
    portENTER_CRITICAL(&settingsMux);
    const uint32_t pending = revision, when = changedAt, value = timeout.load();
    portEXIT_CRITICAL(&settingsMux);
    if (pending == savedRevision || millis() - when < 600) return;
    Preferences prefs;
    const bool opened = prefs.begin(LILKA_DISPLAY_NVS_NAMESPACE, false);
    const bool saved = opened && prefs.putUInt(LILKA_DISPLAY_NVS_TIMEOUT_KEY, value) == sizeof(uint32_t);
    if (opened) prefs.end();
    if (saved) savedRevision = pending;
    else vTaskDelay(pdMS_TO_TICKS(1000));
}

DisplaySettings displaySettings;
} // namespace lilka
