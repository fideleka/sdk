#include "audio.h"
#include "settings_persistence.h"
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
VolumeOverlaySnapshot volumeOverlay;

SemaphoreHandle_t settingsMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}
} // namespace

Audio::Audio() {
}

#if LILKA_VERSION == 2 && !defined(LILKA_NO_AUDIO_HELLO)
namespace {
// Arduino's wrapper omits mck_io_num (thus selects GPIO0). Startup installs
// once, never preempts an existing driver, and explicitly configures all pins.
bool writeStartupFrames(const int16_t* frames, size_t bytes) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(frames);
    while (bytes) {
        size_t written = 0;
        if (esp_i2s::i2s_write(esp_i2s::I2S_NUM_0, data, bytes, &written, pdMS_TO_TICKS(100)) != ESP_OK || !written ||
            written > bytes) {
            return false;
        }
        data += written;
        bytes -= written;
    }
    return true;
}

void outputStartupSound() {
    esp_i2s::i2s_config_t config = {};
    config.mode = static_cast<esp_i2s::i2s_mode_t>(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX);
    config.sample_rate = 22050;
    config.bits_per_sample = esp_i2s::I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = esp_i2s::I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = esp_i2s::I2S_COMM_FORMAT_STAND_I2S;
    config.dma_buf_count = 4;
    config.dma_buf_len = 128;
    config.tx_desc_auto_clear = true;
    if (esp_i2s::i2s_driver_install(esp_i2s::I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return;

    // Cleanup before deleting the FreeRTOS task (which does not unwind).
    struct OwnedDriver {
        ~OwnedDriver() {
            esp_i2s::i2s_driver_uninstall(esp_i2s::I2S_NUM_0);
        }
    } ownedDriver;
    esp_i2s::i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = LILKA_I2S_BCLK;
    pins.ws_io_num = LILKA_I2S_LRCK;
    pins.data_out_num = LILKA_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (esp_i2s::i2s_set_pin(esp_i2s::I2S_NUM_0, &pins) != ESP_OK ||
        esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0) != ESP_OK) {
        return;
    }

    // Immutable mono source; bounded aligned signed-16 stereo output. Refresh
    // live master gain each chunk, retaining the welcome sound's /4 headroom.
    constexpr size_t chunkFrames = 128;
    int16_t frames[chunkFrames * 2];
    const size_t sampleCount = ping_raw_size / sizeof(int16_t);
    for (size_t offset = 0; offset < sampleCount; offset += chunkFrames) {
        const size_t count = sampleCount - offset < chunkFrames ? sampleCount - offset : chunkFrames;
        const int volume = audio.getVolume();
        const int gain = ((volume < 0 ? 0 : (volume > 100 ? 100 : volume)) * 1024) / 100;
        for (size_t i = 0; i < count; ++i) {
            int16_t sample;
            memcpy(&sample, ping_raw + (offset + i) * sizeof(sample), sizeof(sample));
            const int16_t output = static_cast<int16_t>(((sample * gain) >> 10) >> 2);
            frames[i * 2] = output;
            frames[i * 2 + 1] = output;
        }
        if (!writeStartupFrames(frames, count * 2 * sizeof(int16_t))) return;
    }
    // Queue a full DMA ring of signed stereo zero frames to drain the source.
    memset(frames, 0, sizeof(frames));
    for (int i = 0; i < config.dma_buf_count; ++i) {
        if (!writeStartupFrames(frames, sizeof(frames))) return;
    }
}
} // namespace

void welcomePlay(void*) {
    vTaskDelay(pdMS_TO_TICKS(400));
    outputStartupSound();
    vTaskDelete(nullptr);
}
#endif

void Audio::begin() {
    getVolume(); // One NVS read before input and audio tasks start.
    if (!detail::SettingsPersistence::begin(0, &Audio::serviceVolumePersistence)) {
        serial.err("Could not start settings persistence worker");
    }
    initPins();

    // Compatibility defaults only: external Arduino I2S.begin/write remains
    // susceptible to implicit GPIO0 MCLK; startup never uses that wrapper.
    I2S.setAllPins(LILKA_I2S_BCLK, LILKA_I2S_LRCK, LILKA_I2S_DOUT, LILKA_I2S_DOUT, -1);

#ifndef LILKA_NO_AUDIO_HELLO
    if (getStartupSoundEnabled()) playStartupSound();
#endif
}

void Audio::playStartupSound() {
#if LILKA_VERSION == 2 && !defined(LILKA_NO_AUDIO_HELLO)
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
    detail::SettingsPersistence::notify();
}

namespace {
void adjustLiveVolume(int delta, bool shortcut) {
    const uint32_t now = millis();
    // Bounded RAM-only critical section: value, timestamp and revision form one
    // snapshot. No mutex wait, allocation, callback or NVS operation here.
    bool changed = false;
    portENTER_CRITICAL(&volumeMux);
    const int old = liveVolume.load();
    const int bounded = old < 0 ? 0 : (old > 100 ? 100 : old);
    if (shortcut) delta = delta > 0 ? (bounded < 5 ? 1 : 5) : (bounded <= 4 ? -1 : -5);
    const int64_t requested = static_cast<int64_t>(bounded) + delta;
    const int next = requested < 0 ? 0 : (requested > 100 ? 100 : static_cast<int>(requested));
    if (next != old) {
        liveVolume.store(next);
        volumeChangedAt = now;
        ++volumeRevision;
        changed = true;
    }
    if (shortcut) {
        volumeOverlay.level = next;
        volumeOverlay.adjustedAt = now;
        volumeOverlay.valid = true;
    }
    portEXIT_CRITICAL(&volumeMux);
    if (changed) detail::SettingsPersistence::notify();
}
} // namespace

void Audio::changeVolumeLive(int delta) {
    adjustLiveVolume(delta, false);
}

void Audio::stepVolumeShortcut(int direction) {
    if (direction) adjustLiveVolume(direction, true);
}

VolumeOverlaySnapshot Audio::getVolumeOverlay() {
    portENTER_CRITICAL(&volumeMux);
    const auto snapshot = volumeOverlay;
    portEXIT_CRITICAL(&volumeMux);
    return snapshot;
}

uint32_t Audio::serviceVolumePersistence() {
    // Runs on the shared settings task; serializes with public setters, NOT controller scans.
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
    uint32_t next = 0;
    if (revision != savedVolumeRevision) {
        const uint32_t elapsed = now - changedAt;
        next = elapsed < 600 ? 600 - elapsed : 1;
        if (volumeSaveFailed && static_cast<int32_t>(now - volumeRetryAt) < 0 && volumeRetryAt - now > next) {
            next = volumeRetryAt - now;
        }
    }
    xSemaphoreGive(settingsMutex());
    return next;
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
