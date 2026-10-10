#!/usr/bin/env python3
"""Compile real SDK controller/audio sources against bounded host hardware/NVS stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/lilka/src/lilka"
TEST = ROOT / "tests/system_shortcuts"
U8G2 = Path(os.environ.get("U8G2_CLIB", ROOT.parent / "lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib"))
assert (U8G2 / "u8g2.h").is_file(), "Set U8G2_CLIB to an existing read-only dependency directory"
# Host scan helper must mirror the actual input task's post-mutex dispatch.
assert "if (volumeDelta) audio.stepVolumeShortcut(volumeDelta);" in (SOURCE / "controller.cpp").read_text()
assert "display" not in (SOURCE / "audio.cpp").read_text()
with tempfile.TemporaryDirectory(prefix="lilka-shortcuts-") as directory:
    tmp = Path(directory)
    for name in ("controller.cpp", "controller.h", "audio.cpp", "audio.h", "config.h", "system_shortcuts.h", "volume_overlay.h", "brightness.h", "brightness.cpp", "display_settings.h", "display_settings.cpp", "settings_persistence.h", "settings_persistence.cpp"):
        (tmp / name).write_text((SOURCE / name).read_text())
    for name in ("Arduino.h", "I2S.h", "Preferences.h", "serial.h", "driver/uart.h",
                 "freertos/FreeRTOS.h", "freertos/semphr.h", "freertos/task.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('''#pragma once
#include "host.h"
''')
    (tmp / "board.h").write_text('''#pragma once
namespace lilka { struct MockBoard { void enablePowerSavingMode() {} void disablePowerSavingMode() {} }; extern MockBoard board; }
''')
    (tmp / "board_stub.cpp").write_text('#include "board.h"\nnamespace lilka {MockBoard board;}\n')
    (tmp / "ping.h").write_text('''#pragma once
const uint8_t ping_raw[2] = {};
const int ping_raw_size = 2;
''')
    for sanitize in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitize else []
        command = [os.environ.get("CXX", "g++"), "-std=c++11", "-DLILKA_VERSION=2", "-DLILKA_NO_AUDIO_HELLO",
                   "-Wall", "-Wextra", "-Wno-reorder", "-Wno-unused-parameter", *flags,
                   "-I" + str(tmp), "-I" + str(TEST), "-I" + str(U8G2.parent), str(tmp / "controller.cpp"),
                   str(tmp / "audio.cpp"), str(tmp / "brightness.cpp"), str(tmp / "display_settings.cpp"), str(tmp / "settings_persistence.cpp"), str(tmp / "board_stub.cpp"), str(TEST / "regression.cpp"), "-o", str(tmp / "regression")]
        subprocess.run(command, check=True)
        subprocess.run([str(tmp / "regression")], check=True)
        # Compile the installed U8g2 decoder and original font asset read-only.
        # Section GC keeps unrelated font/UTF-8 entry points out of this host link.
        objects = []
        for name in ("u8g2_font.c", "u8g2_hvline.c", "u8g2_intersection.c", "u8g2_fonts.c"):
            obj = tmp / (name + ".o")
            subprocess.run([os.environ.get("CC", "gcc"), "-std=c99", *flags,
                            "-ffunction-sections", "-fdata-sections", "-I" + str(U8G2),
                            "-c", str(U8G2 / name), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        *flags, "-I" + str(tmp), "-I" + str(U8G2.parent),
                        str(TEST / "overlay.cpp"), *objects, "-Wl,--gc-sections",
                        "-o", str(tmp / "overlay")], check=True)
        subprocess.run([str(tmp / "overlay")], check=True)
