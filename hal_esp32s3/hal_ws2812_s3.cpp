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

NeoPico::NeoPico() {
  this->Clear();
}

NeoPico::NeoPico(int ledPin, int numPixels, LEDFormat format) {
  this->Clear();
  this->Setup(ledPin, numPixels, format);
}

void NeoPico::Setup(int ledPin, int inNumPixels, LEDFormat inFormat) {
  if (strip != nullptr) {
    ESP_ERROR_CHECK(led_strip_del(strip));
    strip = nullptr;
  }
  format = inFormat;
  this->ledPin = ledPin;
  numPixels = inNumPixels;
  ESP_LOGI(TAG, "Setup pin=%d pixels=%d format=%d", ledPin, inNumPixels, (int)inFormat);
  if (ledPin < 0 || numPixels <= 0) {
    ESP_LOGW(TAG, "dummy instance (pin<0 or pixels<=0): no RMT channel, all no-ops");
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
  // mem_block_symbols = 0 selects the component default (64). A large
  // explicit request (whole-frame preallocation) risks channel-creation
  // failure on S3 (dark pixels, log-only error); the driver refills short
  // channels from the encoder in ISR, so 64 works for any strip length.
  // (Hardware 2026-09-20: dark with 384 symbols request — using defaults.)
  rmt_config.mem_block_symbols = 0;
  rmt_config.flags.with_dma = false;
  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "led_strip_new_rmt_device failed (%s); pixels stay dark", esp_err_to_name(err));
    strip = nullptr;
    return;
  }
  ESP_LOGI(TAG, "RMT channel ready on pin %d for %d pixel(s)", ledPin, numPixels);
  this->Off();
}

void NeoPico::ChangeNumPixels(int inNumPixels) {
  if (inNumPixels == numPixels) return;
  // RMT channel capacity is fixed at creation; re-run Setup so max_leds
  // tracks the new count (same clamping contract as Pico: FRAME_MAX cap
  // is enforced by the addon, see neopicoleds.cpp pledIndexes guard).
  this->Setup(ledPin, inNumPixels, format);
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
  // S3 lazy RMT bring-up: a channel created during aux setup (~700 ms,
  // pre-WiFi) can silently never emit even though creation reports success,
  // while an identical channel created later in steady state works
  // (proven via pin-sweep test 2026-09-22). Recreate once on first frame
  // push; afterwards the normal path runs.
  if (!rmtReady && ledPin >= 0 && numPixels > 0) {
    ESP_LOGI(TAG, "late RMT recreate on pin %d for %d pixel(s)", ledPin, numPixels);
    this->Setup(ledPin, numPixels, format);
    rmtReady = (strip != nullptr);
  }
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
