#include "wifi_scan.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <new>

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
    wifi_scan_config_t config{};
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 0;
    config.scan_time.active.max = 120;
    // Only blocking scans suppress SCAN_DONE. Stopping an async scan can still
    // dispatch that event, so cancellation drains this bounded-duration scan.
    uint16_t count = BoundedWiFiScan::Capacity;
    resultCount = -2;
    if (!discarded.load() && esp_wifi_scan_start(&config, true) == ESP_OK && !discarded.load() &&
        esp_wifi_scan_get_ap_records(&count, result) == ESP_OK) {
        resultCount = count;
    }
    esp_wifi_clear_ap_list(); // Also release the driver list on retrieval/error/cancel.
    wifi_ap_record_t* dispose = nullptr;
    portENTER_CRITICAL(&scanMux);
    if (discarded.load()) {
        dispose = result;
        result = nullptr;
        stage.store(Stage::Idle);
    } else {
        stage.store(Stage::Ready); // Publish after all driver access ends.
    }
    portEXIT_CRITICAL(&scanMux);
    delete[] dispose;
    vTaskDelete(nullptr);
}
} // namespace

bool BoundedWiFiScan::start() {
    if (stage.load() == Stage::Running || WiFi.scanComplete() == WIFI_SCAN_RUNNING) return false;
    release();
    result = new (std::nothrow) wifi_ap_record_t[Capacity];
    if (!result) return false;
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
    wifi_ap_record_t* dispose = nullptr;
    portENTER_CRITICAL(&scanMux);
    discarded.store(true);
    if (stage.load() != Stage::Running) {
        dispose = result;
        result = nullptr;
        resultCount = -2;
        stage.store(Stage::Idle);
    }
    portEXIT_CRITICAL(&scanMux);
    delete[] dispose;
}

} // namespace detail
} // namespace lilka
