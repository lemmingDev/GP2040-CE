/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "storagemanager.h"

#include "BoardConfig.h"
#include "FlashPROM.h"
#include "drivermanager.h"
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

#include <cstdint>

// Check for saves
#include "ps4/PS4Driver.h"

#include "config_utils.h"

void Storage::init() {
	systemFlashSize = System::getPhysicalFlash(); // System Flash Size must be called once
	EEPROM.start();
	ConfigUtils::load(config);
#if defined(ESP_PLATFORM)
	// S3 48-pin table extension, forward-only (single flashed board): a
	// config persisted by a 30-entry build carries pins_count 30, whose
	// tables cannot address pins 30-48. Reset the pin tables to fresh
	// defaults in-RAM (re-run the defaults path) and save once. Runs once
	// ever; fresh configs already match. No reboot; boot continues normally.
	// Pico keeps its own migration and never compiles this (Pico behavior
	// byte-identical).
	if (config.gpioMappings.pins_count != NUM_BANK0_GPIOS) {
		ConfigUtils::resetGpioMappingsToDefaults(config);
		save(true);
	}
#endif
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
	// Conditions for saving:
	//   1. Force = True
	//   2. Input Mode NOT (PS4/PS5 with USB enabled)
	// Save will disconnect USB host, which is okay for gamepad and keyboard hosts
	if (!force &&
		PeripheralManager::getInstance().isUSBEnabled(0) &&
		(DriverManager::getInstance().getInputMode() == INPUT_MODE_PS4 ||
			DriverManager::getInstance().getInputMode() == INPUT_MODE_PS5) &&
		((PS4Driver*)DriverManager::getInstance().getDriver())->getDongleAuthRequired() == true ) {
		return false;
	}

	return ConfigUtils::save(config);
#elif defined(ESP_PLATFORM)
	// S3: no USB host until Phase 2, so saves are never gated.
	(void)force;
	return ConfigUtils::save(config);
#endif
}

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
	uint32_t profileCeiling = config.profileOptions.gpioMappingsSets_count + 1;
	
	// is this profile defined?
	if (profileNum >= 1 && profileNum <= profileCeiling) {
		// is this profile enabled?
		// profile 1 (core) is always enabled, others we must check
		if (profileNum == 1 || config.profileOptions.gpioMappingsSets[profileNum-2].enabled) {
			// Update the profile number - reinit will be triggered automatically in gp2040.cpp
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
	uint32_t profileCeiling = config.profileOptions.gpioMappingsSets_count + 1;

	if (config.gamepadOptions.profileNumber >= 2 &&
			config.gamepadOptions.profileNumber <= profileCeiling) {
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

/**
 * @brief constructs a temporary pin-mapping in order to correctly initialize GPIO pins before
 * selecting input mode at boot
 */
void Storage::setBootModeFunctionalPinMappings()
{
	BootModeOptions& bootModeOptions = getBootModeOptions();
	if (!bootModeOptions.enabled) {
		return;
	}
	// Relying on the assumption that all profiles share same set of RESERVED/ASSIGNED_TO_ADDON pins
	GpioMappingInfo* pins = getGpioMappings().pins;

	Mask_t mask = bootModeOptions.webConfigPinMask | bootModeOptions.usbModePinMask;
	for (size_t i = 0; i < bootModeOptions.inputModeMappings_count; i++) {
		auto mapping = bootModeOptions.inputModeMappings[i];
		if (mapping.pinMask == UINT64_MAX) { // disabled mapping
			continue;
		}
		mask |= mapping.pinMask;
	}

	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		if (pins[pin].action != GpioAction::RESERVED &&
			pins[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
			(mask & (Mask_t{1} << pin)))
		{
			// Just setting an arbitrary non-zero action
			functionalPinMappings[pin].action = GpioAction::BUTTON_PRESS_A1;
		}
	}
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
