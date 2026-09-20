// LEDC allocation (Task 4; ESP32-S3 LEDC = 4 timers x 8 low-speed channels):
//   TIMER_0/CHANNEL_0 ..... buzzer (via halPwmTone)
//   TIMER_1 + CHANNEL_0/1 . DRV8833 left/right rumble motors
//   TIMER_2 + channels .... reactive LEDs, channel = LED index 0..7
//                           (indices >= 8 are skipped on S3 — no channel left)
//   TIMER_3 + CHANNEL_0..3  PWM player LEDs, channel = LED index 0..3
// NOTE: S3 LEDC channels are a single pool of 8 shared across timers, so
// channel numbers overlap between addons above (last-writer-wins per channel).
// Typical single-addon configs are unaffected; a global channel plan is future
// work if concurrent PWM addons need it (see task-4 report concern C1).
#if defined(ESP_PLATFORM)
#include "hal_pwm_s3.h"

void halPwmConfig(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct,
                  ledc_timer_t timer, ledc_channel_t channel) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = timer;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz = freqHz;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {};
    c.gpio_num = gpioPin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = channel;
    c.timer_sel = timer;
    c.duty = (1023u * dutyPct) / 100u;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void halPwmTone(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct) {
    halPwmConfig(gpioPin, freqHz, dutyPct, LEDC_TIMER_0, LEDC_CHANNEL_0);
}
#endif
