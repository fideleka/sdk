# Battery monitoring regression checks

Run `python3 tests/battery_calibration/run.py` from the SDK repository.

The harness compiles the real battery worker for stock/modified v2 and v1,
both normally and with AddressSanitizer/UndefinedBehaviorSanitizer. It covers
charge transitions, the 30-second unplug stabilization period, persistence
failures, coherent snapshots, ADC failures and task creation failure.

The charge worker reserves **8192 bytes** of stack. ESP-IDF's `xTaskCreate`
argument is measured in bytes, not vanilla FreeRTOS stack words. The earlier
3072-byte allocation overflowed on a device running Lilplayer's source-radio
SDK. Driver/NVS/logging calls must fit in the caller's stack, including the
radio SDK logger's local formatting buffers.

Sampling uses the IDF ADC channel configured by `Battery::begin()`, avoiding
Arduino's lazy ADC attachment and pin configuration/logging inside the worker.
The harness deliberately has no `analogRead` stub, so reintroducing that path
breaks compilation. Negative or out-of-range driver results are not accepted as
calibration samples. Valid readings retain the same 32-sample median and voltage
conversion.

Host tests do **not** measure the ESP32 task's actual stack use. Device acceptance
requires booting the rebuilt source-radio Lilplayer and exercising charging,
charged/unplugged calibration and logging without a `batteryCharge` panic.
Target stack high-water measurements and full firmware builds are separate checks.
