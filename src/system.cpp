#include "system.h"

#if defined(PICO_BOARD)
#include "usbhostmanager.h"

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/multicore.h>

#include <malloc.h>

extern char __flash_binary_start;
extern char __flash_binary_end;
extern char __bss_end__;
extern char __StackLimit;
extern char __StackTop;
#elif defined(ESP_PLATFORM)
#include "esp_system.h"
#include "esp_heap_caps.h"
#endif

uint32_t System::getTotalFlash() {
#if defined(PICO_BOARD)
#if defined(PICO_FLASH_SIZE_BYTES)
    return PICO_FLASH_SIZE_BYTES;
#else
    #warning PICO_FLASH_SIZE_BYTES is not set, defaulting to 2MB
    return 2 * 1024 * 1024;
#endif
#elif defined(ESP_PLATFORM)
    // S3 DevKitC-1 flash size note (sdkconfig: 16 MB); the JEDEC-ID capacity
    // check below is deferred to the storage task — comment it there.
    return 16 * 1024 * 1024;
#endif
}

uint32_t System::getUsedFlash() {
#if defined(PICO_BOARD)
    return &__flash_binary_end - &__flash_binary_start;
#elif defined(ESP_PLATFORM)
    // Pico linker-script symbols don't exist on S3; surface real values in
    // Phase 3 (webconfig status screen).
    return 0;
#endif
}

#define STORAGE_CMD_TOTAL_BYTES 3

// Standard Storage instruction: 9f command prefix, Manufacturer ID, Flash Type, Capacity
#define FLASH_STORAGE_CMD 0x9f
#define FLASH_STORAGE_DATA_BYTES 3
#define FLASH_STORAGE_TOTAL_BYTES (1 + FLASH_STORAGE_DATA_BYTES)

uint32_t System::getPhysicalFlash() {

#if defined(PICO_BOARD)
    uint8_t txbuf[FLASH_STORAGE_TOTAL_BYTES] = {0};
    uint8_t rxbuf[FLASH_STORAGE_TOTAL_BYTES] = {0};
    txbuf[0] = FLASH_STORAGE_CMD;
    flash_do_cmd(txbuf, rxbuf, FLASH_STORAGE_TOTAL_BYTES);
    return 1 << rxbuf[3];
#elif defined(ESP_PLATFORM)
    // S3: flash_do_cmd/JEDEC-ID capacity check deferred to the storage task;
    // report the fixed 16 MiB sdkconfig note for now.
    return 16 * 1024 * 1024;
#endif
}

uint32_t System::getStaticAllocs() {
#if defined(PICO_BOARD)
    const uint32_t inMemorySegmentsSize = reinterpret_cast<uint32_t>(&__bss_end__) - SRAM_BASE;
    const uint32_t stackSize = &__StackTop - &__StackLimit;
    return inMemorySegmentsSize + stackSize;
#elif defined(ESP_PLATFORM)
    // Pico SRAM layout symbols don't exist on S3; Phase 3 surfaces real values.
    return 0;
#endif
}

uint32_t System::getTotalHeap() {
#if defined(PICO_BOARD)
    return &__StackLimit  - &__bss_end__;
#elif defined(ESP_PLATFORM)
    return heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
#endif
}

uint32_t System::getUsedHeap() {
#if defined(PICO_BOARD)
    return mallinfo().uordblks;
#elif defined(ESP_PLATFORM)
    return heap_caps_get_total_size(MALLOC_CAP_DEFAULT) - heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
#endif
}

void System::reboot(BootMode bootMode) {
#if defined(PICO_BOARD)
    // Halt all running USB instances
    USBHostManager::getInstance().shutdown();

    // Make sure that the other core is halted
    // We do not want it to be talking to devices (e.g. OLED display) while we reboot
	multicore_lockout_start_timeout_us(0xfffffffffffffff);

	watchdog_hw->scratch[5] = static_cast<uint32_t>(bootMode);

    // This is based on MicroPythons machine.reset()
	watchdog_reboot(0, 0, 0);
	for (;;) {
		__wfi();
	}
#elif defined(ESP_PLATFORM)
    // S3: no USB host to shut down (Phase 2), no multicore lockout
    // (single-core bring-up). Boot-mode word has no watchdog scratch
    // equivalent yet — Phase 3 webconfig boot-mode: RTC-retain replacement.
    // Default to gamepad mode.
    (void)bootMode;
    esp_restart();
    for (;;) {
    }
#endif
}

System::BootMode System::takeBootMode() {
#if defined(PICO_BOARD)
    // If the boot was not caused by software we don't enter any of the special modes
    if (!watchdog_caused_reboot()) {
        return BootMode::DEFAULT;
    }

    BootMode bootMode = static_cast<BootMode>(watchdog_hw->scratch[5]);
    if (bootMode != BootMode::GAMEPAD && bootMode != BootMode::WEBCONFIG && bootMode != BootMode::USB) {
        bootMode = BootMode::DEFAULT;
    }

    // Reset the scratch register
    // Subsequent reboots should revert to BootMode::DEFAULT
    watchdog_hw->scratch[5] = static_cast<uint32_t>(BootMode::DEFAULT);

    return bootMode;
#elif defined(ESP_PLATFORM)
    // Phase 3 webconfig boot-mode: RTC-retain replacement. Default to
    // gamepad mode until then.
    return BootMode::DEFAULT;
#endif
}
