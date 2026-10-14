#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KD_ABI_VERSION     1u
#define KD_MAX_SETTINGS    12u
#define KD_TYPE_DISPLAY    1u
#define KD_SETTING_INTEGER 1u
#define KD_SETTING_BOOLEAN 2u
#define KD_SETTING_CHOICE  3u

typedef struct {
    uint32_t id;
    const char* label;
    uint32_t kind;
    int32_t default_value, minimum, maximum, step;
    const char* const* choices;
} kd_setting;

typedef struct {
    uint32_t size, abi, type;
    const char* driver_uuid;
    const char* device_uuid;
    const char* name;
    const char* version;
    uint32_t settings_version, setting_count;
    const kd_setting* settings;
    uint32_t state_size;
    int32_t (*set_output)(const void* host, void* state, uint32_t enabled);
    uint32_t (*frame_interval_ms)(const void* host, void* state);
    int32_t (*enable)(const void* host, void* state);
    void (*disable)(const void* host, void* state);
    int32_t (*present)(const void* host, void* state, const uint16_t* pixels, uint16_t width, uint16_t height);
    // Optional ABI-1 tail. Older descriptors end before this member and remain valid.
    int32_t (*present_rows)(
        const void* host, void* state, const uint16_t* pixels, uint16_t width, uint16_t height, uint16_t first_row,
        uint16_t row_count
    );
    // Retired ABI-1 compatibility tail from the 1.1.2 flush experiment.
    // Kept for descriptor-size compatibility only; the host never calls it.
    uint32_t (*prefer_full_frame)(const void* host, void* state);
    // Optional ABI-1 tail: -1 means no external backlight control.
    // Query from immutable settings: the host may call this before enable
    // to hold BLK off during panel reset/black initialization.
    int32_t (*backlight_pin)(const void* host, void* state);
} kd_device;

typedef struct {
    uint32_t size, abi;
    void* context;
    int32_t (*register_device)(void* context, const kd_device* device);
    int32_t (*setting)(void* context, uint32_t id);
    int32_t (*spi_open)(void* context, int32_t clk, int32_t mosi, int32_t dc, int32_t reset, uint32_t hz);
    int32_t (*gpio_write)(void* context, int32_t pin, int32_t level);
    int32_t (*spi_write)(void* context, int32_t data, const uint8_t* bytes, uint32_t count);
    int32_t (*spi_pixels)(void* context, const uint16_t* pixels, uint32_t count);
    void (*spi_close)(void* context);
    void (*delay_ms)(uint32_t ms);
} kd_host;
/* Native module main(1, argv): argv[0] points to kd_host, NOT a text string.
 * Registration is hardware-free. All callbacks are serialized by the host.
 * present borrows pixels until return; no asynchronous retention or allocation.
 * Drivers must be freestanding: no C++ objects, tasks, raw SDK/NVS imports.
 */
#ifdef __cplusplus
}
#endif
