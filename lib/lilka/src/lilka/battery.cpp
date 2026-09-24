#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "battery.h"
#include "config.h"
#include "serial.h"

namespace lilka {

namespace {
constexpr char BATTERY_NVS_NAMESPACE[] = "battery";
constexpr char BATTERY_NVS_FULL_LEVEL_RAW_KEY[] = "fullRawAdc";
constexpr char BATTERY_NVS_DISCHARGE_PROFILE_KEY[] = "profile";
constexpr float BATTERY_MIN_FULL_LEVEL_VOLTAGE = 3.5f;
constexpr uint16_t BATTERY_MAX_RAW_VALUE = 4095;
constexpr float BATTERY_LEVEL_ROUNDING_EPSILON = 0.0001f;
constexpr uint32_t BATTERY_ADC_DEFAULT_VREF_MV = 1100;

esp_adc_cal_characteristics_t batteryAdcCharacteristics;

// Provisional voltage anchors inferred from a nine-hour Doom discharge using
// the previous Very smooth profile. Percentages target usable runtime, not a
// measured cell-capacity curve. All variants preserve the same endpoints.
constexpr float BATTERY_CURVE_VOLTAGES[] =
    {4.200f, 4.000f, 3.853f, 3.730f, 3.639f, 3.571f, 3.541f, 3.482f, 3.379f, 3.200f};
constexpr uint8_t BATTERY_CURVE_LEVELS[][10] = {
    {100, 81, 69, 61, 53, 44, 33, 22, 11, 0}, // Sharp top
    {100, 94, 86, 74, 59, 44, 33, 22, 11, 0}, // Smooth top
    {100, 89, 78, 67, 56, 44, 36, 28, 18, 0}, // Sharp bottom
    {100, 89, 78, 67, 56, 44, 33, 22, 11, 0}, // Normal
    {100, 89, 78, 67, 56, 44, 30, 16, 5, 0}, // Smooth bottom
    {100, 81, 69, 61, 53, 44, 36, 28, 18, 0}, // Sharp ends
    {100, 94, 86, 74, 59, 44, 30, 16, 5, 0}, // Smooth ends
};
constexpr size_t BATTERY_LEVEL_CURVE_POINT_COUNT = sizeof(BATTERY_CURVE_VOLTAGES) / sizeof(BATTERY_CURVE_VOLTAGES[0]);
static_assert(
    sizeof(BATTERY_CURVE_LEVELS) / sizeof(BATTERY_CURVE_LEVELS[0]) ==
        static_cast<uint8_t>(BatteryDischargeProfile::SmoothEnds) + 1,
    "Battery discharge profiles must have a curve for each persisted value"
);
static_assert(
    sizeof(BATTERY_CURVE_LEVELS[0]) / sizeof(BATTERY_CURVE_LEVELS[0][0]) == BATTERY_LEVEL_CURVE_POINT_COUNT,
    "Battery discharge profiles must have the same number of points"
);
} // namespace

Battery::Battery() :
    emptyVoltage(LILKA_DEFAULT_EMPTY_VOLTAGE),
    fullVoltage(LILKA_DEFAULT_FULL_VOLTAGE),
    fullLevelRawValue(0),
    dischargeProfile(BatteryDischargeProfile::Normal) {
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
    esp_adc_cal_characterize(
        ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, BATTERY_ADC_DEFAULT_VREF_MV, &batteryAdcCharacteristics
    );

    uint16_t savedFullLevelRawValue = 0;
    Preferences prefs;
    if (prefs.begin(BATTERY_NVS_NAMESPACE, true)) {
        savedFullLevelRawValue = prefs.getUShort(BATTERY_NVS_FULL_LEVEL_RAW_KEY, 0);
        uint8_t savedProfile =
            prefs.getUChar(BATTERY_NVS_DISCHARGE_PROFILE_KEY, static_cast<uint8_t>(BatteryDischargeProfile::Normal));
        if (savedProfile <= static_cast<uint8_t>(BatteryDischargeProfile::SmoothEnds)) {
            dischargeProfile = static_cast<BatteryDischargeProfile>(savedProfile);
        }
        prefs.end();
    }

    if (savedFullLevelRawValue <= BATTERY_MAX_RAW_VALUE &&
        rawValueToVoltage(savedFullLevelRawValue) >= BATTERY_MIN_FULL_LEVEL_VOLTAGE) {
        fullLevelRawValue = savedFullLevelRawValue;
    }
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

    return levelFromVoltage(rawVoltage);
#endif
}

BatteryDischargeProfile Battery::getDischargeProfile() const {
    return dischargeProfile;
}

void Battery::setDischargeProfile(BatteryDischargeProfile profile) {
    if (profile > BatteryDischargeProfile::SmoothEnds) {
        profile = BatteryDischargeProfile::Normal;
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
    return readRawVoltage();
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
    float adcVoltage = esp_adc_cal_raw_to_voltage(value, &batteryAdcCharacteristics) / 1000.0f;
    return adcVoltage / LILKA_BATTERY_VOLTAGE_DIVIDER;
}

int Battery::levelFromVoltage(float voltage) const {
    const uint8_t* levels = BATTERY_CURVE_LEVELS[static_cast<uint8_t>(dischargeProfile)];
    const float defaultRange = LILKA_DEFAULT_FULL_VOLTAGE - LILKA_DEFAULT_EMPTY_VOLTAGE;
    const float configuredRange = fullVoltage - emptyVoltage;

    auto scaleVoltage = [this, defaultRange, configuredRange](float curveVoltage) {
        return emptyVoltage + (curveVoltage - LILKA_DEFAULT_EMPTY_VOLTAGE) * configuredRange / defaultRange;
    };

    if (voltage >= scaleVoltage(BATTERY_CURVE_VOLTAGES[0])) {
        return levels[0];
    }

    for (size_t i = 1; i < BATTERY_LEVEL_CURVE_POINT_COUNT; i++) {
        float higherVoltage = scaleVoltage(BATTERY_CURVE_VOLTAGES[i - 1]);
        float lowerVoltage = scaleVoltage(BATTERY_CURVE_VOLTAGES[i]);
        if (voltage >= lowerVoltage) {
            float range = higherVoltage - lowerVoltage;
            float position = (voltage - lowerVoltage) / range;
            float level = levels[i] + position * (levels[i - 1] - levels[i]);
            return ceilf(level - BATTERY_LEVEL_ROUNDING_EPSILON);
        }
    }

    return levels[BATTERY_LEVEL_CURVE_POINT_COUNT - 1];
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
