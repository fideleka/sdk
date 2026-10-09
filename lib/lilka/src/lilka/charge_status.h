#pragma once

#include <cmath>
#include <stdint.h>

// Only enable on the 33k/10k optocoupler ADC-tag modification.
// Input is SDK readRawVoltage(): normal-divider reconstructed volts, NOT
// actual GPIO3 volts and NOT calibrated/estimated battery percentage.
namespace lilka {
constexpr float BATTERY_MIN_FULL_LEVEL_VOLTAGE = 3.5f;
constexpr float BATTERY_MAX_VALID_CHARGE_VOLTAGE = 4.6f;
enum class ChargeStatus { Unknown, Battery, Charging, Charged, Absent };

/// Coherent latest hardware sample and debounced charging state.
struct BatteryChargeSnapshot {
    ChargeStatus status = ChargeStatus::Unknown;
    ChargeStatus sampleStatus = ChargeStatus::Unknown;
    float rawVoltage = 0.0f;
    float batteryVoltage = 0.0f; // Last normal sample, never an optocoupler tag.
    int estimatedLevel = -1; // Last normal estimate; -1 until one exists.
    uint32_t updatedAt = 0;
};

class ChargeStatusFilter {
public:
    static ChargeStatus classify(float voltage) {
        // Guard bands around provisional boundaries suppress noisy transitions.
        if (!std::isfinite(voltage) || voltage < 0.0f || voltage > BATTERY_MAX_VALID_CHARGE_VOLTAGE)
            return ChargeStatus::Unknown;
        // Same absent-battery threshold as the SDK, confirmed by debounce.
        if (voltage < 0.5f) return ChargeStatus::Absent;
        if (voltage < 1.40f) return ChargeStatus::Charged;
        if (voltage > 1.50f && voltage < 2.65f) return ChargeStatus::Charging;
        if (voltage > 2.80f) return ChargeStatus::Battery;
        return ChargeStatus::Unknown;
    }

    ChargeStatus update(float voltage) {
        ChargeStatus next = classify(voltage);
        if (next == ChargeStatus::Unknown) {
            candidate = ChargeStatus::Unknown;
            count = 0;
            return confirmed;
        }
        if (next != candidate) {
            candidate = next;
            count = 1;
        } else if (count < 3) {
            ++count;
        }
        // Keep the last confirmed presentation through noisy/guard-band and
        // plug/unplug samples; publish only after three matching samples.
        if (count >= 3) confirmed = candidate;
        return confirmed;
    }

    void reset() {
        confirmed = ChargeStatus::Unknown;
        candidate = ChargeStatus::Unknown;
        count = 0;
    }

private:
    ChargeStatus confirmed = ChargeStatus::Unknown;
    ChargeStatus candidate = ChargeStatus::Unknown;
    uint8_t count = 0;
};
} // namespace lilka
