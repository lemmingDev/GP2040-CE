#include "hal_gpio.h"
#include "hal_time.h"

#if defined(PICO_BOARD)
#include "hardware/gpio.h"
#include "pico/time.h"

namespace hal {
void gpioInit(uint8_t pin) { gpio_init(pin); }
void gpioSetInput(uint8_t pin, bool pullUp) {
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, pullUp, !pullUp);
}
void gpioSetOutput(uint8_t pin) { gpio_set_dir(pin, GPIO_OUT); }
bool gpioGet(uint8_t pin) { return gpio_get(pin); }
void gpioPut(uint8_t pin, bool value) { gpio_put(pin, value); }
void sleepMs(uint32_t ms) { sleep_ms(ms); }
uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }
} // namespace hal

#endif
