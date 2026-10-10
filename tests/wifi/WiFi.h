#pragma once
#include <Arduino.h>
#include <cassert>
#include <vector>
#include <utility>
constexpr int WL_CONNECTED = 3, WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
struct WiFiHAL {
    int statusValue = 0, scanValue = WIFI_SCAN_FAILED, scanStart = WIFI_SCAN_RUNNING;
    bool reconnect = true;
    unsigned scans = 0, deletes = 0, disconnects = 0;
    String connected;
    std::vector<String> attempts;
    std::vector<std::pair<String, int32_t>> visible;
    void persistent(bool value) {
        assert(!value);
    }
    bool connectAfterStatus = false;
    int status() {
        if (connectAfterStatus) {
            connectAfterStatus = false;
            statusValue = WL_CONNECTED;
            return 0;
        }
        return statusValue;
    }
    void setAutoReconnect(bool value) {
        reconnect = value;
    }
    void disconnect() {
        ++disconnects;
        statusValue = 0;
    }
    void begin(const char* ssid, const char*) {
        attempts.push_back(ssid);
        connected = ssid;
    }
    int scanComplete() {
        return scanValue;
    }
    int scanNetworks(bool) {
        ++scans;
        scanValue = scanStart;
        return scanValue;
    }
    void scanDelete() {
        ++deletes;
        scanValue = WIFI_SCAN_FAILED;
    }
    String SSID(int index = -1) {
        return index < 0 ? connected : visible.at(index).first;
    }
    int32_t RSSI(int index) {
        return visible.at(index).second;
    }
};
extern WiFiHAL WiFi;
