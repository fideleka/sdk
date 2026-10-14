#include "display.h"
#include "devices.h"
#include "display_settings.h"
#include "audio.h"
#include "brightness.h"
#include <algorithm>
#include <new>

namespace lilka {
bool Display::nativeOutput() const {
    return !routingStarted || nativeBypass ||
           (writeDepth ? nativeWrite
                       : devices.internalOutput() && !devices.isSleeping() && !displaySettings.isSleeping());
}
Canvas* Display::routingCanvas() {
    if (!routingStarted || nativeBypass || layeredPresentation || !devices.hasRegisteredDriver()) return nullptr;
    if (routedCanvas && (routedCanvas->width() != width() || routedCanvas->height() != height())) {
        delete routedCanvas;
        routedCanvas = nullptr;
    }
    if (!routedCanvas && !routingAllocationFailed) {
        routedCanvas = new (std::nothrow) Canvas(width(), height(), Canvas::Memory::PSRAMOnly);
        if (routedCanvas && !routedCanvas->getFramebuffer()) {
            delete routedCanvas;
            routedCanvas = nullptr;
        }
        if (routedCanvas) routedCanvas->fillScreen(colors::Black);
        else {
            routingAllocationFailed = true;
            devices.setPresentationReady(false);
        }
    }
    return routedCanvas;
}
bool Display::enableRouting() {
    routingStarted = true;
    routingAllocationFailed = false;
    const bool ready = !devices.hasRegisteredDriver() || routingCanvas();
    devices.setPresentationReady(ready);
    return ready;
}
void Display::setLayeredPresentation(bool enabled) {
    layeredPresentation = enabled;
    if (enabled) {
        delete routedCanvas;
        routedCanvas = nullptr;
        devices.setPresentationReady(true);
    }
}
void Display::displayOn() {
    Arduino_ST7789::displayOn();
    nativePanelOn = true;
}
void Display::displayOff() {
    Arduino_ST7789::displayOff();
    nativePanelOn = false;
}
void Display::serviceOutputPower() {
    if (!routingStarted || writeDepth) return;
    const bool powered = devices.internalOutput() && !devices.isSleeping();
    if (powered != nativePanelOn) {
        if (powered) displayOn();
        else displayOff();
    }
}
void Display::beginFrame() {
    if (!frameDepth) {
        if (!layeredPresentation) displaySettings.serviceIdle(true);
        serviceOutputPower();
    }
    ++frameDepth;
}
void Display::endFrame() {
    if (frameDepth && --frameDepth == 0) publishRouting();
}
void Display::publishRouting() {
    serviceOutputPower();
    if (nativeBypass || layeredPresentation || !routedCanvas) return;
    const auto generation = devices.invalidation();
    if (generation != routeGeneration) {
        routingDirty = true;
        // Mode switches must restore the retained frame on the LCD owner, not the SPI worker.
        if (devices.internalOutput() && !devices.isSleeping() && !displaySettings.isSleeping())
            presentNativePixels(0, 0, routedCanvas->getFramebuffer(), width(), height());
        routeGeneration = generation;
    }
    if (routingDirty && devices.externalOutput()) {
        auto overlay = audio.getVolumeOverlay();
        const auto light = brightness.getOverlay();
        const uint32_t now = millis();
        if (light.visible(now) && (!overlay.visible(now) || now - light.adjustedAt < now - overlay.adjustedAt))
            overlay = light;
        const auto selection = devices.getOverlay();
        if (selection.visible(now) && (!overlay.visible(now) || now - selection.adjustedAt < now - overlay.adjustedAt))
            overlay = selection;
        if (devices.capture(nullptr, routedCanvas, overlay, displaySettings.isSleeping())) routingDirty = false;
    }
}
void Display::startWrite() {
    if (!writeDepth) nativeWrite = nativeOutput();
    ++writeDepth;
    if (nativeOutput()) Arduino_ST7789::startWrite();
}
void Display::endWrite() {
    if (nativeOutput()) Arduino_ST7789::endWrite();
    if (writeDepth) --writeDepth;
    if (!writeDepth && !frameDepth) publishRouting();
}
void Display::writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    windowX = x;
    windowY = y;
    windowWidth = w;
    windowHeight = h;
    windowOffset = 0;
    if (nativeOutput()) Arduino_ST7789::writeAddrWindow(x, y, w, h);
}
void Display::writePixels(uint16_t* pixels, uint32_t count) {
    if (auto* canvas = routingCanvas()) {
        const uint32_t available = uint32_t(windowWidth) * windowHeight;
        uint32_t offset = 0;
        while (windowWidth && windowOffset < available && offset < count) {
            const uint32_t column = windowOffset % windowWidth;
            const uint32_t length = std::min<uint32_t>(windowWidth - column, count - offset);
            canvas->draw16bitRGBBitmap(
                windowX + column, windowY + windowOffset / windowWidth, pixels + offset, length, 1
            );
            windowOffset += length;
            offset += length;
        }
        routingDirty = true;
    }
    if (nativeOutput()) Arduino_ST7789::writePixels(pixels, count);
}
void Display::writePixelPreclipped(int16_t x, int16_t y, uint16_t color) {
    if (auto* canvas = routingCanvas()) {
        canvas->drawPixel(x, y, color);
        routingDirty = true;
    }
    if (nativeOutput()) Arduino_ST7789::writePixelPreclipped(x, y, color);
}
void Display::writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (auto* canvas = routingCanvas()) {
        canvas->fillRect(x, y, w, h, color);
        routingDirty = true;
    }
    if (nativeOutput()) Arduino_ST7789::writeFillRectPreclipped(x, y, w, h, color);
}
void Display::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    Arduino_ST7789::writeFastHLine(x, y, w, color);
}
void Display::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    Arduino_ST7789::writeFastVLine(x, y, h, color);
}
void Display::draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t* pixels, int16_t w, int16_t h) {
    draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(pixels), w, h);
}
void Display::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* pixels, int16_t w, int16_t h) {
    beginFrame();
    if (auto* canvas = routingCanvas()) {
        canvas->draw16bitRGBBitmap(x, y, pixels, w, h);
        routingDirty = true;
    }
    if (nativeOutput()) Arduino_ST7789::draw16bitRGBBitmap(x, y, pixels, w, h);
    endFrame();
}
void Display::presentNativePixels(int16_t x, int16_t y, uint16_t* pixels, int16_t w, int16_t h) {
    nativeBypass = true;
    Arduino_ST7789::draw16bitRGBBitmap(x, y, pixels, w, h);
    nativeBypass = false;
}

} // namespace lilka
