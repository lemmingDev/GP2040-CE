#include "FlashPROM.h"

#if defined(ESP_PLATFORM)
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_log.h"

uint8_t FlashPROM::writeCache[EEPROM_SIZE_BYTES];

static const esp_partition_t *gpconfigPart() {
    // 0x06 = custom data subtype (partitions.csv has no IDF enum name for it);
    // static_cast required: IDF v5.4 types the parameter as esp_partition_subtype_t.
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    static_cast<esp_partition_subtype_t>(0x06), "gpconfig");
}

static void commitTimerCb(void *arg) {
    (void)arg;
    const esp_partition_t *p = gpconfigPart();
    if (p == nullptr) return;
    ESP_ERROR_CHECK(esp_partition_erase_range(p, 0, EEPROM_SIZE_BYTES));
    // 16 KiB = 4x 4 KiB sectors; sequential 256 B page writes.
    for (size_t off = 0; off < EEPROM_SIZE_BYTES; off += 256) {
        ESP_ERROR_CHECK(esp_partition_write(p, off, &FlashPROM::writeCache[off], 256));
    }
}

void FlashPROM::start() {
    const esp_partition_t *p = gpconfigPart();
    if (p == nullptr) return;
    ESP_ERROR_CHECK(esp_partition_read(p, 0, writeCache, EEPROM_SIZE_BYTES));
}

void FlashPROM::commit() {
    static esp_timer_handle_t t = nullptr;
    if (t == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = commitTimerCb;
        args.name = "gpconfig_commit";
        ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    }
    ESP_ERROR_CHECK(esp_timer_stop(t));
    ESP_ERROR_CHECK(esp_timer_start_once(t, (uint64_t)EEPROM_WRITE_WAIT * 1000ULL));
}

void FlashPROM::reset() {
    memset(writeCache, 0, EEPROM_SIZE_BYTES);
    commit();
}
#endif
