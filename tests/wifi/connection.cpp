#include <lilka/wifi_connection.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cassert>
#include <cstdio>
#include <limits>
std::map<std::string, std::vector<uint8_t>> Preferences::data;
std::string Preferences::failWrite, Preferences::failRemove;
unsigned Preferences::writes = 0;
bool Preferences::failBegin = false;
WiFiHAL WiFi;
using lilka::NetworkCredentials;
using State = lilka::WiFiConnection::State;
int main() {
    Preferences prefs;
    assert(prefs.begin("network", false));
    const String emoji = "🦄🌈🎉";
    assert(NetworkCredentials::save(prefs, "Home", "homepass"));
    assert(NetworkCredentials::save(prefs, "Weak", "wrongpass", false));
    assert(NetworkCredentials::save(prefs, emoji, "", false));
    auto& selector = lilka::wifiConnection;
    assert(selector.load(prefs));
    unsigned writes = Preferences::writes;
    WiFi.scanValue = WIFI_SCAN_FAILED;
    assert(selector.start(0) == State::Connecting);
    assert(WiFi.attempts.size() == 1 && WiFi.attempts.back() == "Home");
    assert(selector.poll(9999) == State::Connecting);
    assert(selector.poll(10000) == State::Scanning);
    WiFi.visible = {{"Unknown", -10}, {emoji, -80}, {"Weak", -40}, {emoji, -65}, {"Home", -20}};
    WiFi.scanValue = WiFi.visible.size();
    assert(selector.poll(10020) == State::Connecting);
    assert(WiFi.attempts.back() == "Weak");
    assert(selector.poll(20020) == State::Connecting);
    assert(WiFi.attempts.back() == emoji && selector.password().isEmpty());
    WiFi.statusValue = WL_CONNECTED;
    assert(selector.poll(20040) == State::Connected);
    assert(selector.ssid() == emoji && WiFi.scans == 1);
    for (unsigned i = 0; i < 100; ++i)
        assert(selector.poll(30000 + i) == State::Connected);
    assert(WiFi.attempts.size() == 3 && Preferences::writes == writes);
    assert(selector.remember(prefs));
    assert(prefs.getString("last_ssid", "") == emoji);
    writes = Preferences::writes;
    assert(selector.remember(prefs) && Preferences::writes == writes);
    // Restore the preferred network for later timeout tests.
    assert(NetworkCredentials::save(prefs, "Home", "homepass"));
    selector.cancel(false);
    assert(WiFi.statusValue == WL_CONNECTED);
    assert(selector.load(prefs));
    assert(selector.start(31000) == State::Connected);
    assert(WiFi.attempts.size() == 3); // Never roam while connected.
    selector.cancel(true);
    assert(WiFi.statusValue != WL_CONNECTED && !WiFi.reconnect);
    // Unsigned elapsed arithmetic across millis rollover, scan timeout cleanup.
    assert(selector.load(prefs));
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 5000;
    WiFi.scanValue = WIFI_SCAN_FAILED;
    assert(selector.start(start) == State::Connecting);
    assert(selector.poll(start + 9999) == State::Connecting);
    assert(selector.poll(start + 10000) == State::Scanning);
    assert(selector.poll(start + 14999) == State::Scanning);
    assert(selector.poll(start + 15000) == State::Failed);
    assert(WiFi.deletes == 2);
    selector.cancel();
    assert(selector.state() == State::Idle);
    // Cancel/Forget invalidate cached credentials before the next round.
    assert(NetworkCredentials::forget(prefs, "Home"));
    assert(selector.load(prefs));
    WiFi.scanStart = 1;
    WiFi.visible = {{emoji, -50}};
    assert(selector.start(0) == State::Connecting); // No preferred entry: scan immediately.
    assert(WiFi.attempts.back() == emoji);
    selector.cancel();
    assert(selector.state() == State::Idle && selector.password().isEmpty());
    // Respect scans owned by the UI rather than consuming/deleting their result.
    assert(selector.load(prefs));
    WiFi.scanValue = WIFI_SCAN_RUNNING;
    unsigned deletes = WiFi.deletes;
    assert(selector.start(0) == State::Failed);
    assert(WiFi.deletes == deletes);
    Preferences::data.clear();
    assert(!selector.load(prefs));
    WiFi.scanValue = WIFI_SCAN_FAILED;
    assert(selector.start(0) == State::NoCredentials);
    // IP can arrive between owner polls: cancelling must still preserve it.
    assert(NetworkCredentials::save(prefs, "Just connected", "password"));
    assert(selector.load(prefs));
    assert(selector.start(0) == State::Connecting);
    WiFi.statusValue = WL_CONNECTED;
    selector.cancel(false);
    assert(WiFi.statusValue == WL_CONNECTED && selector.state() == State::Idle);
    // IP arriving after the first timeout observation wins teardown.
    selector.cancel(true);
    assert(selector.load(prefs));
    assert(selector.start(0) == State::Connecting);
    const unsigned disconnects = WiFi.disconnects, scans = WiFi.scans;
    WiFi.connectAfterStatus = true;
    assert(selector.poll(10000) == State::Connected);
    assert(WiFi.disconnects == disconnects && WiFi.scans == scans);
    selector.cancel(true);
    assert(selector.load(prefs));
    assert(selector.start(0) == State::Connecting);
    const unsigned scansBeforeOnly = WiFi.scans;
    assert(selector.poll(10000) == State::Failed);
    assert(WiFi.scans == scansBeforeOnly); // Only saved AP already tried.
    puts(
        "Shared SDK WiFi: preferred, ranked/deduped fallback, open/UTF-8, cancellation, rollover, deadlines, no writes "
        "PASS"
    );
}
