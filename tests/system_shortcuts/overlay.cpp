#include "volume_overlay.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace lilka;
struct Image {
    int width, height;
    std::vector<uint16_t> pixels;
    Image(int w, int h, uint16_t color = 0x1234) : width(w), height(h), pixels(w * h, color) {}
    void fillRect(int x, int y, int w, int h, uint16_t color) {
        for (int row = y; row < y + h; ++row)
            for (int col = x; col < x + w; ++col) {
                assert(row >= 0 && row < height && col >= 0 && col < width);
                pixels[row * width + col] = color;
            }
    }
    void drawRect(int x, int y, int w, int h, uint16_t color) {
        fillRect(x, y, w, 1, color);
        fillRect(x, y + h - 1, w, 1, color);
        fillRect(x, y, 1, h, color);
        fillRect(x + w - 1, y, 1, h, color);
    }
};

void test(int width, int height, int level, bool brightness) {
    const auto g = volumeOverlayGeometry(width, height);
    VolumeOverlaySnapshot state;
    state.valid = true;
    state.brightness = brightness;
    state.level = level;
    state.adjustedAt = 100;
    const auto original = state;
    Image full(width, height), rows(width, height);
    drawVolumeOverlay(full, state, width, height, 100);
    for (int y = g.y; y < g.y + g.height; ++y) {
        std::vector<uint16_t> scratch(g.width + 2, 0x4321);
        VolumeOverlayRow row{scratch.data() + 1, g.x, y, g.width};
        drawVolumeOverlay(row, state, width, height, 100);
        assert(scratch.front() == 0x4321 && scratch.back() == 0x4321);
        for (int x = 0; x < g.width; ++x) rows.pixels[y * width + g.x + x] = scratch[x + 1];
    }
    assert(full.pixels == rows.pixels);
    assert(std::memcmp(&state, &original, sizeof(state)) == 0);
    int glyphPixels = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            if (x < g.x || x >= g.x + g.width || y < g.y || y >= g.y + g.height)
                assert(full.pixels[y * width + x] == 0x1234);
            if (y > g.y + 12 && y < g.barY && full.pixels[y * width + x] == 0xffff) ++glyphPixels;
        }
    assert(glyphPixels > 50);
    Image expired(width, height);
    drawVolumeOverlay(expired, state, width, height, 1300);
    assert(expired.pixels == Image(width, height).pixels);
    auto other = state;
    other.brightness = !brightness;
    Image alternate(width, height);
    drawVolumeOverlay(alternate, other, width, height, 100);
    assert(alternate.pixels != full.pixels); // Speaker/sun are distinct at every level.
    if (const char* directory = std::getenv("BRIGHTNESS_PREVIEW_DIR")) {
        char name[256];
        std::snprintf(name, sizeof(name), "%s/%s-%dx%d-%d.ppm", directory,
                      brightness ? "light" : "volume", width, height, level);
        FILE* file = std::fopen(name, "wb");
        assert(file);
        std::fprintf(file, "P6\n%d %d\n255\n", width, height);
        for (auto pixel : full.pixels) {
            unsigned char rgb[] = {static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31),
                                   static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63),
                                   static_cast<unsigned char>((pixel & 31) * 255 / 31)};
            std::fwrite(rgb, 1, 3, file);
        }
        std::fclose(file);
    }
}
int main() {
    for (bool brightness : {false, true})
        for (int level : {0, 50, 100}) {
            test(280, 240, level, brightness);
            test(240, 280, level, brightness);
            test(128, 128, level, brightness);
        }
    puts("Speaker/sun + numeric percentages: real font, full/scanline equivalence, expiry, bounds PASS");
}
