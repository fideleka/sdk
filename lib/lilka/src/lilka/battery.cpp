#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>

#include "battery.h"
#include "config.h"
#include "serial.h"

namespace lilka {

#define fmap(x, in_min, in_max, out_min, out_max) (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
#define fmin(a, b)                                ((a) < (b) ? (a) : (b))

namespace {
constexpr char BATTERY_NVS_NAMESPACE[] = "lilka-battery";
constexpr char BATTERY_NVS_VOLTAGE_OFFSET_KEY[] = "voltageOffsetMv";
constexpr int16_t BATTERY_MIN_VOLTAGE_OFFSET_MV = -500;
constexpr int16_t BATTERY_MAX_VOLTAGE_OFFSET_MV = 500;
} // namespace

Battery::Battery()
    : emptyVoltage(LILKA_DEFAULT_EMPTY_VOLTAGE), fullVoltage(LILKA_DEFAULT_FULL_VOLTAGE), voltageOffsetMilliVolts(0) {
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

    Preferences prefs;
    prefs.begin(BATTERY_NVS_NAMESPACE, true);
    voltageOffsetMilliVolts = prefs.getShort(BATTERY_NVS_VOLTAGE_OFFSET_KEY, 0);
    prefs.end();
#endif
}

int Battery::readLevel() {
#if LILKA_VERSION < 2
    return -1;
#else
    // АЦП може зчитувати напругу від 0 до 3.1V.
    // Напруга акумулятора проходить через дільник напруги (33 КОм і 100 КОм, визначений як LILKA_BATTERY_VOLTAGE_DIVIDER).
    // Але при повністю зарядженому акумуляторі (4.2V) напруга на АЦП може бути трохи вищою за максимальне читабельне значення (3.158V замість 3.1V).
    // Тому ми сприймаємо таке перевищення як "100%".

    float uncalibratedVoltage = readUncalibratedVoltage();
    if (uncalibratedVoltage < 0.5) {
        return -1;
    }
    float voltage = uncalibratedVoltage + voltageOffsetMilliVolts / 1000.0f;

    // Максимальна напруга акумулятора, яку ми можемо виміряти
    float maxVoltage = fmin(
        fullVoltage, LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE + voltageOffsetMilliVolts / 1000.0f
    );

    // Інтерполюємо діапазон [emptyValue;maxVoltage] в діапазон [0;100]
    float level = fmap(voltage, emptyVoltage, maxVoltage, 0, 100);
    return constrain(level, 0, 100);
#endif
}

float Battery::readVoltage() {
#if LILKA_VERSION < 2
    return 0;
#else
    float voltage = readUncalibratedVoltage();
    if (voltage < 0.5) {
        return voltage;
    }
    return voltage + voltageOffsetMilliVolts / 1000.0f;
#endif
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

float Battery::readUncalibratedVoltage() {
    uint16_t value = readRawValue();
    return (float)value / 4095.0 * LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE;
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
