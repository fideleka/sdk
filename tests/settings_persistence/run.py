"""Test real lazy notification worker: one stack, deadline waits, no idle polling."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
mock = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <vector>
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
using TickType_t = uint32_t;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(x) (x)
constexpr int pdPASS = 1, pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
extern unsigned tasks, notifications;
extern uint32_t now;
extern bool fail;
extern void (*entry)(void*);
extern std::vector<uint32_t> waits;
struct Idle {};
inline void portENTER_CRITICAL(int*) {}
inline void portEXIT_CRITICAL(int*) {}
inline void* xSemaphoreCreateMutex() { return reinterpret_cast<void*>(1); }
inline void xSemaphoreTake(void*, int) {}
inline void xSemaphoreGive(void*) {}
inline int xTaskCreate(void(*fn)(void*), const char*, unsigned stack, void*, int, void** handle) {
    if (fail) return 0;
    assert(stack == 3072);
    ++tasks; entry = fn; *handle = reinterpret_cast<void*>(1); return pdPASS;
}
inline void xTaskNotifyGive(void*) { ++notifications; }
inline uint32_t ulTaskNotifyTake(int clear, TickType_t wait) {
    assert(clear == pdTRUE); waits.push_back(wait);
    if (notifications) { notifications = 0; return 1; }
    if (wait == portMAX_DELAY) throw Idle{};
    now += wait; return 0;
}
'''
test = r'''
#include "mock.h"
#include "settings_persistence.h"
unsigned tasks = 0, notifications = 0;
uint32_t now = 0;
bool fail = false;
void (*entry)(void*) = nullptr;
std::vector<uint32_t> waits;
using Saver = lilka::detail::SettingsPersistence;
uint32_t first() { return now < 600 ? 600 - now : 0; }
uint32_t second() { return now < 1000 ? 1000 - now : 0; }
int main() {
    assert(!Saver::begin(3, first) && tasks == 0);
    fail = true;
    assert(!Saver::begin(0, first));
    fail = false;
    assert(Saver::begin(0, first));
    assert(Saver::begin(1, second));
    assert(Saver::begin(0, first) && tasks == 1);
    try { entry(nullptr); } catch (Idle&) {}
    assert(waits == std::vector<uint32_t>({portMAX_DELAY, 600, 400, portMAX_DELAY}));
    assert(now == 1000 && notifications == 0);
    Saver::notify();
    assert(notifications == 1);
}
'''
with tempfile.TemporaryDirectory(prefix='settings-worker-') as directory:
    tmp = Path(directory)
    for name in ('settings_persistence.cpp', 'settings_persistence.h'):
        (tmp / name).write_text((root / 'lib/lilka/src/lilka' / name).read_text())
    (tmp / 'mock.h').write_text(mock)
    (tmp / 'test.cpp').write_text(test)
    for name in ('freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/semphr.h'):
        path = tmp / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "mock.h"\n')
    for sanitized in (False, True):
        flags = ['-fsanitize=address,undefined', '-fno-pie', '-no-pie'] if sanitized else []
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I' + str(tmp), str(tmp / 'settings_persistence.cpp'), str(tmp / 'test.cpp'),
                        '-o', str(tmp / 'test')], check=True)
        subprocess.run([str(tmp / 'test')], check=True)
print('Shared settings worker: creation retry, single task, notifications, earliest deadline, indefinite clean sleep PASS')
