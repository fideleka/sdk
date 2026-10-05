#include "host.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#define private public
#include "controller.h"
#include "audio.h"
#undef private
#include "config.h"
#include "ping.h"

uint32_t hostNow = 0, hostStoredVolume = 50;
int hostNextMutex = 0, hostLocks[64] = {}, hostReads = 0, hostWrites = 0, hostTaskCount = 0;
bool hostHotScan = false, hostWriteFailure = false;
void (*hostWriteHook)() = nullptr;
void (*hostStartupTask)(void*) = nullptr;
MockI2S I2S;
namespace lilka { MockSerial serial; }
using namespace lilka;

// Deterministic interleavings: controller config after Audio.begin; app acquire
// before delayed playback, or attempt while startup owns the port. No preemption.
enum Owner { FREE, STARTUP, APP };
Owner owner = FREE;
int installs, uninstalls, pinCalls, zeroCalls, writeCalls, deleted;
int failInstall, failPins, failZero, failWriteAt;
bool zeroWrite, oversizedWrite, partialWrite, appDuringDelay, appDuringWrite, changeGain;
bool selectPullup = false;
int selectMode = 0;
std::vector<uint8_t> output;
Controller* activeController = nullptr;
void pinMode(int pin, int mode) {
    if (pin == 0) { assert(mode == INPUT_PULLUP); selectPullup = true; selectMode = mode; }
}
int digitalRead(int pin) { assert(pin != 0 || selectPullup); return 1; }
void checkController() {
    assert(selectPullup && selectMode == INPUT_PULLUP && digitalRead(0) == 1);
    hostNow += 20;
    activeController->scanInputs(0, hostNow);
    assert(!activeController->peekState().select.pressed);
}
void vTaskDelay(int ticks) {
    assert(ticks == 400);
    checkController();
    if (appDuringDelay) owner = APP;
}
void vTaskDelete(void*) {
    assert(owner != STARTUP); // Cleanup must precede task deletion.
    ++deleted;
    checkController();
}
namespace esp_i2s {
int i2s_driver_install(i2s_port_t port, const i2s_config_t* config, int queue, void* handle) {
    assert(port == I2S_NUM_0 && !queue && !handle);
    assert(config->mode == (I2S_MODE_MASTER | I2S_MODE_TX));
    assert(config->sample_rate == 22050 && config->bits_per_sample == I2S_BITS_PER_SAMPLE_16BIT);
    assert(config->channel_format == I2S_CHANNEL_FMT_RIGHT_LEFT);
    assert(config->communication_format == I2S_COMM_FORMAT_STAND_I2S);
    assert(config->dma_buf_count == 4 && config->dma_buf_len == 128 && config->tx_desc_auto_clear);
    assert(!config->use_apll && !config->fixed_mclk);
    ++installs;
    if (owner != FREE || failInstall) return -1;
    owner = STARTUP;
    return ESP_OK;
}
int i2s_driver_uninstall(i2s_port_t) {
    assert(owner == STARTUP); // Never release an app-owned port.
    owner = FREE;
    ++uninstalls;
    checkController();
    return ESP_OK;
}
int i2s_set_pin(i2s_port_t, const i2s_pin_config_t* pins) {
    assert(owner == STARTUP);
    assert(pins->mck_io_num == -1 && pins->data_in_num == -1);
    assert(pins->bck_io_num == LILKA_I2S_BCLK && pins->ws_io_num == LILKA_I2S_LRCK);
    assert(pins->data_out_num == LILKA_I2S_DOUT);
    assert(pins->bck_io_num != 0 && pins->ws_io_num != 0 && pins->data_out_num != 0);
    ++pinCalls;
    checkController();
    return failPins ? -1 : ESP_OK;
}
int i2s_zero_dma_buffer(i2s_port_t) {
    assert(owner == STARTUP);
    ++zeroCalls;
    return failZero ? -1 : ESP_OK;
}
int i2s_write(i2s_port_t port, const void* data, size_t bytes, size_t* written, int wait) {
    assert(port == I2S_NUM_0 && owner == STARTUP && bytes <= 512 && wait == 100);
    ++writeCalls;
    checkController();
    if (appDuringWrite && writeCalls == 1) {
        i2s_config_t config = {};
        config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
        config.sample_rate = 22050; config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        config.dma_buf_count = 4; config.dma_buf_len = 128; config.tx_desc_auto_clear = true;
        assert(i2s_driver_install(port, &config, 0, nullptr) != ESP_OK);
        assert(owner == STARTUP);
    }
    if (writeCalls == failWriteAt) { *written = 0; return -1; }
    if (zeroWrite) { *written = 0; return ESP_OK; }
    if (oversizedWrite) { *written = bytes + 1; return ESP_OK; }
    // Deliberately odd-byte short writes verify byte-continuation correctness.
    *written = partialWrite ? std::min(bytes, size_t(73)) : bytes;
    const auto* begin = static_cast<const uint8_t*>(data);
    output.insert(output.end(), begin, begin + *written);
    if (changeGain && writeCalls == 1) audio.changeVolumeLive(-50);
    return ESP_OK;
}
}
void reset() {
    owner = FREE;
    installs = uninstalls = pinCalls = zeroCalls = writeCalls = deleted = 0;
    failInstall = failPins = failZero = failWriteAt = 0;
    zeroWrite = oversizedWrite = partialWrite = appDuringDelay = appDuringWrite = changeGain = false;
    output.clear();
    audio.setVolume(100);
}
void run() {
    assert(hostStartupTask);
    hostStartupTask(nullptr);
    assert(deleted == 1);
    checkController();
}
void verifyPcm(int volume, bool changed = false) {
    const size_t samples = ping_raw_size / 2;
    assert(output.size() == samples * 4 + 4 * 128 * 4);
    for (size_t i = 0; i < samples; ++i) {
        int16_t source, left, right;
        memcpy(&source, ping_raw + i * 2, 2);
        memcpy(&left, output.data() + i * 4, 2);
        memcpy(&right, output.data() + i * 4 + 2, 2);
        const int gain = ((changed && i >= 128 ? 50 : volume) * 1024) / 100;
        assert(left == (((source * gain) >> 10) >> 2));
        assert(right == left);
    }
    for (size_t i = samples * 4; i < output.size(); ++i) assert(output[i] == 0);
}
int main() {
    // Calls REAL public begin and the task captured from REAL playStartupSound.
    audio.begin();
    Controller controller;
    activeController = &controller;
    controller.begin(); // GPIO0 input initialized after audio, before delayed task.
    assert(LILKA_GPIO_SELECT == 0);
    reset(); run(); verifyPcm(100);
    assert(installs == 1 && pinCalls == 1 && zeroCalls == 1 && uninstalls == 1);
    reset(); partialWrite = true; run(); verifyPcm(100);
    reset(); audio.setVolume(50); run(); verifyPcm(50);
    reset(); audio.setVolume(0); run(); verifyPcm(0);
    reset(); changeGain = true; run(); verifyPcm(100, true);
    reset(); audio.setVolume(150); run(); verifyPcm(100);
    reset(); audio.setVolume(-20); run(); verifyPcm(0);
    reset(); partialWrite = true; failWriteAt = 2; run();
    assert(output.size() == 73 && writeCalls == 2 && uninstalls == 1);
    reset(); appDuringDelay = true; run();
    assert(owner == APP && installs == 1 && !pinCalls && !zeroCalls && !writeCalls && !uninstalls);
    reset(); owner = APP; run(); assert(owner == APP && !pinCalls && !uninstalls);
    reset(); failInstall = 1; run(); assert(!pinCalls && !zeroCalls && !writeCalls && !uninstalls);
    reset(); failPins = 1; run(); assert(pinCalls == 1 && !zeroCalls && !writeCalls && uninstalls == 1);
    reset(); failZero = 1; run(); assert(zeroCalls == 1 && !writeCalls && uninstalls == 1);
    for (int at : {1, 2, 173, 174, 176}) {
        reset(); failWriteAt = at; run(); assert(writeCalls == at && uninstalls == 1);
    }
    reset(); zeroWrite = true; run(); assert(writeCalls == 1 && uninstalls == 1);
    reset(); oversizedWrite = true; run(); assert(writeCalls == 1 && uninstalls == 1);
    reset(); appDuringWrite = true; run(); verifyPcm(100); assert(installs == 2 && uninstalls == 1);
    puts("real startup native IDF ownership/GPIO0/controller interleavings/PCM/live gain/errors/short writes PASS");
}
