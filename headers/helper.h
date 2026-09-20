#ifndef _HELPER_H_
#define _HELPER_H_

#if defined(PICO_BOARD)
#include "pico/time.h"
#endif
#include <string>

#include "BoardConfig.h"
#include <stdint.h>
#if defined(PICO_BOARD) || defined(ESP_PLATFORM)
// S3: AnimationStation/NeoPico types (RGB, LEDFormat) come from the
// Task-5 RMT backend (hal_ws2812_s3.h, identical enum values); the header
// is S3-safe (Pico SDK includes are shim-backed, proven by neopicoleds.cpp).
#include "AnimationStation.hpp"
#endif
#include "PlayerLEDs.h"

// GP2040-CE Board Config (64 character limit)
#ifndef GP2040_BOARDCONFIG
#define GP2040_BOARDCONFIG "Unknown"
#endif

#define PLED_REPORT_SIZE 32

#ifndef PLED1_PIN
#define PLED1_PIN -1
#endif
#ifndef PLED2_PIN
#define PLED2_PIN -1
#endif
#ifndef PLED3_PIN
#define PLED3_PIN -1
#endif
#ifndef PLED4_PIN
#define PLED4_PIN -1
#endif
#ifndef PLED_TYPE
#define PLED_TYPE PLED_TYPE_NONE
#endif
#ifndef PLED_COLOR
#define PLED_COLOR ColorWhite // White
#endif

#ifndef CASE_RGB_TYPE
#define CASE_RGB_TYPE CASE_RGB_TYPE_NONE
#endif

#ifndef CASE_RGB_INDEX
#define CASE_RGB_INDEX -1
#endif

#ifndef CASE_RGB_COLOR
#define CASE_RGB_COLOR ColorGreen // Green
#endif

#ifndef CASE_RGB_COUNT
#define CASE_RGB_COUNT 0
#endif

static inline bool isValidPin(int32_t pin) {
#if defined(ESP_PLATFORM)
    // S3: routable GPIOs are 0-48 except the native-USB pair (19/20), which
    // must never be GPIO (found on hardware 2026-09-20: pins 38/48 failed
    // this check against the RP2040-era 30-pin window, so the onboard pixel
    // never initialized). Non-existent pins (22-32) fail later at the GPIO
    // driver with a log; strapping pins are the board table's business.
    return pin >= 0 && pin <= 48 && pin != 19 && pin != 20;
#else
    int32_t numBank0GPIOS = NUM_BANK0_GPIOS;
    return pin >= 0 && pin < numBank0GPIOS;
#endif
}

#endif
