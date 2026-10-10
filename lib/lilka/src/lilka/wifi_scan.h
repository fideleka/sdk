#pragma once

#include <esp_wifi.h>

namespace lilka {
namespace detail {

// Serialized owner API. Arduino owns SCAN_DONE and the result allocation; the
// adapter exposes at most Capacity records without another copy. Total driver/
// Arduino allocation is NOT capped. A temporary worker keeps callers responsive.
// Cancel discards the result; the short scan drains before another starts.
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
