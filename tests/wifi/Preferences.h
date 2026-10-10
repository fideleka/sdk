#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <cstring>
struct Preferences {
    static std::map<std::string, std::vector<uint8_t>> data;
    static std::string failWrite, failRemove;
    static unsigned writes;
    static bool failBegin;
    bool begin(const char*, bool) {
        return !failBegin;
    }
    void end() {
    }
    int getInt(const char*) {
        return 78;
    }
    bool isKey(const char* key) {
        return data.count(key);
    }
    size_t getBytesLength(const char* key) {
        return isKey(key) ? data[key].size() : 0;
    }
    size_t getBytes(const char* key, void* out, size_t length) {
        if (getBytesLength(key) != length) return 0;
        std::memcpy(out, data[key].data(), length);
        return length;
    }
    size_t putBytes(const char* key, const void* bytes, size_t length) {
        if (failWrite == key) return 0;
        const auto* first = static_cast<const uint8_t*>(bytes);
        data[key] = std::vector<uint8_t>(first, first + length);
        ++writes;
        return length;
    }
    String getString(const char* key, const char* fallback) {
        return isKey(key) ? String(std::string(data[key].begin(), data[key].end())) : String(fallback);
    }
    size_t putString(const char* key, const String& value) {
        return putBytes(key, value.c_str(), value.length());
    }
    bool remove(const char* key) {
        if (failRemove == key) return false;
        ++writes;
        return data.erase(key) > 0;
    }
};
