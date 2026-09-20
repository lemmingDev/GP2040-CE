 /*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef STORAGE_H_
#define STORAGE_H_

#include <stdint.h>
#if defined(PICO_BOARD)
#include "NeoPico.hpp"
#elif defined(ESP_PLATFORM)
// S3: NeoPico (PIO WS2812) is Pico-only; the RMT backend lands in Task 5.
// Storage never touches LEDs on S3.
#endif
#include "FlashPROM.h"

#include "enums.h"
#if defined(PICO_BOARD)
#include "helper.h"
#elif defined(ESP_PLATFORM)
// S3: helper.h pulls Pico-SDK-only chains (pico/time.h, hardware/clocks.h
// via AnimationStation, PlayerLEDs); nothing in the S3 core loop uses it.
#endif
#include "gamepad.h"

#include "config.pb.h"
#include <atomic>
#if defined(PICO_BOARD)
#include "pico/critical_section.h"
#elif defined(ESP_PLATFORM)
#include <mutex>
#endif

#define SI Storage::getInstance()

// Storage manager for board, LED options, and thread-safe settings
class Storage {
public:
	Storage(Storage const&) = delete;
	void operator=(Storage const&)  = delete;
	static Storage& getInstance() // Thread-safe storage ensures cross-thread talk
	{
		static Storage instance;
		return instance;
	}

	Config& getConfig() { return config; }
	GamepadOptions& getGamepadOptions() { return config.gamepadOptions; }
	HotkeyOptions& getHotkeyOptions() { return config.hotkeyOptions; }
	ForcedSetupOptions& getForcedSetupOptions() { return config.forcedSetupOptions; }
	PinMappings& getDeprecatedPinMappings() { return config.deprecatedPinMappings; }
	GpioMappings& getGpioMappings() { return config.gpioMappings; }
	KeyboardMapping& getKeyboardMapping() { return config.keyboardMapping; }
	DisplayOptions& getDisplayOptions() { return config.displayOptions; }
	DisplayOptions& getPreviewDisplayOptions() { return previewDisplayOptions; }
	LEDOptions& getLedOptions() { return config.ledOptions; }
	AddonOptions& getAddonOptions() { return config.addonOptions; }
	AnimationOptions_Proto& getAnimationOptions() { return config.animationOptions; }
	ProfileOptions& getProfileOptions() { return config.profileOptions; }
	GpioMappingInfo* getProfilePinMappings() { return functionalPinMappings; }
	PeripheralOptions& getPeripheralOptions() { return config.peripheralOptions; }

	void init();
	bool save();
	bool save(const bool force);

	// Perform saves that were enqueued from core1
	void performEnqueuedSaves();

#if defined(PICO_BOARD)
	void enqueueAnimationOptionsSave(const AnimationOptions& animationOptions);
#elif defined(ESP_PLATFORM)
	// S3: LED animation stack (and its AnimationOptions type) lands in
	// Tasks 4/5; no animation saves are enqueued in Phase 1.
#endif

	void SetConfigMode(bool); 			// Config Mode (on-boot)
	bool GetConfigMode();

	void SetGamepad(Gamepad *); 		// MPGS Gamepad Get/Set
	Gamepad * GetGamepad();

	void SetProcessedGamepad(Gamepad *); // MPGS Processed Gamepad Get/Set
	Gamepad * GetProcessedGamepad();

	bool setProfile(const uint32_t);		// profile support for multiple mappings
	void nextProfile();
	void previousProfile();
	void setFunctionalPinMappings();
	char* currentProfileLabel();

	void ResetSettings(); 				// EEPROM Reset Feature

private:
	Storage() {}
	bool CONFIG_MODE = false; 			// Config mode (boot)
	Gamepad * gamepad = nullptr;    		// Gamepad data
	Gamepad * processedGamepad = nullptr; // Gamepad with ONLY processed data
	uint8_t featureData[32]; // USB X-Input Feature Data
	DisplayOptions previewDisplayOptions;
	Config config;
	std::atomic<bool> animationOptionsSavePending;
#if defined(PICO_BOARD)
	critical_section_t animationOptionsCs;
#elif defined(ESP_PLATFORM)
	// S3: std::mutex replaces the Pico SDK spinlock; no init call needed.
	std::mutex animationOptionsCs;
#endif
#if defined(PICO_BOARD)
	uint32_t animationOptionsCrc = 0;
	AnimationOptions animationOptionsToSave = {};
#endif
	GpioMappingInfo functionalPinMappings[NUM_BANK0_GPIOS];
};

#endif
