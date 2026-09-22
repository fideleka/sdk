#ifndef LILKA_BATTERY_H
#define LILKA_BATTERY_H

#include <stdint.h>

namespace lilka {

/// Номінальне значення напруги LiPo акумулятора, при якій вважається, що він порожній.
#define LILKA_DEFAULT_EMPTY_VOLTAGE 3.2
/// Номінальне значення напруги LiPo акумулятора, при якій вважається, що він повністю заряджений.
#define LILKA_DEFAULT_FULL_VOLTAGE 4.2

// Батарея підключена до ADC0 через дільник напруги (33 КОм і 100 КОм)
#define LILKA_BATTERY_VOLTAGE_DIVIDER (100.0 / (33.0 + 100.0))

// Напруга акумулятора, при якій ми отримуємо максимальне значення АЦП
// Будь-яка напруга, яка є вищою, буде вважатися 100%
#define LILKA_BATTERY_MAX_MEASURABLE_VOLTAGE 3.1 / LILKA_BATTERY_VOLTAGE_DIVIDER // 4.123 V

/// Клас для вимірювання рівня заряду акумулятора.
/// Використовується для вимірювання напруги на акумуляторі через дільник напруги.
/// Вимірювання проводиться через ADC.
///
/// \note Цей клас не потрібно створювати вручну, оскільки він вже створений за замовчуванням і доступний як `lilka::battery`.
///
/// Приклад використання:
/// @code
/// #include <lilka.h>
///
/// void setup() {
///     lilka::begin();
/// }
///
/// void loop() {
///     int batteryLevel = lilka::battery.readLevel();
///     Serial.println("Заряд батареї: " + String(batteryLevel) + "%");
///     delay(1000);
/// }
/// @endcode
class Battery {
public:
    Battery();
    /// Почати вимірювання рівня заряду акумулятора.
    /// \warning Цей метод викликається автоматично при виклику `lilka::begin()`.
    void begin();
    /// Прочитати рівень заряду акумулятора.
    /// \return Рівень заряду акумулятора від 0 до 100. Якщо акумулятор відсутній, повертається -1.
    ///
    /// \note Цей метод зберігає історичну лінійну модель рівня заряду та не
    /// враховує калібровки. Використовуйте readEstimatedLevel() для нової
    /// оцінки за LiPo-кривою.
    int readLevel();
    /// Прочитати сире значення АЦП напруги акумулятора.
    /// \return Значення АЦП напруги акумулятора від 0 до 4095.
    /// \see readLevel
    uint16_t readRawValue();

    /// Прочитати напругу акумулятора, розраховану з сирого значення АЦП.
    ///
    /// Значення не враховує ручну корекцію напруги.
    float readRawVoltage();
    /// Прочитати напругу акумулятора з урахуванням ручної корекції.
    float readVoltage();

    /// Прочитати оцінений рівень заряду за типовою LiPo-кривою.
    ///
    /// Враховує калібровку повного заряду, якщо вона збережена. До такої
    /// калібровки використовує ручну корекцію напруги.
    /// \return Рівень заряду акумулятора від 0 до 100. Якщо акумулятор відсутній, повертається -1.
    int readEstimatedLevel();

    /// Зберегти поточне значення АЦП як рівень повного заряду.
    ///
    /// Викликайте цей метод лише коли акумулятор повністю заряджений і USB відключено.
    /// Калібровка впливає на readEstimatedLevel(), але не на readVoltage().
    /// \return true, якщо значення було збережено.
    bool calibrateFullLevel();
    /// Скинути збережену калібровку рівня повного заряду.
    void resetFullLevelCalibration();
    /// Перевірити, чи збережено калібровку рівня повного заряду.
    bool hasFullLevelCalibration() const;

    /// Отримати ручну корекцію напруги у мілівольтах.
    int16_t getVoltageOffsetMilliVolts() const;
    /// Встановити ручну корекцію напруги у мілівольтах.
    ///
    /// Корекція застосовується до readVoltage(). Вона застосовується до readEstimatedLevel()
    /// лише якщо рівень повного заряду ще не відкалібровано.
    void setVoltageOffsetMilliVolts(int16_t offset);
    /// Скинути ручну корекцію напруги.
    void resetVoltageOffset();

    /// Встановити напругу акумулятора, при якій він вважається порожнім.
    /// За замовчуванням використовується значення `LILKA_DEFAULT_EMPTY_VOLTAGE`.
    void setEmptyVoltage(float voltage);
    /// Встановити напругу акумулятора, при якій він вважається повним.
    /// За замовчуванням використовується значення `LILKA_DEFAULT_FULL_VOLTAGE`.
    void setFullVoltage(float voltage);

private:
    float emptyVoltage;
    float fullVoltage;
    uint16_t fullLevelRawValue;
    int16_t voltageOffsetMilliVolts;

    float rawValueToVoltage(uint16_t value) const;
    int levelFromVoltage(float voltage) const;
};

/// Екземпляр класу `Battery`, який можна використовувати для вимірювання рівня заряду акумулятора.
/// Вам не потрібно інстанціювати `Battery` вручну.
extern Battery battery;

} // namespace lilka

#endif
