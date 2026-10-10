"""Real scan adapter: Arduino completion ownership, borrowed results and drain."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'lib/lilka/src/lilka'
mock = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(int*) {}
inline void portEXIT_CRITICAL(int*) {}
constexpr int pdPASS = 1, ESP_OK = 0, WIFI_SCAN_RUNNING = -1, WIFI_SCAN_TYPE_ACTIVE = 0;
extern void (*worker)(void*);
extern void (*scanHook)();
extern void (*deleteHook)();
extern bool taskFail, driverFail, retrieveFail;
extern int scans, retrievals, clears, discovered;
struct wifi_ap_record_t { uint8_t ssid[33]; int8_t rssi; };
struct wifi_scan_config_t { int scan_type; struct { struct { unsigned min, max; } active; } scan_time; };
inline int xTaskCreate(void(*fn)(void*), const char*, unsigned stack, void*, int, void*) {
    assert(stack == 3072);
    if (taskFail) return 0;
    worker = fn;
    return pdPASS;
}
inline void vTaskDelete(void*) {}
struct WiFiHAL {
    int external = -2, count = 0;
    wifi_ap_record_t* results = nullptr;
    int scanComplete() { return external; }
    int scanNetworks(bool async, bool hidden, bool passive, uint32_t dwell) {
        assert(!async && !hidden && !passive && dwell == 120);
        ++scans;
        if (scanHook) scanHook();
        if (driverFail) return -2;
        // Model Arduino SCAN_DONE consuming the entire IDF list before the
        // blocking call returns, including scans initiated directly via IDF.
        count = retrieveFail ? 0 : discovered;
        if (count) {
            results = new wifi_ap_record_t[count]{};
            for (int i = 0; i < count; ++i) {
                std::strcpy(reinterpret_cast<char*>(results[i].ssid), "AP");
                results[i].rssi = -40;
            }
        }
        return count;
    }
    void* getScanInfoByIndex(int i) { return results && i < count ? results + i : nullptr; }
    void scanDelete() {
        if (deleteHook) deleteHook();
        delete[] results;
        results = nullptr;
        count = 0;
        ++clears;
    }
};
extern WiFiHAL WiFi;
inline int esp_wifi_scan_start(const wifi_scan_config_t*, bool block) {
    assert(block);
    WiFi.scanNetworks(false, false, false, 120);
    return driverFail ? -1 : ESP_OK;
}
inline int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t*) {
    ++retrievals;
    *count = 0; // Arduino has already consumed the driver list.
    return retrieveFail ? -1 : ESP_OK;
}
inline int esp_wifi_clear_ap_list() { ++clears; return ESP_OK; }
'''
test = r'''
#include "mock.h"
#include "wifi_scan.h"
void (*worker)(void*) = nullptr;
void (*scanHook)() = nullptr;
void (*deleteHook)() = nullptr;
bool taskFail = false, driverFail = false, retrieveFail = false;
int scans = 0, retrievals = 0, clears = 0, discovered = 200;
WiFiHAL WiFi;
using Scan = lilka::detail::BoundedWiFiScan;
void finish() { auto fn = worker; worker = nullptr; assert(fn); fn(nullptr); }
int main() {
    WiFi.external = WIFI_SCAN_RUNNING;
    assert(!Scan::start() && !worker);
    WiFi.external = -2;
    taskFail = true;
    assert(!Scan::start() && !Scan::running() && !Scan::records());
    taskFail = false;
    assert(Scan::start() && Scan::running() && !Scan::start());
    finish();
    assert(Scan::count() == 64 && Scan::records()[63].rssi == -40);
    assert(Scan::records() == WiFi.results && retrievals == 0);
    Scan::release();
    assert(!Scan::records() && Scan::count() == -2 && !WiFi.results);
    discovered = 3;
    assert(Scan::start());
    finish();
    assert(Scan::count() == 3 && Scan::records()[2].rssi == -40);
    Scan::release();
    assert(Scan::start());
    Scan::release(); // Cancellation before worker begins: no scan.
    assert(Scan::running() && !Scan::start());
    const int before = scans;
    deleteHook = [] { assert(Scan::running() && !Scan::start()); };
    finish();
    deleteHook = nullptr;
    assert(scans == before && !Scan::running() && !Scan::records());
    assert(Scan::start());
    scanHook = [] { Scan::release(); }; // Cancel while completion drains.
    deleteHook = [] { assert(Scan::running() && !Scan::start()); };
    finish();
    scanHook = nullptr;
    deleteHook = nullptr;
    assert(!Scan::running() && !Scan::records() && Scan::count() == -2 && !WiFi.results);
    driverFail = true;
    assert(Scan::start());
    finish();
    assert(Scan::count() == -2);
    Scan::release();
    driverFail = false;
    for (int empty = 0; empty < 2; ++empty) {
        retrieveFail = empty == 0;
        discovered = 0;
        assert(Scan::start());
        finish();
        assert(Scan::count() == 0 && !Scan::records());
        Scan::release();
    }
    assert(!WiFi.results && retrievals == 0);
}
'''
with tempfile.TemporaryDirectory(prefix='wifi-scan-completion-') as directory:
    tmp = Path(directory)
    (tmp / 'mock.h').write_text(mock)
    for name in ('wifi_scan.cpp', 'wifi_scan.h'):
        (tmp / name).write_text((source / name).read_text())
    (tmp / 'test.cpp').write_text(test)
    for name in ('WiFi.h', 'esp_wifi.h', 'freertos/FreeRTOS.h', 'freertos/task.h'):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "mock.h"\n')
    for sanitized in (False, True):
        flags = ['-fsanitize=address,undefined', '-fno-pie', '-no-pie'] if sanitized else []
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I' + str(tmp), str(tmp / 'wifi_scan.cpp'), str(tmp / 'test.cpp'),
                        '-o', str(tmp / 'test')], check=True)
        subprocess.run([str(tmp / 'test')], check=True)
print('Scan completion ownership, 200/3/0 APs, borrowed results, cancellation drain, external ownership and failures PASS')
