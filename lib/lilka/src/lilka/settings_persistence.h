#pragma once

#include <stdint.h>

namespace lilka {
namespace detail {

// Three coalesced settings clients, one lazy worker. Callbacks return the delay
// until their next deadline, or zero when clean. No idle periodic polling.
class SettingsPersistence {
public:
    using Callback = uint32_t (*)();
    static bool begin(unsigned slot, Callback callback);
    static void notify(); // RAM-only, nonblocking; call outside hardware locks.
};

} // namespace detail
} // namespace lilka
