#include <lilka/rgb565_pack.h>
#include "devices.h"
#include "display_profile.h"

#include "frame_row.h"
#include "localization.h"
#ifndef LILKA_MEASURE_DISPLAY
#    define LILKA_MEASURE_DISPLAY 0
#endif
namespace lilka {
bool DeviceManager::setWorkerCore(BaseType_t core) {
    if (worker || (core != tskNO_AFFINITY && (core < 0 || core >= portNUM_PROCESSORS))) return false;
    workerCore = core;
    return true;
}
bool DeviceManager::setWorkerPriority(UBaseType_t priority) {
    if (worker || priority == 0 || priority >= configMAX_PRIORITIES) return false;
    workerPriority = priority;
    return true;
}
} // namespace lilka
#if LILKA_VERSION != 1
#    include "display.h"
#    include "elfcheck.h"
#    include "brightness.h"
#    include "serial.h"
#    include <Preferences.h>
#    include <cstdarg>
#    include <ArduinoJson.h>
#    include <dirent.h>
#    include <sys/stat.h>
#    include <esp_heap_caps.h>
#    include <esp_timer.h>
#    include <soc/soc.h>
#    include <driver/gpio.h>
#    include <cstring>
#    include <new>

namespace lilka {
namespace {
// Bound ArduinoJson's elastic allocator, including malicious/irrelevant manifest fields.
class ManifestAllocator : public ArduinoJson::Allocator {
    size_t used = 0;

public:
    void* allocate(size_t size) override {
        if (size > 8192 - used) return nullptr;
        auto* block =
            static_cast<size_t*>(heap_caps_malloc(size + sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!block) return nullptr;
        *block = size;
        used += size;
        return block + 1;
    }
    void deallocate(void* pointer) override {
        if (!pointer) return;
        auto* block = static_cast<size_t*>(pointer) - 1;
        used -= *block;
        heap_caps_free(block);
    }
    void* reallocate(void* pointer, size_t size) override {
        if (!pointer) return allocate(size);
        auto* old = static_cast<size_t*>(pointer) - 1;
        const size_t length = *old;
        if (size > 8192 - used + length) return nullptr;
        auto* block =
            static_cast<size_t*>(heap_caps_realloc(old, size + sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!block) return nullptr;
        used = used - length + size;
        *block = size;
        return block + 1;
    }
};
String formatString(const char* format, ...) {
    char bytes[128];
    va_list args;
    va_start(args, format);
    vsnprintf(bytes, sizeof(bytes), format, args);
    va_end(args);
    return String(bytes);
}
constexpr size_t FramePixels = 320 * 240;
constexpr unsigned RecordCount = 32;
struct Record {
    uint32_t magic, version, schema, count;
    char uuid[37], driverUuid[37];
    uint8_t enabled, reserved[5];
    struct Field {
        uint32_t id;
        int32_t value;
    } fields[KD_MAX_SETTINGS];
};
static_assert(sizeof(Record) < 256, "NVS record budget");
bool uuidValid(const char* uuid) {
    if (!uuid || strlen(uuid) != 36) return false;
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else if (!((uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f'))) return false;
    }
    return true;
}
uint32_t pinBit(int pin) {
    const int pins[] = {LILKA_P8, LILKA_P7, LILKA_P6, LILKA_P5, LILKA_P4, LILKA_P3};
    for (unsigned i = 0; i < 6; ++i)
        if (pins[i] == pin) return uint32_t(1) << i;
    return 0;
}
bool pinAllowed(int pin) {
    return pinBit(pin) != 0;
}
bool bounded(const char* text, size_t maximum) {
    return text && strnlen(text, maximum) < maximum;
}
// ABI-1 pin choices already use GPIO<number>, optionally Disconnected. Infer
// host-side metadata from that convention without changing the SD driver ABI.
bool pinChoiceName(const char* text) {
    if (!strcmp(text, "Disconnected")) return true;
    if (strncmp(text, "GPIO", 4) || !text[4]) return false;
    for (const char* p = text + 4; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    return true;
}
bool pinSetting(const kd_setting& field) {
    if (field.kind != KD_SETTING_CHOICE) return false;
    bool hasPin = false;
    for (int value = field.minimum; value <= field.maximum; ++value) {
        if (!pinChoiceName(field.choices[value])) return false;
        hasPin = hasPin || !strncmp(field.choices[value], "GPIO", 4);
    }
    return hasPin;
}
bool choicePin(const char* text, int& pin) {
    if (!strcmp(text, "Disconnected")) {
        pin = -1;
        return true;
    }
    if (!pinChoiceName(text)) return false;
    pin = 0;
    for (const char* p = text + 4; *p; ++p) {
        if (pin > 48) return false;
        pin = pin * 10 + (*p - '0');
    }
    return pinAllowed(pin);
}
bool describeSetting(const kd_setting& field, int32_t value, DeviceManager::SettingInfo& result) {
    if (value < field.minimum || value > field.maximum) return false;
    strlcpy(result.label, field.label, sizeof(result.label));
    result.value = value;
    result.minimum = field.minimum;
    result.maximum = field.maximum;
    result.step = field.step;
    result.kind = field.kind;
    result.pin = pinSetting(field);
    strlcpy(result.choice, field.kind == KD_SETTING_CHOICE ? field.choices[value] : "", sizeof(result.choice));
    return true;
}
struct LayerView {
    const uint16_t* pixels;
    int x, y, width, height;
};
LayerView layerView(lilka::Canvas* canvas) {
    return canvas ? LayerView{canvas->getFramebuffer(), canvas->x(), canvas->y(), canvas->width(), canvas->height()}
                  : LayerView{nullptr, 0, 0, 0, 0};
}
const uint16_t* fullRow(const LayerView& layer, int y, int width) {
    if (!layer.pixels || layer.x != 0 || layer.width < width || y < layer.y || y >= layer.y + layer.height)
        return nullptr;
    return layer.pixels + (y - layer.y) * layer.width;
}
void copyLayerRow(uint16_t* row, int width, int y, const LayerView& layer) {
    if (!layer.pixels || y < layer.y || y >= layer.y + layer.height || layer.x >= width) return;
    const int left = layer.x < 0 ? 0 : layer.x;
    const int right = layer.x + layer.width < width ? layer.x + layer.width : width;
    if (right > left)
        memcpy(
            row + left, layer.pixels + (y - layer.y) * layer.width + left - layer.x, (right - left) * sizeof(uint16_t)
        );
}
} // namespace

DeviceManager::DeviceManager() {
}
bool DeviceManager::begin(bool skip) {
    if (worker) return true;
    if (!mutex || !internalMutex || !frameMutex) return false;
    discover(skip);
    for (unsigned i = 0; i < deviceCount; ++i)
        serial.log("display-device: %s: %s", slots[i].name, slots[i].status);
    if (!hasRegisteredDriver()) {
        serial.log("display-device: no registered display driver; internal fallback");
        return true;
    }
    if (xTaskCreatePinnedToCore(workerEntry, "display-devices", 6144, this, workerPriority, &worker, workerCore) !=
        pdPASS) {
        workerReady = false;
        serial.err(
            "display-device: worker allocation failed: requested=6144 internal-free=%u largest=%u",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))
        );
        deactivate();
        return false;
    }
    workerReady = true;
    serial.log("display-device: worker ready, selected mode=%u", static_cast<unsigned>(mode.load()));
    return true;
}
void DeviceManager::workerEntry(void* context) {
    static_cast<DeviceManager*>(context)->run();
}
void DeviceManager::requestCycle() {
    if (workerReady && hasRegisteredDriver()) ++cycleRequests;
}
void DeviceManager::setPresentationReady(bool ready) {
    presentationReady = ready;
    if (!mutex || !frameMutex || !internalMutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    route();
    xSemaphoreGive(mutex);
}
void DeviceManager::setSleeping(bool value) {
    sleeping = value;
    if (!mutex || !frameMutex || !internalMutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    route();
    xSemaphoreGive(mutex);
}
DeviceManager::~DeviceManager() {
    // Lifecycle owner must stop/join service before destroying it.
    deactivate();
    for (auto& slot : slots)
        if (slot.loaded) lilka_elf_deinit(&slot.elf);
    for (auto* pixels : frames)
        heap_caps_free(pixels);
    vSemaphoreDelete(internalMutex);
    vSemaphoreDelete(frameMutex);
    vSemaphoreDelete(mutex);
}
void DeviceManager::delayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms > 500 ? 500 : ms));
}
void DeviceManager::discover(bool skip) {
    bootSkipped = skip;

    Preferences prefs;
    mode = Internal;
    if (!skip && prefs.begin("devices", true)) {
        const uint8_t savedMode = prefs.getUChar("displayMode", Internal);
        if (savedMode <= Both) mode = static_cast<Mode>(savedMode);
        prefs.end();
    }
    DIR* directory = opendir("/sd/drivers");
    if (!directory) return;
    unsigned scanned = 0;
    while (auto* item = readdir(directory)) {
        if (deviceCount == MaxDevices || ++scanned > 64) break;
        if (item->d_name[0] == '.' || strlen(item->d_name) > 48) continue;
        const String base = String("/sd/drivers/") + item->d_name;
        FILE* file = fopen((base + "/manifest.json").c_str(), "rb");
        if (!file) continue;
        auto* manifest = static_cast<char*>(heap_caps_malloc(4097, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!manifest) {
            fclose(file);
            break;
        }
        const size_t length = fread(manifest, 1, 4097, file);
        fclose(file);
        if (!length || length > 4096) {
            heap_caps_free(manifest);
            continue;
        }
        ManifestAllocator allocator;
        JsonDocument document(&allocator);
        const auto error = deserializeJson(
            document, static_cast<const char*>(manifest), length, DeserializationOption::NestingLimit(4)
        );
        // Force copied JSON strings by parsing a const buffer before freeing input.
        if (error) {
            heap_caps_free(manifest);
            continue;
        }
        heap_caps_free(manifest);
        auto& slot = slots[deviceCount++];
        slot.owner = this;
        strlcpy(slot.name, document["name"] | item->d_name, sizeof(slot.name));
        strlcpy(slot.uuid, document["device_uuid"] | "", sizeof(slot.uuid));
        strlcpy(slot.driverUuid, document["driver_uuid"] | "", sizeof(slot.driverUuid));
        strlcpy(slot.status, "Invalid manifest", sizeof(slot.status));
        if (!document["abi"].is<unsigned>() || document["abi"].as<unsigned>() != KD_ABI_VERSION ||
            !uuidValid(document["device_uuid"] | "") || !uuidValid(document["driver_uuid"] | "") ||
            strcmp(document["type"] | "", "display") || strcmp(document["module"] | "", "driver.so"))
            continue;
        document.clear(); // Metadata is copied; release parser memory before ELF relocation.
        bool duplicate = false;
        for (unsigned i = 0; i + 1 < deviceCount; ++i)
            if (!strcmp(slots[i].uuid, slot.uuid)) duplicate = true;
        if (duplicate) {
            strlcpy(slot.status, "Duplicate device UUID", sizeof(slot.status));
            continue;
        }
        if (skip) {
            strlcpy(slot.status, "Skipped: Select held at boot", sizeof(slot.status));
            restore(slot);
            continue;
        }
        if (!load(slot, (base + "/driver.so").c_str())) {
            restore(slot);
            continue;
        }
        restore(slot);
    }
    closedir(directory);
    // Prefer the previously active UUID, independent of filesystem enumeration order.
    char preferred[37] = {};

    if (prefs.begin("devices", true)) {
        prefs.getString("activeDisplay", "").toCharArray(preferred, sizeof(preferred));
        prefs.end();
    }

    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].enabled && slots[i].descriptor && !strcmp(preferred, slots[i].uuid) && activate(i)) return;
    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].enabled && slots[i].descriptor && activate(i)) return;
}
bool DeviceManager::load(Slot& slot, const char* path) {
    strlcpy(slot.status, "ELF load/validation failed", sizeof(slot.status));
    FILE* file = fopen(path, "rb");
    if (!file) {
        strlcpy(slot.status, "Missing driver.so", sizeof(slot.status));
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    rewind(file);
    auto* bytes = length > 0 && length <= 65536
                      ? static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                      : nullptr;
    size_t codeBytes = 0;
    bool valid = bytes && fread(bytes, 1, length, file) == static_cast<size_t>(length) &&
                 validateDriverElf(bytes, length, &codeBytes);
    fclose(file);
    // Check the validated module's actual contiguous executable allocation, not
    // a fixed free-heap watermark. Applications may already own their stacks.
    // SPI buffers and the worker retain checked allocation/rollback paths below.
    if (valid && heap_caps_get_largest_free_block(MALLOC_CAP_EXEC) < codeBytes) {
        strlcpy(slot.status, "Insufficient executable RAM", sizeof(slot.status));
        serial.err(
            "display-device: code allocation unavailable: requested=%u largest-exec=%u internal-free=%u",
            static_cast<unsigned>(codeBytes),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_EXEC)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))
        );
        valid = false;
    }
    if (valid) valid = lilka_elf_relocate(&slot.elf, bytes, length) == 0;
    heap_caps_free(bytes); // ELF no longer needed; do not retain a second copy.
    const uintptr_t code = reinterpret_cast<uintptr_t>(slot.elf.ptext);
    // The existing loader's PSRAM instruction mirror cannot safely execute L32R literals.
    if (valid && !(code >= SOC_IRAM_LOW && code < SOC_IRAM_HIGH)) valid = false;
    slot.host = {
        sizeof(kd_host),
        KD_ABI_VERSION,
        &slot,
        registerDevice,
        setting,
        spiOpen,
        gpioWrite,
        spiWrite,
        spiPixels,
        spiClose,
        delayMs
    };
    if (valid) {
        char* arguments[] = {reinterpret_cast<char*>(&slot.host)};
        valid = lilka_elf_run(&slot.elf, 1, arguments) == 0 && slot.descriptor;
    }
    if (!valid) {
        lilka_elf_deinit(&slot.elf);
        slot.descriptor = nullptr;
        return false;
    }
    slot.loaded = true;
    strlcpy(slot.status, "Disabled", sizeof(slot.status));
    return true;
}
int32_t DeviceManager::registerDevice(void* context, const kd_device* d) {
    auto& slot = *static_cast<Slot*>(context);
    if (slot.descriptor || !d ||
        (d->size != offsetof(kd_device, present_rows) && d->size != offsetof(kd_device, prefer_full_frame) &&
         d->size != offsetof(kd_device, backlight_pin) && d->size != sizeof(kd_device)) ||
        d->abi != KD_ABI_VERSION || d->type != KD_TYPE_DISPLAY || !bounded(d->driver_uuid, 37) ||
        !bounded(d->device_uuid, 37) || strcmp(d->driver_uuid, slot.driverUuid) || strcmp(d->device_uuid, slot.uuid) ||
        !bounded(d->name, 48) || !bounded(d->version, 24) || !d->settings_version ||
        d->setting_count > KD_MAX_SETTINGS || (d->setting_count && !d->settings) || d->state_size > 256 || !d->enable ||
        !d->disable || !d->present)
        return -1;
    for (unsigned i = 0; i < d->setting_count; ++i) {
        const auto& f = d->settings[i];
        if (!f.id || !bounded(f.label, 48) || f.kind < KD_SETTING_INTEGER || f.kind > KD_SETTING_CHOICE ||
            f.minimum > f.maximum || f.default_value < f.minimum || f.default_value > f.maximum || f.step <= 0 ||
            static_cast<int64_t>(f.maximum) - f.minimum > 100000000 ||
            (f.kind == KD_SETTING_BOOLEAN && (f.minimum != 0 || f.maximum != 1)) ||
            (f.kind == KD_SETTING_CHOICE && (f.minimum != 0 || f.maximum > 15 || !f.choices)))
            return -1;
        if (f.kind == KD_SETTING_CHOICE)
            for (int choice = 0; choice <= f.maximum; ++choice)
                if (!bounded(f.choices[choice], 32)) return -1;
        for (unsigned j = 0; j < i; ++j)
            if (d->settings[j].id == f.id) return -1;
        slot.values[i] = f.default_value;
    }
    slot.schema = d->settings_version;
    slot.descriptor = d;
    strlcpy(slot.name, d->name, sizeof(slot.name));
    return 0;
}
int32_t DeviceManager::setting(void* context, uint32_t id) {
    const auto& slot = *static_cast<Slot*>(context);
    if (slot.descriptor)
        for (unsigned i = 0; i < slot.descriptor->setting_count; ++i)
            if (slot.descriptor->settings[i].id == id) return slot.values[i];
    return 0;
}
bool DeviceManager::restore(Slot& slot) {
    bool good = true;

    Preferences prefs;
    if (prefs.begin("devices", true)) {
        for (unsigned i = 0; i < RecordCount; ++i) {
            const String key = formatString("d%02u", i);
            if (!prefs.isKey(key.c_str())) continue;
            Record r = {};
            const size_t length = prefs.getBytesLength(key.c_str());
            const size_t legacyLength = offsetof(Record, fields) + 8 * sizeof(Record::Field);
            if ((length != sizeof(r) && length != legacyLength) || prefs.getBytes(key.c_str(), &r, length) != length)
                continue;
            if (!memchr(r.uuid, 0, sizeof(r.uuid)) || strcmp(r.uuid, slot.uuid)) continue;
            if (r.magic != 0x4b445631 || r.version != 1 ||
                r.count > (length - offsetof(Record, fields)) / sizeof(Record::Field) || r.enabled > 1 ||
                !memchr(r.driverUuid, 0, sizeof(r.driverUuid)) || strcmp(r.driverUuid, slot.driverUuid) ||
                (slot.descriptor && r.schema != slot.schema)) {
                slot.readOnly = true;
                good = false;
                break;
            }
            slot.enabled = r.enabled;
            for (unsigned f = 0; slot.descriptor && f < slot.descriptor->setting_count; ++f)
                for (unsigned j = 0; j < r.count; ++j) {
                    const auto& definition = slot.descriptor->settings[f];
                    int32_t value = r.fields[j].value;
                    bool matches = definition.id == r.fields[j].id;
                    // ILI9341 1.1.8 removes 50/60/70 MHz. Preserve 10/20/40/80;
                    // removed speeds migrate conservatively to 40, never upward.
                    if (definition.id == 12 && r.fields[j].id == 5 && value >= 0 && value <= 6 &&
                        !strcmp(slot.driverUuid, "cfa4bc25-c2d6-4a18-ade9-16506b19acf1") &&
                        !strcmp(slot.uuid, "674db8e0-cd55-4bfb-90b0-1e521e97c1cd")) {
                        value = value == 6 ? 3 : (value > 2 ? 2 : value);
                        matches = true;
                    }
                    if (matches && value >= definition.minimum && value <= definition.maximum) slot.values[f] = value;
                }
            break;
        }
        prefs.end();
    }

    if (!good && slot.descriptor)
        strlcpy(slot.status, "Unsupported saved settings; reset required", sizeof(slot.status));
    return good;
}
bool DeviceManager::persist(Slot& slot) {
    if (slot.readOnly || !slot.descriptor) return false;
    Record r = {};
    r.magic = 0x4b445631;
    r.version = 1;
    r.schema = slot.schema;
    r.count = slot.descriptor->setting_count;
    r.enabled = slot.enabled;
    strlcpy(r.uuid, slot.uuid, sizeof(r.uuid));
    strlcpy(r.driverUuid, slot.driverUuid, sizeof(r.driverUuid));
    for (unsigned f = 0; f < r.count; ++f) {
        r.fields[f].id = slot.descriptor->settings[f].id;
        r.fields[f].value = slot.values[f];
    }
    bool good = false;

    Preferences prefs;
    if (prefs.begin("devices", false)) {
        int selected = -1, free = -1;
        for (unsigned i = 0; i < RecordCount; ++i) {
            const String key = formatString("d%02u", i);
            if (!prefs.isKey(key.c_str())) {
                if (free < 0) free = i;
                continue;
            }
            Record old = {};
            const size_t length = prefs.getBytesLength(key.c_str());
            if ((length == sizeof(old) || length == offsetof(Record, fields) + 8 * sizeof(Record::Field)) &&
                prefs.getBytes(key.c_str(), &old, length) == length && memchr(old.uuid, 0, sizeof(old.uuid)) &&
                !strcmp(old.uuid, slot.uuid)) {
                selected = i;
                break;
            }
        }
        if (selected < 0) selected = free;
        if (selected >= 0) good = prefs.putBytes(formatString("d%02u", selected).c_str(), &r, sizeof(r)) == sizeof(r);
        prefs.end();
    }

    if (!good) strlcpy(slot.status, "NVS save failed / registry full", sizeof(slot.status));
    return good;
}
bool DeviceManager::route() {
    const bool external = active >= 0 && mode != Internal && presentationReady;
    const bool powered = external && !sleeping;
    if (active >= 0 && slots[active].descriptor->set_output &&
        slots[active].descriptor->set_output(&slots[active].host, slots[active].state, powered)) {
        internal = true;
        routed = false;
        return false;
    }
    xSemaphoreTake(internalMutex, portMAX_DELAY);
    xSemaphoreTake(frameMutex, portMAX_DELAY);
    if (external && !frame) {
        const int w = lilka::display.width(), h = lilka::display.height();
        const size_t pixels = w > 0 && h > 0 ? static_cast<size_t>(w) * h : 0;
        if (pixels && pixels <= FramePixels) {
            for (auto& buffer : frames)
                buffer = static_cast<uint16_t*>(
                    heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                );
            if (frames[0] && frames[1]) {
                frame = frames[0];
                frameCapacity = pixels;
            } else {
                for (auto& buffer : frames) {
                    heap_caps_free(buffer);
                    buffer = nullptr;
                }
            }
        }
    }
    if (external && !frame) {
        strlcpy(slots[active].status, "PSRAM frame allocation failed", sizeof(slots[active].status));
        internal = true;
        routed = false;
        xSemaphoreGive(frameMutex);
        xSemaphoreGive(internalMutex);
        return false;
    }
    // The driver mutex ensures its reader has finished before rerouting/freeing.
    if (!external) {
        for (auto& buffer : frames) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
        frame = nullptr;
        frameCapacity = 0;
    }
    pending = false;
    frameValid = false;
    memset(staleRows, 0xff, sizeof(staleRows));
    memset(dirtyRows, 0, sizeof(dirtyRows));
    memset(internalDirtyRows, 0, sizeof(internalDirtyRows));
    routed = external;
    internal = !external || mode != External;
    xSemaphoreGive(frameMutex);
    xSemaphoreGive(internalMutex);
    if (lilka::brightness.isEnabled() &&
        !lilka::brightness.setActiveDisplays(!external || mode != External, external)) {
        internal = true;
        routed = false;
        lilka::brightness.setActiveDisplays(true, false);
        return false;
    }
    ++generation;
    return true;
}
void DeviceManager::deactivate() {
    if (backlightPin >= 0) {
        lilka::brightness.detachExternalBacklight();
        ownedPins = ownedPins.load() & ~pinBit(backlightPin);
        backlightPin = -1;
    }
    if (active >= 0) {
        auto& slot = slots[active];
        slot.descriptor->disable(&slot.host, slot.state);
        spiClose(&slot);
        heap_caps_free(slot.state);
        slot.state = nullptr;
        slot.active = false;
        strlcpy(slot.status, slot.enabled ? "Enabled, inactive" : "Disabled", sizeof(slot.status));
    }
    active = -1;
    route();
}
bool DeviceManager::activate(unsigned index) {
    auto& slot = slots[index];
    if (!slot.descriptor || slot.readOnly || bootSkipped) return false;
    deactivate();
    slot.state = heap_caps_calloc(1, slot.descriptor->state_size ? slot.descriptor->state_size : 1, MALLOC_CAP_8BIT);
    strlcpy(
        slot.status,
        slot.state ? "Driver enable callback failed" : "Driver state allocation failed",
        sizeof(slot.status)
    );
    if (!slot.state) return false;
    if (slot.descriptor->size >= sizeof(kd_device) && slot.descriptor->backlight_pin) {
        const int pin = slot.descriptor->backlight_pin(&slot.host, slot.state);
        if (pin != -1) {
            if (!pinAllowed(pin) || extensionPinOwned(pin) || !lilka::brightness.attachExternalBacklight(pin, false)) {
                slot.descriptor->disable(&slot.host, slot.state);
                spiClose(&slot);
                heap_caps_free(slot.state);
                slot.state = nullptr;
                strlcpy(slot.status, "Backlight GPIO conflict / PWM unavailable", sizeof(slot.status));
                return false;
            }
            backlightPin = pin;
            ownedPins = ownedPins.load() | pinBit(pin);
        }
    }
    if (slot.descriptor->enable(&slot.host, slot.state)) {
        char error[80];
        strlcpy(error, slot.status, sizeof(error));
        slot.descriptor->disable(&slot.host, slot.state);
        spiClose(&slot);
        if (backlightPin >= 0) {
            lilka::brightness.detachExternalBacklight();
            backlightPin = -1;
        }
        heap_caps_free(slot.state);
        slot.state = nullptr;
        strlcpy(slot.status, error, sizeof(slot.status));
        return false;
    }
    slot.active = true;
    active = index;
    if (!route()) {
        char error[80];
        strlcpy(error, slot.status, sizeof(error));
        deactivate();
        strlcpy(slot.status, error, sizeof(slot.status));
        return false;
    }
    strlcpy(slot.status, "Active", sizeof(slot.status));
    return true;
}
bool DeviceManager::extensionPinOwned(int pin) const {
    return (ownedPins.load() & pinBit(pin)) != 0;
}
bool DeviceManager::hasRegisteredDriver() const {
    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].descriptor) return true;
    return false;
}
bool DeviceManager::info(unsigned index, Info& result) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const auto& slot = slots[index];
    strlcpy(result.name, slot.name, sizeof(result.name));
    strlcpy(result.uuid, slot.uuid, sizeof(result.uuid));
    strlcpy(result.version, slot.descriptor ? slot.descriptor->version : "", sizeof(result.version));
    strlcpy(result.status, slot.status, sizeof(result.status));
    result.enabled = slot.enabled;
    result.active = slot.active && workerReady;
    if (slot.active && !workerReady)
        strlcpy(result.status, "Worker not running; internal fallback", sizeof(result.status));
    result.settingCount = slot.descriptor ? slot.descriptor->setting_count : 0;
    xSemaphoreGive(mutex);
    return true;
}
bool DeviceManager::settingInfo(unsigned index, unsigned field, SettingInfo& result) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const auto& slot = slots[index];
    const bool good = slot.descriptor && field < slot.descriptor->setting_count &&
                      describeSetting(slot.descriptor->settings[field], slot.values[field], result);
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::beginSettingsEdit(unsigned index, SettingsDraft& draft) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const auto& slot = slots[index];
    const bool good = slot.descriptor && !slot.readOnly;
    if (good) {
        draft = SettingsDraft{};
        draft.count = slot.descriptor->setting_count;
        draft.schema = slot.schema;
        strlcpy(draft.uuid, slot.uuid, sizeof(draft.uuid));
        memcpy(draft.original, slot.values, sizeof(draft.original));
        memcpy(draft.values, slot.values, sizeof(draft.values));
    }
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::previewSetting(unsigned index, unsigned field, const SettingsDraft& draft, SettingInfo& result) {
    if (index >= deviceCount || field >= KD_MAX_SETTINGS) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const auto& slot = slots[index];
    const bool good = slot.descriptor && draft.count == slot.descriptor->setting_count && draft.schema == slot.schema &&
                      !strncmp(draft.uuid, slot.uuid, sizeof(draft.uuid)) && field < draft.count &&
                      describeSetting(slot.descriptor->settings[field], draft.values[field], result);
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::applySettings(unsigned index, const SettingsDraft& draft) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto& slot = slots[index];
    bool good = slot.descriptor && !slot.readOnly && draft.count == slot.descriptor->setting_count &&
                draft.schema == slot.schema && !strncmp(draft.uuid, slot.uuid, sizeof(draft.uuid));
    if (good && memcmp(draft.original, slot.values, draft.count * sizeof(int32_t))) {
        strlcpy(slot.status, "Settings changed; reopen Pins", sizeof(slot.status));
        good = false;
    }
    if (good) good = applySettingsLocked(index, draft.values);
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::applySettingsLocked(unsigned index, const int32_t* values) {
    auto& slot = slots[index];
    if (!slot.descriptor || slot.readOnly) return false;
    uint32_t pins = 0;
    for (unsigned field = 0; field < slot.descriptor->setting_count; ++field) {
        const auto& definition = slot.descriptor->settings[field];
        if (values[field] < definition.minimum || values[field] > definition.maximum) {
            strlcpy(slot.status, "Invalid setting value", sizeof(slot.status));
            return false;
        }
        if (pinSetting(definition)) {
            int pin = -1;
            if (!choicePin(definition.choices[values[field]], pin) || (pin >= 0 && (pins & pinBit(pin)))) {
                strlcpy(slot.status, "Invalid or conflicting pin assignment", sizeof(slot.status));
                return false;
            }
            if (pin >= 0) pins |= pinBit(pin);
        }
    }
    const size_t bytes = slot.descriptor->setting_count * sizeof(int32_t);
    if (!memcmp(values, slot.values, bytes)) return true;
    int32_t previous[KD_MAX_SETTINGS];
    memcpy(previous, slot.values, sizeof(previous));
    const bool wasActive = slot.active;
    // Disable while the old settings are still visible to the old driver.
    if (wasActive) deactivate();
    memcpy(slot.values, values, bytes);
    bool good = !wasActive || activate(index);
    if (good) good = persist(slot);
    if (!good) {
        char error[80];
        strlcpy(error, slot.status, sizeof(error));
        if (slot.active) deactivate();
        memcpy(slot.values, previous, sizeof(previous));
        const bool restored = !wasActive || activate(index);
        if (restored) strlcpy(slot.status, error, sizeof(slot.status));
        else strlcpy(slot.status, "Apply failed; previous setup could not restart", sizeof(slot.status));
    } else if (!wasActive) {
        strlcpy(slot.status, slot.enabled ? "Enabled, inactive" : "Disabled", sizeof(slot.status));
    }
    ++generation;
    return good;
}
bool DeviceManager::setEnabled(unsigned index, bool enabled) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto& slot = slots[index];
    const bool old = slot.enabled;
    const int previous = active;
    bool good = slot.descriptor && !slot.readOnly && (!enabled || workerReady);
    if (enabled && !workerReady)
        strlcpy(slot.status, "Driver worker unavailable; internal fallback", sizeof(slot.status));
    if (good && enabled) good = activate(index);
    if (good) {
        slot.enabled = enabled;
        good = persist(slot);
        if (!good) {
            slot.enabled = old;
            if (active == static_cast<int>(index)) deactivate();
        } else if (!enabled && active == static_cast<int>(index)) deactivate();
        if (good && enabled) {
            // Other enabled preferences remain intact, but only this display is active.

            Preferences prefs;
            if (prefs.begin("devices", false)) {
                good = prefs.putString("activeDisplay", slot.uuid) > 0;
                prefs.end();
            } else good = false;
        }
    }
    if (!good && previous >= 0 && active != previous) {
        char error[80];
        strlcpy(error, slot.status, sizeof(error));
        activate(previous);
        strlcpy(slot.status, error, sizeof(slot.status));
    }
    ++generation;
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::changeSetting(unsigned index, unsigned field, int direction) {
    if (index >= deviceCount || !direction) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const auto& slot = slots[index];
    bool good = slot.descriptor && !slot.readOnly && field < slot.descriptor->setting_count;
    if (good) {
        int32_t values[KD_MAX_SETTINGS];
        memcpy(values, slot.values, sizeof(values));
        const auto& definition = slot.descriptor->settings[field];
        int64_t next = static_cast<int64_t>(values[field]) + (direction > 0 ? definition.step : -definition.step);
        if (next > definition.maximum) next = definition.minimum;
        if (next < definition.minimum) next = definition.maximum;
        values[field] = next;
        good = applySettingsLocked(index, values);
    }
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::resetSettings(unsigned index) {
    if (index >= deviceCount) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto& slot = slots[index];
    bool good = slot.descriptor;
    if (good) {
        int32_t previous[KD_MAX_SETTINGS];
        memcpy(previous, slot.values, sizeof(previous));
        const bool readOnly = slot.readOnly, wasActive = slot.active;
        slot.readOnly = false;
        for (unsigned f = 0; f < slot.descriptor->setting_count; ++f)
            slot.values[f] = slot.descriptor->settings[f].default_value;
        good = !wasActive || activate(index);
        if (good) good = persist(slot);
        if (!good) {
            char error[80];
            strlcpy(error, slot.status, sizeof(error));
            memcpy(slot.values, previous, sizeof(previous));
            slot.readOnly = readOnly;
            if (wasActive) activate(index);
            strlcpy(slot.status, error, sizeof(slot.status));
        }
    }
    ++generation;
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::setDisplayMode(Mode value) {
    if (value > Both || !mutex || !frameMutex || !internalMutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const Mode previous = mode;
    mode = value;
    bool good = route();
    if (good) {
        Preferences prefs;
        if (prefs.begin("devices", false)) {
            good = prefs.putUChar("displayMode", value) == 1;
            prefs.end();
        } else good = false;
    }
    if (!good) {
        mode = previous;
        route();
    }
    if (good && value != previous) {
        xSemaphoreTake(frameMutex, portMAX_DELAY);
        modeFeedback.valid = true;
        modeFeedback.adjustedAt = millis();
        modeFeedback.displayMode = value;
        const char* names[] = {LILKA_S_DISPLAY_INTERNAL, LILKA_S_DISPLAY_EXTERNAL, LILKA_S_DISPLAY_BOTH};
        static_assert(sizeof(LILKA_S_DISPLAY_INTERNAL) <= sizeof(modeFeedback.muteLabel), "Display label too long");
        static_assert(sizeof(LILKA_S_DISPLAY_EXTERNAL) <= sizeof(modeFeedback.muteLabel), "Display label too long");
        static_assert(sizeof(LILKA_S_DISPLAY_BOTH) <= sizeof(modeFeedback.muteLabel), "Display label too long");
        strlcpy(modeFeedback.muteLabel, names[value], sizeof(modeFeedback.muteLabel));
        xSemaphoreGive(frameMutex);
    }
    xSemaphoreGive(mutex);
    return good;
}
bool DeviceManager::capture(
    lilka::Canvas* panel, lilka::Canvas* app, const lilka::VolumeOverlaySnapshot& overlay, bool sleeping
) {
    if (xSemaphoreTake(frameMutex, 0) != pdTRUE) return false;
    bool good = frame && routed.load() && mode != Internal;
    const int w = lilka::display.width(), h = lilka::display.height();
    if (good && w > 0 && h > 0 && w <= 320 && h <= 320 && static_cast<size_t>(w) * h <= frameCapacity) {
#    if LILKA_MEASURE_DISPLAY
        const int64_t start = esp_timer_get_time();
#    endif
        const bool force = !frameValid || width != w || height != h;
        // Only the external worker retains a frame between compositor iterations.
        // Reuse the other frame even if it was previously published: latest wins.
        const unsigned outputIndex = externalReading == 0 ? 1 : 0;
        auto* output = frames[outputIndex];
        const auto top = layerView(sleeping ? nullptr : app);
        const auto status = layerView(sleeping ? nullptr : panel);
        const auto geometry = lilka::volumeOverlayGeometry(w, h);
        const uint32_t overlayNow = millis();
        const bool overlayVisible = !sleeping && overlay.visible(overlayNow) && geometry.width;
        uint16_t scratch[320]; // One temporary row, not another framebuffer.
        bool changed = false;
        for (int y = 0; y < h; ++y) {
            const uint16_t* source = fullRow(top, y, w);
            // A partial app overlaps the panel: compose rather than bypass it.
            const bool topCovers = top.pixels && y >= top.y && y < top.y + top.height;
            if (!source && !topCovers) source = fullRow(status, y, w);
            const bool overlayRow = overlayVisible && y >= geometry.y && y < geometry.y + geometry.height;
            if (!source || overlayRow) {
                if (source) memcpy(scratch, source, w * sizeof(uint16_t));
                else {
                    memset(scratch, 0, w * sizeof(uint16_t));
                    copyLayerRow(scratch, w, y, status);
                    copyLayerRow(scratch, w, y, top);
                }
                if (overlayRow) {
                    lilka::VolumeOverlayRow target{scratch, 0, y, w};
                    lilka::drawVolumeOverlay(target, overlay, w, h, overlayNow);
                }
                source = scratch;
            }
            const auto* previous = frame + y * w;
            auto* destination = output + y * w;
            const uint32_t bit = uint32_t(1) << (y % 32);
            const bool fresh = !(staleRows[outputIndex][y / 32] & bit);
            const bool rowChanged = displayFrame::copyCompareRow(destination, source, previous, w, force, fresh);
            staleRows[outputIndex][y / 32] &= ~bit;
            if (rowChanged) {
                staleRows[1 - outputIndex][y / 32] |= bit;
                dirtyRows[y / 32] |= bit;
                internalDirtyRows[y / 32] |= bit;
                changed = true;
            }
        }
        frame = output;
        width = w;
        height = h;
        frameValid = true;
        pending = pending || changed;
#    if LILKA_MEASURE_DISPLAY
        ++captureCount;
        if (!changed) ++unchangedCaptures;
        captureUs = esp_timer_get_time() - start;
        captureTiming.record(captureUs);
        displayProfile::record(displayProfile::Stage::Capture, captureUs);
#    endif
    } else good = false;
    xSemaphoreGive(frameMutex);
    return good;
}
bool DeviceManager::presentInternal() {
    // Only the compositor calls capture/presentInternal, in sequence. Protect
    // lifetime against settings/rerouting, but release frameMutex before SPI so
    // the external worker can acquire/release its lease during internal DMA.
    xSemaphoreTake(internalMutex, portMAX_DELAY);
    xSemaphoreTake(frameMutex, portMAX_DELAY);
    const bool good = frameValid && routed.load() && mode == Both;
    // Arduino_GFX's const bitmap overload writes one pixel at a time. Its RAM
    // overload uses bulk writePixels and does not modify the source on the SDK's ESP32 HWSPI bus.
    uint16_t* pixels = good ? frame : nullptr;
    const uint16_t w = width, h = height;
    uint32_t rows[10];
    memcpy(rows, internalDirtyRows, sizeof(rows));
    if (good) memset(internalDirtyRows, 0, sizeof(internalDirtyRows));
    xSemaphoreGive(frameMutex);
    bool transferred = false;
    if (pixels) {
        for (unsigned y = 0; y < h;) {
            if (!(rows[y / 32] & (uint32_t(1) << (y % 32)))) {
                ++y;
                continue;
            }
            const unsigned first = y;
            while (y < h && (rows[y / 32] & (uint32_t(1) << (y % 32))))
                ++y;
            lilka::display.presentNativePixels(0, first, pixels + first * w, w, y - first);
            transferred = true;
        }
    }
    xSemaphoreGive(internalMutex);
    return transferred;
}
int DeviceManager::presentPending(
    Slot& slot, const uint16_t* pixels, uint16_t width, uint16_t height, const uint32_t* dirty
) {
    unsigned rows = 0, bands = 0;
    bool inside = false;
    for (unsigned y = 0; y < height; ++y) {
        const bool rowDirty = dirty[y / 32] & (uint32_t(1) << (y % 32));
        if (rowDirty) {
            ++rows;
            if (!inside) ++bands;
        }
        inside = rowDirty;
    }
    // Do not read beyond an older ABI-1 descriptor.
    const bool supportsRows =
        slot.descriptor->size >= offsetof(kd_device, prefer_full_frame) && slot.descriptor->present_rows;
    if (!supportsRows || bands > 8 || rows * 4 >= unsigned(height) * 3) {
#    if LILKA_MEASURE_DISPLAY
        ++fullTransfers;
        transferPixels += uint32_t(width) * height;
#    endif
        return slot.descriptor->present(&slot.host, slot.state, pixels, width, height);
    }
    for (unsigned y = 0; y < height;) {
        if (!(dirty[y / 32] & (uint32_t(1) << (y % 32)))) {
            ++y;
            continue;
        }
        const unsigned first = y;
        while (y < height && (dirty[y / 32] & (uint32_t(1) << (y % 32))))
            ++y;
#    if LILKA_MEASURE_DISPLAY
        ++rowTransfers;
        transferPixels += uint32_t(width) * (y - first);
#    endif
        const int result =
            slot.descriptor->present_rows(&slot.host, slot.state, pixels, width, height, first, y - first);
        if (result) return result;
    }
    return 0;
}
void DeviceManager::run() {
    workerReady = true;
    ++generation;
    uint32_t previousTransfer = 0;
#    if LILKA_MEASURE_DISPLAY
    uint32_t previousLog = millis(), loggedTransfers = 0;
    DisplayFrameMetrics transferTiming;
#    endif
    while (true) {
        const unsigned cycles = cycleRequests.exchange(0);
        if (cycles) setDisplayMode(static_cast<Mode>((mode.load() + cycles) % 3));
        xSemaphoreTake(mutex, portMAX_DELAY);
        const uint32_t interval =
            active >= 0 && slots[active].descriptor->frame_interval_ms
                ? slots[active].descriptor->frame_interval_ms(&slots[active].host, slots[active].state)
                : 0;
        const uint16_t* pixels = nullptr;
        uint16_t frameWidth = 0, frameHeight = 0;
        uint32_t rows[10] = {};
        xSemaphoreTake(frameMutex, portMAX_DELAY);
        if (!sleeping && pending && frameValid && active >= 0 && mode != Internal &&
            millis() - previousTransfer >= interval) {
            pixels = frame;
            externalReading = frame == frames[0] ? 0 : 1;
            frameWidth = width;
            frameHeight = height;
            memcpy(rows, dirtyRows, sizeof(rows));
            memset(dirtyRows, 0, sizeof(dirtyRows));
            pending = false;
        }
        xSemaphoreGive(frameMutex);
        if (pixels) {
            previousTransfer = millis();
#    if LILKA_MEASURE_DISPLAY
            const int64_t start = esp_timer_get_time();
#    endif
            auto& slot = slots[active];
            const int result = presentPending(slot, pixels, frameWidth, frameHeight, rows);
            xSemaphoreTake(frameMutex, portMAX_DELAY);
            externalReading = -1;
            xSemaphoreGive(frameMutex);
#    if LILKA_MEASURE_DISPLAY
            transferUs = esp_timer_get_time() - start;
            transferTiming.record(transferUs);
            ++transferCount;
#    endif
            if (result) {
                deactivate();
                strlcpy(
                    slot.status,
                    spiFault ? "SPI timeout; restart required" : "Transfer failed; internal fallback",
                    sizeof(slot.status)
                );
            }
        }
#    if LILKA_MEASURE_DISPLAY
        if (millis() - previousLog >= 5000u && active >= 0 && mode != Internal) {
            const uint32_t elapsed = millis() - previousLog;
            const uint32_t externalFps = transferTiming.fpsX10(elapsed);
            xSemaphoreTake(frameMutex, portMAX_DELAY);
            const auto captured = captureCount, copiedUs = captureUs, unchanged = unchangedCaptures;
            const auto copying = captureTiming;
            captureTiming = DisplayFrameMetrics{};
            xSemaphoreGive(frameMutex);
            // Keep each record below the SDK's 256-byte formatting buffer.
            lilka::serial.log(
                "perf-ext: window-ms=%u external-fps=%u.%u spi-hz=%u spi-requested-hz=%u "
                "capture-avg/max-us=%u/%u transfer-avg/max-us=%u/%u",
                elapsed,
                externalFps / 10,
                externalFps % 10,
                spiHz,
                spiRequestedHz,
                copying.averageUs(),
                copying.maxUs,
                transferTiming.averageUs(),
                transferTiming.maxUs
            );
            lilka::serial.log(
                "devices: captures=%u transfers=%u fps-x1000=%u capture=%uus transfer=%uus "
                "unchanged=%u full=%u bands=%u pixels=%u",
                captured,
                transferCount,
                static_cast<uint32_t>((uint64_t(transferCount - loggedTransfers) * 1000000) / elapsed),
                copiedUs,
                transferUs,
                unchanged,
                fullTransfers,
                rowTransfers,
                transferPixels
            );
            lilka::serial.log(
                "devices-memory: internal-free=%u largest=%u min=%u psram-free=%u stack=%u",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                uxTaskGetStackHighWaterMark(nullptr)
            );
#        if LILKA_MEASURE_DISPLAY
            lilka::serial.log(
                "measure-spi: pack-chunks=%u pack-total-us=%llu pack-avg/max-us=%u/%u wait-chunks=%u "
                "wait-total-us=%llu wait-avg/max-us=%u/%u",
                packingTiming.count,
                static_cast<unsigned long long>(packingTiming.totalUs),
                packingTiming.averageUs(),
                packingTiming.maxUs,
                dmaWaitTiming.count,
                static_cast<unsigned long long>(dmaWaitTiming.totalUs),
                dmaWaitTiming.averageUs(),
                dmaWaitTiming.maxUs
            );
            packingTiming = dmaWaitTiming = DisplayFrameMetrics{};
#        endif
            loggedTransfers = transferCount;
            previousLog = millis();
            transferTiming = DisplayFrameMetrics{};
        }
#    endif
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(routed ? 2 : 100));
    }
}
int32_t DeviceManager::spiOpen(void* context, int32_t clk, int32_t mosi, int32_t dc, int32_t reset, uint32_t hz) {
    auto& slot = *static_cast<Slot*>(context);
    auto& manager = *slot.owner;
    if (!slot.state) return fail(slot, "SPI before activation", ESP_ERR_INVALID_STATE);
    if (manager.spiOwner)
        return fail(slot, manager.spiFault ? "SPI needs restart" : "SPI already owned", ESP_ERR_INVALID_STATE);
    if (!pinAllowed(clk) || !pinAllowed(mosi) || !pinAllowed(dc) || !pinAllowed(reset) || clk == mosi || clk == dc ||
        clk == reset || mosi == dc || mosi == reset || dc == reset || clk == manager.backlightPin ||
        mosi == manager.backlightPin || dc == manager.backlightPin || reset == manager.backlightPin)
        return fail(slot, "Invalid/conflicting GPIO pins", ESP_ERR_INVALID_ARG);
    if (hz < 1000000 || hz > 80000000) return fail(slot, "Invalid SPI speed", ESP_ERR_INVALID_ARG);
    spi_bus_config_t bus = {};
    bus.mosi_io_num = mosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = clk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    // ESP32-S3 has four additional octal data pins. Zero means GPIO0, not unused.
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = 4096;
    const esp_err_t busResult = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (busResult != ESP_OK) return fail(slot, "SPI bus init", busResult);
    manager.spiOwner = &slot;
    manager.spiRequestedHz = hz;
    manager.spiHz = spi_get_actual_clock(APB_CLK_FREQ, hz, 128);
    manager.ownedPins = manager.ownedPins.load() | pinBit(clk) | pinBit(mosi) | pinBit(dc) | pinBit(reset);
    manager.clkPin = clk;
    manager.mosiPin = mosi;
    manager.dcPin = dc;
    manager.resetPin = reset;
    manager.dma = static_cast<uint8_t*>(heap_caps_malloc(8192, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = hz;
    device.spics_io_num = -1;
    device.queue_size = 1;
    if (!manager.dma) {
        fail(slot, "DMA buffer allocation", ESP_ERR_NO_MEM);
        spiClose(context);
        return -1;
    }
    const esp_err_t deviceResult = spi_bus_add_device(SPI2_HOST, &device, &manager.spi);
    if (deviceResult != ESP_OK) {
        fail(slot, "SPI device setup", deviceResult);
        spiClose(context);
        return -1;
    }
    const esp_err_t dcResult = gpio_set_direction(static_cast<gpio_num_t>(dc), GPIO_MODE_OUTPUT);
    const esp_err_t resetResult = gpio_set_direction(static_cast<gpio_num_t>(reset), GPIO_MODE_OUTPUT);
    if (dcResult != ESP_OK || resetResult != ESP_OK) {
        fail(slot, "GPIO output setup", dcResult != ESP_OK ? dcResult : resetResult);
        spiClose(context);
        return -1;
    }
    return 0;
}
int32_t DeviceManager::gpioWrite(void* context, int32_t pin, int32_t level) {
    auto& slot = *static_cast<Slot*>(context);
    const auto& manager = *slot.owner;
    if (manager.spiOwner != context || (pin != manager.dcPin && pin != manager.resetPin))
        return fail(slot, "GPIO claim invalid", ESP_ERR_INVALID_STATE);
    const esp_err_t result = gpio_set_level(static_cast<gpio_num_t>(pin), !!level);
    return result == ESP_OK ? 0 : fail(slot, "GPIO write", result);
}
int32_t DeviceManager::spiWrite(void* context, int32_t data, const uint8_t* bytes, uint32_t size) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiFault || manager.spiOwner != context || !manager.spi || !bytes || size > 4096) return -1;
    gpio_set_level(static_cast<gpio_num_t>(manager.dcPin), !!data);
    memcpy(manager.dma, bytes, size);
    return manager.transmit(size * 8);
}
int32_t DeviceManager::spiPixels(void* context, const uint16_t* pixels, uint32_t count) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiFault || manager.spiOwner != context || !manager.spi || !pixels || count > FramePixels) return -1;
    gpio_set_level(static_cast<gpio_num_t>(manager.dcPin), 1);
    spi_transaction_t* inFlight = nullptr;
    unsigned buffer = 0;
    while (count) {
        const uint32_t length = count < 2048 ? count : 2048;
        uint8_t* staging = manager.dma + buffer * 4096;
#    if LILKA_MEASURE_DISPLAY
        const int64_t packingStart = esp_timer_get_time();
#    endif
        lilka::packRGB565(staging, pixels, length);
#    if LILKA_MEASURE_DISPLAY
        const uint32_t packedUs = esp_timer_get_time() - packingStart;
        manager.packingTiming.record(packedUs);
        displayProfile::record(displayProfile::Stage::ExternalPack, packedUs, length * 2);
#    endif
        // Pack the other staging buffer while the preceding chunk shifts out.
        // Only one transaction is outstanding: no descriptor/buffer is reused
        // until completion, and DC cannot change until this function returns.
        if (inFlight && manager.finish(*inFlight)) return -1;
        auto& transfer = buffer ? manager.pixelTransaction : manager.transaction;
        if (manager.submit(transfer, staging, length * 16)) return -1;
        inFlight = &transfer;
        buffer ^= 1;
        pixels += length;
        count -= length;
    }
    if (inFlight && manager.finish(*inFlight)) return -1;
    return 0;
}
void DeviceManager::spiClose(void* context) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiOwner != context || manager.spiFault) return;
    if (manager.spi) spi_bus_remove_device(manager.spi);
    spi_bus_free(SPI2_HOST);
    for (int pin : {manager.clkPin, manager.mosiPin, manager.dcPin, manager.resetPin})
        gpio_reset_pin(static_cast<gpio_num_t>(pin));
    heap_caps_free(manager.dma);
    manager.dma = nullptr;
    manager.spi = nullptr;
    manager.spiOwner = nullptr;
    manager.ownedPins = 0;
    manager.clkPin = manager.mosiPin = manager.dcPin = manager.resetPin = -1;
}

int DeviceManager::submit(spi_transaction_t& transfer, const uint8_t* bytes, uint32_t bits) {
#    if LILKA_MEASURE_DISPLAY
    displayProfile::Scope metric(displayProfile::Stage::ExternalFeed);
#    endif
    transfer = {};
    transfer.length = bits;
    transfer.tx_buffer = bytes;
    const esp_err_t result = spi_device_queue_trans(spi, &transfer, pdMS_TO_TICKS(100));
    return result == ESP_OK ? 0 : fail(*spiOwner, "SPI transfer submit", result);
}

int DeviceManager::finish(const spi_transaction_t& transfer) {
#    if LILKA_MEASURE_DISPLAY
    const int64_t waitStart = esp_timer_get_time();
#    endif
    spi_transaction_t* completed = nullptr;
    const esp_err_t result = spi_device_get_trans_result(spi, &completed, pdMS_TO_TICKS(100));
#    if LILKA_MEASURE_DISPLAY
    const uint32_t waitedUs = esp_timer_get_time() - waitStart;
    dmaWaitTiming.record(waitedUs);
    displayProfile::record(displayProfile::Stage::ExternalWait, waitedUs);
#    endif
    if (result != ESP_OK || completed != &transfer) {
        // Retain BOTH staging buffers/descriptors/bus on uncertain completion.
        spiFault = true;
        return fail(*spiOwner, "SPI transfer finish", result != ESP_OK ? result : ESP_ERR_INVALID_STATE);
    }
    return 0;
}

int DeviceManager::transmit(uint32_t bits) {
    if (submit(transaction, dma, bits)) return -1;
    return finish(transaction);
}
int32_t DeviceManager::fail(Slot& slot, const char* operation, int32_t error) {
    const String message =
        formatString("%s: %s (0x%x)", operation, esp_err_to_name(error), static_cast<unsigned>(error));
    strlcpy(slot.status, message.c_str(), sizeof(slot.status));
    lilka::serial.err("devices [%s]: %s", slot.name, slot.status);
    return -1;
}
DeviceManager devices;
} // namespace lilka
#else
namespace lilka {
DeviceManager::DeviceManager() {
}
DeviceManager::~DeviceManager() {
    vSemaphoreDelete(internalMutex);
    vSemaphoreDelete(frameMutex);
    vSemaphoreDelete(mutex);
}
bool DeviceManager::begin(bool) {
    return true;
}
void DeviceManager::discover(bool) {
}
void DeviceManager::requestCycle() {
}
void DeviceManager::setSleeping(bool) {
}
void DeviceManager::setPresentationReady(bool) {
}
bool DeviceManager::hasRegisteredDriver() const {
    return false;
}
bool DeviceManager::extensionPinOwned(int) const {
    return false;
}
bool DeviceManager::info(unsigned, Info&) {
    return false;
}
bool DeviceManager::settingInfo(unsigned, unsigned, SettingInfo&) {
    return false;
}
bool DeviceManager::beginSettingsEdit(unsigned, SettingsDraft&) {
    return false;
}
bool DeviceManager::previewSetting(unsigned, unsigned, const SettingsDraft&, SettingInfo&) {
    return false;
}
bool DeviceManager::applySettings(unsigned, const SettingsDraft&) {
    return false;
}
bool DeviceManager::setEnabled(unsigned, bool) {
    return false;
}
bool DeviceManager::changeSetting(unsigned, unsigned, int) {
    return false;
}
bool DeviceManager::resetSettings(unsigned) {
    return false;
}
bool DeviceManager::setDisplayMode(Mode value) {
    return value == Internal;
}
bool DeviceManager::capture(Canvas*, Canvas*, const VolumeOverlaySnapshot&, bool) {
    return false;
}
bool DeviceManager::presentInternal() {
    return false;
}
DeviceManager devices;
} // namespace lilka
#endif

namespace lilka {
VolumeOverlaySnapshot DeviceManager::getOverlay() {
    if (!frameMutex) return VolumeOverlaySnapshot();
    xSemaphoreTake(frameMutex, portMAX_DELAY);
    const auto result = modeFeedback;
    xSemaphoreGive(frameMutex);
    return result;
}
} // namespace lilka
