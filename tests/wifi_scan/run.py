"""Exercise the real bounded scan worker and cancellation lifecycle, no firmware build."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = root / 'lib/lilka/src/lilka'
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
extern bool taskFail, driverFail, retrieveFail;
extern int scans, retrievals, clears;
struct wifi_ap_record_t { uint8_t ssid[33]; int8_t rssi; };
struct wifi_scan_config_t { int scan_type; struct { struct { unsigned min, max; } active; } scan_time; };
inline int xTaskCreate(void(*fn)(void*), const char*, unsigned stack, void*, int, void*) {
    assert(stack == 3072);
    if (taskFail) return 0;
    worker = fn;
    return pdPASS;
}
inline void vTaskDelete(void*) {}
inline int esp_wifi_scan_start(const wifi_scan_config_t* c, bool block) {
    assert(block && c->scan_time.active.max == 120);
    ++scans;
    if (scanHook) scanHook();
    return driverFail ? -1 : ESP_OK;
}
inline int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t* records) {
    ++retrievals;
    assert(*count == 64); // A dense 200-AP scan still retrieves only 64.
    if (retrieveFail) return -1;
    for (unsigned i = 0; i < *count; ++i) {
        std::strcpy(reinterpret_cast<char*>(records[i].ssid), "AP");
        records[i].rssi = -40;
    }
    return ESP_OK;
}
inline int esp_wifi_clear_ap_list() { ++clears; return ESP_OK; }
struct WiFiHAL { int external = -2; int scanComplete() { return external; } };
extern WiFiHAL WiFi;
'''
test = r'''
#include "mock.h"
#include "wifi_scan.h"
void (*worker)(void*) = nullptr;
void (*scanHook)() = nullptr;
bool taskFail = false, driverFail = false, retrieveFail = false;
int scans = 0, retrievals = 0, clears = 0;
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
    Scan::release();
    assert(!Scan::records() && Scan::count() == -2);
    assert(Scan::start());
    Scan::release(); // Cancellation before worker begins: no driver scan at all.
    assert(Scan::running() && !Scan::start());
    const int before = scans;
    finish();
    assert(scans == before && !Scan::running() && !Scan::records());
    assert(Scan::start());
    scanHook = [] { Scan::release(); }; // Cancel while the blocking driver scan drains.
    finish();
    scanHook = nullptr;
    assert(!Scan::running() && !Scan::records() && Scan::count() == -2);
    for (int scenario = 0; scenario < 2; ++scenario) {
        driverFail = scenario == 0;
        retrieveFail = scenario == 1;
        assert(Scan::start());
        finish();
        assert(Scan::count() == -2);
        Scan::release();
    }
    assert(clears == 5); // Driver list cleanup on success, cancellation and errors.
}
'''
with tempfile.TemporaryDirectory(prefix='bounded-wifi-scan-') as directory:
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
print('Bounded IDF scan: dense results, cancellation drain/free, external ownership, task/driver/retrieval failures PASS')
