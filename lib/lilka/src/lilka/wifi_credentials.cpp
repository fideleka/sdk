#include "wifi_credentials.h"
#include <Preferences.h>
#include <cstring>
#include <cstdio>
#include <utility>

namespace lilka {

namespace {
constexpr uint32_t RecordMagic = 0x57494631;
} // namespace

bool NetworkCredentials::valid(const String& ssid) {
    return ssid.length() > 0 && ssid.length() <= 32 && std::strlen(ssid.c_str()) == ssid.length();
}

String NetworkCredentials::passwordKey(const String& ssid) {
    uint32_t hash = 0;
    for (size_t i = 0; i < ssid.length(); ++i) {
        hash = hash * 31 + static_cast<uint8_t>(ssid[i]);
    }
    char key[12];
    std::snprintf(key, sizeof(key), "%08x_pw", unsigned(hash));
    return String(key);
}

String NetworkCredentials::slotKey(unsigned slot) {
    char key[12];
    std::snprintf(key, sizeof(key), "wifi%u", slot);
    return String(key);
}

bool NetworkCredentials::load(Preferences& prefs, unsigned slot, Record& record) {
    const String key = slotKey(slot);
    if (prefs.getBytesLength(key.c_str()) != sizeof(record) ||
        prefs.getBytes(key.c_str(), &record, sizeof(record)) != sizeof(record)) {
        return false;
    }
    return record.magic == RecordMagic && record.active <= 1 && record.ssid[32] == 0 && record.password[64] == 0 &&
           valid(String(record.ssid));
}

bool NetworkCredentials::read(Preferences& prefs, const String& ssid, String& password) {
    password = "";
    if (!valid(ssid)) {
        return false;
    }
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        if (load(prefs, slot, record) && ssid == record.ssid) {
            if (!record.active) {
                return false; // Tombstone blocks legacy resurrection after partial cleanup.
            }
            password = record.password;
            return true;
        }
    }
    const String key = passwordKey(ssid);
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        if (load(prefs, slot, record) && String(record.ssid) != ssid && passwordKey(String(record.ssid)) == key) {
            return false; // A legacy hash collision is not evidence of a known SSID.
        }
    }
    if (!prefs.isKey(key.c_str())) {
        return false;
    }
    password = prefs.getString(key.c_str(), "");
    if (password.length() > 64) {
        password = "";
        return false;
    }
    return true;
}

bool NetworkCredentials::save(Preferences& prefs, const String& ssid, const String& password, bool preferred) {
    if (!valid(ssid) || password.length() > 64 || std::strlen(password.c_str()) != password.length()) {
        return false;
    }
    int chosen = -1;
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        const bool loaded = load(prefs, slot, record);
        if (loaded && ssid == record.ssid) {
            chosen = slot;
            break;
        }
        // Don't overwrite corrupt or unknown records, or incomplete Forget tombstones.
        if (chosen < 0 && ((!prefs.isKey(slotKey(slot).c_str())) ||
                           (loaded && !record.active && !prefs.isKey(passwordKey(String(record.ssid)).c_str())))) {
            chosen = slot;
        }
    }
    if (chosen < 0) {
        return false;
    }
    Record record{};
    record.magic = RecordMagic;
    record.active = 1;
    std::memcpy(record.ssid, ssid.c_str(), ssid.length());
    std::memcpy(record.password, password.c_str(), password.length());
    const String key = slotKey(chosen);
    Record previous{};
    if ((!load(prefs, chosen, previous) || previous.active != record.active ||
         std::strcmp(previous.ssid, record.ssid) != 0 || std::strcmp(previous.password, record.password) != 0) &&
        prefs.putBytes(key.c_str(), &record, sizeof(record)) != sizeof(record)) {
        return false;
    }
    // Compatibility: Lilplayer and older firmware read these existing keys.
    const String legacyKey = passwordKey(ssid);
    const String selected = prefs.getString("last_ssid", "");
    const bool mirror = preferred || selected == ssid || passwordKey(selected) != legacyKey;
    if (mirror && (!prefs.isKey(legacyKey.c_str()) || prefs.getString(legacyKey.c_str(), "") != password)) {
        prefs.putString(legacyKey.c_str(), password);
        if (!prefs.isKey(legacyKey.c_str()) || prefs.getString(legacyKey.c_str(), "") != password) {
            return false;
        }
    }
    if (preferred && prefs.getString("last_ssid", "") != ssid) {
        prefs.putString("last_ssid", ssid);
        if (prefs.getString("last_ssid", "") != ssid) {
            return false;
        }
    }
    return true;
}

bool NetworkCredentials::forget(Preferences& prefs, const String& ssid) {
    if (!valid(ssid)) {
        return false;
    }
    bool found = false;
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        if (load(prefs, slot, record) && ssid == record.ssid) {
            found = true;
            record.active = 0;
            std::memset(record.password, 0, sizeof(record.password));
            if (prefs.putBytes(slotKey(slot).c_str(), &record, sizeof(record)) != sizeof(record)) {
                return false;
            }
            break;
        }
    }
    const String key = passwordKey(ssid);
    if (!found && !prefs.isKey(key.c_str())) {
        return false;
    }
    if (prefs.isKey(key.c_str()) && !prefs.remove(key.c_str())) {
        return false;
    }
    // Preserve an unrelated selected network whose old 32-bit hash collides.
    const String selected = prefs.getString("last_ssid", "");
    if (selected != ssid && passwordKey(selected) == key) {
        String password;
        if (read(prefs, selected, password)) {
            prefs.putString(key.c_str(), password);
            if (!prefs.isKey(key.c_str()) || prefs.getString(key.c_str(), "") != password) {
                return false;
            }
        }
    }
    if (prefs.getString("last_ssid", "") == ssid && !prefs.remove("last_ssid")) {
        return false;
    }
    return true;
}

std::vector<NetworkCredentials::SavedNetwork> NetworkCredentials::snapshot(Preferences& prefs) {
    std::vector<SavedNetwork> result;
    result.reserve(Capacity + 1);
    const String last = prefs.getString("last_ssid", "");
    const String legacyKey = passwordKey(last);
    bool selectedIndexed = false, collision = false;
    std::vector<String> seen;
    seen.reserve(Capacity);
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        if (!load(prefs, slot, record)) continue;
        const String name(record.ssid);
        if (name == last) selectedIndexed = true; // Includes Forget tombstones.
        else if (passwordKey(name) == legacyKey) collision = true;
        bool duplicate = false;
        for (const String& previous : seen)
            duplicate |= previous == name;
        if (duplicate) continue;
        seen.push_back(name);
        if (!record.active) continue;
        SavedNetwork entry;
        entry.ssid = name;
        entry.password = record.password;
        result.push_back(std::move(entry));
    }
    if (!selectedIndexed && !collision && valid(last) && prefs.isKey(legacyKey.c_str())) {
        SavedNetwork entry;
        entry.ssid = last;
        entry.password = prefs.getString(legacyKey.c_str(), "");
        if (entry.password.length() <= 64) result.push_back(std::move(entry));
    }
    return result;
}

std::vector<String> NetworkCredentials::list(Preferences& prefs) {
    std::vector<String> result;
    for (unsigned slot = 0; slot < Capacity; ++slot) {
        Record record{};
        if (load(prefs, slot, record) && record.active) {
            result.push_back(String(record.ssid));
        }
    }
    const String last = prefs.getString("last_ssid", "");
    String password;
    bool included = false;
    for (const String& name : result) {
        included |= name == last;
    }
    if (!included && read(prefs, last, password)) {
        result.push_back(last);
    }
    return result;
}

String NetworkCredentials::displayName(const String& ssid) {
    String result;
    for (size_t i = 0; i < ssid.length();) {
        const uint8_t byte = static_cast<uint8_t>(ssid[i]);
        // Current SDK font/decoder is BMP-only. Escape non-BMP UTF-8 safely.
        if (byte >= 0xf0 && byte <= 0xf4 && i + 3 < ssid.length()) {
            uint32_t point = byte & 7;
            bool continuations = true;
            for (size_t j = 1; j < 4; ++j) {
                const uint8_t next = static_cast<uint8_t>(ssid[i + j]);
                continuations &= (next & 0xc0) == 0x80;
                point = (point << 6) | (next & 0x3f);
            }
            if (continuations && point >= 0x10000 && point <= 0x10ffff) {
                char label[16];
                std::snprintf(label, sizeof(label), "[U+%X]", unsigned(point));
                result += label;
                i += 4;
                continue;
            }
        }
        if (byte < 32 || byte == 127) {
            result += '?';
        } else {
            result += ssid[i];
        }
        ++i;
    }
    return result;
}

} // namespace lilka
