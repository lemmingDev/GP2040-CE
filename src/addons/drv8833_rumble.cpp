#if defined(PICO_BOARD)
#include "hardware/pwm.h"
#endif
#include "addons/drv8833_rumble.h"
#include "storagemanager.h"
#if defined(PICO_BOARD)
#include "peripheralmanager.h"
#endif
#include "usbdriver.h"
#include "math.h"
#include "helper.h"
#include "config.pb.h"

#include <stdio.h>
#if defined(PICO_BOARD)
#include "pico/stdlib.h"
#elif defined(ESP_PLATFORM)
#include "hal_gpio.h"
#include "hal_pwm_s3.h"
#endif

bool DRV8833RumbleAddon::available() {
	const DRV8833RumbleOptions& options = Storage::getInstance().getAddonOptions().drv8833RumbleOptions;
	return options.enabled && (isValidPin(options.leftMotorPin) && isValidPin(options.rightMotorPin));
}

void DRV8833RumbleAddon::setup() {
	const DRV8833RumbleOptions& options = Storage::getInstance().getAddonOptions().drv8833RumbleOptions;
    Gamepad * gamepad = Storage::getInstance().GetProcessedGamepad();

	leftMotorPin = options.leftMotorPin;
	rightMotorPin = options.rightMotorPin;
	motorSleepPin = options.motorSleepPin;
	pwmFrequency = options.pwmFrequency;
	dutyMin = options.dutyMin;
	dutyMax = options.dutyMax;

	// TODO: More robust clock check. Currently just assumes 120 MHz if USB Enabled, 125 MHz otherwise.
#if defined(PICO_BOARD)
	if ( PeripheralManager::getInstance().isUSBEnabled(0) )
		sysClock = 120000000;
	else
		sysClock = 125000000;
#endif
	// S3: sysClock unused — LEDC takes the explicit pwmFrequency below.


	// enable haptics in Aux sensors depending on pin assignments
	if(isValidPin(leftMotorPin)) {
#if defined(PICO_BOARD)
		gpio_set_function(leftMotorPin, GPIO_FUNC_PWM);
		leftMotorPinSlice = pwm_gpio_to_slice_num (leftMotorPin);
		leftMotorPinChannel = pwm_gpio_to_channel (leftMotorPin);
		pwmSetFreqDuty(leftMotorPinSlice, leftMotorPinChannel, pwmFrequency, 0);
		pwm_set_enabled(leftMotorPinSlice, true);
#elif defined(ESP_PLATFORM)
		// Same pwmFrequency at 0% duty; TIMER_1/CHANNEL_0 per allocation.
		halPwmConfig(leftMotorPin, pwmFrequency, 0, LEDC_TIMER_1, LEDC_CHANNEL_0);
#endif
		gamepad->auxState.haptics.leftActuator.enabled = true;
	}

	if(isValidPin(rightMotorPin)) {
#if defined(PICO_BOARD)
		gpio_set_function(rightMotorPin, GPIO_FUNC_PWM);
		rightMotorPinSlice = pwm_gpio_to_slice_num (rightMotorPin);
		rightMotorPinChannel = pwm_gpio_to_channel (rightMotorPin);
		pwmSetFreqDuty(rightMotorPinSlice, rightMotorPinChannel, pwmFrequency, 0);
		pwm_set_enabled(rightMotorPinSlice, true);
#elif defined(ESP_PLATFORM)
		// Same pwmFrequency at 0% duty; TIMER_1/CHANNEL_1 per allocation.
		halPwmConfig(rightMotorPin, pwmFrequency, 0, LEDC_TIMER_1, LEDC_CHANNEL_1);
#endif
		gamepad->auxState.haptics.rightActuator.enabled = true;
	}

	if(isValidPin(motorSleepPin)) {
#if defined(PICO_BOARD)
		gpio_init(motorSleepPin);
		gpio_set_dir(motorSleepPin, GPIO_OUT);
		// turn on sleep mode
		gpio_put(motorSleepPin, false);
#elif defined(ESP_PLATFORM)
		hal::gpioInit(motorSleepPin);
		hal::gpioSetOutput(motorSleepPin);
		// turn on sleep mode
		hal::gpioPut(motorSleepPin, false);
#endif
	}
}

bool DRV8833RumbleAddon::compareRumbleState(Gamepad * gamepad) {
	if (currentRumbleState.leftActuator.active == gamepad->auxState.haptics.leftActuator.active && 
		currentRumbleState.leftActuator.intensity == gamepad->auxState.haptics.leftActuator.intensity && 
		currentRumbleState.rightActuator.active == gamepad->auxState.haptics.rightActuator.active && 
		currentRumbleState.rightActuator.intensity == gamepad->auxState.haptics.rightActuator.intensity)
		return true;

	return false;

}

void DRV8833RumbleAddon::setRumbleState(Gamepad * gamepad) {
	currentRumbleState.leftActuator.active = gamepad->auxState.haptics.leftActuator.active;
	currentRumbleState.leftActuator.intensity = gamepad->auxState.haptics.leftActuator.intensity;
	currentRumbleState.rightActuator.active = gamepad->auxState.haptics.rightActuator.active;
	currentRumbleState.rightActuator.intensity = gamepad->auxState.haptics.rightActuator.intensity;
}

void DRV8833RumbleAddon::disableMotors() {
	// if motorSleepPin set and all motors are off, enable motor driver sleep mode
	if (isValidPin(motorSleepPin))
#if defined(PICO_BOARD)
		gpio_put(motorSleepPin, false);
#elif defined(ESP_PLATFORM)
		hal::gpioPut(motorSleepPin, false);
#endif

#if defined(PICO_BOARD)
	pwmSetFreqDuty(leftMotorPinSlice, leftMotorPinChannel, pwmFrequency, 0);
	pwmSetFreqDuty(rightMotorPinSlice, rightMotorPinChannel, pwmFrequency, 0);
#elif defined(ESP_PLATFORM)
	halPwmConfig(leftMotorPin, pwmFrequency, 0, LEDC_TIMER_1, LEDC_CHANNEL_0);
	halPwmConfig(rightMotorPin, pwmFrequency, 0, LEDC_TIMER_1, LEDC_CHANNEL_1);
#endif
}

void DRV8833RumbleAddon::enableMotors(Gamepad * gamepad) {
#if defined(PICO_BOARD)
	pwmSetFreqDuty(leftMotorPinSlice, leftMotorPinChannel, pwmFrequency, (gamepad->auxState.haptics.leftActuator.intensity == 0) ? 0 : scaleDuty(motorToDuty(gamepad->auxState.haptics.leftActuator.intensity), dutyMin, dutyMax));
	pwmSetFreqDuty(rightMotorPinSlice, rightMotorPinChannel, pwmFrequency, (gamepad->auxState.haptics.rightActuator.intensity == 0) ? 0 : scaleDuty(motorToDuty(gamepad->auxState.haptics.rightActuator.intensity), dutyMin, dutyMax));
#elif defined(ESP_PLATFORM)
	// Identical duty curve to Pico (motorToDuty/scaleDuty percent, same
	// dutyMin/dutyMax/pins/frequency); +0.5f rounds float percent to nearest
	// uint8 dutyPct.
	float leftDuty = (gamepad->auxState.haptics.leftActuator.intensity == 0) ? 0 : scaleDuty(motorToDuty(gamepad->auxState.haptics.leftActuator.intensity), dutyMin, dutyMax);
	float rightDuty = (gamepad->auxState.haptics.rightActuator.intensity == 0) ? 0 : scaleDuty(motorToDuty(gamepad->auxState.haptics.rightActuator.intensity), dutyMin, dutyMax);
	halPwmConfig(leftMotorPin, pwmFrequency, (uint8_t)(leftDuty + 0.5f), LEDC_TIMER_1, LEDC_CHANNEL_0);
	halPwmConfig(rightMotorPin, pwmFrequency, (uint8_t)(rightDuty + 0.5f), LEDC_TIMER_1, LEDC_CHANNEL_1);
#endif

	// if motorSleepPin set and any motors are on, disable motor driver sleep mode
	if (isValidPin(motorSleepPin))
#if defined(PICO_BOARD)
		gpio_put(motorSleepPin, true);
#elif defined(ESP_PLATFORM)
		hal::gpioPut(motorSleepPin, true);
#endif
}

void DRV8833RumbleAddon::process() {
	Gamepad * gamepad = Storage::getInstance().GetProcessedGamepad();

	if (!compareRumbleState(gamepad)) {
		setRumbleState(gamepad);
		if (!(gamepad->auxState.haptics.leftActuator.active || gamepad->auxState.haptics.rightActuator.active)) {
			disableMotors();
			return;
		}
		enableMotors(gamepad);
	}
}

#if defined(PICO_BOARD)
uint32_t DRV8833RumbleAddon::pwmSetFreqDuty(uint slice, uint channel, uint32_t frequency, float duty) {
	uint32_t divider16 = sysClock / frequency / 4096 +
							(sysClock % (frequency * 4096) != 0);
	if (divider16 / 16 == 0)
	divider16 = 16;
	uint32_t wrap = sysClock * 16 / divider16 / frequency - 1;
	pwm_set_clkdiv_int_frac(slice, divider16/16,
										divider16 & 0xF);
	pwm_set_wrap(slice, wrap);
	pwm_set_chan_level(slice, channel, wrap * duty / 100);
	return wrap;
}
#endif

