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
    bool brightness = false;
    /// Owned UTF-8 BMP label (31 bytes + terminator). Standalone default.
    /// Label changes do not renew the feedback timeout.
    char muteLabel[32] = "Mute";
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
    decoder.user_x0 = g.x + 2;
    decoder.user_x1 = g.x + g.width - 2;
    decoder.user_y0 = g.y + 12;
    // The baseline is not the bottom: native Cyrillic glyphs have descenders.
    // Keep the text window above the unchanged bar, including those lower rows.
    decoder.user_y1 = g.barY;
}

inline void volumeOverlayFontWindow(u8g2_t& decoder, const VolumeOverlayRow& row, const VolumeOverlayGeometry& g) {
    volumeOverlayFontWindow<VolumeOverlayRow>(decoder, row, g);
    decoder.user_y0 = row.y;
    decoder.user_y1 = row.y + 1;
}

/// Bounded UTF-8 decoder for the font's BMP glyph API. Invalid, truncated,
/// overlong and non-BMP sequences stop decoding; no reads past the snapshot.
inline uint16_t volumeOverlayNextGlyph(const char* text, size_t capacity, size_t& offset) {
    if (offset >= capacity) return 0;
    const uint8_t first = static_cast<uint8_t>(text[offset++]);
    if (first < 0x80) return first;
    const int extra = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : 0;
    if (!extra || offset + extra > capacity) return 0;
    uint16_t glyph = first & (extra == 1 ? 0x1f : 0x0f);
    for (int i = 0; i < extra; ++i) {
        const uint8_t next = static_cast<uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80) return 0;
        glyph = (glyph << 6) | (next & 0x3f);
    }
    if ((extra == 2 && glyph < 0x800) || (glyph >= 0xd800 && glyph <= 0xdfff)) return 0;
    return glyph;
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
    if (level == 100) text[count++] = '1';
    if (level >= 10) text[count++] = '0' + (level / 10) % 10;
    text[count++] = '0' + level % 10;
    text[count++] = '%';
    const char* label = text;
    const size_t capacity = sizeof(text);
    VolumeOverlayFontTarget<Target> context{};
    context.target = &target;
    auto& decoder = context.decoder;
    static const u8g2_cb_t callbacks = {nullptr, nullptr, VolumeOverlayFontTarget<Target>::line};
    decoder.cb = &callbacks;
    volumeOverlayFontWindow(decoder, target, g);
    // Only the text band needs font decoding during scanline presentation.
    if (decoder.user_y1 <= g.y + 12 || decoder.user_y0 >= g.barY) return;
#ifdef U8G2_WITH_CLIP_WINDOW_SUPPORT
    decoder.is_page_clip_window_intersection = 1;
#endif
    decoder.draw_color = 1;
    u8g2_SetFont(&decoder, u8g2_font_10x20_t_cyrillic);
    u8g2_SetFontMode(&decoder, 1);
    u8g2_SetFontPosBaseline(&decoder);
    int advance = 0;
    size_t offset = 0;
    uint16_t glyph;
    while ((glyph = volumeOverlayNextGlyph(label, capacity, offset)))
        advance += u8g2_GetGlyphWidth(&decoder, glyph);
    // Centre a compact icon + numeric percentage, with no language-dependent words.
    const int iconX = g.x + (g.width - advance - 26) / 2;
    const int iconY = g.y + 17;
    if (state.brightness) {
        for (int y = -4; y <= 4; ++y)
            for (int x = -4; x <= 4; ++x) {
                const int distance = x * x + y * y;
                if (distance >= 9 && distance <= 16) target.fillRect(iconX + 9 + x, iconY + 9 + y, 1, 1, white);
            }
        target.fillRect(iconX + 8, iconY, 2, 3, white);
        target.fillRect(iconX + 8, iconY + 16, 2, 3, white);
        target.fillRect(iconX, iconY + 8, 3, 2, white);
        target.fillRect(iconX + 16, iconY + 8, 3, 2, white);
        for (int x = 2; x <= 14; x += 12)
            for (int y = 2; y <= 14; y += 12) target.fillRect(iconX + x, iconY + y, 2, 2, white);
    } else {
        target.fillRect(iconX, iconY + 6, 4, 6, white);
        target.fillRect(iconX + 4, iconY + 4, 2, 10, white);
        target.fillRect(iconX + 6, iconY + 2, 2, 14, white);
        if (level) {
            target.fillRect(iconX + 11, iconY + 5, 2, 8, white);
            target.fillRect(iconX + 15, iconY + 3, 2, 12, white);
        } else {
            for (int i = 0; i < 16; ++i) target.fillRect(iconX + i, iconY + i, 1, 2, white);
        }
    }
    int x = iconX + 26;
    offset = 0;
    while ((glyph = volumeOverlayNextGlyph(label, capacity, offset)))
        x += u8g2_DrawGlyph(&decoder, x, g.y + 32, glyph);
}

} // namespace lilka
