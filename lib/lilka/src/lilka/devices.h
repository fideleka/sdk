#pragma once
#include "volume_overlay.h"
#include "frame_metrics.h"
#include "device_driver.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lilka/dynloader.h>
#include <driver/spi_master.h>
#include <atomic>

#ifndef LILKA_MEASURE_DISPLAY
#    define LILKA_MEASURE_DISPLAY 0
#endif

namespace lilka {
class Canvas;

/// SD-loaded display drivers, settings and shared frame routing.
class DeviceManager {
public:
    static constexpr unsigned MaxDevices = 4;
    enum Mode : uint8_t { Internal, External, Both };
    struct Info {
        char name[48], uuid[37], version[24], status[80];
        bool enabled, active;
        uint32_t settingCount;
    };
    struct SettingInfo {
        char label[48], choice[32];
        int32_t value, minimum, maximum, step;
        uint32_t kind;
        bool pin;
    };
    struct SettingsDraft {
        uint32_t count = 0, schema = 0;
        char uuid[37] = {};
        int32_t original[KD_MAX_SETTINGS] = {}, values[KD_MAX_SETTINGS] = {};
    };
    DeviceManager();
    ~DeviceManager();
    /// Discover once after SD/display initialization and start the bounded SPI worker.
    bool begin(bool skip = false);
    void discover(bool skip);
    /// Queue a mode change; no SPI, allocation or NVS on the input scan task.
    void requestCycle();
    /// Atomic availability check for the input task, safe during startup discovery.
    bool canCycleDisplays() const {
        return workerReady.load();
    }
    /// Set panel power without forgetting the selected route.
    void setSleeping(bool sleeping);
    bool isSleeping() const {
        return sleeping.load();
    }
    /// Allocation fallback: never hide native output without a frame source.
    void setPresentationReady(bool ready);
    bool hasRegisteredDriver() const;
    bool extensionPinOwned(int pin) const;
    unsigned count() const {
        return deviceCount;
    }
    bool info(unsigned index, Info& result);
    bool settingInfo(unsigned device, unsigned field, SettingInfo& result);
    /// Snapshot values for editing without changing hardware or persistent state.
    bool beginSettingsEdit(unsigned index, SettingsDraft& draft);
    /// Describe a draft value without applying it or checking cross-pin conflicts.
    bool previewSetting(unsigned index, unsigned field, const SettingsDraft& draft, SettingInfo& result);
    /// Validate the complete draft, then activate/persist once; reject stale drafts.
    bool applySettings(unsigned index, const SettingsDraft& draft);
    bool setEnabled(unsigned index, bool enabled);
    bool changeSetting(unsigned index, unsigned field, int direction);
    bool resetSettings(unsigned index);
    Mode displayMode() const {
        return mode.load();
    }
    bool setDisplayMode(Mode value);
    /// Successful mode-switch feedback; short frame lock, never waits for SPI.
    VolumeOverlaySnapshot getOverlay();
    bool externalOutput() const {
        return workerReady.load() && routed.load();
    }
    bool internalOutput() const {
        return !workerReady.load() || internal.load();
    }
    uint32_t invalidation() const {
        return generation.load();
    }
    // Compositor task only, holding layer locks. Never blocks on driver transfers.
    bool capture(lilka::Canvas* panel, lilka::Canvas* app, const lilka::VolumeOverlaySnapshot& overlay, bool sleeping);
    // Same compositor task, with no canvas locks. Both LCDs read shared pixels.
    bool presentInternal();

private:
    struct Slot {
        DeviceManager* owner = nullptr;
        kd_host host = {};
        lilka_elf_t elf = {};
        const kd_device* descriptor = nullptr;
        char driverUuid[37] = {}, uuid[37] = {}, name[48] = {}, status[80] = {};
        uint32_t schema = 0;
        int32_t values[KD_MAX_SETTINGS] = {};
        bool loaded = false, enabled = false, active = false, readOnly = false;
        void* state = nullptr;
    };
    Slot slots[MaxDevices];
    unsigned deviceCount = 0;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    std::atomic<Mode> mode{Internal};
    std::atomic<bool> internal{true};
    std::atomic<bool> routed{false};
    std::atomic<bool> workerReady{false};
    std::atomic<uint32_t> ownedPins{0};
    std::atomic<uint32_t> generation{1};
    // Lock order: driver mutex -> internalMutex -> frameMutex.
    // Compositor capture takes only frameMutex; external DMA holds only driver mutex.
    SemaphoreHandle_t internalMutex = xSemaphoreCreateMutex();
    SemaphoreHandle_t frameMutex = xSemaphoreCreateMutex();
    uint16_t* frames[2] = {};
    int externalReading = -1;
    int active = -1;
    uint16_t* frame = nullptr;
    size_t frameCapacity = 0;
    uint16_t width = 0, height = 0;
    bool pending = false, bootSkipped = false, frameValid = false;
    VolumeOverlaySnapshot modeFeedback; // guarded by frameMutex
    // Bit set means this buffer row is stale relative to the current frame.
    uint32_t staleRows[2][10] = {};
    uint32_t dirtyRows[10] = {}, internalDirtyRows[10] = {};
    uint32_t unchangedCaptures = 0, fullTransfers = 0, rowTransfers = 0, transferPixels = 0, spiHz = 0,
             spiRequestedHz = 0;
    int presentPending(Slot& slot, const uint16_t* pixels, uint16_t width, uint16_t height, const uint32_t* rows);
    DisplayFrameMetrics captureTiming;
#if LILKA_MEASURE_DISPLAY
    DisplayFrameMetrics packingTiming, dmaWaitTiming;
#endif
    uint32_t captureCount = 0, transferCount = 0, captureUs = 0, transferUs = 0;
    spi_device_handle_t spi = nullptr;
    uint8_t* dma = nullptr;
    Slot* spiOwner = nullptr;
    bool spiFault = false;
    spi_transaction_t transaction = {}, pixelTransaction = {};
    int transmit(uint32_t bits);
    int submit(spi_transaction_t& transfer, const uint8_t* bytes, uint32_t bits);
    int finish(const spi_transaction_t& transfer);
    static int32_t fail(Slot& slot, const char* operation, int32_t error);
    int backlightPin = -1;
    int dcPin = -1, resetPin = -1, clkPin = -1, mosiPin = -1;
    TaskHandle_t worker = nullptr;
    std::atomic<unsigned> cycleRequests{0};
    std::atomic<bool> sleeping{false};
    std::atomic<bool> presentationReady{true};
    void run();
    static void workerEntry(void* context);
    bool load(Slot& slot, const char* path);
    bool restore(Slot& slot);
    bool persist(Slot& slot);
    bool applySettingsLocked(unsigned index, const int32_t* values);
    bool activate(unsigned index);
    void deactivate();
    bool route();
    static int32_t registerDevice(void* context, const kd_device* descriptor);
    static int32_t setting(void* context, uint32_t id);
    static int32_t spiOpen(void* context, int32_t clk, int32_t mosi, int32_t dc, int32_t reset, uint32_t hz);
    static int32_t gpioWrite(void* context, int32_t pin, int32_t level);
    static int32_t spiWrite(void* context, int32_t data, const uint8_t* bytes, uint32_t size);
    static int32_t spiPixels(void* context, const uint16_t* pixels, uint32_t count);
    static void spiClose(void* context);
    static void delayMs(uint32_t ms);
};

/// Shared device registry used by every SDK-based firmware.
extern DeviceManager devices;
} // namespace lilka
