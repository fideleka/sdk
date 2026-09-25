#ifndef LILKA_SYS_H
#define LILKA_SYS_H

#include <Arduino.h>
#include <esp_partition.h>

namespace lilka {

class Sys {
public:
    int get_partition_labels(String labels[]);
    uint64_t get_partition_address(const char* label);
    uint64_t get_partition_size(const char* label);
    void print_partition_table();

    /// Restart after a host-visible USB disconnect and full digital reset.
    ///
    /// On Lilka v2 this restores the hardware USB-Serial/JTAG PHY and clears
    /// retained GPIO/USB state. The boot partition is not changed: a firmware
    /// launched by MultiBoot returns to the configured primary firmware.
    /// Call only from a normal task after application writes have completed.
    /// On Lilka v1 this uses the ordinary ESP restart.
    [[noreturn]] void restart();
};

extern Sys sys;

} // namespace lilka

#endif // LILKA_SYS_H
