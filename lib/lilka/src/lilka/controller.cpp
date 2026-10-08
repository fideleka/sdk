#include <Arduino.h>

#include <driver/uart.h>

#include "serial.h"
#include "controller.h"
#include "audio.h"
#include "brightness.h"
#include "display_settings.h"

namespace lilka {

class AcquireController {
public:
    explicit AcquireController(SemaphoreHandle_t semaphore) {
        this->semaphore = semaphore;
        xSemaphoreTakeRecursive(semaphore, portMAX_DELAY);
    }
    ~AcquireController() {
        xSemaphoreGiveRecursive(semaphore);
    }

private:
    SemaphoreHandle_t semaphore;
};

Controller::Controller() : state{}, semaphore(xSemaphoreCreateRecursiveMutex()) {
    for (int i = 0; i < Button::COUNT; i++) {
        _StateButtons& buttons = *reinterpret_cast<_StateButtons*>(&state);

        buttons[i] = (ButtonState){
            .pressed = false,
            .justPressed = false,
            .justReleased = false,
            .time = 0,
            .nextRepeatTime = 0,
            .repeatRate = 0,
            .repeatDelay = 0,
        };
    }
    xSemaphoreGive(semaphore);
    clearHandlers();
}

int Controller::scanInputs(uint16_t rawPressed, uint32_t now) {
    AcquireController acquire(semaphore);
    displaySettings.noteInput(rawPressed, now);
    if (displaySettings.isSleeping() || displaySettings.wakePending() || wakeSuppressed) {
        wakeSuppressed |= rawPressed;
    }
    const uint16_t hiddenWake = wakeSuppressed;
    if (!rawPressed) wakeSuppressed = 0;
    rawPressed &= ~hiddenWake; // Consume the wake gesture through physical release.
    // Debounce a complete physical snapshot BEFORE chord arbitration or dispatch.
    // Visible state cannot serve as physical history: consumed buttons stay invisible.
    for (int i = 0; i < Button::ANY; ++i) {
        const uint16_t bit = 1 << i;
        if (now - physicalTime[i] >= LILKA_DEBOUNCE_TIME && ((rawPressed ^ physicalPressed) & bit)) {
            physicalPressed ^= bit;
            physicalTime[i] = now;
        }
    }
    // A raw Start press wins even during its debounce window. A raw Select
    // release stops adjustments immediately, without changing normal Select events.
    uint16_t shortcutPressed = physicalPressed | (rawPressed & (1 << START));
    auto adjustment = shortcuts.scan(
        shortcutPressed, now, systemShortcutsEnabled, rawPressed & (1 << SELECT), brightness.isEnabled()
    );
    brightnessSteps = adjustment.brightnessSteps;
    _StateButtons& buttons = *reinterpret_cast<_StateButtons*>(&state);
    state.selectHeld = physicalPressed & (1 << SELECT);
    state.selectConsumed = adjustment.selectConsumed;
    state.any.pressed = false;
    uint16_t changed = 0;
    for (int i = 0; i < Button::ANY; ++i) {
        ButtonState& button = buttons[i];
        if (adjustment.suppressed & (1 << i)) {
            // Pair the immediately delivered Select press with cancellation.
            // Callbacks run only after publishing the complete snapshot.
            if (i == SELECT && button.pressed) changed |= 1 << i;
            button.pressed = false;
            button.justPressed = false;
            button.justReleased = false;
            button.nextRepeatTime = 0;
            continue;
        }
        const bool pressed = physicalPressed & (1 << i);
        const bool shouldRepeat = pressed && button.nextRepeatTime && now >= button.nextRepeatTime;
        if (pressed != button.pressed || shouldRepeat) {
            button.pressed = pressed;
            button.justPressed = pressed;
            button.justReleased = !pressed;
            state.any.justPressed = state.any.justPressed || pressed;
            state.any.justReleased = state.any.justReleased || !pressed;
            changed |= 1 << i;
            button.time = now;
        }
        state.any.pressed = state.any.pressed || pressed;
        if (pressed && button.repeatRate && button.repeatDelay) {
            if (button.nextRepeatTime == 0) {
                button.nextRepeatTime = now + button.repeatDelay;
            } else if (shouldRepeat) {
                button.nextRepeatTime += 1000 / button.repeatRate;
            }
        } else if (!pressed) {
            button.nextRepeatTime = 0;
        }
    }
    // Publish the complete visible snapshot before callbacks. A handler that peeks
    // Select/Start must not see a half-dispatched simultaneous scan.
    for (int i = 0; i < Button::ANY; ++i) {
        if (!(changed & (1 << i))) continue;
        const bool pressed = buttons[i].pressed;
        if (handlers[i] != NULL) handlers[i](pressed);
        if (globalHandler != NULL) globalHandler((Button)i, pressed);
    }
    // Modified hardware only; PWM/NVS updates are outside the controller mutex.
    return adjustment.volumeSteps * 5;
}

void Controller::inputTask() {
    while (1) {
        uint16_t rawPressed = 0;
        for (int i = 0; i < Button::ANY; ++i) {
            if (pins[i] >= 0 && !digitalRead(pins[i])) rawPressed |= 1 << i;
        }
        const int volumeDelta = scanInputs(rawPressed, millis());
        // Atomic RAM-only update, outside controller mutex. Persistence is serviced
        // on the audio settings task, never in this scan or an application callback.
        if (volumeDelta) audio.stepVolumeShortcut(volumeDelta);
        if (brightnessSteps) brightness.stepBrightnessShortcut(brightnessSteps);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void Controller::setSystemShortcutsEnabled(bool enabled) {
    AcquireController acquire(semaphore);
    systemShortcutsEnabled = enabled;
}

void Controller::resetState() {
    AcquireController acquire(semaphore);
    for (int i = 0; i < Button::COUNT; i++) {
        _StateButtons& buttons = *reinterpret_cast<_StateButtons*>(&state);
        ButtonState* buttonState = &buttons[i];
        buttonState->justPressed = false;
        buttonState->justReleased = false;
    }
}

void Controller::begin() {
    serial.log("initializing controller");

#if LILKA_VERSION == 1
    // Detach UART from GPIO20 & GPIO21 to use them as normal IOs
    // https://esp32developer.com/programming-in-c-c/console/using-uart0-disable-logging-output
    esp_log_level_set("*", ESP_LOG_NONE); // DISABLE ESP32 LOGGING ON UART0
    if (uart_driver_delete(UART_NUM_0) != ESP_OK) {
        serial.err("failed to detach UART0");
    }
    gpio_reset_pin(GPIO_NUM_20);
    gpio_reset_pin(GPIO_NUM_21);
#endif

    for (int i = 0; i < Button::COUNT; i++) {
        if (pins[i] < 0) {
            continue;
        }
        pinMode(pins[i], INPUT_PULLUP);
    }

    // Create RTOS task for handling button presses
    xTaskCreate([](void* arg) { static_cast<Controller*>(arg)->inputTask(); }, "input", 2048, this, 1, NULL);

    serial.log("controller ready");
}

State Controller::getState() {
    AcquireController acquire(semaphore);
    State _current = state;
    resetState();
    return _current;
}

State Controller::peekState() {
    AcquireController acquire(semaphore);
    return state;
}

void Controller::setGlobalHandler(void (*handler)(Button, bool)) {
    AcquireController acquire(semaphore);
    globalHandler = handler;
}

void Controller::setHandler(Button button, void (*handler)(bool)) {
    AcquireController acquire(semaphore);
    handlers[button] = handler;
}

void Controller::clearHandlers() {
    AcquireController acquire(semaphore);
    for (int i = 0; i < Button::COUNT; i++) {
        handlers[i] = NULL;
    }
    globalHandler = NULL;
}

void Controller::setAutoRepeat(Button button, uint32_t rate, uint32_t delay) {
    AcquireController acquire(semaphore);
    _StateButtons& buttons = *reinterpret_cast<_StateButtons*>(&state);
    ButtonState* buttonState = &buttons[button];
    buttonState->repeatRate = rate;
    buttonState->repeatDelay = delay;
}

Controller controller;

} // namespace lilka
