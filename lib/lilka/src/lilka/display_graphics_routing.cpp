#include "display.h"

namespace lilka {
size_t Display::write(const uint8_t* buffer, size_t size) {
    if (!size) return 0;
    DisplayFrame completed(*this);
    // Keep Arduino_GFX's byte writer and UTF-8 decoder on this instance.
    // Nested drawChar transactions must not publish intermediate glyphs.
    return Print::write(buffer, size);
}

// Accelerated TFT methods may bypass writePixel. Mirror their completed result
// into RAM, then use the native optimized path without recursively mirroring it.
void Display::writeSlashLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->writeSlashLine(x0, y0, x1, y1, color);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::writeSlashLine(x0, y0, x1, y1, color);
        nativeBypass = previous;
    }
}
void Display::drawBitmap(
    int16_t x, int16_t y, const uint8_t* pixels, int16_t w, int16_t h, uint16_t color, uint16_t background
) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawBitmap(x, y, pixels, w, h, color, background);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::drawBitmap(x, y, pixels, w, h, color, background);
        nativeBypass = previous;
    }
}
void Display::drawBitmap(
    int16_t x, int16_t y, uint8_t* pixels, int16_t w, int16_t h, uint16_t color, uint16_t background
) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawBitmap(x, y, pixels, w, h, color, background);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::drawBitmap(x, y, pixels, w, h, color, background);
        nativeBypass = previous;
    }
}
void Display::drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t* pixels, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawGrayscaleBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::drawGrayscaleBitmap(x, y, pixels, w, h);
        nativeBypass = previous;
    }
}
void Display::drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t* pixels, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawGrayscaleBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::drawGrayscaleBitmap(x, y, pixels, w, h);
        nativeBypass = previous;
    }
}
void Display::drawIndexedBitmap(
    int16_t x, int16_t y, uint8_t* pixels, uint16_t* palette, int16_t w, int16_t h, int16_t skip
) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawIndexedBitmap(x, y, pixels, palette, w, h, skip);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::drawIndexedBitmap(x, y, pixels, palette, w, h, skip);
        nativeBypass = previous;
    }
}
void Display::drawIndexedBitmap(
    int16_t x, int16_t y, uint8_t* pixels, uint16_t* palette, uint8_t chroma, int16_t w, int16_t h, int16_t skip
) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->drawIndexedBitmap(x, y, pixels, palette, chroma, w, h, skip);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_GFX::drawIndexedBitmap(x, y, pixels, palette, chroma, w, h, skip);
        nativeBypass = previous;
    }
}
void Display::draw16bitRGBBitmapWithMask(int16_t x, int16_t y, uint16_t* pixels, uint8_t* mask, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->draw16bitRGBBitmapWithMask(x, y, pixels, mask, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::draw16bitRGBBitmapWithMask(x, y, pixels, mask, w, h);
        nativeBypass = previous;
    }
}
void Display::draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t* pixels, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->draw16bitBeRGBBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::draw16bitBeRGBBitmap(x, y, pixels, w, h);
        nativeBypass = previous;
    }
}
void Display::draw24bitRGBBitmap(int16_t x, int16_t y, const uint8_t* pixels, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->draw24bitRGBBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::draw24bitRGBBitmap(x, y, pixels, w, h);
        nativeBypass = previous;
    }
}
void Display::draw24bitRGBBitmap(int16_t x, int16_t y, uint8_t* pixels, int16_t w, int16_t h) {
    DisplayFrame completed(*this);
    if (auto* canvas = routingCanvas()) {
        canvas->draw24bitRGBBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) {
        const bool previous = nativeBypass;
        nativeBypass = true;
        Arduino_ST7789::draw24bitRGBBitmap(x, y, pixels, w, h);
        nativeBypass = previous;
    }
}
void Display::drawChar(int16_t x, int16_t y, unsigned char character, uint16_t color, uint16_t background) {
    DisplayFrame completed(*this);
    // U8g2's write() prepares glyph decoder state on THIS graphics instance.
    // Calling Canvas::drawChar directly uses its unprepared decoder pointer.
    // Decode once here; virtual pixel/fill spans already mirror to the canvas
    // and suppress native SPI when only the external display is active.
    Arduino_ST7789::drawChar(x, y, character, color, background);
}
} // namespace lilka
