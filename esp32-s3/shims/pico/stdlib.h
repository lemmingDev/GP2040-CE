// S3-only pico/stdlib.h shim (Task 5). The real SDK header pulls the whole
// Pico runtime; S3-compiled TUs (display stack, AnimationStation) only need
// the time API plus gpio_get_all(), so this shim provides exactly that
// surface. See pico/time.h and hardware/gpio.h in this directory.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "pico/time.h"
#include "hardware/gpio.h"

// Runtime-surface parity (Task 5, extend by compile demand only): the real
// Pico SDK header pulls the whole Pico runtime, which the display stack
// relies on transitively — MIN/MAX (tiny_ssd1306.cpp:152, via displaybase.h)
// and <algorithm> (std::sort in GPScreen.cpp:12, via GPGFX.h). Both call
// sites are in UNTOUCHED display sources, so the S3 shim provides them here.
// Guarded so an IDF header defining MIN/MAX first still wins silently.
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifdef __cplusplus
#include <algorithm>
#endif
