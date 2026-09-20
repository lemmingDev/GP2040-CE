// S3 implementations of the esp32-s3/shims Pico SDK surface (Task 5).
// The time API itself is static-inline in shims/pico/time.h; this TU holds
// the two sleep routines plus gpio_get_all(), which mirrors the Pico
// bitmask over pins 0..NUM_BANK0_GPIOS-1 via per-pin hal::gpioGet()
// (S3 GPIOs >= 30 are invisible to the mask, same 30-pin window the core
// loop iterates).
#include "hal_gpio.h"

#include "pico/time.h"
#include "hardware/gpio.h"
#include "types.h"

#if defined(ESP_PLATFORM)
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void sleep_us(uint64_t us) {
    esp_rom_delay_us((uint32_t)us);
}

uint32_t gpio_get_all(void) {
    uint32_t mask = 0;
    for (int32_t pin = 0; pin < (int32_t)NUM_BANK0_GPIOS; pin++) {
        if (hal::gpioGet((uint8_t)pin)) {
            mask |= (1u << pin);
        }
    }
    return mask;
}

#endif
