#include "wifi_scan.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <algorithm>

namespace lilka {
namespace detail {
namespace {
enum class Stage { Idle, Running, Ready };
std::atomic<Stage> stage{Stage::Idle};
std::atomic<bool> discarded{false};
portMUX_TYPE scanMux = portMUX_INITIALIZER_UNLOCKED;
wifi_ap_record_t* result = nullptr;
int resultCount = -2;

void collect(void*) {
    // IDF 4.4 still posts SCAN_DONE for a blocking scan. Arduino's handler
    // consumes the driver list, so it must be the sole result collector.
    // Borrow its contiguous records instead of retrieving/copying them again.
    resultCount = -2;
    if (!discarded.load()) {
        const int count = WiFi.scanNetworks(false, false, false, 120);
        if (count >= 0 && !discarded.load()) {
            resultCount = std::min(count, int(BoundedWiFiScan::Capacity));
            result = static_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(0));
            if (resultCount && !result) resultCount = -2;
        }
    }
    if (resultCount < 0 && !discarded.load()) WiFi.scanDelete();
    bool dispose = false;
    portENTER_CRITICAL(&scanMux);
    if (discarded.load()) {
        dispose = true;
        result = nullptr;
    } else {
        stage.store(Stage::Ready); // Publish after all driver access ends.
    }
    portEXIT_CRITICAL(&scanMux);
    if (dispose) {
        WiFi.scanDelete();
        stage.store(Stage::Idle); // Do not let another scan start before cleanup.
    }
    vTaskDelete(nullptr);
}
} // namespace

bool BoundedWiFiScan::start() {
    if (stage.load() == Stage::Running || WiFi.scanComplete() == WIFI_SCAN_RUNNING) return false;
    release();
    discarded.store(false);
    stage.store(Stage::Running);
    if (xTaskCreate(collect, "wifiScan", 3072, nullptr, 1, nullptr) != pdPASS) {
        stage.store(Stage::Ready);
        release();
        return false;
    }
    return true;
}

bool BoundedWiFiScan::running() {
    return stage.load() == Stage::Running;
}

int BoundedWiFiScan::count() {
    if (stage.load() == Stage::Running) return -1;
    return stage.load() == Stage::Ready && !discarded.load() ? resultCount : -2;
}

const wifi_ap_record_t* BoundedWiFiScan::records() {
    return stage.load() == Stage::Ready && !discarded.load() ? result : nullptr;
}

void BoundedWiFiScan::release() {
    bool dispose = false;
    portENTER_CRITICAL(&scanMux);
    discarded.store(true);
    if (stage.load() != Stage::Running) {
        dispose = result != nullptr;
        result = nullptr;
        resultCount = -2;
        stage.store(Stage::Idle);
    }
    portEXIT_CRITICAL(&scanMux);
    if (dispose) WiFi.scanDelete();
}

} // namespace detail
} // namespace lilka
