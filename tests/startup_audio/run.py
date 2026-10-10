#!/usr/bin/env python3
"""Real startup + controller sources, immutable real PCM, deterministic IDF model."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/lilka/src/lilka"
TEST = Path(__file__).resolve().parent
U8G2 = Path(os.environ.get("U8G2_CLIB", ROOT.parent / "lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib"))
assert (U8G2 / "u8g2.h").is_file(), "Set U8G2_CLIB to an existing read-only dependency directory"
with tempfile.TemporaryDirectory(prefix="lilka-startup-") as directory:
    tmp = Path(directory)
    for name in ("controller.cpp", "controller.h", "audio.cpp", "audio.h", "config.h", "system_shortcuts.h", "volume_overlay.h", "ping.h", "brightness.h", "brightness.cpp", "display_settings.h", "display_settings.cpp", "settings_persistence.h", "settings_persistence.cpp"):
        (tmp / name).write_text((SOURCE / name).read_text())
    host = (ROOT / "tests/system_shortcuts/host.h").read_text()
    host = host.replace("inline void vTaskDelay(int) {}", "void vTaskDelay(int);")
    host = host.replace("inline void vTaskDelete(void*) {}", "void vTaskDelete(void*);")
    host = host.replace("inline void pinMode(int, int) {}", "void pinMode(int, int);")
    host = host.replace("inline int digitalRead(int) { return 1; }", "int digitalRead(int);")
    host = host.replace("    xTaskCreate(fn, name, stack, arg, priority, task);", "    extern void (*hostStartupTask)(void*); hostStartupTask = fn;\n    xTaskCreate(fn, name, stack, arg, priority, task);")
    host = host.replace("void begin(int, int, int) {}", "void begin(int, int, int) { assert(false); }")
    host = host.replace("void write(int) {}", "void write(int) { assert(false); }")
    host = host.replace("void end() {}", "void end() { assert(false); }", 1)
    host = host.replace("inline void gpio_pad_select_gpio(int) {}", "inline void gpio_pad_select_gpio(int pin) { assert(pin != 0); }")
    host = host.replace("inline void gpio_set_direction(int, int) {}", "inline void gpio_set_direction(int pin, int) { assert(pin != 0); }")
    host = host.replace("inline void gpio_matrix_out(int, int, bool, bool) {}", "inline void gpio_matrix_out(int pin, int, bool, bool) { assert(pin != 0); }")
    (tmp / "host.h").write_text(host + (TEST / "idf.h").read_text())
    for name in ("Arduino.h", "I2S.h", "Preferences.h", "serial.h", "driver/uart.h", "freertos/FreeRTOS.h", "freertos/semphr.h", "freertos/task.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "host.h"\n')
    (tmp / "board.h").write_text('#pragma once\nnamespace lilka { struct MockBoard { void enablePowerSavingMode() {} void disablePowerSavingMode() {} }; extern MockBoard board; }\n')
    (tmp / "board_stub.cpp").write_text('#include "board.h"\nnamespace lilka {MockBoard board;}\n')
    for sanitize in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitize else []
        command = [os.environ.get("CXX", "g++"), "-std=c++11", "-DLILKA_VERSION=2", "-Wall", "-Wextra", "-Wno-reorder", "-Wno-unused-parameter", *flags,
                   "-I" + str(tmp), "-I" + str(TEST), "-I" + str(U8G2.parent), str(tmp / "controller.cpp"), str(tmp / "audio.cpp"), str(tmp / "brightness.cpp"), str(tmp / "display_settings.cpp"), str(tmp / "settings_persistence.cpp"), str(tmp / "board_stub.cpp"), str(TEST / "regression.cpp"), "-o", str(tmp / "regression")]
        subprocess.run(command, check=True)
        subprocess.run([str(tmp / "regression")], check=True)
