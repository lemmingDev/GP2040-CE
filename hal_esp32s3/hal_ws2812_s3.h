// S3 RMT WS2812 backend (Task 5). Same public method set as Pico NeoPico
// (lib/NeoPico/src/NeoPico.hpp) so src/addons/neopicoleds.cpp and the
// AnimationStation headers compile against it via a 3-line include swap;
// pixels are driven by the Espressif led_strip component over RMT
// (800 kHz WS2812 timing + GRB order handled by the component).
//
// Deviations from the PIO path (deliberate, documented):
// - Adds a destructor: the addon does `delete neopico` on reconfigure, and
//   the RMT channel + encoder must be freed (Pico leaks the PIO here).
// - RGBW/RGBW formats use the component's native GRBW pixel format +
//   led_strip_set_pixel_rgbw(), so a nonzero W drives the white die instead
//   of being bit-shifted through the PIO FIFO. In practice W is always 0
//   (RGB::value() never sets it — no 4-arg RGB exists in the codebase).
// - Show() blocks in rmt_tx_wait_all_done() inside led_strip_refresh()
//   instead of sleeping 10 ms after pushing (same latch guarantee, no
//   fixed stall on the aux core).
// - A dummy instance (pin < 0 or 0 pixels, as setup() builds) allocates no
//   RMT channel; every method is a safe no-op.
#ifndef _HAL_WS2812_S3_H_
#define _HAL_WS2812_S3_H_

#if defined(ESP_PLATFORM)
#include <stdint.h>
#include "led_strip.h"

// Identical values to Pico NeoPico's LEDFormat (Animation::format and
// RGB::value() switch on these; do not renumber).
typedef enum
{
  LED_FORMAT_GRB = 0,
  LED_FORMAT_RGB = 1,
  LED_FORMAT_GRBW = 2,
  LED_FORMAT_RGBW = 3,
} LEDFormat;

class NeoPico
{
public:
  NeoPico(int ledPin, int numPixels, LEDFormat format = LED_FORMAT_GRB);
  ~NeoPico();
  void Show();
  void Clear();
  void Off();
  // DIAG-PIXEL (temporary): no log channel while OTG owns USB.
  bool IsLive() const { return strip != nullptr; }
  LEDFormat GetFormat();
  void SetFrame(uint32_t newFrame[100]);
private:
  void PutPixel(int index, uint32_t pixel);
  LEDFormat format;
  int ledPin = -1;
  int numPixels = 0;
  uint32_t frame[100];
  led_strip_handle_t strip = nullptr;
};

#endif // defined(ESP_PLATFORM)
#endif // _HAL_WS2812_S3_H_
