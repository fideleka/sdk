#include "../driver_api.h"

// Compiler-generated aggregate clearing, with no external libc dependency.
void* memset(void* destination, int value, size_t count) {
    volatile unsigned char* bytes = destination;
    for (size_t i = 0; i < count; ++i)
        bytes[i] = (unsigned char)value;
    return destination;
}

typedef struct {
    uint16_t width, height;
    uint8_t rotation, opened, test_drawn;
} State;
static const char* const pin_names[] = {"GPIO12", "GPIO13", "GPIO14", "GPIO21", "GPIO47", "GPIO48"};
static const int32_t pins[] = {12, 13, 14, 21, 47, 48};
static const char* const speed_names[] =
    {"10 MHz", "20 MHz", "40 MHz (test)", "50 MHz (test)", "60 MHz (test)", "70 MHz (test)", "80 MHz (test)"};
static const uint32_t speeds[] = {10000000, 20000000, 40000000, 50000000, 60000000, 70000000, 80000000};
static const char* const rotation_names[] = {"Automatic", "Automatic, flipped"};
static const char* const refresh_names[] = {"15 FPS", "20 FPS", "30 FPS", "60 FPS"};
static const uint32_t intervals[] = {67, 50, 34, 17};
static const char* const drive_names[] = {"79Hz default", "70Hz line", "61Hz line", "79Hz frame"};
static const char* const backlight_names[] = {"Disconnected", "GPIO47", "GPIO48"};
static const int32_t backlight_pins[] = {-1, 47, 48};
static const char* const test_names[] = {"Off", "Fixed image + writes", "Fixed image, hold"};
static const kd_setting settings[] = {
    {1, "Clock pin", KD_SETTING_CHOICE, 0, 0, 5, 1, pin_names},
    {2, "MOSI pin", KD_SETTING_CHOICE, 1, 0, 5, 1, pin_names},
    {3, "Reset pin", KD_SETTING_CHOICE, 2, 0, 5, 1, pin_names},
    {4, "DC pin", KD_SETTING_CHOICE, 3, 0, 5, 1, pin_names},
    {5, "SPI speed", KD_SETTING_CHOICE, 1, 0, 6, 1, speed_names},
    {6, "Orientation", KD_SETTING_CHOICE, 0, 0, 1, 1, rotation_names},
    {7, "Refresh limit", KD_SETTING_CHOICE, 2, 0, 3, 1, refresh_names},
    // ID 8 belonged to the removed full-frame experiment. Never reinterpret it.
    {9, "Panel drive", KD_SETTING_CHOICE, 0, 0, 3, 1, drive_names},
    {10, "Backlight pin", KD_SETTING_CHOICE, 0, 0, 2, 1, backlight_names},
    {11, "SPI test", KD_SETTING_CHOICE, 0, 0, 2, 1, test_names},
};
static int command(const kd_host* host, uint8_t code, const uint8_t* bytes, uint32_t count) {
    if (host->spi_write(host->context, 0, &code, 1)) return -1;
    return count ? host->spi_write(host->context, 1, bytes, count) : 0;
}
static int window(const kd_host* host, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    const uint16_t right = x + width - 1, bottom = y + height - 1;
    const uint8_t columns[] = {x >> 8, x & 255, right >> 8, right & 255};
    const uint8_t rows[] = {y >> 8, y & 255, bottom >> 8, bottom & 255};
    return command(host, 0x2a, columns, 4) || command(host, 0x2b, rows, 4) || command(host, 0x2c, 0, 0) ? -1 : 0;
}
static int enable(const void* api, void* memory) {
    const kd_host* host = api;
    State* state = memory;
    if (host->spi_open(
            host->context,
            pins[host->setting(host->context, 1)],
            pins[host->setting(host->context, 2)],
            pins[host->setting(host->context, 4)],
            pins[host->setting(host->context, 3)],
            speeds[host->setting(host->context, 5)]
        ))
        return -1;
    state->opened = 1;
    const int reset = pins[host->setting(host->context, 3)];
    if (host->gpio_write(host->context, reset, 1)) return -1;
    host->delay_ms(10);
    if (host->gpio_write(host->context, reset, 0)) return -1;
    host->delay_ms(20);
    if (host->gpio_write(host->context, reset, 1)) return -1;
    host->delay_ms(120);
    if (command(host, 0x01, 0, 0)) return -1;
    host->delay_ms(120);
    // Same initialization as the device-confirmed compiled-in PoC.
    static const uint8_t initialization[] = {
        3,    0xef, 0x03, 0x80, 0x02, 3,    0xcf, 0x00, 0xc1, 0x30, 4,    0xed, 0x64, 0x03, 0x12, 0x81, 3,
        0xe8, 0x85, 0x00, 0x78, 5,    0xcb, 0x39, 0x2c, 0x00, 0x34, 0x02, 1,    0xf7, 0x20, 2,    0xea, 0,
        0,    1,    0xc0, 0x23, 1,    0xc1, 0x10, 2,    0xc5, 0x3e, 0x28, 1,    0xc7, 0x86, 1,    0x3a, 0x55,
        2,    0xb1, 0,    0x18, 3,    0xb6, 0x08, 0x82, 0x27, 1,    0xf2, 0,    1,    0x26, 1,    15,   0xe0,
        0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0,    15,   0xe1,
        0,    0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f
    };
    for (unsigned offset = 0; offset < sizeof(initialization);) {
        const unsigned count = initialization[offset++];
        const uint8_t code = initialization[offset++];
        if (command(host, code, initialization + offset, count)) return -1;
        offset += count;
    }
    const int drive = host->setting(host->context, 9);
    if (drive) {
        // Datasheet B1: DIVA=0, RTNA=27/31/24. B4: NLA selects normal-mode
        // line/frame polarity inversion, not RGB color inversion (20h/21h).
        const uint8_t rates[] = {0x18, 0x1b, 0x1f, 0x18};
        const uint8_t frame_rate[] = {0x00, rates[drive]};
        const uint8_t inversion = drive == 3 ? 0x06 : 0x02;
        if (command(host, 0xb1, frame_rate, 2) || command(host, 0xb4, &inversion, 1)) return -1;
    }
    if (command(host, 0x20, 0, 0) || command(host, 0x11, 0, 0)) return -1;
    host->delay_ms(120);
    // Never expose uninitialized GRAM. Keep scanout off and clear the complete panel.
    const uint8_t madctl = 0x48;
    if (command(host, 0x28, 0, 0) || command(host, 0x36, &madctl, 1) || window(host, 0, 0, 240, 320)) return -1;
    const uint16_t black[256] = {0};
    for (unsigned remaining = 240 * 320; remaining; remaining -= 256)
        if (host->spi_pixels(host->context, black, 256)) return -1;
    return 0;
}
static void disable(const void* api, void* memory) {
    const kd_host* host = api;
    State* state = memory;
    if (state && state->opened) {
        command(host, 0x28, 0, 0);
        host->spi_close(host->context);
        state->opened = 0;
    }
}
static int set_output(const void* api, void* memory, uint32_t enabled) {
    const kd_host* host = api;
    const State* state = memory;
    return state && state->opened ? command(host, enabled ? 0x29 : 0x28, 0, 0) : -1;
}
static uint32_t interval(const void* api, void* memory) {
    (void)memory;
    const kd_host* host = api;
    return intervals[host->setting(host->context, 7)];
}
static int test_image(const kd_host* host, State* state, unsigned width, unsigned height) {
    // A bounded row buffer, no retained frame/allocation. Identical physical
    // pixels in both test modes; never blank between repeated writes.
    uint16_t row[320];
    static const uint16_t colors[] = {0x0000, 0xffff, 0x8410, 0xfd20, 0xf800, 0x07e0, 0x001f, 0x4208};
    if (window(host, 0, 0, width, height)) return -1;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (y < height / 2) row[x] = colors[x * 8 / width];
            else if (x < width / 2) row[x] = ((x / 8 + y / 8) & 1) ? 0x8410 : 0x4208;
            else row[x] = (y & 1) ? 0x8410 : 0x4208;
        }
        if (host->spi_pixels(host->context, row, width)) return -1;
    }
    state->test_drawn = 1;
    return 0;
}
static int present_rows(
    const void* api, void* memory, const uint16_t* pixels, uint16_t width, uint16_t height, uint16_t first_row,
    uint16_t row_count
) {
    const kd_host* host = api;
    State* state = memory;
    const uint8_t rotation = (width > height ? 1 : 0) + host->setting(host->context, 6) * 2;
    const unsigned panel_width = rotation & 1 ? 320 : 240, panel_height = rotation & 1 ? 240 : 320;
    if (!state->opened || !pixels || !width || !height || width > panel_width || height > panel_height ||
        first_row >= height || !row_count || row_count > height - first_row)
        return -1;
    const int test = host->setting(host->context, 11);
    // Settings changes reactivate with zeroed state. Once held, even incoming
    // orientation/geometry changes must not issue commands or pixel transfers.
    if (test == 2 && state->test_drawn) return 0;
    if (state->width != width || state->height != height || state->rotation != rotation) {
        static const uint8_t madctl[] = {0x48, 0x28, 0x88, 0xe8};
        if (command(host, 0x36, madctl + rotation, 1) || window(host, 0, 0, panel_width, panel_height)) return -1;
        // Enable already cleared all GRAM. Rotation alone cannot introduce old
        // pixels before the first frame; only later geometry changes need a clear.
        if (state->width) {
            const uint16_t black[256] = {0};
            for (unsigned left = panel_width * panel_height; left;) {
                const unsigned count = left < 256 ? left : 256;
                if (host->spi_pixels(host->context, black, count)) return -1;
                left -= count;
            }
        }
        state->width = width;
        state->height = height;
        state->rotation = rotation;
    }
    if (test) return test_image(host, state, panel_width, panel_height);
    if (window(host, (panel_width - width) / 2, (panel_height - height) / 2 + first_row, width, row_count)) return -1;
    return host->spi_pixels(host->context, pixels + (uint32_t)first_row * width, (uint32_t)width * row_count);
}
static int present(const void* api, void* memory, const uint16_t* pixels, uint16_t width, uint16_t height) {
    return present_rows(api, memory, pixels, width, height, 0, height);
}
static int32_t backlight_pin(const void* api, void* memory) {
    (void)memory;
    const kd_host* host = api;
    const int selected = host->setting(host->context, 10);
    return backlight_pins[selected];
}
static const kd_device descriptor = {
    sizeof(kd_device),
    KD_ABI_VERSION,
    KD_TYPE_DISPLAY,
    "cfa4bc25-c2d6-4a18-ade9-16506b19acf1",
    "674db8e0-cd55-4bfb-90b0-1e521e97c1cd",
    "ILI9341 display",
    "1.1.7",
    1,
    sizeof(settings) / sizeof(settings[0]),
    settings,
    sizeof(State),
    set_output,
    interval,
    enable,
    disable,
    present,
    present_rows,
    0,
    backlight_pin
};
int main(int argc, char** argv) {
    if (argc != 1 || !argv || !argv[0]) return -1;
    const kd_host* host = (const kd_host*)argv[0];
    if (host->size != sizeof(kd_host) || host->abi != KD_ABI_VERSION || !host->register_device) return -1;
    return host->register_device(host->context, &descriptor);
}
