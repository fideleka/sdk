#pragma once

#include "config.h"
#include <Arduino_GFX_Library.h>

#if LILKA_VERSION == 2 && CONFIG_IDF_TARGET_ESP32S3
namespace lilka {

// Extend the existing Arduino bus rather than initialize a competing IDF bus.
// beginWrite/endWrite still hold SPI1's transaction mutex and LCD CS throughout
// every DMA transfer, so Arduino SD transactions cannot change the registers.
class PixelDMABus : public Arduino_HWSPI {
public:
    PixelDMABus();
    bool enablePixelDMA();
    void beginWrite() override;
    void endWrite() override;
    void writePixels(uint16_t* pixels, uint32_t count) override;

private:
    struct State;
    State* state = nullptr;
    bool attempted = false;
    void* writer = nullptr;
};

} // namespace lilka
#endif
