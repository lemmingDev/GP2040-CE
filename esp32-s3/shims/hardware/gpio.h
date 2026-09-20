// S3-only hardware/gpio.h shim (Task 5). Only gpio_get_all() is used by
// S3-compiled TUs (PinViewerScreen.cpp, GPButton.cpp, via the
// peripheral_i2c.h include chain); it is implemented in
// hal_esp32s3/hal_pico_shim_s3.cpp as a per-pin hal::gpioGet() loop over
// NUM_BANK0_GPIOS (global constraint: no bulk GPIO read on S3).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t gpio_get_all(void);

#ifdef __cplusplus
}
#endif
