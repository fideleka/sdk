// Selector-only deterministic adapter. Actual backend is tested by scan.cpp.
#include <lilka/wifi_scan.h>
#include <WiFi.h>
#include <cstring>
namespace lilka {
namespace detail {
bool BoundedWiFiScan::start() {
    WiFi.scanNetworks(true);
    return true;
}
bool BoundedWiFiScan::running() {
    return WiFi.scanComplete() == WIFI_SCAN_RUNNING;
}
int BoundedWiFiScan::count() {
    return WiFi.scanComplete();
}
const wifi_ap_record_t* BoundedWiFiScan::records() {
    static wifi_ap_record_t result[Capacity];
    for (int i = 0; i < count() && i < int(Capacity); ++i) {
        const String name = WiFi.SSID(i);
        std::strncpy(reinterpret_cast<char*>(result[i].ssid), name.c_str(), 32);
        result[i].ssid[32] = 0;
        result[i].rssi = WiFi.RSSI(i);
    }
    return result;
}
void BoundedWiFiScan::release() {
    WiFi.scanDelete();
}
} // namespace detail
} // namespace lilka
