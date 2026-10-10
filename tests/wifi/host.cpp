#include <lilka/wifi_credentials.h>
#include <Preferences.h>
using lilka::NetworkCredentials;
#include <cassert>
#include <cstdio>
std::map<std::string, std::vector<uint8_t>> Preferences::data;
std::string Preferences::failWrite, Preferences::failRemove;
unsigned Preferences::writes = 0;
bool Preferences::failBegin = false;
static void reset() {
    Preferences::data.clear();
    Preferences::failWrite.clear();
    Preferences::failRemove.clear();
    Preferences::writes = 0;
}
int main() {
    Preferences prefs;
    String password;
    assert(!NetworkCredentials::read(prefs, "", password));
    assert(NetworkCredentials::list(prefs).empty());
    const String emoji = "🦄🌈🎉";
    assert(emoji.length() == 12);
    assert(NetworkCredentials::passwordKey(emoji) == "c069f722_pw");
    assert(NetworkCredentials::displayName(emoji) == "[U+1F984][U+1F308][U+1F389]");
    assert(NetworkCredentials::displayName("Радіо Cafe") == "Радіо Cafe");
    prefs.putString(NetworkCredentials::passwordKey("Open").c_str(), "");
    prefs.putString("last_ssid", "Open");
    assert(NetworkCredentials::read(prefs, "Open", password) && password.isEmpty());
    assert(NetworkCredentials::list(prefs).size() == 1);
    assert(NetworkCredentials::save(prefs, emoji, "password123"));
    assert(NetworkCredentials::read(prefs, emoji, password) && password == "password123");
    assert(prefs.getString("last_ssid", "") == emoji);
    assert(prefs.getString(NetworkCredentials::passwordKey(emoji).c_str(), "") == "password123");
    const unsigned writes = Preferences::writes;
    assert(NetworkCredentials::save(prefs, emoji, "password123"));
    assert(writes == Preferences::writes);
    assert(NetworkCredentials::save(prefs, "Open", "", false));
    assert(prefs.getString("last_ssid", "") == emoji);
    assert(NetworkCredentials::list(prefs).size() == 2);
    assert(NetworkCredentials::forget(prefs, emoji));
    assert(!NetworkCredentials::read(prefs, emoji, password));
    assert(!prefs.isKey("last_ssid"));
    assert(NetworkCredentials::read(prefs, "Open", password));
    assert(NetworkCredentials::list(prefs).size() == 1);
    assert(NetworkCredentials::save(prefs, emoji, "changed123"));
    assert(NetworkCredentials::read(prefs, emoji, password) && password == "changed123");
    Preferences::failRemove = NetworkCredentials::passwordKey(emoji).c_str();
    assert(!NetworkCredentials::forget(prefs, emoji));
    assert(!NetworkCredentials::read(prefs, emoji, password));
    Preferences::failRemove.clear();
    assert(NetworkCredentials::forget(prefs, emoji));
    assert(!NetworkCredentials::read(prefs, emoji, password));
    reset();
    for (unsigned i = 0; i < NetworkCredentials::Capacity; ++i) {
        assert(NetworkCredentials::save(prefs, String(std::to_string(i)), "password123"));
    }
    assert(!NetworkCredentials::save(prefs, "overflow", "password123"));
    assert(NetworkCredentials::forget(prefs, "3"));
    assert(NetworkCredentials::save(prefs, "replacement", "password123"));
    assert(NetworkCredentials::list(prefs).size() == NetworkCredentials::Capacity);
    assert(!NetworkCredentials::save(prefs, String(std::string(33, 'a')), "password123"));
    assert(!NetworkCredentials::save(prefs, "longPassword", String(std::string(65, 'b'))));
    reset();
    Preferences::failWrite = "wifi0";
    assert(!NetworkCredentials::save(prefs, "failed", "password123"));
    assert(Preferences::data.empty());
    reset();
    assert(NetworkCredentials::passwordKey("Aa") == NetworkCredentials::passwordKey("BB"));
    assert(NetworkCredentials::save(prefs, "Aa", "passwordAAA"));
    assert(!NetworkCredentials::read(prefs, "BB", password));
    assert(NetworkCredentials::save(prefs, "BB", "passwordBBB"));
    assert(NetworkCredentials::read(prefs, "Aa", password) && password == "passwordAAA");
    assert(NetworkCredentials::read(prefs, "BB", password) && password == "passwordBBB");
    assert(NetworkCredentials::forget(prefs, "Aa"));
    assert(prefs.getString(NetworkCredentials::passwordKey("BB").c_str(), "") == "passwordBBB");
    assert(!NetworkCredentials::read(prefs, "Aa", password));
    reset();
    Preferences::data["wifi0"] = {1, 2, 3};
    assert(NetworkCredentials::save(prefs, emoji, "password123"));
    assert(Preferences::data["wifi0"] == std::vector<uint8_t>({1, 2, 3}));
    assert(NetworkCredentials::list(prefs).size() == 1);
    puts(
        "Production WiFi credentials: UTF-8 emoji/open/legacy/collisions/forget/failure/capacity/no redundant writes "
        "PASS"
    );
}
