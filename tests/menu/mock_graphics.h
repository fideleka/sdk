#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using String = std::string;
constexpr double PI = 3.141592653589793;
constexpr int portTICK_PERIOD_MS = 1;
constexpr int LILKA_UI_UPDATE_DELAY_MS = 10;
constexpr uint16_t menu_icon_width = 24;
constexpr uint16_t menu_icon_height = 24;
using menu_icon_t = const uint16_t[menu_icon_width * menu_icon_height];
inline uint64_t hostTime = 1000;
inline uint64_t millis() {
    return hostTime;
}
inline void vTaskDelay(int) {
}
inline constexpr int FONT_6x13 = 6;
inline constexpr int FONT_10x20 = 10;
inline uint16_t getTextWidth(int font, const char* text) {
    return std::strlen(text) * font;
}

struct TextDraw {
    String text;
    uint16_t color;
    int x;
    int y;
    int font;
    int size;
    int boundX;
    int boundY;
    int boundWidth;
    int boundHeight;
};
struct Transform {
    Transform rotate(double) {
        return *this;
    }
};
struct Image {
    std::vector<uint16_t> storage;
    uint16_t* pixels;
    Image(int width, int height, uint16_t color, int, int) : storage(width * height, color), pixels(storage.data()) {
    }
};
// Recording GFX: fill and bitmap drawing must not reset the foreground.
// Marquee framebuffer transfers propagate the recorded text to its parent canvas.
class Arduino_GFX {
public:
    explicit Arduino_GFX(int width = 240, int height = 240) : w(width), h(height) {
    }
    int width() const {
        return w;
    }
    int height() const {
        return h;
    }
    void fillScreen(uint16_t) {
        draws.clear();
    }
    void fillTriangle(int, int, int, int, int, int, uint16_t) {
    }
    void fillRect(int, int, int, int, uint16_t) {
    }
    void setFont(int value) {
        font = value;
    }
    void setTextSize(int value) {
        size = value;
    }
    void setCursor(int x, int y) {
        cursorX = x;
        cursorY = y;
    }
    void setTextColor(uint16_t value) {
        foreground = value;
    }
    void setTextBound(int x, int y, int width, int height) {
        boundX = x;
        boundY = y;
        boundWidth = width;
        boundHeight = height;
    }
    void println(const String& text) {
        draws.push_back({text, foreground, cursorX, cursorY, font, size, boundX, boundY, boundWidth, boundHeight});
    }
    void getTextBounds(const String& text, int x, int y, int16_t* x1, int16_t* y1, uint16_t* width, uint16_t* height) {
        *x1 = x;
        *y1 = y;
        *width = text.length() * font * size;
        *height = 20;
    }
    void draw16bitRGBBitmapWithTranColor(int x, int y, uint16_t* pixels, uint16_t, int, int);
    std::vector<TextDraw> draws;

private:
    int w;
    int h;
    uint16_t foreground = 0x1234;
    int cursorX = 0;
    int cursorY = 0;
    int font = 0;
    int size = 1;
    int boundX = 0;
    int boundY = 0;
    int boundWidth = 0;
    int boundHeight = 0;
};
namespace lilka {
namespace colors {
constexpr uint16_t White = 0xffff;
constexpr uint16_t Black = 0;
constexpr uint16_t Blue = 0x001f;
constexpr uint16_t Yellow = 0xffe0;
constexpr uint16_t Orange_red = 0xfa20;
} // namespace colors
class Canvas : public Arduino_GFX {
public:
    Canvas(int width, int height) : Arduino_GFX(width, height) {
        live.push_back(this);
    }
    ~Canvas() {
        live.erase(std::find(live.begin(), live.end(), this));
    }
    uint16_t* getFramebuffer() {
        return &framebufferToken;
    }
    void drawImageTransformed(Image*, int, int, Transform) {
    }
    inline static std::vector<Canvas*> live;

private:
    uint16_t framebufferToken = 0;
};
enum Button { UP, DOWN, LEFT, RIGHT, A, COUNT };
struct ButtonState {
    bool justPressed = false;
};
struct State {
    ButtonState up, down, left, right, a;
};
using _StateButtons = ButtonState[5];
struct Controller {
    State state;
    State getState() {
        return state;
    }
};
inline Controller controller;
} // namespace lilka
inline void Arduino_GFX::draw16bitRGBBitmapWithTranColor(int x, int y, uint16_t* pixels, uint16_t, int, int) {
    for (auto* canvas : lilka::Canvas::live) {
        if (canvas->getFramebuffer() == pixels) {
            for (auto draw : canvas->draws) {
                draw.x += x;
                draw.y += y;
                draws.push_back(draw);
            }
        }
    }
}
