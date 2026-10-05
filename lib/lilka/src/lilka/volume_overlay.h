#pragma once

#include <stdint.h>

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

/// Draw on an off-screen presentation target, never an application's source canvas.
/// Uses existing rectangle primitives, no allocation or changes to font/cursor state.
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
    // Compact 3x5 font at 4x scale: legible 12x20 glyphs. Alphabet: 0..9,%,M,U,T,E.
    static const uint16_t glyphs[] = {
        0x7b6f,
        0x2492,
        0x73e7,
        0x73cf,
        0x5bc9,
        0x79cf,
        0x79ef,
        0x7249,
        0x7bef,
        0x7bcf,
        0x52a5,
        0x5fed,
        0x5b6f,
        0x7492,
        0x79e7
    };
    int text[4], count = 0;
    if (!level) {
        text[0] = 11;
        text[1] = 12;
        text[2] = 13;
        text[3] = 14;
        count = 4;
    } else {
        if (level == 100) text[count++] = 1;
        if (level >= 10) text[count++] = (level / 10) % 10;
        text[count++] = level % 10;
        text[count++] = 10;
    }
    const int startX = g.x + (g.width - (count * 16 - 4)) / 2;
    for (int i = 0; i < count; ++i) {
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (glyphs[text[i]] & (1 << (14 - row * 3 - col))) {
                    target.fillRect(startX + i * 16 + col * 4, g.y + 12 + row * 4, 4, 4, white);
                }
            }
        }
    }
}

} // namespace lilka
