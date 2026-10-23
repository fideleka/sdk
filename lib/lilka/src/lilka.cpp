#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>

#include "lilka.h"

namespace lilka {

void begin() {
    serial.begin();
    board.begin(false);
    multiboot.begin();
    buzzer.begin(); // Play notification sound
    audio.begin();
    spi_begin();
    fileutils.begin();
    display.begin(); // Must be initialized AFTER SD card
    controller.begin(); // Must be initialized AFTER SD card (since SD card somehow messes with GPIO)
    devices.begin(gpio_get_level(static_cast<gpio_num_t>(LILKA_GPIO_SELECT)) == 0);
    display.enableRouting();
    // Discovery may fall back without activating a driver. Release the boot
    // backlight hold only after black initialization and final route selection.
    brightness.setActiveDisplays(devices.internalOutput(), devices.externalOutput());
    brightness.resume();
#if LILKA_VERSION == 2 && LILKA_NATIVE_PANEL_RAW_DIAGNOSTIC
    display.showNativeGrayDiagnostic();
#endif
    displaySettings.begin();
    battery.begin();
    // TODO: I2S
    // TODO: Delete Task Watchdog Timer - we'll be running long tasks
    // TODO: Maybe keep it?
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if (idle_0 != NULL) {
        esp_task_wdt_delete(idle_0);
    }
#if LILKA_VERSION > 1
    TaskHandle_t idle_1 = xTaskGetIdleTaskHandleForCPU(1);
    if (idle_1 != NULL) {
        esp_task_wdt_delete(idle_1);
    }
#endif
}

} // namespace lilka
