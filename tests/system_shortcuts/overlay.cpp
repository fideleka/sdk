#include "volume_overlay.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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

// Independent, unclipped real-font reference. Explicit codepoints avoid relying
// on the production UTF-8 decoder or production text-band window for the oracle.
void reference(Image& image, const VolumeOverlayGeometry& g, bool oldClip) {
    const uint16_t glyphs[] = {0x0411, 0x0435, 0x0437, ' ', 0x0437, 0x0432, 0x0443, 0x043a, 0x0443};
    VolumeOverlayFontTarget<Image> context{};
    context.target = &image;
    auto& d = context.decoder;
    static const u8g2_cb_t cb = {nullptr, nullptr, VolumeOverlayFontTarget<Image>::line};
    d.cb = &cb;
    d.user_x0 = 0;
    d.user_x1 = image.width;
    d.user_y0 = 0;
    d.user_y1 = oldClip ? g.y + 32 : image.height;
#ifdef U8G2_WITH_CLIP_WINDOW_SUPPORT
    d.is_page_clip_window_intersection = 1;
#endif
    d.draw_color = 1;
    u8g2_SetFont(&d, u8g2_font_10x20_t_cyrillic);
    u8g2_SetFontMode(&d, 1);
    u8g2_SetFontPosBaseline(&d);
    int advance = 0;
    for (auto glyph : glyphs) advance += u8g2_GetGlyphWidth(&d, glyph);
    assert(advance == 90); // Native 10px advance, no scaling/substitute font.
    int x = g.x + (g.width - advance) / 2;
    for (auto glyph : glyphs) x += u8g2_DrawGlyph(&d, x, g.y + 32, glyph);
}

void test(int width, int height) {
    const auto g = volumeOverlayGeometry(width, height);
    assert(g.height == 76 && g.barY == g.y + 44 && g.barHeight == 22);
    VolumeOverlaySnapshot state;
    state.valid = true;
    state.adjustedAt = 100;
    std::strcpy(state.muteLabel, "Без звуку");
    const auto original = state;
    Image full(width, height), rows(width, height), ref(width, height, 0), clipped(width, height, 0);
    drawVolumeOverlay(full, state, width, height, 100);
    reference(ref, g, false);
    reference(clipped, g, true);
    for (int y = g.y; y < g.y + g.height; ++y) {
        // Guard both ends of each bounded scanline, as in presentation scratch.
        std::vector<uint16_t> scratch(g.width + 2, 0x4321);
        VolumeOverlayRow row{scratch.data() + 1, g.x, y, g.width};
        drawVolumeOverlay(row, state, width, height, 100);
        assert(scratch.front() == 0x4321 && scratch.back() == 0x4321);
        for (int x = 0; x < g.width; ++x) rows.pixels[y * width + g.x + x] = scratch[x + 1];
    }
    assert(full.pixels == rows.pixels);
    int minY = height, maxY = 0, lost = 0, tails[2] = {};
    const int textX = g.x + (g.width - 90) / 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int index = y * width + x;
            if (ref.pixels[index]) {
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
                if (!clipped.pixels[index]) ++lost;
                if (y >= g.y + 32) {
                    if (x >= textX + 60 && x < textX + 70) ++tails[0];
                    if (x >= textX + 80 && x < textX + 90) ++tails[1];
                }
            }
            if (x >= g.x + 2 && x < g.x + g.width - 2 && y >= g.y + 2 && y < g.barY)
                assert(full.pixels[index] == ref.pixels[index]);
            if (x < g.x || x >= g.x + g.width || y < g.y || y >= g.y + g.height)
                assert(full.pixels[index] == 0x1234); // Source-independent composition.
        }
    assert(tails[0] > 0 && tails[1] > 0 && lost == tails[0] + tails[1]);
    assert(maxY >= g.y + 32 && maxY < g.barY);
    assert(std::memcmp(&state, &original, sizeof(state)) == 0);
    Image expired(width, height);
    drawVolumeOverlay(expired, state, width, height, 1300);
    assert(expired.pixels == Image(width, height).pixels);
    printf(
        "%dx%d: glyph y offsets [%d,%d], old exclusive bottom 32 lost %d pixels; "
        "у tails %d/%d; full/scanline/reference PASS\n",
        width,
        height,
        minY - g.y,
        maxY - g.y,
        lost,
        tails[0],
        tails[1]
    );
}
int main() {
    test(280, 240);
    test(240, 280);
    test(128, 128);
}
