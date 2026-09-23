#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>

#include "battery.h"
#include "config.h"
#include "serial.h"

namespace lilka {

namespace {
constexpr char BATTERY_NVS_NAMESPACE[] = "battery";
constexpr char BATTERY_NVS_FULL_LEVEL_RAW_KEY[] = "fullRawAdc";
constexpr char BATTERY_NVS_VOLTAGE_OFFSET_KEY[] = "voltageOffsetMv";
constexpr char BATTERY_NVS_DISCHARGE_PROFILE_KEY[] = "profile";
constexpr int16_t BATTERY_MIN_VOLTAGE_OFFSET_MV = -500;
constexpr int16_t BATTERY_MAX_VOLTAGE_OFFSET_MV = 500;
constexpr float BATTERY_MIN_FULL_LEVEL_VOLTAGE = 3.5f;
constexpr uint16_t BATTERY_MAX_RAW_VALUE = 4095;
constexpr float BATTERY_LEVEL_ROUNDING_EPSILON = 0.0001f;

struct BatteryCurvePoint {
    float voltage;
    uint8_t level;
};

// Typical 1S LiPo discharge curve at moderate load. The curve is scaled to the
// configured full and empty voltages before use.
constexpr BatteryCurvePoint BATTERY_LEVEL_CURVE_TYPICAL[] = {
    {4.20f, 100},
    {4.15f, 95},
    {4.10f, 90},
    {4.00f, 80},
    {3.92f, 70},
    {3.86f, 60},
    {3.82f, 50},
    {3.79f, 40},
    {3.77f, 30},
    {3.73f, 20},
    {3.69f, 10},
    {3.60f, 5},
    {3.35f, 2},
    {3.20f, 0},
};

// A cell with a gradual low-voltage discharge keeps more usable capacity below
// 3.8 V. This avoids parking the indicator at 5-10% for a large part of runtime.
constexpr BatteryCurvePoint BATTERY_LEVEL_CURVE_SMOOTH[] = {
    {4.20f, 100},
    {4.15f, 95},
    {4.10f, 90},
    {4.00f, 80},
    {3.92f, 72},
    {3.86f, 65},
    {3.82f, 58},
    {3.79f, 52},
    {3.77f, 46},
    {3.73f, 38},
    {3.69f, 30},
    {3.60f, 18},
    {3.35f, 7},
    {3.20f, 0},
};

// A cell with a very long low-voltage plateau can retain substantial usable
// capacity below 3.6 V.
constexpr BatteryCurvePoint BATTERY_LEVEL_CURVE_VERY_SMOOTH[] = {
    {4.20f, 100},
    {4.15f, 96},
    {4.10f, 92},
    {4.00f, 85},
    {3.92f, 78},
    {3.86f, 70},
    {3.82f, 64},
    {3.79f, 58},
    {3.77f, 53},
    {3.73f, 47},
    {3.69f, 42},
    {3.60f, 35},
    {3.35f, 18},
    {3.20f, 0},
};

// A cell with a steep end-of-discharge drop has little usable capacity left at
// low voltage. This reaches the warning range earlier than the typical curve.
constexpr BatteryCurvePoint BATTERY_LEVEL_CURVE_SHARP[] = {
    {4.20f, 100},
    {4.15f, 95},
    {4.10f, 90},
    {4.00f, 78},
    {3.92f, 65},
    {3.86f, 52},
    {3.82f, 40},
    {3.79f, 30},
    {3.77f, 22},
    {3.73f, 14},
    {3.69f, 8},
    {3.60f, 3},
    {3.35f, 1},
    {3.20f, 0},
};

constexpr size_t BATTERY_LEVEL_CURVE_POINT_COUNT =
    sizeof(BATTERY_LEVEL_CURVE_TYPICAL) / sizeof(BATTERY_LEVEL_CURVE_TYPICAL[0]);
static_assert(
    sizeof(BATTERY_LEVEL_CURVE_SMOOTH) / sizeof(BATTERY_LEVEL_CURVE_SMOOTH[0]) == BATTERY_LEVEL_CURVE_POINT_COUNT,
    "Battery discharge profiles must have the same number of points"
);
static_assert(
    sizeof(BATTERY_LEVEL_CURVE_VERY_SMOOTH) / sizeof(BATTERY_LEVEL_CURVE_VERY_SMOOTH[0]) ==
        BATTERY_LEVEL_CURVE_POINT_COUNT,
    "Battery discharge profiles must have the same number of points"
);
static_assert(
    sizeof(BATTERY_LEVEL_CURVE_SHARP) / sizeof(BATTERY_LEVEL_CURVE_SHARP[0]) == BATTERY_LEVEL_CURVE_POINT_COUNT,
    "Battery discharge profiles must have the same number of points"
);
} // namespace

Battery::Battery() :
    emptyVoltage(LILKA_DEFAULT_EMPTY_VOLTAGE),
    fullVoltage(LILKA_DEFAULT_FULL_VOLTAGE),
    fullLevelRawValue(0),
    voltageOffsetMilliVolts(0),
    dischargeProfile(BatteryDischargeProfile::Smooth) {
}

void Battery::begin() {
#if LILKA_VERSION < 2
    serial.err("Battery is not supported in this version of Lilka");
#else
    pinMode(LILKA_BATTERY_ADC, INPUT_PULLDOWN); // If battery is not connected, the reading will be 0
    // adcX_config_channel_atten(adc1_channel_t channel, adc_atten_t atten)
    LILKA_BATTERY_ADC_FUNC(config_channel_atten)(LILKA_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11); // 0..3100mV
    // adcX_config_width(adc_width_t width)
    LILKA_BATTERY_ADC_FUNC(config_width)(ADC_WIDTH_BIT_12);

    uint16_t savedFullLevelRawValue = 0;
    Preferences prefs;
    if (prefs.begin(BATTERY_NVS_NAMESPACE, true)) {
        savedFullLevelRawValue = prefs.getUShort(BATTERY_NVS_FULL_LEVEL_RAW_KEY, 0);
        voltageOffsetMilliVolts = prefs.getShort(BATTERY_NVS_VOLTAGE_OFFSET_KEY, 0);
        uint8_t savedProfile =
            prefs.getUChar(BATTERY_NVS_DISCHARGE_PROFILE_KEY, static_cast<uint8_t>(BatteryDischargeProfile::Smooth));
        if (savedProfile <= static_cast<uint8_t>(BatteryDischargeProfile::VerySmooth)) {
            dischargeProfile = static_cast<BatteryDischargeProfile>(savedProfile);
        }
        prefs.end();
    }

    if (savedFullLevelRawValue <= BATTERY_MAX_RAW_VALUE &&
        rawValueToVoltage(savedFullLevelRawValue) >= BATTERY_MIN_FULL_LEVEL_VOLTAGE) {
        fullLevelRawValue = savedFullLevelRawValue;
    }
    voltageOffsetMilliVolts =
        constrain(voltageOffsetMilliVolts, BATTERY_MIN_VOLTAGE_OFFSET_MV, BATTERY_MAX_VOLTAGE_OFFSET_MV);
#endif
}

int Battery::readLevel() {
#if LILKA_VERSION < 2
    return -1;
#else
    // Preserve the original public API behavior for existing applications.
    float voltage = rawValueToVoltage(readRawValue());
    if (voltage < 0.5f) {
        return -1;
    }

    float maxVoltage =
        fullVoltage < LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE ? fullVoltage : LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE;
    float level = (voltage - emptyVoltage) * 100.0f / (maxVoltage - emptyVoltage);
    return constrain(level, 0, 100);
#endif
}

int Battery::readEstimatedLevel() {
#if LILKA_VERSION < 2
    return -1;
#else
    uint16_t rawValue = readRawValue();
    float rawVoltage = rawValueToVoltage(rawValue);
    if (rawVoltage < 0.5f) {
        return -1;
    }

    if (hasFullLevelCalibration()) {
        float fullLevelVoltage = rawValueToVoltage(fullLevelRawValue);
        float measuredRange = fullLevelVoltage - emptyVoltage;
        if (measuredRange > 0.0f) {
            float configuredRange = fullVoltage - emptyVoltage;
            float normalizedVoltage = emptyVoltage + (rawVoltage - emptyVoltage) * configuredRange / measuredRange;
            return levelFromVoltage(normalizedVoltage);
        }
    }

    return levelFromVoltage(rawVoltage + voltageOffsetMilliVolts / 1000.0f);
#endif
}

BatteryDischargeProfile Battery::getDischargeProfile() const {
    return dischargeProfile;
}

void Battery::setDischargeProfile(BatteryDischargeProfile profile) {
    if (profile > BatteryDischargeProfile::VerySmooth) {
        profile = BatteryDischargeProfile::Smooth;
    }
    dischargeProfile = profile;
#if LILKA_VERSION >= 2
    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, false);
    prefs.putUChar(BATTERY_NVS_DISCHARGE_PROFILE_KEY, static_cast<uint8_t>(dischargeProfile));
    prefs.end();
#endif
}

float Battery::readRawVoltage() {
#if LILKA_VERSION < 2
    return 0;
#else
    return rawValueToVoltage(readRawValue());
#endif
}

float Battery::readVoltage() {
    float voltage = readRawVoltage();
    if (voltage < 0.5f) {
        return voltage;
    }
    return voltage + voltageOffsetMilliVolts / 1000.0f;
}

bool Battery::calibrateFullLevel() {
#if LILKA_VERSION < 2
    return false;
#else
    uint16_t rawValue = readRawValue();
    if (rawValueToVoltage(rawValue) < BATTERY_MIN_FULL_LEVEL_VOLTAGE) {
        return false;
    }

    fullLevelRawValue = rawValue;
    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, false);
    prefs.putUShort(BATTERY_NVS_FULL_LEVEL_RAW_KEY, fullLevelRawValue);
    prefs.end();
    return true;
#endif
}

void Battery::resetFullLevelCalibration() {
    fullLevelRawValue = 0;
#if LILKA_VERSION >= 2
    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, false);
    prefs.remove(BATTERY_NVS_FULL_LEVEL_RAW_KEY);
    prefs.end();
#endif
}

bool Battery::hasFullLevelCalibration() const {
    return fullLevelRawValue != 0;
}

int16_t Battery::getVoltageOffsetMilliVolts() const {
    return voltageOffsetMilliVolts;
}

void Battery::setVoltageOffsetMilliVolts(int16_t offset) {
    voltageOffsetMilliVolts = constrain(offset, BATTERY_MIN_VOLTAGE_OFFSET_MV, BATTERY_MAX_VOLTAGE_OFFSET_MV);
#if LILKA_VERSION >= 2
    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, false);
    prefs.putShort(BATTERY_NVS_VOLTAGE_OFFSET_KEY, voltageOffsetMilliVolts);
    prefs.end();
#endif
}

void Battery::resetVoltageOffset() {
    voltageOffsetMilliVolts = 0;
#if LILKA_VERSION >= 2
    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, false);
    prefs.remove(BATTERY_NVS_VOLTAGE_OFFSET_KEY);
    prefs.end();
#endif
}

uint16_t Battery::readRawValue() {
#if LILKA_VERSION < 2
    return 0;
#else
    // Зчитуємо значення АЦП 32 рази, щоб вибрати медіану
    uint16_t count = 32;
    uint16_t values[count];
    for (int i = 0; i < count; i++) {
        values[i] = analogRead(LILKA_BATTERY_ADC);
    }
    // Сортуємо масив значень АЦП
    std::sort(values, values + count);
    // Вибираємо медіану
    uint16_t value = values[count / 2];
    return value;
#endif
}

float Battery::rawValueToVoltage(uint16_t value) const {
    return (float)value / BATTERY_MAX_RAW_VALUE * LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE;
}

int Battery::levelFromVoltage(float voltage) const {
    const BatteryCurvePoint* curve = BATTERY_LEVEL_CURVE_TYPICAL;
    switch (dischargeProfile) {
        case BatteryDischargeProfile::Smooth:
            curve = BATTERY_LEVEL_CURVE_SMOOTH;
            break;
        case BatteryDischargeProfile::VerySmooth:
            curve = BATTERY_LEVEL_CURVE_VERY_SMOOTH;
            break;
        case BatteryDischargeProfile::Sharp:
            curve = BATTERY_LEVEL_CURVE_SHARP;
            break;
        case BatteryDischargeProfile::Typical:
        default:
            break;
    }
    const float defaultRange = LILKA_DEFAULT_FULL_VOLTAGE - LILKA_DEFAULT_EMPTY_VOLTAGE;
    const float configuredRange = fullVoltage - emptyVoltage;

    auto scaleVoltage = [this, defaultRange, configuredRange](float curveVoltage) {
        return emptyVoltage + (curveVoltage - LILKA_DEFAULT_EMPTY_VOLTAGE) * configuredRange / defaultRange;
    };

    if (voltage >= scaleVoltage(curve[0].voltage)) {
        return curve[0].level;
    }

    for (size_t i = 1; i < BATTERY_LEVEL_CURVE_POINT_COUNT; i++) {
        float higherVoltage = scaleVoltage(curve[i - 1].voltage);
        float lowerVoltage = scaleVoltage(curve[i].voltage);
        if (voltage >= lowerVoltage) {
            float range = higherVoltage - lowerVoltage;
            float position = (voltage - lowerVoltage) / range;
            float level = curve[i].level + position * (curve[i - 1].level - curve[i].level);
            return ceilf(level - BATTERY_LEVEL_ROUNDING_EPSILON);
        }
    }

    return curve[BATTERY_LEVEL_CURVE_POINT_COUNT - 1].level;
}

void Battery::setEmptyVoltage(float voltage) {
    // Встановлюємо напругу акумулятора, при якій вважаємо його порожнім.
    emptyVoltage = voltage;
}

void Battery::setFullVoltage(float voltage) {
    // Встановлюємо напругу акумулятора, при якій вважаємо його повністю зарядженим.
    fullVoltage = voltage;
}

Battery battery;

} // namespace lilka
