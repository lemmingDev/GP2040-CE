// S3 RMT WS2812 backend (Task 5). See hal_ws2812_s3.h for the contract and
// documented deviations from the Pico PIO path.
#include "hal_ws2812_s3.h"

#if defined(ESP_PLATFORM)
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ws2812_s3";

LEDFormat NeoPico::GetFormat() {
  return format;
}

void NeoPico::PutPixel(int index, uint32_t pixel) {
  if (strip == nullptr) return;
  uint32_t r = 0, g = 0, b = 0, w = 0;
  switch (format) {
    case LED_FORMAT_GRB:
      g = (pixel >> 16) & 0xFF;
      r = (pixel >> 8) & 0xFF;
      b = pixel & 0xFF;
      break;
    case LED_FORMAT_RGB:
      r = (pixel >> 16) & 0xFF;
      g = (pixel >> 8) & 0xFF;
      b = pixel & 0xFF;
      break;
    case LED_FORMAT_GRBW:
      if (pixel <= 0xFF) {
        // Grayscale shortcut in RGB::value(): white level lands on W.
        w = pixel & 0xFF;
      } else {
        g = (pixel >> 24) & 0xFF;
        r = (pixel >> 16) & 0xFF;
        b = (pixel >> 8) & 0xFF;
        w = pixel & 0xFF;
      }
      break;
    case LED_FORMAT_RGBW:
      if (pixel <= 0xFF) {
        w = pixel & 0xFF;
      } else {
        r = (pixel >> 24) & 0xFF;
        g = (pixel >> 16) & 0xFF;
        b = (pixel >> 8) & 0xFF;
        w = pixel & 0xFF;
      }
      break;
  }
  if (format == LED_FORMAT_GRBW || format == LED_FORMAT_RGBW) {
    ESP_ERROR_CHECK(led_strip_set_pixel_rgbw(strip, (uint32_t)index, r, g, b, w));
  } else {
    ESP_ERROR_CHECK(led_strip_set_pixel(strip, (uint32_t)index, r, g, b));
  }
}

NeoPico::NeoPico(int ledPin, int numPixels, LEDFormat format)
    : format(format), ledPin(ledPin), numPixels(numPixels) {
  this->Clear();
  if (ledPin < 0 || numPixels <= 0) {
    return; // Dummy instance (addon setup path): no RMT channel, all no-ops.
  }
  const bool isRgbw = (format == LED_FORMAT_GRBW) || (format == LED_FORMAT_RGBW);
  led_strip_config_t strip_config = {};
  strip_config.strip_gpio_num = ledPin;
  strip_config.max_leds = (uint32_t)numPixels;
  strip_config.led_pixel_format = isRgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB;
  strip_config.led_model = LED_MODEL_WS2812;
  strip_config.flags.invert_out = false;
  led_strip_rmt_config_t rmt_config = {};
  rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_config.resolution_hz = 0; // Component default (10 MHz) = 800 kHz WS2812 timing.
  // One RMT symbol per strip bit; allocate the whole frame so a >64-symbol
  // strip never underflows the channel FIFO. S3 shares 192 blocks of 64
  // symbols across 4 channels; 100 px * 32 bits = 50 blocks worst case.
  size_t symbols = (size_t)numPixels * (isRgbw ? 32u : 24u);
  rmt_config.mem_block_symbols = symbols < 64 ? 64 : symbols;
  rmt_config.flags.with_dma = false;
  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "led_strip_new_rmt_device failed (%s); pixels stay dark", esp_err_to_name(err));
    strip = nullptr;
    return;
  }
  this->Off();
}

NeoPico::~NeoPico() {
  if (strip != nullptr) {
    ESP_ERROR_CHECK(led_strip_del(strip));
    strip = nullptr;
  }
}

void NeoPico::Clear() {
  memset(frame, 0, sizeof(frame));
}

void NeoPico::SetFrame(uint32_t newFrame[100]) {
  memcpy(frame, newFrame, sizeof(frame));
}

void NeoPico::Show() {
  if (strip == nullptr) return;
  for (int i = 0; i < this->numPixels; ++i) {
    this->PutPixel(i, this->frame[i]);
  }
  ESP_ERROR_CHECK(led_strip_refresh(strip));
}

void NeoPico::Off() {
  Clear();
  if (strip == nullptr) return;
  ESP_ERROR_CHECK(led_strip_clear(strip));
  ESP_ERROR_CHECK(led_strip_refresh(strip));
}

#endif
