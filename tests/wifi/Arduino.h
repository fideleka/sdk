#pragma once
#include <cstdint>
#pragma once
#include <string>
#include <algorithm>
#include <cctype>
class String {
    std::string value;

public:
    char operator[](size_t index) const {
        return value[index];
    }
    String() = default;
    String(const char* s) : value(s) {
    }
    String(std::string s) : value(std::move(s)) {
    }
    const char* c_str() const {
        return value.c_str();
    }
    int length() const {
        return value.size();
    }
    bool isEmpty() const {
        return value.empty();
    }
    bool startsWith(const char* s) const {
        return value.rfind(s, 0) == 0;
    }
    bool endsWith(const char* s) const {
        std::string tail(s);
        return value.size() >= tail.size() && value.compare(value.size() - tail.size(), tail.size(), tail) == 0;
    }
    void toLowerCase() {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    }
    String& operator+=(char c) {
        value += c;
        return *this;
    }
    int indexOf(char c, int from = 0) const {
        auto pos = value.find(c, from);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        auto pos = value.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    String substring(int from, int to) const {
        return value.substr(from, to - from);
    }
    String substring(int from) const {
        return value.substr(from);
    }
    String& operator+=(const String& other) {
        value += other.value;
        return *this;
    }
    friend bool operator==(const String& a, const String& b) {
        return a.value == b.value;
    }
    friend bool operator!=(const String& a, const String& b) {
        return !(a == b);
    }
};
