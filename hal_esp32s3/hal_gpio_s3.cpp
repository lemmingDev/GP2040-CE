#include "hal_gpio.h"

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"

namespace hal {
// Reserved-pin rule: GPIO 19/20 (USB) and 0/3/45/46 (strapping) must never be
// passed here — enforced by the Task 1 board table.
void gpioInit(uint8_t pin) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = GPIO_MODE_INPUT;
    gpio_config(&cfg);
}
void gpioSetInput(uint8_t pin, bool pullUp) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)pin, pullUp ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}
void gpioSetOutput(uint8_t pin) { gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT); }
bool gpioGet(uint8_t pin) { return gpio_get_level((gpio_num_t)pin) != 0; }
void gpioPut(uint8_t pin, bool value) { gpio_set_level((gpio_num_t)pin, value ? 1 : 0); }
} // namespace hal

#endif
