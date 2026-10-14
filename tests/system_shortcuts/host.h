#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cassert>

using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE* mux) {
    ++*mux;
}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux) {
    assert(*mux == 1);
    --*mux;
}
using SemaphoreHandle_t = int;
using TaskHandle_t = void*;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1;
inline uint32_t ulTaskNotifyTake(int, TickType_t) {
    return 0;
}
inline void xTaskNotifyGive(TaskHandle_t) {
}
extern uint32_t hostNow;
extern int hostNextMutex, hostLocks[64], hostReads, hostWrites, hostTaskCount;
extern bool hostHotScan, hostWriteFailure;
extern uint32_t hostStoredVolume;
extern void (*hostWriteHook)();
constexpr int portMAX_DELAY = -1;
constexpr int pdPASS = 1;
constexpr int portTICK_PERIOD_MS = 1;
constexpr int INPUT_PULLUP = 1;
#define pdMS_TO_TICKS(ms) (ms)
inline uint32_t millis() {
    return hostNow;
}
inline int xSemaphoreCreateMutex() {
    return ++hostNextMutex;
}
inline int xSemaphoreCreateRecursiveMutex() {
    return xSemaphoreCreateMutex();
}
inline void xSemaphoreTake(int id, int) {
    ++hostLocks[id];
}
inline void xSemaphoreTakeRecursive(int id, int wait) {
    xSemaphoreTake(id, wait);
}
inline void xSemaphoreGive(int id) {
    if (hostLocks[id]) --hostLocks[id];
}
inline void xSemaphoreGiveRecursive(int id) {
    xSemaphoreGive(id);
}
inline void vTaskDelay(int) {
}
inline void vTaskDelete(void*) {
}
inline void pinMode(int, int) {
}
inline int digitalRead(int) {
    return 1;
}
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, TaskHandle_t* task) {
    ++hostTaskCount;
    if (task) *task = reinterpret_cast<void*>(1);
    return pdPASS;
}
inline void xTaskCreatePinnedToCore(
    void (*fn)(void*), const char* name, int stack, void* arg, int priority, TaskHandle_t* task, int
) {
    xTaskCreate(fn, name, stack, arg, priority, task);
}
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
constexpr int I2S0O_BCK_OUT_IDX = 1, I2S0O_WS_OUT_IDX = 2, I2S0O_SD_OUT_IDX = 3;
inline void gpio_pad_select_gpio(int) {
}
inline int gpio_set_direction(int, int) {
    return 0;
}
inline void gpio_matrix_out(int, int, bool, bool) {
}
constexpr int I2S_PHILIPS_MODE = 1;
struct MockI2S {
    void begin(int, int, int) {
    }
    void setAllPins(int, int, int, int, int) {
    }
    void write(int) {
    }
    void end() {
    }
};
extern MockI2S I2S;
class Preferences {
public:
    bool backlight = false;
    bool begin(const char* name, bool) {
        assert(!hostHotScan);
        backlight = strcmp(name, "backlight") == 0;
        assert(backlight || strcmp(name, "sound") == 0);
        return true;
    }
    uint32_t getUInt(const char* key, uint32_t fallback) {
        if (backlight) return fallback;
        assert(strcmp(key, "volumeLevel") == 0);
        ++hostReads;
        return hostStoredVolume;
    }
    size_t putUInt(const char* key, uint32_t value) {
        if (backlight) return sizeof(value);
        assert(strcmp(key, "volumeLevel") == 0);
        ++hostWrites;
        if (hostWriteHook) {
            auto hook = hostWriteHook;
            hostWriteHook = nullptr;
            hook();
        }
        if (hostWriteFailure) return 0;
        hostStoredVolume = value;
        return sizeof(value);
    }
    bool getBool(const char*, bool value) {
        return value;
    }
    void putBool(const char*, bool) {
    }
    void end() {
    }
};
namespace lilka {
struct MockSerial {
    void log(const char*) {
    }
    void err(const char*) {
    }
};
extern MockSerial serial;
} // namespace lilka

constexpr int ESP_OK = 0;
constexpr int GPIO_MODE_OUTPUT_OD = 3, GPIO_FLOATING = 0;
inline int gpio_reset_pin(int) {
    assert(false);
    return 0;
}
inline int gpio_set_pull_mode(int, int) {
    assert(false);
    return 0;
}
inline int gpio_set_level(int, int) {
    assert(false);
    return 0;
}
using ledc_mode_t = int;
using ledc_channel_t = int;
constexpr int LEDC_LOW_SPEED_MODE = 0, LEDC_CHANNEL_6 = 6, LEDC_CHANNEL_7 = 7, LEDC_TIMER_3 = 3;
constexpr int LEDC_TIMER_8_BIT = 8, LEDC_AUTO_CLK = 0, LEDC_INTR_DISABLE = 0, LEDC_LS_SIG_OUT0_IDX = 73;
struct ledc_timer_config_t {
    int speed_mode, timer_num, duty_resolution, freq_hz, clk_cfg;
};
struct ledc_channel_config_t {
    int speed_mode, channel, timer_sel, intr_type, gpio_num;
    uint32_t duty;
};
inline int ledc_timer_config(const ledc_timer_config_t*) {
    assert(false);
    return 0;
}
inline int ledc_channel_config(const ledc_channel_config_t*) {
    assert(false);
    return 0;
}
inline int ledc_set_duty(int, int, uint32_t) {
    assert(false);
    return 0;
}
inline int ledc_update_duty(int, int) {
    assert(false);
    return 0;
}
inline int ledc_stop(int, int, int) {
    assert(false);
    return 0;
}
inline int ledc_bind_channel_timer(int, int, int) {
    assert(false);
    return 0;
}
inline int ledc_set_duty_with_hpoint(int, int, uint32_t, uint32_t) {
    assert(false);
    return 0;
}
inline void esp_rom_gpio_connect_out_signal(uint32_t, uint32_t, bool, bool) {
    assert(false);
}
