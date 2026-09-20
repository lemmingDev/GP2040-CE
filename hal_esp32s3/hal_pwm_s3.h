// S3-only LEDC PWM helpers (Task 4). Shared by the buzzer, DRV8833 rumble,
// reactive-LED, and PWM player-LED addons; Task 5+ reuse halPwmConfig as-is.
#ifndef _HAL_PWM_S3_H_
#define _HAL_PWM_S3_H_

#if defined(ESP_PLATFORM)
#include <stdint.h>
#include "driver/ledc.h"

// Configure one LEDC channel (10-bit duty) on the given timer/channel.
void halPwmConfig(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct,
                  ledc_timer_t timer, ledc_channel_t channel);
// Buzzer shorthand: halPwmConfig(pin, freq, duty, TIMER_0, CHANNEL_0).
void halPwmTone(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct);

#endif // defined(ESP_PLATFORM)
#endif // _HAL_PWM_S3_H_
