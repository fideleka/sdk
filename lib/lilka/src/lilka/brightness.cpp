#include "brightness.h"
#include "config.h"

#if LILKA_VERSION == 2 && LILKA_INDEPENDENT_BACKLIGHT
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Preferences.h>
#include <Arduino.h>
#include <atomic>

namespace lilka {
namespace {
constexpr ledc_mode_t mode = LEDC_LOW_SPEED_MODE;
constexpr ledc_channel_t channel = LEDC_CHANNEL_7;
std::atomic<bool> enabled{false};
std::atomic<int> requested{100};
SemaphoreHandle_t hardwareMutex = nullptr;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
VolumeOverlaySnapshot feedback;
bool sleeping = false;
uint32_t revision = 0, changedAt = 0, savedRevision = 0;

int clamp(int64_t value) {
    return value < 0 ? 0 : (value > 100 ? 100 : static_cast<int>(value));
}

bool apply(int level) {
    // The exact full-on value is 256, not 255, for an 8-bit LEDC timer.
    const uint32_t duty = level == 100 ? 256 : (level * 255 + 50) / 100;
    return ledc_set_duty(mode, channel, duty) == ESP_OK && ledc_update_duty(mode, channel) == ESP_OK;
}

bool update(int value, bool relative, bool showFeedback) {
    if (!enabled.load()) return false;
    xSemaphoreTake(hardwareMutex, portMAX_DELAY);
    const int next = clamp(relative ? static_cast<int64_t>(requested.load()) + value : value);
    if (!sleeping && !apply(next)) {
        xSemaphoreGive(hardwareMutex);
        return false;
    }
    portENTER_CRITICAL(&snapshotMux);
    if (next != requested.load()) {
        requested.store(next);
        ++revision;
        changedAt = millis();
    }
    if (showFeedback) {
        feedback.level = next;
        feedback.adjustedAt = millis();
        feedback.valid = true;
        feedback.brightness = true;
    }
    portEXIT_CRITICAL(&snapshotMux);
    xSemaphoreGive(hardwareMutex);
    return true;
}
} // namespace

bool Brightness::begin() {
    if (enabled.load()) return true;
    if (!hardwareMutex) hardwareMutex = xSemaphoreCreateMutex();
    if (!hardwareMutex) return false;
    Preferences prefs;
    uint32_t stored = 100;
    if (prefs.begin("backlight", true)) {
        stored = prefs.getUInt("level", 100);
        prefs.end();
    }
    // A saved off state must not make the next boot appear dead.
    const int initial = stored == 0 ? 5 : clamp(stored);
    ledc_timer_config_t timer = {};
    timer.speed_mode = mode;
    timer.timer_num = LEDC_TIMER_3;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz = 20000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t output = {};
    output.speed_mode = mode;
    output.channel = channel;
    output.timer_sel = LEDC_TIMER_3;
    output.intr_type = LEDC_INTR_DISABLE;
    output.gpio_num = LILKA_SLEEP;
    output.duty = initial == 100 ? 256 : (initial * 255 + 50) / 100;
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&output) != ESP_OK) {
        gpio_reset_pin(static_cast<gpio_num_t>(LILKA_SLEEP));
        gpio_set_direction(static_cast<gpio_num_t>(LILKA_SLEEP), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(LILKA_SLEEP), 1);
        return false;
    }
    requested.store(initial);
    enabled.store(true);
    // Persistence never runs on the controller/audio task or under hardwareMutex.
    TaskHandle_t task = nullptr;
    const BaseType_t created = xTaskCreate(
        [](void*) {
            while (1) {
                Brightness::servicePersistence();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        },
        "backlightSave", 3072, nullptr, 1, &task
    );
    if (created != pdPASS) {
        enabled.store(false);
        ledc_stop(mode, channel, 1);
        gpio_reset_pin(static_cast<gpio_num_t>(LILKA_SLEEP));
        gpio_set_direction(static_cast<gpio_num_t>(LILKA_SLEEP), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(LILKA_SLEEP), 1);
        requested.store(100);
        return false;
    }
    return true;
}

bool Brightness::isEnabled() {
    return enabled.load();
}
bool Brightness::isPWMChannelReserved(int value) {
    return enabled.load() && (value == 6 || value == 7);
}
int Brightness::getBrightness() {
    return enabled.load() ? requested.load() : 100;
}
bool Brightness::setBrightness(int value) {
    return update(value, false, true);
}
bool Brightness::changeBrightnessLive(int delta) {
    return update(delta, true, true);
}
bool Brightness::stepBrightnessShortcut(int steps) {
    if (!steps) return false;
    const int delta = steps > 10 ? 100 : (steps < -10 ? -100 : steps * 10);
    return update(delta, true, true);
}

VolumeOverlaySnapshot Brightness::getOverlay() {
    portENTER_CRITICAL(&snapshotMux);
    const auto result = feedback;
    portEXIT_CRITICAL(&snapshotMux);
    return result;
}

bool Brightness::suspend() {
    if (!enabled.load()) return false;
    xSemaphoreTake(hardwareMutex, portMAX_DELAY);
    const bool ok = apply(0);
    if (ok) sleeping = true;
    xSemaphoreGive(hardwareMutex);
    return ok;
}

bool Brightness::resume() {
    if (!enabled.load()) return false;
    xSemaphoreTake(hardwareMutex, portMAX_DELAY);
    const bool ok = apply(requested.load());
    if (ok) sleeping = false;
    xSemaphoreGive(hardwareMutex);
    return ok;
}

void Brightness::servicePersistence() {
    portENTER_CRITICAL(&snapshotMux);
    const uint32_t pending = revision, when = changedAt;
    const int level = requested.load();
    portEXIT_CRITICAL(&snapshotMux);
    if (pending == savedRevision || millis() - when < 600) return;
    Preferences prefs;
    const bool opened = prefs.begin("backlight", false);
    const bool saved = opened && prefs.putUInt("level", level) == sizeof(uint32_t);
    if (opened) prefs.end();
    if (saved) savedRevision = pending; // Concurrent later revisions stay dirty.
    else vTaskDelay(pdMS_TO_TICKS(1000));
}

Brightness brightness;
} // namespace lilka

#else
namespace lilka {
bool Brightness::begin() {
    return false;
}
bool Brightness::isEnabled() {
    return false;
}
bool Brightness::isPWMChannelReserved(int) {
    return false;
}
int Brightness::getBrightness() {
    return 100;
}
bool Brightness::setBrightness(int) {
    return false;
}
bool Brightness::changeBrightnessLive(int) {
    return false;
}
bool Brightness::stepBrightnessShortcut(int) {
    return false;
}
VolumeOverlaySnapshot Brightness::getOverlay() {
    return {};
}
bool Brightness::suspend() {
    return false;
}
bool Brightness::resume() {
    return false;
}
void Brightness::servicePersistence() {}
Brightness brightness;
} // namespace lilka
#endif
