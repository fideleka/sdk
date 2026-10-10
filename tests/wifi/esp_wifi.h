#pragma once
inline int esp_wifi_scan_stop() {
    return 0;
}

constexpr int WIFI_STORAGE_RAM = 0, ESP_OK = 0;
inline int esp_wifi_set_storage(int storage) {
    assert(storage == WIFI_STORAGE_RAM);
    return ESP_OK;
}
