#include "wifi_connection.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <algorithm>

namespace lilka {

WiFiConnection wifiConnection;

bool WiFiConnection::load(Preferences& prefs) {
    known.clear();
    preferred = prefs.getString("last_ssid", "");
    for (const String& name : NetworkCredentials::list(prefs)) {
        Credential entry;
        entry.ssid = name;
        if (NetworkCredentials::read(prefs, name, entry.password)) {
            known.push_back(entry);
        }
    }
    return !known.empty();
}

bool WiFiConnection::preserveConnected() {
    if (WiFi.status() != WL_CONNECTED) return false;
    stopScan();
    if (ownsAssociation && WiFi.SSID() != selectedSSID) {
        selectedSSID = selectedPassword = "";
    }
    ownsAssociation = false;
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
    WiFi.disconnect();
    WiFi.begin(selectedSSID.c_str(), selectedPassword.c_str());
}

WiFiConnection::State WiFiConnection::start(uint32_t now) {
    stopScan();
    selectedSSID = selectedPassword = "";
    ownsAssociation = false;
    scanned = false;
    for (Credential& entry : known) {
        entry.tried = false;
        entry.rssi = -1000;
    }
    if (WiFi.status() == WL_CONNECTED) {
        return current = State::Connected;
    }
    if (known.empty()) {
        return current = State::NoCredentials;
    }
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return current = State::Failed;
    }
    WiFi.persistent(false);
    // The adapter may already have been initialized with Arduino's Flash default.
    // Change driver storage too: failed associations must not rewrite credentials.
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return current = State::Failed;
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
    scanned = true;
    // Don't consume or delete a scan started by an application UI.
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return current = State::Failed;
    }
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    ownsAssociation = false;
    ownsScan = true;
    started = now;
    current = State::Scanning;
    const int result = WiFi.scanNetworks(true);
    return result == WIFI_SCAN_RUNNING ? current : poll(now);
}

void WiFiConnection::stopScan() {
    if (ownsScan) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
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
        return current = State::Failed;
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
        return current = State::Failed;
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
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING && uint32_t(now - started) < ScanTimeoutMs) {
        return current;
    }
    if (count >= 0) {
        // Bound allocation/CPU work and match exact SSID bytes; unknown APs are ignored.
        for (int i = 0; i < std::min(count, 64); ++i) {
            const String name = WiFi.SSID(i);
            for (Credential& entry : known) {
                if (entry.ssid == name) {
                    entry.rssi = std::max(entry.rssi, WiFi.RSSI(i));
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
    known.clear();
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
