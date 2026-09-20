// S3-only Pico SDK time API shim (Task 5).
//
// Why this exists: the LED/display stacks (lib/AnimationStation,
// src/addons/neopicoleds, src/display) use the Pico SDK time API
// (absolute_time_t, nil_time, make_timeout_time_ms, time_reached, ...),
// and those sources must stay UNTOUCHED on the S3 path.
//
// Semantics are Task 4's PlayerLEDs.h S3 block, promoted here verbatim:
// int64 microseconds-since-boot deadlines over esp_timer_get_time().
// PlayerLEDs.h now includes this file on BOTH platforms (same "pico/time.h"
// path hits the SDK on Pico, this shim on S3), so there is exactly one S3
// time base. Extensions beyond Task 4 (nil_time, *_us variants, diff,
// sleep_*) cover the AnimationStation/display call sites.
//
// Scope rules: this directory is on the S3 include path ONLY (never on the
// Pico path). Files here shadow Pico SDK headers by relative path
// (pico/..., hardware/...). Keep each shim to the symbols S3-compiled TUs
// actually use; extend by compile demand, never speculatively.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Monotonic microsecond deadline (Task 4 PlayerLEDs.h definition).
typedef int64_t absolute_time_t;

#define nil_time ((absolute_time_t)0)

static inline uint64_t to_us_since_boot(absolute_time_t t) {
    return (uint64_t)t;
}

static inline bool is_nil_time(absolute_time_t t) {
    return t == 0;
}

static inline absolute_time_t get_absolute_time(void) {
    return esp_timer_get_time();
}

static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}

static inline absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return esp_timer_get_time() + (absolute_time_t)ms * 1000;
}

static inline absolute_time_t make_timeout_time_us(uint64_t us) {
    return esp_timer_get_time() + (absolute_time_t)us;
}

static inline bool time_reached(absolute_time_t t) {
    return esp_timer_get_time() >= t;
}

// Implemented in hal_esp32s3/hal_pico_shim_s3.cpp (need FreeRTOS/ROM calls).
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

#ifdef __cplusplus
}
#endif
