#ifndef _HAL_TIME_H_
#define _HAL_TIME_H_

#include <stdint.h>

namespace hal {
void sleepMs(uint32_t ms);
uint32_t millis();
} // namespace hal

#endif
