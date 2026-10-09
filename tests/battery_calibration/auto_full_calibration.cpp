#include "auto_full_calibration.h"
#include <cassert>
#include <limits>
#include <initializer_list>

using lilka::AutoFullCalibration;
void confirm(AutoFullCalibration& calibration, float voltage, uint32_t start) {
    for (uint32_t second = 0; second < 3; ++second) {
        assert(!calibration.update(voltage, start + second * 1000));
    }
}
void waitAndCalibrate(AutoFullCalibration& calibration, uint32_t start) {
    // Third normal sample publishes Battery; stabilization starts there.
    confirm(calibration, 4.08f, start);
    const uint32_t confirmedAt = start + 2000;
    assert(!calibration.update(4.08f, confirmedAt + 29999));
    assert(calibration.update(4.08f, confirmedAt + 30000));
    for (uint32_t second = 31; second < 100; ++second) {
        assert(!calibration.update(4.08f, confirmedAt + second * 1000));
    }
}
int main() {
    AutoFullCalibration calibration;
    confirm(calibration, 1.1f, 0); // Charged, never calibrate the tag itself.
    waitAndCalibrate(calibration, 3000);
    confirm(calibration, 2.1f, 150000);
    confirm(calibration, 1.1f, 153000);
    waitAndCalibrate(calibration, 156000); // Each new charge cycle gets one save.

    // Startup on Battery and unplugging from Charging never arm calibration.
    for (float initial : {4.08f, 2.1f}) {
        AutoFullCalibration other;
        confirm(other, initial, 0);
        for (uint32_t second = 3; second < 80; ++second) {
            assert(!other.update(4.08f, second * 1000));
        }
    }
    // A Charged candidate that never reached confirmation isn't enough.
    AutoFullCalibration candidate;
    assert(!candidate.update(1.1f, 0));
    assert(!candidate.update(1.1f, 1000));
    for (uint32_t second = 2; second < 80; ++second) {
        assert(!candidate.update(4.08f, second * 1000));
    }
    // Any reconnect/tag/absence/invalid or implausibly low sample cancels.
    for (float interruption : {1.1f, 2.1f, 0.0f, 2.7f, 3.4f, 4.7f, std::numeric_limits<float>::quiet_NaN()}) {
        AutoFullCalibration interrupted;
        confirm(interrupted, 1.1f, 0);
        confirm(interrupted, 4.08f, 3000);
        assert(!interrupted.update(interruption, 20000));
        for (uint32_t second = 21; second < 80; ++second) {
            assert(!interrupted.update(4.08f, second * 1000));
        }
    }
    // A short Charging excursion cannot hide behind retained Charged status.
    AutoFullCalibration charging;
    confirm(charging, 1.1f, 0);
    assert(!charging.update(2.1f, 3000));
    for (uint32_t second = 4; second < 80; ++second) {
        assert(!charging.update(4.08f, second * 1000));
    }
    // Timer arithmetic works across millis() wrap.
    AutoFullCalibration wrapped;
    confirm(wrapped, 1.1f, 0xffffd000u);
    waitAndCalibrate(wrapped, 0xffffe000u);
}
