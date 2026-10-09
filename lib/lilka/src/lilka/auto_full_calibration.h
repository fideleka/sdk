#pragma once

#include "charge_status.h"
#include <stdint.h>

namespace lilka {
// One calibration opportunity per confirmed Charged -> Battery transition.
// Volts are uncalibrated normal-divider values, never optocoupler tags.
class AutoFullCalibration {
public:
    static constexpr uint32_t stabilizationMs = 30000;

    bool update(float voltage, uint32_t now) {
        const ChargeStatus sample = ChargeStatusFilter::classify(voltage);
        const ChargeStatus confirmed = filter.update(voltage);
        const bool plausibleFullBattery = sample == ChargeStatus::Battery && voltage >= BATTERY_MIN_FULL_LEVEL_VOLTAGE;

        if (sample == ChargeStatus::Charged && confirmed == ChargeStatus::Charged) {
            armed = true;
            waiting = false;
        } else if (sample != ChargeStatus::Battery) {
            // Reconnection, absence, guard bands and invalid samples invalidate
            // the opportunity. Retained UI state is not evidence of unplugging.
            armed = false;
            waiting = false;
        } else if (!plausibleFullBattery) {
            armed = false;
            waiting = false;
        } else if (confirmed == ChargeStatus::Battery && previous == ChargeStatus::Charged && armed) {
            startedAt = now;
            waiting = true;
            armed = false;
        }
        previous = confirmed;

        if (waiting && confirmed == ChargeStatus::Battery && plausibleFullBattery &&
            now - startedAt >= stabilizationMs) {
            waiting = false;
            return true; // Consume before attempting the save, including failure.
        }
        return false;
    }

    ChargeStatus getStatus() const {
        return previous;
    }

private:
    ChargeStatusFilter filter;
    ChargeStatus previous = ChargeStatus::Unknown;
    bool armed = false;
    bool waiting = false;
    uint32_t startedAt = 0;
};
} // namespace lilka
