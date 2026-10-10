#include "settings_persistence.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

namespace lilka {
namespace detail {
namespace {
SettingsPersistence::Callback clients[3]{};
portMUX_TYPE clientsMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<TaskHandle_t> worker{nullptr};

void save(void*) {
    TickType_t wait = portMAX_DELAY;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, wait);
        SettingsPersistence::Callback snapshot[3];
        portENTER_CRITICAL(&clientsMux);
        for (unsigned i = 0; i < 3; ++i)
            snapshot[i] = clients[i];
        portEXIT_CRITICAL(&clientsMux);
        uint32_t delay = 0;
        for (auto callback : snapshot) {
            if (!callback) continue;
            const uint32_t next = callback();
            if (next && (!delay || next < delay)) delay = next;
        }
        wait = delay ? pdMS_TO_TICKS(delay) : portMAX_DELAY;
        if (delay && !wait) wait = 1;
    }
}
} // namespace

bool SettingsPersistence::begin(unsigned slot, Callback callback) {
    if (slot >= 3 || !callback) return false;
    // SDK initialization normally serializes registration; protect lazy creation
    // as well for callers that initialize settings from separate owner tasks.
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!worker.load()) {
        TaskHandle_t task = nullptr;
        if (xTaskCreate(save, "settingsSave", 3072, nullptr, 1, &task) != pdPASS) {
            xSemaphoreGive(mutex);
            return false;
        }
        worker.store(task);
    }
    portENTER_CRITICAL(&clientsMux);
    clients[slot] = callback;
    portEXIT_CRITICAL(&clientsMux);
    xSemaphoreGive(mutex);
    notify(); // Catch changes made before registration.
    return true;
}

void SettingsPersistence::notify() {
    const TaskHandle_t task = worker.load();
    if (task) xTaskNotifyGive(task);
}

} // namespace detail
} // namespace lilka
