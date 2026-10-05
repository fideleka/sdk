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
    for name in ("controller.cpp", "controller.h", "audio.cpp", "audio.h", "config.h", "system_shortcuts.h", "volume_overlay.h"):
        (tmp / name).write_text((SOURCE / name).read_text())
    for name in ("Arduino.h", "I2S.h", "Preferences.h", "serial.h", "driver/uart.h",
                 "freertos/FreeRTOS.h", "freertos/semphr.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('''#pragma once
#include "host.h"
''')
    (tmp / "ping.h").write_text('''#pragma once
const uint8_t ping_raw[2] = {};
const int ping_raw_size = 2;
''')
    for sanitize in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitize else []
        command = [os.environ.get("CXX", "g++"), "-std=c++11", "-DLILKA_VERSION=2", "-DLILKA_NO_AUDIO_HELLO",
                   "-Wall", "-Wextra", "-Wno-reorder", "-Wno-unused-parameter", *flags,
                   "-I" + str(tmp), "-I" + str(TEST), "-I" + str(U8G2), str(tmp / "controller.cpp"),
                   str(tmp / "audio.cpp"), str(TEST / "regression.cpp"), "-o", str(tmp / "regression")]
        subprocess.run(command, check=True)
        subprocess.run([str(tmp / "regression")], check=True)
