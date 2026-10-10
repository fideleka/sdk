#pragma once

#include "wifi_credentials.h"

namespace lilka {

/// Cooperative connection selector. Call load under the application's NVS lock,
/// then start/poll from one serialized owner task. Never call from Wi-Fi events.
/// No task, radio activation or NVS writes are performed at SDK startup.
class WiFiConnection {
public:
    enum class State { Idle, Connecting, Scanning, Connected, Failed, NoCredentials };
    static constexpr uint32_t ConnectTimeoutMs = 10000;
    static constexpr uint32_t ScanTimeoutMs = 5000;
    static constexpr uint32_t RoundTimeoutMs = 35000;

    /// Snapshot saved credentials from the open "network" namespace.
    bool load(Preferences& prefs);
    /// Preserve an already working connection; otherwise try last_ssid first.
    State start(uint32_t now);
    /// Advance without waiting. After failure of the preferred AP, scan once and
    /// try visible saved APs strongest first, once per SSID, at most 16+1 attempts.
    State poll(uint32_t now);
    /// Stop owned scans/association. false preserves an established connection.
    void cancel(bool disconnect = false);
    /// Remember a successful selected network using the existing compatibility
    /// keys. Caller owns NVS; refuses removed/changed credentials. No-op if same.
    bool remember(Preferences& prefs);
    State state() const;
    const String& ssid() const;
    const String& password() const;

private:
    struct Credential {
        String ssid;
        String password;
        int32_t rssi = -1000;
        bool tried = false;
    };
    std::vector<Credential> known;
    String preferred;
    String selectedSSID;
    String selectedPassword;
    State current = State::Idle;
    uint32_t started = 0;
    uint32_t roundStarted = 0;
    bool scanned = false;
    bool ownsAssociation = false;
    bool ownsScan = false;

    bool preserveConnected();
    void attempt(size_t index, uint32_t now);
    State scan(uint32_t now);
    State next(uint32_t now);
    void stopScan();
};

/// Shared lazy connector; applications serialize use and supply their own task.
extern WiFiConnection wifiConnection;

} // namespace lilka
