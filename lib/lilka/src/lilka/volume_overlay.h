#pragma once

#include <stdint.h>
#include <stddef.h>
#include <type_traits>
#include <clib/u8g2.h>

namespace lilka {

/// Immutable RAM snapshot. Unsigned elapsed time also handles millis() wrap.
struct VolumeOverlaySnapshot {
    int level = 0;
    uint32_t adjustedAt = 0;
    bool valid = false;
    bool visible(uint32_t now) const {
        return valid && now - adjustedAt < 1200;
    }
};

struct VolumeOverlayGeometry {
    int x, y, width, height, barX, barY, barWidth, barHeight;
};

/// Centered, rotation-independent panel; too-small targets are omitted.
inline VolumeOverlayGeometry volumeOverlayGeometry(int width, int height) {
    if (width < 96 || height < 80) return {0, 0, 0, 0, 0, 0, 0, 0};
    const int w = width * 3 / 4;
    const int h = 76;
    const int x = (width - w) / 2, y = (height - h) / 2;
    return {x, y, w, h, x + 10, y + 44, w - 20, 22};
}

/// Bounded off-screen scanline target for the same overlay renderer. Rectangle
/// primitives only touch RAM. Reused for each row, never writes to the LCD.
struct VolumeOverlayRow {
    uint16_t* pixels;
    int x, y, width;
    void fillRect(int left, int top, int w, int h, uint16_t color) {
        if (y < top || y >= top + h) return;
        const int first = left > x ? left : x;
        const int end = left + w < x + width ? left + w : x + width;
        for (int i = first; i < end; ++i)
            pixels[i - x] = color;
    }
    void drawRect(int left, int top, int w, int h, uint16_t color) {
        fillRect(left, top, w, 1, color);
        fillRect(left, top + h - 1, w, 1, color);
        fillRect(left, top, 1, h, color);
        fillRect(left + w - 1, top, 1, h, color);
    }
};

/// Private decoder and RAM-only callback, independent of application GFX state.
/// The first-member cast avoids requiring U8X8_WITH_USER_PTR on embedded builds.
template <typename Target>
struct VolumeOverlayFontTarget {
    u8g2_t decoder;
    Target* target;
    static void line(u8g2_t* decoder, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t length, uint8_t direction) {
        using Context = VolumeOverlayFontTarget<Target>;
        static_assert(
            std::is_standard_layout<Context>::value && offsetof(Context, decoder) == 0, "Decoder must be first"
        );
        auto* context = reinterpret_cast<Context*>(decoder);
        context->target->fillRect(x, y, direction ? 1 : length, direction ? length : 1, 0xffff);
    }
};

template <typename Target>
void volumeOverlayFontWindow(u8g2_t& decoder, const Target&, const VolumeOverlayGeometry& g) {
    decoder.user_x0 = g.x;
    decoder.user_x1 = g.x + g.width;
    decoder.user_y0 = g.y + 12;
    decoder.user_y1 = g.y + 32;
}

inline void volumeOverlayFontWindow(u8g2_t& decoder, const VolumeOverlayRow& row, const VolumeOverlayGeometry& g) {
    volumeOverlayFontWindow<VolumeOverlayRow>(decoder, row, g);
    decoder.user_y0 = row.y;
    decoder.user_y1 = row.y + 1;
}

/// Draw on an off-screen presentation target, never an application's source canvas.
/// Uses the regular SDK FONT_10x20 asset unscaled; no heap or font/cursor mutations.
template <typename Target>
void drawVolumeOverlay(Target& target, const VolumeOverlaySnapshot& state, int width, int height, uint32_t now) {
    if (!state.visible(now)) return;
    const auto g = volumeOverlayGeometry(width, height);
    if (!g.width) return;
    constexpr uint16_t black = 0x0000, white = 0xffff, cyan = 0x07ff;
    target.fillRect(g.x, g.y, g.width, g.height, black);
    target.drawRect(g.x, g.y, g.width, g.height, white);
    target.drawRect(g.x + 1, g.y + 1, g.width - 2, g.height - 2, white);
    target.drawRect(g.barX, g.barY, g.barWidth, g.barHeight, white);
    const int level = state.level < 0 ? 0 : (state.level > 100 ? 100 : state.level);
    const int filled = (g.barWidth - 4) * level / 100;
    if (filled) target.fillRect(g.barX + 2, g.barY + 2, filled, g.barHeight - 4, cyan);
    char text[5] = {};
    int count = 0;
    if (!level) {
        text[0] = 'M';
        text[1] = 'U';
        text[2] = 'T';
        text[3] = 'E';
        count = 4;
    } else {
        if (level == 100) text[count++] = '1';
        if (level >= 10) text[count++] = '0' + (level / 10) % 10;
        text[count++] = '0' + level % 10;
        text[count++] = '%';
    }
    VolumeOverlayFontTarget<Target> context{};
    context.target = &target;
    auto& decoder = context.decoder;
    static const u8g2_cb_t callbacks = {nullptr, nullptr, VolumeOverlayFontTarget<Target>::line};
    decoder.cb = &callbacks;
    volumeOverlayFontWindow(decoder, target, g);
    // Only the text band needs font decoding during scanline presentation.
    if (decoder.user_y1 <= g.y + 12 || decoder.user_y0 >= g.y + 32) return;
#ifdef U8G2_WITH_CLIP_WINDOW_SUPPORT
    decoder.is_page_clip_window_intersection = 1;
#endif
    decoder.draw_color = 1;
    u8g2_SetFont(&decoder, u8g2_font_10x20_t_cyrillic);
    u8g2_SetFontMode(&decoder, 1);
    u8g2_SetFontPosBaseline(&decoder);
    int advance = 0;
    for (int i = 0; i < count; ++i)
        advance += u8g2_GetGlyphWidth(&decoder, text[i]);
    int x = g.x + (g.width - advance) / 2;
    for (int i = 0; i < count; ++i)
        x += u8g2_DrawGlyph(&decoder, x, g.y + 32, text[i]);
}

} // namespace lilka
