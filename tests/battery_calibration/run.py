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
#include <atomic>
#include <cstring>
using TaskHandle_t = void*;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(x) (x)
constexpr int pdPASS = 1;
extern bool failTask;
extern int tasks, criticalDepth;
extern uint32_t now;
extern void (*taskEntry)(void*);
extern void* taskContext;
inline void portENTER_CRITICAL(int* value) { ++*value; ++criticalDepth; }
inline void portEXIT_CRITICAL(int* value) { assert(*value == 1); --*value; --criticalDepth; }
inline uint32_t millis() { return now; }
struct Done {};
extern uint16_t adcValue;
inline void vTaskDelay(int delay) {
  assert(delay==1000 && criticalDepth==0);
  now+=delay;adcValue=now<3000?827:3068;
  if(now>=70000)throw Done{};
}
inline int xTaskCreate(void (*entry)(void*), const char*, int stack, void* context, int, TaskHandle_t* task) {
  assert(stack >= 8192); ++tasks;
  if (failTask) return 0;
  taskEntry=entry;taskContext=context;*task=reinterpret_cast<void*>(1);return pdPASS;
}
extern uint16_t adcValue, stored;
extern int reads, writes;
extern bool failOpen, failWrite;
extern bool failAdc;
constexpr int INPUT_PULLDOWN = 1, LILKA_BATTERY_ADC = 3;
constexpr int LILKA_BATTERY_ADC_CHANNEL = 2;
constexpr int ADC_ATTEN_DB_11 = 11, ADC_WIDTH_BIT_12 = 12, ADC_UNIT_1 = 1;
#define LILKA_BATTERY_ADC_FUNC(name) name
inline void pinMode(int, int) {}
inline void config_channel_atten(int, int) {}
inline void config_width(int) {}
// No analogRead stub: using Arduino's lazy ADC setup must fail this harness.
inline int get_raw(int channel) {
  assert(channel == LILKA_BATTERY_ADC_CHANNEL); ++reads;
  return failAdc ? -1 : adcValue;
}
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
   assert(!strcmp(key, "fullRawAdc")); assert(criticalDepth==0); ++writes;
   if (failWrite) return 0;
   stored = value; return sizeof(value);
 }
 size_t putUChar(const char *, uint8_t) { return 1; }
 bool remove(const char *) { stored = 0; return true; }
 void end() {}
};
namespace lilka {
struct MockSerial { void err(const char *) {} void log(const char *) {} };
extern MockSerial serial;
}
"""
TEST = r"""
#include "mock.h"
#define private public
#include "battery.h"
#undef private
bool failTask=false;
int tasks=0,criticalDepth=0;
uint32_t now=0;
void (*taskEntry)(void*)=nullptr;
void* taskContext=nullptr;
uint16_t adcValue = 3068, stored = 0;
int reads = 0, writes = 0;
bool failOpen = false, failWrite = false;
bool failAdc = false;
namespace lilka { MockSerial serial; }
int main() {
  auto &battery = lilka::battery;
  battery.begin();
#if LILKA_ADC_CHARGE_STATUS && LILKA_VERSION >= 2
  assert(tasks==1 && battery.beginChargeMonitoring() && tasks==1);
#else
  assert(!battery.beginChargeMonitoring() && tasks==0);
#endif
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
  adcValue = 65535;
  assert(battery.readRawValue() == 0);
  assert(!battery.calibrateFullLevel() && stored == 3000 && writes == 3);
  adcValue = 3000;
  failAdc = true;
  assert(battery.readRawValue() == 0);
  assert(!battery.calibrateFullLevel() && stored == 3000 && writes == 3);
  failAdc = false;
  assert(battery.readEstimatedLevel() == 100);
  battery.setDischargeProfile(lilka::BatteryDischargeProfile::SharpTop);
  assert(battery.getDischargeProfile() == lilka::BatteryDischargeProfile::SharpTop);
  battery.resetFullLevelCalibration();
  assert(!battery.hasFullLevelCalibration() && stored == 0);
#if LILKA_ADC_CHARGE_STATUS
  // Actual worker polling shares one median with state/percentage consumers.
  reads=0;writes=0;now=0;
  battery.automaticCalibration = lilka::AutoFullCalibration{};
  for(int second=0;second<40;++second){
    now=second*1000;adcValue=second<3?827:3068;
    battery.pollChargeState();
    const int sampled=reads;
    const auto snapshot=battery.getChargeSnapshot();
    assert(reads==sampled && criticalDepth==0);
    assert(snapshot.status==(second<2?lilka::ChargeStatus::Unknown:second<5?lilka::ChargeStatus::Charged:lilka::ChargeStatus::Battery));
    if(second>=35)assert(writes==1&&stored==3068&&snapshot.estimatedLevel==100);
    else assert(writes==0);
  }
  assert(reads==40*32+32);
  const auto normal=battery.getChargeSnapshot();
  now=40000;adcValue=827;battery.pollChargeState();
  const auto pending=battery.getChargeSnapshot();
  assert(pending.status==lilka::ChargeStatus::Battery && pending.sampleStatus==lilka::ChargeStatus::Charged);
  assert(pending.batteryVoltage==normal.batteryVoltage && pending.estimatedLevel==normal.estimatedLevel);
  assert(pending.rawVoltage!=pending.batteryVoltage && pending.updatedAt==now);
  // The real task loop, not only the poll helper, owns calibration and delay.
  battery.automaticCalibration = lilka::AutoFullCalibration{};
  now=0;adcValue=827;writes=0;
  try { taskEntry(taskContext); assert(false); } catch (Done&) {}
  assert(writes==1);
  // Initialization failure never publishes a fictitious status or starts work.
  battery.chargeTask=nullptr;failTask=true;
  assert(!battery.beginChargeMonitoring());
  failTask=false;
#endif
#else
  assert(!battery.calibrateFullLevel() && !battery.hasFullLevelCalibration());
  assert(battery.readEstimatedLevel() == -1 && battery.readLevel() == -1);
  assert(reads == 0 && writes == 0);
#endif
}
"""
with tempfile.TemporaryDirectory(prefix="sdk-battery-cal-") as directory:
    tmp = Path(directory)
    for name in ("battery.cpp", "battery.h", "charge_status.h", "auto_full_calibration.h"):
        (tmp / name).write_text((SOURCE / name).read_text())
    (tmp / "mock.h").write_text(MOCK)
    (tmp / "test.cpp").write_text(TEST)
    for name in (
        "Arduino.h",
        "Preferences.h",
        "config.h",
        "serial.h",
        "driver/adc.h",
        "esp_adc_cal.h",
        "freertos/FreeRTOS.h",
        "freertos/task.h",
    ):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "mock.h"\n')
    for sanitized in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitized else []
        for version, enabled in ((1, 0), (1, 1), (2, 0), (2, 1)):
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
                    f"-DLILKA_ADC_CHARGE_STATUS={enabled}",
                    "-I" + str(tmp),
                    str(tmp / "battery.cpp"),
                    str(tmp / "test.cpp"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)
with tempfile.TemporaryDirectory(prefix="sdk-charge-transition-") as directory:
    for sanitized in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitized else []
        output = Path(directory) / "transition"
        subprocess.run(
            [
                os.environ.get("CXX", "g++"),
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                *flags,
                "-I" + str(SOURCE),
                str(ROOT / "tests/battery_calibration/auto_full_calibration.cpp"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)
print(
    "PASS: SDK worker, coherent no-I/O snapshots, all transition/cancellation/30s cases, checked saves, legacy APIs, stock/modified/v1 and task failure (normal + ASan/UBSan)"
)
