/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "storagemanager.h"

#include "BoardConfig.h"
#if defined(PICO_BOARD) || defined(ESP_PLATFORM)
#include "AnimationStorage.hpp"
#endif
#include "FlashPROM.h"
#include "eventmanager.h"
#if defined(PICO_BOARD)
#include "peripheralmanager.h"
#elif defined(ESP_PLATFORM)
// S3: PeripheralManager is not compiled in Phase 1 (no I2C/SPI/USB-PIO init,
// no host-gated saves); see Storage::save() below.
#endif
#include "config.pb.h"
#if defined(PICO_BOARD)
#include "hardware/watchdog.h"
#elif defined(ESP_PLATFORM)
#include "esp_system.h"
#endif
#include "CRC32.h"
#include "types.h"

#include "config_utils.h"

void Storage::init() {
	EEPROM.start();
#if defined(PICO_BOARD)
	critical_section_init(&animationOptionsCs);
#elif defined(ESP_PLATFORM)
	// S3: animationOptionsCs is a std::mutex; no init call needed.
#endif
	ConfigUtils::load(config);
}

/**
 * @brief Save the config, but only if it is safe to (as in USB host is not being used.)
 */
bool Storage::save()
{
	return save(false);
}

/**
 * @brief Save the config; if forcing a save is requested, or if USB host is not enabled, this will write to flash.
 */
bool Storage::save(const bool force) {
#if defined(PICO_BOARD)
	if (!PeripheralManager::getInstance().isUSBEnabled(0) || force) {
		return ConfigUtils::save(config);
	} else {
		return false;
	}
#elif defined(ESP_PLATFORM)
	// S3: no USB host until Phase 2, so saves are never gated.
	(void)force;
	return ConfigUtils::save(config);
#endif
}

#if defined(PICO_BOARD)
static void updateAnimationOptionsProto(const AnimationOptions& options)
{
	AnimationOptions_Proto& optionsProto = Storage::getInstance().getAnimationOptions();

	optionsProto.baseAnimationIndex			= options.baseAnimationIndex;
	optionsProto.brightness					= options.brightness;
	optionsProto.staticColorIndex			= options.staticColorIndex;
	optionsProto.buttonColorIndex			= options.buttonColorIndex;
	optionsProto.chaseCycleTime				= options.chaseCycleTime;
	optionsProto.rainbowCycleTime			= options.rainbowCycleTime;
	optionsProto.themeIndex					= options.themeIndex;
	optionsProto.hasCustomTheme				= options.hasCustomTheme;
	optionsProto.customThemeUp				= options.customThemeUp;
	optionsProto.customThemeDown			= options.customThemeDown;
	optionsProto.customThemeLeft			= options.customThemeLeft;
	optionsProto.customThemeRight			= options.customThemeRight;
	optionsProto.customThemeB1				= options.customThemeB1;
	optionsProto.customThemeB2				= options.customThemeB2;
	optionsProto.customThemeB3				= options.customThemeB3;
	optionsProto.customThemeB4				= options.customThemeB4;
	optionsProto.customThemeL1				= options.customThemeL1;
	optionsProto.customThemeR1				= options.customThemeR1;
	optionsProto.customThemeL2				= options.customThemeL2;
	optionsProto.customThemeR2				= options.customThemeR2;
	optionsProto.customThemeS1				= options.customThemeS1;
	optionsProto.customThemeS2				= options.customThemeS2;
	optionsProto.customThemeA1				= options.customThemeA1;
	optionsProto.customThemeA2				= options.customThemeA2;
	optionsProto.customThemeL3				= options.customThemeL3;
	optionsProto.customThemeR3				= options.customThemeR3;
	optionsProto.customThemeUpPressed		= options.customThemeUpPressed;
	optionsProto.customThemeDownPressed		= options.customThemeDownPressed;
	optionsProto.customThemeLeftPressed		= options.customThemeLeftPressed;
	optionsProto.customThemeRightPressed	= options.customThemeRightPressed;
	optionsProto.customThemeB1Pressed		= options.customThemeB1Pressed;
	optionsProto.customThemeB2Pressed		= options.customThemeB2Pressed;
	optionsProto.customThemeB3Pressed		= options.customThemeB3Pressed;
	optionsProto.customThemeB4Pressed		= options.customThemeB4Pressed;
	optionsProto.customThemeL1Pressed		= options.customThemeL1Pressed;
	optionsProto.customThemeR1Pressed		= options.customThemeR1Pressed;
	optionsProto.customThemeL2Pressed		= options.customThemeL2Pressed;
	optionsProto.customThemeR2Pressed		= options.customThemeR2Pressed;
	optionsProto.customThemeS1Pressed		= options.customThemeS1Pressed;
	optionsProto.customThemeS2Pressed		= options.customThemeS2Pressed;
	optionsProto.customThemeA1Pressed		= options.customThemeA1Pressed;
	optionsProto.customThemeA2Pressed		= options.customThemeA2Pressed;
	optionsProto.customThemeL3Pressed		= options.customThemeL3Pressed;
	optionsProto.customThemeR3Pressed		= options.customThemeR3Pressed;
	optionsProto.buttonPressColorCooldownTimeInMs = options.buttonPressColorCooldownTimeInMs;	
}
#endif
// S3: updateAnimationOptionsProto is Pico-only in Phase 1 (AnimationOptions
// lives in the Task-4/5 LED stack); performEnqueuedSaves below is a no-op.

void Storage::performEnqueuedSaves()
{
#if defined(PICO_BOARD)
	if (animationOptionsSavePending.load())
	{
		critical_section_enter_blocking(&animationOptionsCs);
		updateAnimationOptionsProto(animationOptionsToSave);
		save();
		animationOptionsSavePending.store(false);
		critical_section_exit(&animationOptionsCs);
	}
#elif defined(ESP_PLATFORM)
	// S3: LED animation stack lands in Tasks 4/5; nothing is ever enqueued.
	(void)animationOptionsSavePending;
#endif
}

#if defined(PICO_BOARD)
void Storage::enqueueAnimationOptionsSave(const AnimationOptions& animationOptions)
{
	const uint32_t crc = CRC32::calculate(&animationOptions);
	critical_section_enter_blocking(&animationOptionsCs);
	if (crc != animationOptionsCrc)
	{
		animationOptionsToSave = animationOptions;
		animationOptionsCrc = crc;
		animationOptionsSavePending.store(true);
	}
	critical_section_exit(&animationOptionsCs);
}
#endif
// S3: enqueueAnimationOptionsSave is Pico-only in Phase 1 (see above).

void Storage::ResetSettings()
{
	EEPROM.reset();
#if defined(PICO_BOARD)
	watchdog_reboot(0, SRAM_END, 2000);
#elif defined(ESP_PLATFORM)
	esp_restart();
#endif
}

bool Storage::setProfile(const uint32_t profileNum)
{
	// is this profile defined?
	if (profileNum >= 1 && profileNum <= config.profileOptions.gpioMappingsSets_count + 1) {
		// is this profile enabled?
		// profile 1 (core) is always enabled, others we must check
		if (profileNum == 1 || config.profileOptions.gpioMappingsSets[profileNum-2].enabled) {
			EventManager::getInstance().triggerEvent(new GPProfileChangeEvent(this->config.gamepadOptions.profileNumber, profileNum));
			this->config.gamepadOptions.profileNumber = profileNum;
			return true;
		}
	}
	// if we get here, the requested profile doesn't exist or isn't enabled, so don't change it
	return false;
}

void Storage::nextProfile()
{
	uint32_t profileCeiling = config.profileOptions.gpioMappingsSets_count + 1;
	uint32_t requestedProfile = (this->config.gamepadOptions.profileNumber % profileCeiling) + 1;
	while (!setProfile(requestedProfile)) {
		// if the set failed, try again with the next in the sequence
		requestedProfile = (requestedProfile % profileCeiling) + 1;
	}
}
void Storage::previousProfile()
{
	uint32_t profileCeiling = config.profileOptions.gpioMappingsSets_count + 1;
	uint32_t requestedProfile = this->config.gamepadOptions.profileNumber > 1 ?
			config.gamepadOptions.profileNumber - 1 : profileCeiling;
	while (!setProfile(requestedProfile)) {
		// if the set failed, try again with the next in the sequence
		requestedProfile = requestedProfile > 1 ? requestedProfile - 1 : profileCeiling;
	}
}

/**
 * @brief Return the current profile label.
 */
char* Storage::currentProfileLabel() {
	if (this->config.gamepadOptions.profileNumber == 1)
		return this->config.gpioMappings.profileLabel;
	else
		return this->config.profileOptions.gpioMappingsSets[config.gamepadOptions.profileNumber-2].profileLabel;
}

void Storage::setFunctionalPinMappings()
{
	GpioMappingInfo* alts = nullptr;
	if (config.gamepadOptions.profileNumber >= 2 &&
			config.gamepadOptions.profileNumber <= config.profileOptions.gpioMappingsSets_count + 1) {
		if (config.profileOptions.gpioMappingsSets[config.gamepadOptions.profileNumber-2].enabled) {
			alts = config.profileOptions.gpioMappingsSets[config.gamepadOptions.profileNumber-2].pins;
		}
	}

	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		// assign the functional pin to the profile pin if:
		// 1: there was a profile to load
		// 2: the new action isn't RESERVED or ASSIGNED_TO_ADDON (profiles can't affect special addons)
		// 3: the old action isn't RESERVED or ASSIGNED_TO_ADDON (profiles can't affect special addons)
		// else use whatever is in the core mapping
		if (alts != nullptr &&
				alts[pin].action != GpioAction::RESERVED &&
				alts[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
				this->config.gpioMappings.pins[pin].action != GpioAction::RESERVED &&
				this->config.gpioMappings.pins[pin].action != GpioAction::ASSIGNED_TO_ADDON) {
			functionalPinMappings[pin] = alts[pin];
		} else {
			functionalPinMappings[pin] = this->config.gpioMappings.pins[pin];
		}
	}
}

void Storage::SetConfigMode(bool mode) { // hack for config mode
	CONFIG_MODE = mode;
	previewDisplayOptions = config.displayOptions;
}

bool Storage::GetConfigMode()
{
	return CONFIG_MODE;
}

void Storage::SetGamepad(Gamepad * newpad)
{
	gamepad = newpad;
}

Gamepad * Storage::GetGamepad()
{
	return gamepad;
}

void Storage::SetProcessedGamepad(Gamepad * newpad)
{
	processedGamepad = newpad;
}

Gamepad * Storage::GetProcessedGamepad()
{
	return processedGamepad;
}

/* Animation stuffs */
#if defined(PICO_BOARD)
AnimationOptions AnimationStorage::getAnimationOptions()
{
	AnimationOptions options;
	const AnimationOptions_Proto& optionsProto = Storage::getInstance().getAnimationOptions();

	options.checksum				= 0;
	options.baseAnimationIndex		= std::min<uint32_t>(optionsProto.baseAnimationIndex, 255);
	options.brightness				= std::min<uint32_t>(optionsProto.brightness, 255);
	options.staticColorIndex		= std::min<uint32_t>(optionsProto.staticColorIndex, 255);
	options.buttonColorIndex		= std::min<uint32_t>(optionsProto.buttonColorIndex, 255);
	options.chaseCycleTime			= std::min<int32_t>(optionsProto.chaseCycleTime, 65535);
	options.rainbowCycleTime		= std::min<int32_t>(optionsProto.rainbowCycleTime, 65535);
	options.themeIndex				= std::min<uint8_t>(optionsProto.themeIndex, 255);
	options.hasCustomTheme			= optionsProto.hasCustomTheme;
	options.customThemeUp			= optionsProto.customThemeUp;
	options.customThemeDown			= optionsProto.customThemeDown;
	options.customThemeLeft			= optionsProto.customThemeLeft;
	options.customThemeRight		= optionsProto.customThemeRight;
	options.customThemeB1			= optionsProto.customThemeB1;
	options.customThemeB2			= optionsProto.customThemeB2;
	options.customThemeB3			= optionsProto.customThemeB3;
	options.customThemeB4			= optionsProto.customThemeB4;
	options.customThemeL1			= optionsProto.customThemeL1;
	options.customThemeR1			= optionsProto.customThemeR1;
	options.customThemeL2			= optionsProto.customThemeL2;
	options.customThemeR2			= optionsProto.customThemeR2;
	options.customThemeS1			= optionsProto.customThemeS1;
	options.customThemeS2			= optionsProto.customThemeS2;
	options.customThemeA1			= optionsProto.customThemeA1;
	options.customThemeA2			= optionsProto.customThemeA2;
	options.customThemeL3			= optionsProto.customThemeL3;
	options.customThemeR3			= optionsProto.customThemeR3;
	options.customThemeUpPressed	= optionsProto.customThemeUpPressed;
	options.customThemeDownPressed	= optionsProto.customThemeDownPressed;
	options.customThemeLeftPressed	= optionsProto.customThemeLeftPressed;
	options.customThemeRightPressed	= optionsProto.customThemeRightPressed;
	options.customThemeB1Pressed	= optionsProto.customThemeB1Pressed;
	options.customThemeB2Pressed	= optionsProto.customThemeB2Pressed;
	options.customThemeB3Pressed	= optionsProto.customThemeB3Pressed;
	options.customThemeB4Pressed	= optionsProto.customThemeB4Pressed;
	options.customThemeL1Pressed	= optionsProto.customThemeL1Pressed;
	options.customThemeR1Pressed	= optionsProto.customThemeR1Pressed;
	options.customThemeL2Pressed	= optionsProto.customThemeL2Pressed;
	options.customThemeR2Pressed	= optionsProto.customThemeR2Pressed;
	options.customThemeS1Pressed	= optionsProto.customThemeS1Pressed;
	options.customThemeS2Pressed	= optionsProto.customThemeS2Pressed;
	options.customThemeA1Pressed	= optionsProto.customThemeA1Pressed;
	options.customThemeA2Pressed	= optionsProto.customThemeA2Pressed;
	options.customThemeL3Pressed	= optionsProto.customThemeL3Pressed;
	options.customThemeR3Pressed	= optionsProto.customThemeR3Pressed;
	options.buttonPressColorCooldownTimeInMs = optionsProto.buttonPressColorCooldownTimeInMs;		

	return options;
}

void AnimationStorage::save()
{
#if defined(PICO_BOARD)
	Storage::getInstance().enqueueAnimationOptionsSave(AnimationStation::options);
#elif defined(ESP_PLATFORM)
	// S3: persistence is a RAM stub in Phase 1 (ConfigUtils::save no-ops,
	// performEnqueuedSaves above is a no-op) — nothing to enqueue.
#endif
}
#elif defined(ESP_PLATFORM)
// S3: same proto-to-struct mapping as Pico (clamps included), read from the
// RAM config; save() is a no-op while persistence stays stubbed (Task 3c
// owns the ConfigUtils port). Pico branch above is untouched.
AnimationOptions AnimationStorage::getAnimationOptions()
{
	AnimationOptions options = {};
	const AnimationOptions_Proto& optionsProto = Storage::getInstance().getAnimationOptions();

	options.baseAnimationIndex		= std::min<uint32_t>(optionsProto.baseAnimationIndex, 255);
	options.brightness				= std::min<uint32_t>(optionsProto.brightness, 255);
	options.staticColorIndex		= std::min<uint32_t>(optionsProto.staticColorIndex, 255);
	options.buttonColorIndex		= std::min<uint32_t>(optionsProto.buttonColorIndex, 255);
	options.chaseCycleTime			= std::min<int32_t>(optionsProto.chaseCycleTime, 65535);
	options.rainbowCycleTime		= std::min<int32_t>(optionsProto.rainbowCycleTime, 65535);
	options.themeIndex				= std::min<uint8_t>(optionsProto.themeIndex, 255);
	options.hasCustomTheme			= optionsProto.hasCustomTheme;
	options.customThemeUp			= optionsProto.customThemeUp;
	options.customThemeDown			= optionsProto.customThemeDown;
	options.customThemeLeft			= optionsProto.customThemeLeft;
	options.customThemeRight			= optionsProto.customThemeRight;
	options.customThemeB1			= optionsProto.customThemeB1;
	options.customThemeB2			= optionsProto.customThemeB2;
	options.customThemeB3			= optionsProto.customThemeB3;
	options.customThemeB4			= optionsProto.customThemeB4;
	options.customThemeL1			= optionsProto.customThemeL1;
	options.customThemeR1			= optionsProto.customThemeR1;
	options.customThemeL2			= optionsProto.customThemeL2;
	options.customThemeR2			= optionsProto.customThemeR2;
	options.customThemeS1			= optionsProto.customThemeS1;
	options.customThemeS2			= optionsProto.customThemeS2;
	options.customThemeA1			= optionsProto.customThemeA1;
	options.customThemeA2			= optionsProto.customThemeA2;
	options.customThemeL3			= optionsProto.customThemeL3;
	options.customThemeR3			= optionsProto.customThemeR3;
	options.customThemeUpPressed	= optionsProto.customThemeUpPressed;
	options.customThemeDownPressed	= optionsProto.customThemeDownPressed;
	options.customThemeLeftPressed	= optionsProto.customThemeLeftPressed;
	options.customThemeRightPressed	= optionsProto.customThemeRightPressed;
	options.customThemeB1Pressed	= optionsProto.customThemeB1Pressed;
	options.customThemeB2Pressed	= optionsProto.customThemeB2Pressed;
	options.customThemeB3Pressed	= optionsProto.customThemeB3Pressed;
	options.customThemeB4Pressed	= optionsProto.customThemeB4Pressed;
	options.customThemeL1Pressed	= optionsProto.customThemeL1Pressed;
	options.customThemeR1Pressed	= optionsProto.customThemeR1Pressed;
	options.customThemeL2Pressed	= optionsProto.customThemeL2Pressed;
	options.customThemeR2Pressed	= optionsProto.customThemeR2Pressed;
	options.customThemeS1Pressed	= optionsProto.customThemeS1Pressed;
	options.customThemeS2Pressed	= optionsProto.customThemeS2Pressed;
	options.customThemeA1Pressed	= optionsProto.customThemeA1Pressed;
	options.customThemeA2Pressed	= optionsProto.customThemeA2Pressed;
	options.customThemeL3Pressed	= optionsProto.customThemeL3Pressed;
	options.customThemeR3Pressed	= optionsProto.customThemeR3Pressed;
	options.buttonPressColorCooldownTimeInMs = optionsProto.buttonPressColorCooldownTimeInMs;

	return options;
}

void AnimationStorage::save()
{
	// S3: persistence is a RAM stub in Phase 1 (ConfigUtils::save no-ops,
	// performEnqueuedSaves above is a no-op) — nothing to enqueue.
	// (void) marks the AnimationStorage.hpp static used in this TU.
	(void)AnimationStore;
}
#endif
