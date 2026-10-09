#!/usr/bin/env python3
"""Exercise actual SDK battery calibration, persistence failures and legacy APIs."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/lilka/src/lilka"
MOCK = r"""
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstring>
extern uint16_t adcValue, stored;
extern int reads, writes;
extern bool failOpen, failWrite;
constexpr int INPUT_PULLDOWN = 1, LILKA_BATTERY_ADC = 3;
constexpr int LILKA_BATTERY_ADC_CHANNEL = 2;
constexpr int ADC_ATTEN_DB_11 = 11, ADC_WIDTH_BIT_12 = 12, ADC_UNIT_1 = 1;
#define LILKA_BATTERY_ADC_FUNC(name) name
inline void pinMode(int, int) {}
inline void config_channel_atten(int, int) {}
inline void config_width(int) {}
inline uint16_t analogRead(int) { ++reads; return adcValue; }
struct esp_adc_cal_characteristics_t {};
inline int esp_adc_cal_characterize(int, int, int, uint32_t, esp_adc_cal_characteristics_t *) { return 0; }
inline uint32_t esp_adc_cal_raw_to_voltage(uint32_t value, const esp_adc_cal_characteristics_t *) { return value; }
inline float constrain(float value, int low, int high) { return std::min(float(high), std::max(float(low), value)); }
class Preferences {
public:
 bool begin(const char *name, bool) { assert(!strcmp(name, "battery")); return !failOpen; }
 uint16_t getUShort(const char *key, uint16_t) { assert(!strcmp(key, "fullRawAdc")); return stored; }
 uint8_t getUChar(const char *, uint8_t fallback) { return fallback; }
 size_t putUShort(const char *key, uint16_t value) {
   assert(!strcmp(key, "fullRawAdc")); ++writes;
   if (failWrite) return 0;
   stored = value; return sizeof(value);
 }
 size_t putUChar(const char *, uint8_t) { return 1; }
 bool remove(const char *) { stored = 0; return true; }
 void end() {}
};
namespace lilka {
struct MockSerial { void err(const char *) {} };
extern MockSerial serial;
}
"""
TEST = r"""
#include "mock.h"
#include "battery.h"
uint16_t adcValue = 3068, stored = 0;
int reads = 0, writes = 0;
bool failOpen = false, failWrite = false;
namespace lilka { MockSerial serial; }
int main() {
  auto &battery = lilka::battery;
  battery.begin();
#if LILKA_VERSION >= 2
  assert(!battery.hasFullLevelCalibration());
  const int original = battery.readLevel();
  assert(battery.calibrateFullLevel());
  assert(reads == 64 && writes == 1 && stored == 3068);
  assert(battery.hasFullLevelCalibration() && battery.readEstimatedLevel() == 100);
  assert(battery.readLevel() == original); // Historical linear API unaffected.
  adcValue = 3000;
  const int estimate = battery.readEstimatedLevel();
  assert(estimate < 100);
  failOpen = true;
  assert(!battery.calibrateFullLevel() && stored == 3068 && writes == 1);
  assert(battery.readEstimatedLevel() == estimate);
  failOpen = false; failWrite = true;
  assert(!battery.calibrateFullLevel() && stored == 3068 && writes == 2);
  assert(battery.readEstimatedLevel() == estimate);
  failWrite = false;
  // A fresh sample is validated inside the save, not trusted from the caller.
  for (uint16_t tag : {0, 827, 1579, 2500, 3600, 4095}) {
    adcValue = tag;
    assert(!battery.calibrateFullLevel() && stored == 3068 && writes == 2);
  }
  adcValue = 3000;
  assert(battery.calibrateFullLevel() && stored == 3000 && writes == 3);
  assert(battery.readEstimatedLevel() == 100);
  battery.setDischargeProfile(lilka::BatteryDischargeProfile::SharpTop);
  assert(battery.getDischargeProfile() == lilka::BatteryDischargeProfile::SharpTop);
  battery.resetFullLevelCalibration();
  assert(!battery.hasFullLevelCalibration() && stored == 0);
#else
  assert(!battery.calibrateFullLevel() && !battery.hasFullLevelCalibration());
  assert(battery.readEstimatedLevel() == -1 && battery.readLevel() == -1);
  assert(reads == 0 && writes == 0);
#endif
}
"""
with tempfile.TemporaryDirectory(prefix="sdk-battery-cal-") as directory:
    tmp = Path(directory)
    for name in ("battery.cpp", "battery.h"):
        (tmp / name).write_text((SOURCE / name).read_text())
    (tmp / "mock.h").write_text(MOCK)
    (tmp / "test.cpp").write_text(TEST)
    for name in ("Arduino.h", "Preferences.h", "config.h", "serial.h", "driver/adc.h", "esp_adc_cal.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "mock.h"\n')
    for sanitized in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitized else []
        for version in (1, 2):
            output = tmp / "test"
            subprocess.run(
                [
                    os.environ.get("CXX", "g++"),
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *flags,
                    f"-DLILKA_VERSION={version}",
                    "-I" + str(tmp),
                    str(tmp / "battery.cpp"),
                    str(tmp / "test.cpp"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)
print(
    "PASS: actual battery source, checked NVS saving, previous-reference preservation, fresh tag rejection, legacy percent/profile APIs and v1 (normal + ASan/UBSan)"
)
