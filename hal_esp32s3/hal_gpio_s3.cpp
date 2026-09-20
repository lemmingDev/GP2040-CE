#include "hal_gpio.h"

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"

namespace hal {
// Reserved-pin rule: GPIO 19/20 (USB) and 0/3/45/46 (strapping) must never be
// passed here — enforced by the Task 1 board table.
// Range guard: S3 GPIOs are 0-48. Reject anything above (e.g. (uint8_t)-1
// from an unassigned -1 pin) before it can shift-overflow the bit mask or
// reach the driver; nonexistent pins (22-32) fail here too instead of
// deep inside IDF.
static inline bool gpioNumOk(uint8_t pin) { return pin <= 48; }
void gpioInit(uint8_t pin) {
    if (!gpioNumOk(pin)) return;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = GPIO_MODE_INPUT;
    gpio_config(&cfg);
}
void gpioSetInput(uint8_t pin, bool pullUp) {
    if (!gpioNumOk(pin)) return;
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)pin, pullUp ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}
void gpioSetOutput(uint8_t pin) {
    if (!gpioNumOk(pin)) return;
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}
bool gpioGet(uint8_t pin) {
    if (!gpioNumOk(pin)) return false;
    return gpio_get_level((gpio_num_t)pin) != 0;
}
void gpioPut(uint8_t pin, bool value) {
    if (!gpioNumOk(pin)) return;
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);
}
} // namespace hal

#endif
