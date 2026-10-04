#include "audio.h"
#include "config.h"
#include "ping.h"
#include "Preferences.h"
#include "serial.h"
#include <atomic>
#include <freertos/semphr.h>

namespace lilka {
namespace {
std::atomic<int> liveVolume{LILKA_SOUND_NVS_DEFAULT_VOLUME};
std::atomic<bool> volumeLoaded{false};
portMUX_TYPE volumeMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t volumeRevision = 0;
uint32_t volumeChangedAt = 0;
uint32_t savedVolumeRevision = 0; // protected by settingsMutex
uint32_t volumeRetryAt = 0;
bool volumeSaveFailed = false;

SemaphoreHandle_t settingsMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}
} // namespace

Audio::Audio() {
}

// Thing to be run in parralel thread performing actual output
void welcomePlay(void* arg) {
#if LILKA_VERSION == 1
    serial.err("This part of code should never be called. Audio not supported for this version of lilka");
#elif LILKA_VERSION == 2
    // Signed 16-bit PCM
    const int16_t* ping = reinterpret_cast<const int16_t*>(ping_raw);
    vTaskDelay(400 / portTICK_PERIOD_MS);

    int16_t buf;
    I2S.begin(I2S_PHILIPS_MODE, 22050, 16);
    for (int i = 0; i < ping_raw_size / 2; i++) {
        memcpy(&buf, &ping[i], 2);
        lilka::audio.adjustVolume(&buf, 2, 16, audio.getVolume());

        I2S.write(buf >> 2);
        I2S.write(buf >> 2);
    }
    I2S.end();

    vTaskDelete(NULL);
#endif
}

void Audio::begin() {
    getVolume(); // One NVS read before input and audio tasks start.
    static TaskHandle_t settingsTask = nullptr;
    if (!settingsTask) {
        xTaskCreate(
            [](void*) {
                while (1) {
                    Audio::serviceVolumePersistence();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            },
            "audioSettings", 2048, nullptr, 1, &settingsTask
        );
    }
    initPins();

    I2S.setAllPins(LILKA_I2S_BCLK, LILKA_I2S_LRCK, LILKA_I2S_DOUT, LILKA_I2S_DOUT, -1);

#ifndef LILKA_NO_AUDIO_HELLO
    if (getStartupSoundEnabled()) playStartupSound();
#endif
}

void Audio::playStartupSound() {
#if LILKA_VERSION == 2
    xTaskCreatePinnedToCore(welcomePlay, "welcomePlay", 4096, NULL, 1, NULL, 0);
#endif
}

void Audio::initPins() {
    // Set up I2S pins globally
    constexpr uint8_t pinCount = 3;
    uint8_t pins[pinCount] = {LILKA_I2S_BCLK, LILKA_I2S_LRCK, LILKA_I2S_DOUT};
    uint8_t funcs[pinCount] = {I2S0O_BCK_OUT_IDX, I2S0O_WS_OUT_IDX, I2S0O_SD_OUT_IDX};
    for (int i = 0; i < pinCount; i++) {
        gpio_pad_select_gpio(pins[i]);
        gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
        gpio_matrix_out(pins[i], funcs[i], false, false);
    }
}

void Audio::adjustVolume(void* buffer, size_t size, int bitsPerSample, uint32_t volumeLevel) {
    int gain = (volumeLevel * 1024.0) / 100.0;
    int samples = size / (bitsPerSample / 8);

    if (bitsPerSample == 8) {
        uint8_t* smp = static_cast<uint8_t*>(buffer);

        for (int i = 0; i < samples; i++) {
            *smp = (*smp * gain) >> 10;
            smp++;
        }
    } else if (bitsPerSample == 16) {
        int16_t* smp = static_cast<int16_t*>(buffer);

        for (int i = 0; i < samples; i++) {
            *smp = (*smp * gain) >> 10;
            smp++;
        }
    } else if (bitsPerSample == 32) {
        int32_t* smp = static_cast<int32_t*>(buffer);

        for (int i = 0; i < samples; i++) {
            *smp = (static_cast<int64_t>(*smp) * gain) >> 10;
            smp++;
        }
    }
}

// Existing setter remains an immediate persistent write using the same key/type.
// Once loaded, playback getters only read RAM, never NVS.
int Audio::getVolume() {
    if (!volumeLoaded.load()) {
        xSemaphoreTake(settingsMutex(), portMAX_DELAY);
        if (!volumeLoaded.load()) {
            Preferences prefs;
            prefs.begin(LILKA_SOUND_NVS_NAMESPACE, true);
            liveVolume.store(prefs.getUInt(LILKA_SOUND_NVS_VOLUME_LEVEL_KEY, LILKA_SOUND_NVS_DEFAULT_VOLUME));
            prefs.end();
            volumeLoaded.store(true);
        }
        xSemaphoreGive(settingsMutex());
    }
    return liveVolume.load();
}

void Audio::setVolume(int level) {
    xSemaphoreTake(settingsMutex(), portMAX_DELAY);
    portENTER_CRITICAL(&volumeMux);
    const uint32_t revision = ++volumeRevision;
    liveVolume.store(level);
    volumeLoaded.store(true);
    portEXIT_CRITICAL(&volumeMux);
    Preferences prefs;
    prefs.begin(LILKA_SOUND_NVS_NAMESPACE, false);
    const bool saved = prefs.putUInt(LILKA_SOUND_NVS_VOLUME_LEVEL_KEY, level) != 0;
    prefs.end();
    if (saved) {
        savedVolumeRevision = revision;
        volumeSaveFailed = false;
    }
    xSemaphoreGive(settingsMutex());
}

void Audio::changeVolumeLive(int delta) {
    const uint32_t now = millis();
    // Bounded RAM-only critical section: value, timestamp and revision form one
    // snapshot. No mutex wait, allocation, callback or NVS operation here.
    portENTER_CRITICAL(&volumeMux);
    const int old = liveVolume.load();
    const int bounded = old < 0 ? 0 : (old > 100 ? 100 : old);
    const int64_t requested = static_cast<int64_t>(bounded) + delta;
    const int next = requested < 0 ? 0 : (requested > 100 ? 100 : static_cast<int>(requested));
    if (next != old) {
        liveVolume.store(next);
        volumeChangedAt = now;
        ++volumeRevision;
    }
    portEXIT_CRITICAL(&volumeMux);
}

void Audio::serviceVolumePersistence() {
    // Runs on its own task; serializes with public setters, NOT controller scans.
    xSemaphoreTake(settingsMutex(), portMAX_DELAY);
    portENTER_CRITICAL(&volumeMux);
    const uint32_t revision = volumeRevision;
    const uint32_t changedAt = volumeChangedAt;
    const int level = liveVolume.load();
    portEXIT_CRITICAL(&volumeMux);
    const uint32_t now = millis();
    const bool retryReady = !volumeSaveFailed || static_cast<int32_t>(now - volumeRetryAt) >= 0;
    if (retryReady && revision != savedVolumeRevision && now - changedAt >= 600) {
        Preferences prefs;
        prefs.begin(LILKA_SOUND_NVS_NAMESPACE, false);
        const bool saved = prefs.putUInt(LILKA_SOUND_NVS_VOLUME_LEVEL_KEY, level) != 0;
        prefs.end();
        // A concurrent RAM adjustment gets a later revision and remains dirty.
        if (saved) {
            savedVolumeRevision = revision;
            volumeSaveFailed = false;
        } else {
            volumeSaveFailed = true;
            volumeRetryAt = now + 1000;
        }
    }
    xSemaphoreGive(settingsMutex());
}

uint32_t Audio::getStartupSoundEnabled() {
    Preferences prefs;

    prefs.begin(LILKA_SOUND_NVS_NAMESPACE, true);
    bool startupSound = prefs.getBool(LILKA_SOUND_NVS_WELCOME_SOUND_KEY, LILKA_SOUND_NVS_DEFAULT_WELCOME_SOUND);

    prefs.end();
    return startupSound;
}

void Audio::setStartupSoundEnabled(bool enable) {
    Preferences prefs;

    prefs.begin(LILKA_SOUND_NVS_NAMESPACE, false);
    prefs.putBool(LILKA_SOUND_NVS_WELCOME_SOUND_KEY, enable);

    prefs.end();
}

Audio audio;

} // namespace lilka
