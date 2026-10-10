#pragma once

#include <esp_wifi.h>

namespace lilka {
namespace detail {

// Serialized owner API. Blocking IDF scans do not dispatch SCAN_DONE to Arduino,
// avoiding its uncapped allocation. A temporary worker keeps the caller responsive.
// Cancel discards the result; the short driver scan drains before another starts.
class BoundedWiFiScan {
public:
    static constexpr unsigned Capacity = 64;
    static bool start();
    static bool running();
    static int count(); // -1 running, -2 unavailable/failed
    static const wifi_ap_record_t* records(); // valid until release(), owner only
    static void release();
};

} // namespace detail
} // namespace lilka
