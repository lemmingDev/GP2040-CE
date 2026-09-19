#ifndef _HAL_GPIO_H_
#define _HAL_GPIO_H_

#include <stdint.h>

namespace hal {
void gpioInit(uint8_t pin);
void gpioSetInput(uint8_t pin, bool pullUp);
void gpioSetOutput(uint8_t pin);
bool gpioGet(uint8_t pin);
void gpioPut(uint8_t pin, bool value);
} // namespace hal

#endif
