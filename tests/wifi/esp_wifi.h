#pragma once
#include <cstdint>
#include <cassert>
struct wifi_ap_record_t {
    uint8_t ssid[33]{};
    int8_t rssi = 0;
};
inline int esp_wifi_scan_stop() {
    return 0;
}

constexpr int WIFI_STORAGE_RAM = 0, ESP_OK = 0;
inline int esp_wifi_set_storage(int storage) {
    assert(storage == WIFI_STORAGE_RAM);
    return ESP_OK;
}
