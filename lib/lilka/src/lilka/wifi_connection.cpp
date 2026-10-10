#include "wifi_connection.h"
#include "wifi_scan.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <algorithm>

namespace lilka {

WiFiConnection wifiConnection;

bool WiFiConnection::load(Preferences& prefs) {
    known.clear();
    preferred = prefs.getString("last_ssid", "");
    known.reserve(NetworkCredentials::Capacity + 1);
    for (auto& saved : NetworkCredentials::snapshot(prefs)) {
        Credential entry;
        entry.ssid = std::move(saved.ssid);
        entry.password = std::move(saved.password);
        known.push_back(std::move(entry));
    }
    if (known.empty()) {
        releaseCandidates();
        return false;
    }
    return true;
}

void WiFiConnection::releaseCandidates() {
    std::vector<Credential>().swap(known);
    preferred = "";
}

WiFiConnection::State WiFiConnection::fail() {
    releaseCandidates();
    selectedPassword = "";
    return current = State::Failed;
}

bool WiFiConnection::preserveConnected() {
    if (WiFi.status() != WL_CONNECTED) return false;
    stopScan();
    if (ownsAssociation && WiFi.SSID() != selectedSSID) {
        selectedSSID = selectedPassword = "";
    }
    ownsAssociation = false;
    releaseCandidates();
    current = State::Connected;
    return true;
}

void WiFiConnection::attempt(size_t index, uint32_t now) {
    if (preserveConnected()) return;
    Credential& entry = known[index];
    entry.tried = true;
    selectedSSID = entry.ssid;
    selectedPassword = entry.password;
    ownsAssociation = true;
    current = State::Connecting;
    started = now;
    // The selector owns retry order; driver autoreconnect must not fight it.
    WiFi.setAutoReconnect(false);
    // Initial/candidate attempts start from an already disconnected state.
    WiFi.begin(selectedSSID.c_str(), selectedPassword.c_str());
}

WiFiConnection::State WiFiConnection::start(uint32_t now) {
    roundStarted = now;
    stopScan();
    selectedSSID = selectedPassword = "";
    ownsAssociation = false;
    scanned = false;
    for (Credential& entry : known) {
        entry.tried = false;
        entry.rssi = -1000;
    }
    if (preserveConnected()) return current;
    if (known.empty()) {
        return current = State::NoCredentials;
    }
    if (detail::BoundedWiFiScan::running() || WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return fail();
    }
    WiFi.persistent(false);
    // The adapter may already have been initialized with Arduino's Flash default.
    // Change driver storage too: failed associations must not rewrite credentials.
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return fail();
    }
    for (size_t i = 0; i < known.size(); ++i) {
        if (known[i].ssid == preferred) {
            attempt(i, now);
            return current;
        }
    }
    return scan(now);
}

WiFiConnection::State WiFiConnection::scan(uint32_t now) {
    if (preserveConnected()) return current;
    // Automatic discovery cannot help once every saved candidate was tried.
    if (std::none_of(known.begin(), known.end(), [](const Credential& entry) { return !entry.tried; })) {
        selectedPassword = "";
        return fail();
    }
    scanned = true;
    // Don't consume or delete a scan started by an application UI.
    if (detail::BoundedWiFiScan::running() || WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return fail();
    }
    WiFi.setAutoReconnect(false);
    ownsAssociation = false;
    ownsScan = true;
    started = now;
    current = State::Scanning;
    if (!detail::BoundedWiFiScan::start()) {
        ownsScan = false;
        return fail();
    }
    return detail::BoundedWiFiScan::running() ? current : poll(now);
}

void WiFiConnection::stopScan() {
    if (ownsScan) {
        detail::BoundedWiFiScan::release();
        ownsScan = false;
    }
}

WiFiConnection::State WiFiConnection::next(uint32_t now) {
    if (preserveConnected()) return current;
    size_t strongest = known.size();
    for (size_t i = 0; i < known.size(); ++i) {
        const Credential& entry = known[i];
        if (!entry.tried && entry.rssi != -1000 && (strongest == known.size() || entry.rssi > known[strongest].rssi)) {
            strongest = i;
        }
    }
    if (strongest == known.size()) {
        selectedPassword = "";
        return fail();
    }
    attempt(strongest, now);
    return current;
}

WiFiConnection::State WiFiConnection::poll(uint32_t now) {
    if (current == State::Idle || current == State::Failed || current == State::NoCredentials) {
        return current;
    }
    if (preserveConnected()) return current;
    if (current == State::Connected) {
        return fail();
    }
    if (uint32_t(now - roundStarted) >= RoundTimeoutMs) {
        // One bound for the entire round, regardless of saved-network count.
        if (preserveConnected()) return current;
        stopScan();
        if (ownsAssociation) WiFi.disconnect();
        ownsAssociation = false;
        selectedPassword = "";
        return fail();
    }
    if (current == State::Connecting) {
        if (uint32_t(now - started) < ConnectTimeoutMs) {
            return current;
        }
        // Recheck at the destructive boundary: IP may arrive after the poll's
        // first status observation. Driver events remain asynchronous.
        if (preserveConnected()) return current;
        WiFi.disconnect();
        ownsAssociation = false;
        return scanned ? next(now) : scan(now);
    }
    const int count = detail::BoundedWiFiScan::count();
    if (count == WIFI_SCAN_RUNNING && uint32_t(now - started) < ScanTimeoutMs) {
        return current;
    }
    if (count >= 0) {
        // Retrieval was capped before allocating the adapter copy; ignore unknown APs.
        const wifi_ap_record_t* records = detail::BoundedWiFiScan::records();
        for (int i = 0; i < std::min(count, 64); ++i) {
            const String name(reinterpret_cast<const char*>(records[i].ssid));
            for (Credential& entry : known) {
                if (entry.ssid == name) {
                    entry.rssi = std::max(entry.rssi, int32_t(records[i].rssi));
                }
            }
        }
    }
    stopScan();
    return next(now);
}

void WiFiConnection::cancel(bool disconnect) {
    stopScan();
    if (disconnect || (ownsAssociation && WiFi.status() != WL_CONNECTED)) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();
    }
    ownsAssociation = false;
    selectedSSID = selectedPassword = "";
    releaseCandidates();
    current = State::Idle;
}

bool WiFiConnection::remember(Preferences& prefs) {
    String storedPassword;
    if (current != State::Connected || !selectedSSID.length() ||
        !NetworkCredentials::read(prefs, selectedSSID, storedPassword) || storedPassword != selectedPassword) {
        return false;
    }
    // Existing indexed records and mirrors are compared before writing.
    return NetworkCredentials::save(prefs, selectedSSID, storedPassword);
}

WiFiConnection::State WiFiConnection::state() const {
    return current;
}

const String& WiFiConnection::ssid() const {
    return selectedSSID;
}

const String& WiFiConnection::password() const {
    return selectedPassword;
}

} // namespace lilka
