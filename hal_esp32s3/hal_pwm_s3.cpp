#if defined(ESP_PLATFORM)
#include "driver/ledc.h"

void halPwmTone(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz = freqHz;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {};
    c.gpio_num = gpioPin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = (1023u * dutyPct) / 100u;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
#endif
