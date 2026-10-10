#pragma once

#include <Arduino.h>
class Preferences;
#include <vector>

namespace lilka {

// Caller owns the network namespace and NVS lock. No Wi-Fi or logging here.
class NetworkCredentials {
public:
    static constexpr unsigned Capacity = 16;
    static bool read(Preferences& prefs, const String& ssid, String& password);
    static bool save(Preferences& prefs, const String& ssid, const String& password, bool preferred = true);
    static bool forget(Preferences& prefs, const String& ssid);
    static std::vector<String> list(Preferences& prefs);
    static String passwordKey(const String& ssid);
    // Label only: preserve the original UTF-8 SSID for storage and connection.
    static String displayName(const String& ssid);

private:
    struct Record {
        uint32_t magic;
        uint8_t active;
        char ssid[33];
        char password[65];
    };
    static bool valid(const String& ssid);
    static String slotKey(unsigned slot);
    static bool load(Preferences& prefs, unsigned slot, Record& record);
};

} // namespace lilka
