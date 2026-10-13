#include "pixel_dma.h"

#if LILKA_VERSION == 2 && CONFIG_IDF_TARGET_ESP32S3
#    include "rgb565_pack.h"
#    include "spi.h"
#    include "serial.h"
#    include <esp_heap_caps.h>
#    include <esp_intr_alloc.h>
#    include <esp_system.h>
#    include <esp_private/gdma.h>
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>
#    include <soc/spi_struct.h>
#    include <soc/lldesc.h>
#    include <soc/interrupts.h>
#    include <new>

namespace lilka {

struct PixelDMABus::State {
    static constexpr size_t ChunkBytes = 4092; // one DMA descriptor's 12-bit size
    uint8_t buffers[2][ChunkBytes] = {};
    lldesc_t descriptor = {};
    gdma_channel_handle_t channel = nullptr;
    intr_handle_t interrupt = nullptr;
    SemaphoreHandle_t done = nullptr;

    static void completion(void* context) {
        auto& self = *static_cast<State*>(context);
        if (!GPSPI3.dma_int_raw.trans_done) return;
        GPSPI3.dma_int_ena.trans_done = 0;
        GPSPI3.dma_int_clr.trans_done = 1;
        BaseType_t wake = pdFALSE;
        xSemaphoreGiveFromISR(self.done, &wake);
        if (wake) portYIELD_FROM_ISR();
    }

    // A failed/uncertain hardware transfer must never return and release the
    // shared SD mutex or reuse DMA memory. Reboot without unlocking that bus.
    [[noreturn]] static void fault() {
        serial.err("Native LCD DMA failed; restarting before releasing shared SPI");
        esp_restart();
        for (;;)
            vTaskDelay(portMAX_DELAY);
    }

    void start(unsigned buffer, size_t bytes) {
        if (gdma_reset(channel) != ESP_OK) fault();
        descriptor = {};
        descriptor.buf = buffers[buffer];
        descriptor.length = bytes;
        descriptor.size = (bytes + 3) & ~size_t(3);
        descriptor.owner = 1;
        descriptor.eof = 1;
        GPSPI3.dma_conf.dma_afifo_rst = 1;
        GPSPI3.dma_conf.dma_afifo_rst = 0;
        GPSPI3.ms_dlen.ms_data_bitlen = bytes * 8 - 1;
        GPSPI3.dma_int_clr.trans_done = 1;
        GPSPI3.dma_int_ena.trans_done = 1;
        if (gdma_start(channel, reinterpret_cast<intptr_t>(&descriptor)) != ESP_OK) fault();
        GPSPI3.cmd.update = 1;
        while (GPSPI3.cmd.update) {
        } // register latch only, not wire completion
        GPSPI3.cmd.usr = 1;
    }

    void wait() {
        // SPI transaction-done, NOT GDMA EOF (FIFO may still be shifting).
        if (xSemaphoreTake(done, pdMS_TO_TICKS(100)) != pdTRUE) fault();
        if (GPSPI3.cmd.usr || gdma_stop(channel) != ESP_OK) fault();
    }
};

PixelDMABus::PixelDMABus() :
    Arduino_HWSPI(LILKA_DISPLAY_DC, LILKA_DISPLAY_CS, LILKA_SPI_SCK, LILKA_SPI_MOSI, LILKA_SPI_MISO, &SPI1, true) {
}

bool PixelDMABus::enablePixelDMA() {
    if (attempted) return state != nullptr;
    attempted = true; // called during startup, before display/SD worker tasks
    void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!memory) return false;
    auto* candidate = new (memory) State;
    candidate->done = xSemaphoreCreateBinary();
    gdma_channel_alloc_config_t config = {};
    config.direction = GDMA_CHANNEL_DIRECTION_TX;
    bool connected = false;
    if (candidate->done && gdma_new_channel(&config, &candidate->channel) == ESP_OK) {
        connected = gdma_connect(candidate->channel, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_SPI, 3)) == ESP_OK;
        if (connected &&
            esp_intr_alloc(ETS_SPI3_INTR_SOURCE, 0, State::completion, candidate, &candidate->interrupt) == ESP_OK) {
            state = candidate;
            return true;
        }
    }
    if (connected) gdma_disconnect(candidate->channel);
    if (candidate->channel) gdma_del_channel(candidate->channel);
    if (candidate->done) vSemaphoreDelete(candidate->done);
    candidate->~State();
    heap_caps_free(memory);
    return false; // allocation/setup failure retains the original polling bus
}

void PixelDMABus::beginWrite() {
    Arduino_HWSPI::beginWrite();
    writer = xTaskGetCurrentTaskHandle();
}

void PixelDMABus::endWrite() {
    writer = nullptr;
    Arduino_HWSPI::endWrite();
}

void PixelDMABus::writePixels(uint16_t* pixels, uint32_t count) {
    if (!state || count < 64 || writer != xTaskGetCurrentTaskHandle()) {
        Arduino_HWSPI::writePixels(pixels, count);
        return;
    }
    // Caller has acquired the normal Arduino transaction mutex/CS. Preserve
    // all changed registers so subsequent command bytes and SD use their
    // original CPU FIFO path; never register a second driver on SPI3.
    const uint32_t dmaConfig = GPSPI3.dma_conf.val;
    const uint32_t user = GPSPI3.user.val;
    const uint32_t bitLength = GPSPI3.ms_dlen.val;
    GPSPI3.dma_conf.dma_rx_ena = 0;
    GPSPI3.dma_conf.dma_tx_ena = 1;
    GPSPI3.user.usr_mosi_highpart = 0;
    GPSPI3.user.usr_miso = 0;
    GPSPI3.user.usr_mosi = 1;
    unsigned buffer = 0;
    bool pending = false;
    while (count) {
        const uint32_t length = count < State::ChunkBytes / 2 ? count : State::ChunkBytes / 2;
        packRGB565(state->buffers[buffer], pixels, length);
        if (pending) state->wait();
        state->start(buffer, length * 2);
        pending = true;
        buffer ^= 1;
        pixels += length;
        count -= length;
    }
    if (pending) state->wait();
    GPSPI3.dma_conf.val = dmaConfig;
    GPSPI3.user.val = user;
    GPSPI3.ms_dlen.val = bitLength;
}

} // namespace lilka
#endif
