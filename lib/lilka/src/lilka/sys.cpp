#include "sys.h"

#if CONFIG_IDF_TARGET_ESP32S3
#    include <esp_private/system_internal.h>
#    include <soc/rtc_cntl_reg.h>
#    include <soc/soc.h>
#    include <soc/usb_serial_jtag_reg.h>
#    if CONFIG_TINYUSB_ENABLED
#        include <tusb.h>
#    endif
#endif

namespace lilka {

#define FOREACH_PARTITION()                                                                                          \
    esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL); \
    int i = 0;                                                                                                       \
    for (i = 0; iterator != NULL; i++, iterator = esp_partition_next(iterator))

int Sys::get_partition_labels(String labels[]) {
    FOREACH_PARTITION() {
        const esp_partition_t* partition = esp_partition_get(iterator);
        labels[i] = String(partition->label);
    }
    return i;
}

uint64_t Sys::get_partition_address(const char* label) {
    FOREACH_PARTITION() {
        const esp_partition_t* partition = esp_partition_get(iterator);
        if (strcmp(partition->label, label) == 0) {
            return partition->address;
        }
    }
    return 0;
}

uint64_t Sys::get_partition_size(const char* label) {
    FOREACH_PARTITION() {
        const esp_partition_t* partition = esp_partition_get(iterator);
        if (strcmp(partition->label, label) == 0) {
            return partition->size;
        }
    }
    return 0;
}

void Sys::print_partition_table() {
    esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (iterator != NULL) {
        const esp_partition_t* partition = esp_partition_get(iterator);
        printf(
            "Partition label=%10s type=%02X subtype=%02X address=0x%08X size=0x%08X\n",
            partition->label,
            partition->type,
            partition->subtype,
            partition->address,
            partition->size
        );
        iterator = esp_partition_next(iterator);
    }
}

[[noreturn]] void Sys::restart() {
#if CONFIG_IDF_TARGET_ESP32S3
#    if CONFIG_TINYUSB_ENABLED
    if (tud_inited()) {
        tud_disconnect();
    } else {
        CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    }
#    else
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
#    endif

    // Give the host time to observe removal of either TinyUSB or hardware
    // CDC/JTAG before the bootloader presents a USB device again.
    vTaskDelay(pdMS_TO_TICKS(2000));

    CLEAR_PERI_REG_MASK(
        RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE
    );
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    esp_restart_noos_dig();
#else
    esp_restart();
#endif
}

Sys sys;

} // namespace lilka
