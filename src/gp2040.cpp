// GP2040 includes
#include "gp2040.h"
#if defined(PICO_BOARD)
#include "helper.h"
#endif
#include "system.h"
#include "enums.pb.h"

#if defined(PICO_BOARD)
#include "build_info.h"
#include "configmanager.h" // Global Managers
#include "peripheralmanager.h"
#elif defined(ESP_PLATFORM)
// S3: PeripheralManager (I2C/SPI/USB-PIO init) is not compiled in Phase 1;
// native USB needs no PIO init and I2C lands in Task 5.
#endif
#include "eventmanager.h"
#include "storagemanager.h"
#include "addonmanager.h"
#include "types.h"
#if defined(PICO_BOARD)
#include "usbhostmanager.h"
#endif

// Inputs for Core0
#if defined(PICO_BOARD)
// Addon set is Pico-only in Phase 1; the S3 core loop runs with no addons
// (LED/audio in Task 4, NeoPixel/display in Task 5, host addons in Phase 2).
#include "addons/analog.h"
#include "addons/bootsel_button.h"
#include "addons/focus_mode.h"
#include "addons/dualdirectional.h"
#include "addons/tilt.h"
#include "addons/keyboard_host.h"
#include "addons/i2canalog1219.h"
#include "addons/playernum.h"
#include "addons/reverse.h"
#include "addons/turbo.h"
#include "addons/slider_socd.h"
#include "addons/spi_analog_ads1256.h"
#include "addons/wiiext.h"
#include "addons/input_macro.h"
#include "addons/snes_input.h"
#include "addons/rotaryencoder.h"
#include "addons/i2c_gpio_pcf8575.h"
#include "addons/gamepad_usb_host.h"
#endif


// Pico includes
#if defined(PICO_BOARD)
#include "pico/bootrom.h"
#include "pico/time.h"
#include "hardware/adc.h"
#elif defined(ESP_PLATFORM)
#include "hal_gpio.h"
#include "hal_time.h"
#include "driver/gpio.h"
#include "esp_private/usb_phy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// TinyUSB
#include "tusb.h"

// USB Input Class Drivers
#include "drivermanager.h"

static const uint32_t REBOOT_HOTKEY_ACTIVATION_TIME_MS = 50;
static const uint32_t REBOOT_HOTKEY_HOLD_TIME_MS = 4000;

void GP2040::setup() {
	Storage::getInstance().init();

#if defined(PICO_BOARD)
	PeripheralManager::getInstance().initI2C();
	PeripheralManager::getInstance().initSPI();
	PeripheralManager::getInstance().initUSB();

	// Reduce CPU if USB host is enabled
	if ( PeripheralManager::getInstance().isUSBEnabled(0) ) {
		set_sys_clock_khz(120000, true); // Set Clock to 120MHz to avoid potential USB timing issues
	}
#elif defined(ESP_PLATFORM)
	// S3: RMT/LEDC take explicit clocks; no sys-clock switch, no PIO-USB init.
#endif

	Gamepad * gamepad = new Gamepad();
	Gamepad * processedGamepad = new Gamepad();
	Storage::getInstance().SetGamepad(gamepad);
	Storage::getInstance().SetProcessedGamepad(processedGamepad);

	// Set pin mappings for all GPIO functions
	Storage::getInstance().setFunctionalPinMappings();

	// Setup Gamepad
	gamepad->setup();
	
	// now we can load the latest configured profile, which will map the
	// new set of GPIOs to use...
    this->initializeStandardGpio();

    const GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    // check setup options and add modes to the list
    // user modes
    bootActions.insert({GAMEPAD_MASK_B1, gamepadOptions.inputModeB1});
    bootActions.insert({GAMEPAD_MASK_B2, gamepadOptions.inputModeB2});
    bootActions.insert({GAMEPAD_MASK_B3, gamepadOptions.inputModeB3});
    bootActions.insert({GAMEPAD_MASK_B4, gamepadOptions.inputModeB4});
    bootActions.insert({GAMEPAD_MASK_L1, gamepadOptions.inputModeL1});
    bootActions.insert({GAMEPAD_MASK_L2, gamepadOptions.inputModeL2});
    bootActions.insert({GAMEPAD_MASK_R1, gamepadOptions.inputModeR1});
    bootActions.insert({GAMEPAD_MASK_R2, gamepadOptions.inputModeR2});

	// Initialize our ADC (various add-ons)
#if defined(PICO_BOARD)
	adc_init();
#elif defined(ESP_PLATFORM)
	// S3: ADC is owned per-channel by halAdcRead; no global init.
#endif

	// Setup Add-ons
#if defined(PICO_BOARD)
	addons.LoadUSBAddon(new KeyboardHostAddon(), CORE0_INPUT);
	addons.LoadUSBAddon(new GamepadUSBHostAddon(), CORE0_INPUT);
	addons.LoadAddon(new AnalogInput(), CORE0_INPUT);
	addons.LoadAddon(new BootselButtonAddon(), CORE0_INPUT);
	addons.LoadAddon(new DualDirectionalInput(), CORE0_INPUT);
	addons.LoadAddon(new FocusModeAddon(), CORE0_INPUT);
	addons.LoadAddon(new I2CAnalog1219Input(), CORE0_INPUT);
	addons.LoadAddon(new SPIAnalog1256Input(), CORE0_INPUT);
	addons.LoadAddon(new WiiExtensionInput(), CORE0_INPUT);
	addons.LoadAddon(new SNESpadInput(), CORE0_INPUT);
	addons.LoadAddon(new PlayerNumAddon(), CORE0_USBREPORT);
	addons.LoadAddon(new SliderSOCDInput(), CORE0_INPUT);
	addons.LoadAddon(new TiltInput(), CORE0_INPUT);
	addons.LoadAddon(new RotaryEncoderInput(), CORE0_INPUT);
	addons.LoadAddon(new PCF8575Addon(), CORE0_INPUT);

	// Input override addons
	addons.LoadAddon(new ReverseInput(), CORE0_INPUT);
	addons.LoadAddon(new TurboInput(), CORE0_INPUT); // Turbo overrides button states and should be close to the end
	addons.LoadAddon(new InputMacro(), CORE0_INPUT);
#elif defined(ESP_PLATFORM)
	// S3 Phase 1: no addons in the core loop (Tasks 4/5 wire LED/audio/
	// NeoPixel/display; USB-host addons wait for Phase 2).
#endif

	InputMode inputMode = gamepad->getOptions().inputMode;
	const BootAction bootAction = getBootAction();
	switch (bootAction) {
		case BootAction::ENTER_WEBCONFIG_MODE:
#if defined(PICO_BOARD)
			// Move this to the Net driver initialize
			Storage::getInstance().SetConfigMode(true);
			DriverManager::getInstance().setup(INPUT_MODE_CONFIG);
			ConfigManager::getInstance().setup(CONFIG_TYPE_WEB);
			return;
#elif defined(ESP_PLATFORM)
			// S3: no webconfig until Phase 3 — boot as HID gamepad instead.
			inputMode = INPUT_MODE_GENERIC;
			break;
#endif
		case BootAction::ENTER_USB_MODE:
#if defined(PICO_BOARD)
			reset_usb_boot(0, 0);
			return;
#elif defined(ESP_PLATFORM)
			// S3: use BOOT+RESET into TinyUF2/DFU (no-op here); boot gamepad.
			break;
#endif
		case BootAction::SET_INPUT_MODE_SWITCH:
			inputMode = INPUT_MODE_SWITCH;
			break;
		case BootAction::SET_INPUT_MODE_KEYBOARD:
			inputMode = INPUT_MODE_KEYBOARD;
			break;
		case BootAction::SET_INPUT_MODE_GENERIC:
			inputMode = INPUT_MODE_GENERIC;
			break;
		case BootAction::SET_INPUT_MODE_NEOGEO:
			inputMode = INPUT_MODE_NEOGEO;
			break;
		case BootAction::SET_INPUT_MODE_MDMINI:
			inputMode = INPUT_MODE_MDMINI;
			break;
		case BootAction::SET_INPUT_MODE_PCEMINI:
			inputMode = INPUT_MODE_PCEMINI;
			break;
		case BootAction::SET_INPUT_MODE_EGRET:
			inputMode = INPUT_MODE_EGRET;
			break;
		case BootAction::SET_INPUT_MODE_ASTRO:
			inputMode = INPUT_MODE_ASTRO;
			break;
		case BootAction::SET_INPUT_MODE_PSCLASSIC:
			inputMode = INPUT_MODE_PSCLASSIC;
			break;
		case BootAction::SET_INPUT_MODE_XINPUT: // X-Input Driver
			inputMode = INPUT_MODE_XINPUT;
			break;
		case BootAction::SET_INPUT_MODE_PS3: // PS3 (HID with quirks) driver
			inputMode = INPUT_MODE_PS3;
			break;
		case BootAction::SET_INPUT_MODE_PS4: // PS4 / PS5 Driver
			inputMode = INPUT_MODE_PS4;
			break;
		case BootAction::SET_INPUT_MODE_PS5: // PS4 / PS5 Driver
			inputMode = INPUT_MODE_PS5;
			break;
		case BootAction::SET_INPUT_MODE_XBONE: // Xbox One Driver
			inputMode = INPUT_MODE_XBONE;
			break;
		case BootAction::SET_INPUT_MODE_XBOXORIGINAL: // Xbox OG Driver
			inputMode = INPUT_MODE_XBOXORIGINAL;
			break;
		case BootAction::NONE:
		default:
			break;
	}

	// Setup USB Driver
	DriverManager::getInstance().setup(inputMode);

	// Save the changed input mode
	if (inputMode != gamepad->getOptions().inputMode) {	
		gamepad->setInputMode(inputMode);
		// save to match user expectations on choosing mode at boot, and this is
		// before USB host will be used so we can force it to ignore the check
		Storage::getInstance().save(true);
	}
}

/**
 * @brief Initialize standard input button GPIOs that are present in the currently loaded profile.
 */
void GP2040::initializeStandardGpio() {
	GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
	buttonGpios = 0;
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
	{
		// (NONE=-10, RESERVED=-5, ASSIGNED_TO_ADDON=0, everything else is ours)
		if (pinMappings[pin].action > 0)
		{
#if defined(PICO_BOARD)
			gpio_init(pin);             // Initialize pin
			gpio_set_dir(pin, GPIO_IN); // Set as INPUT
			gpio_pull_up(pin);          // Set as PULLUP
#elif defined(ESP_PLATFORM)
			hal::gpioInit(pin);            // Initialize pin
			hal::gpioSetInput(pin, true);  // Set as INPUT with PULLUP
#endif
			buttonGpios |= 1 << pin;    // mark this pin as mattering for GPIO debouncing
		}
	}
}

/**
 * @brief Deinitialize standard input button GPIOs that are present in the currently loaded profile.
 */
void GP2040::deinitializeStandardGpio() {
	GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
	{
		// (NONE=-10, RESERVED=-5, ASSIGNED_TO_ADDON=0, everything else is ours)
		if (pinMappings[pin].action > 0)
		{
#if defined(PICO_BOARD)
			gpio_deinit(pin);
#elif defined(ESP_PLATFORM)
			gpio_reset_pin((gpio_num_t)pin);
#endif
		}
	}
}

/**
 * @brief Populate a debounced version of gpio_get_all suitable for use for buttons.
 *
 * For GPIO that are assigned to buttons (based on GpioMappings, see GP2040::initializeStandardGpio),
 * we can centralize their debouncing here and provide access to it to button users.
 *
 * For ease of use this provides the mask bitwise NOTed so that callers don't have to. To avoid misuse
 * and to simplify this method, non-button GPIO IS NOT PRESENT in this result. Use gpio_get_all directly
 * instead, if you don't want debounced data.
 */
void GP2040::debounceGpioGetAll() {
#if defined(PICO_BOARD)
	Mask_t raw_gpio = ~gpio_get_all();
#elif defined(ESP_PLATFORM)
	// S3: no gpio_get_all() — sample each button pin per-pin (active-low,
	// matching the Pico ~gpio_get_all() polarity: bit = 1 means pressed).
	Mask_t raw_gpio = 0;
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		Mask_t pin_mask = 1 << pin;
		if ((buttonGpios & pin_mask) && !hal::gpioGet((uint8_t)pin)) {
			raw_gpio |= pin_mask;
		}
	}
#endif
	Gamepad* gamepad = Storage::getInstance().GetGamepad();
	// return if state isn't different than the actual
	if (gamepad->debouncedGpio == (raw_gpio & buttonGpios)) return;

	uint32_t debounceDelay = Storage::getInstance().getGamepadOptions().debounceDelay;
	// abort if no delay is configured
	if (debounceDelay == 0) {
		gamepad->debouncedGpio = raw_gpio;
		return;
	}

	uint32_t now = getMillis();
	// check each button use case GPIO for state
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		Mask_t pin_mask = 1 << pin;
		if (buttonGpios & pin_mask) {
			// Allow debouncer to change state if button state changed and debounce delay threshold met
			if ((gamepad->debouncedGpio & pin_mask) != \
					(raw_gpio & pin_mask) && ((now - gpioDebounceTime[pin]) > debounceDelay)) {
				gamepad->debouncedGpio ^= pin_mask;
				gpioDebounceTime[pin] = now;
			}
		}
	}
}

void GP2040::run() {
	GPDriver * inputDriver = DriverManager::getInstance().getDriver();
	Gamepad * gamepad = Storage::getInstance().GetGamepad();
	Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();
	bool configMode = Storage::getInstance().GetConfigMode();
    GamepadState prevState;
#if defined(ESP_PLATFORM)
	// TEMP hardware probe (revert): prove run() is reached and which driver.
	ESP_LOGI("loop_diag", "run() driver=%p usesUSB=%d cfgMode=%d",
		(const void*)inputDriver, (int)(inputDriver ? inputDriver->usesUSB() : -1), (int)configMode);
#endif
    
    // Start the TinyUSB Device functionality
    if (DriverManager::getInstance().getDriver()->usesUSB()) {
#if defined(ESP_PLATFORM)
        ESP_LOGI("loop_diag", "pre-phy");
        // S3: raw TinyUSB does not enable the USB OTG peripheral itself
        // (no clocks, no internal PHY, no GPIO19/20 mux) — without this the
        // device silently never enumerates and USB-Serial/JTAG keeps the pins.
        static usb_phy_handle_t s_usb_phy = nullptr;
        if (s_usb_phy == nullptr) {
            usb_phy_config_t phy_config = {
                .controller = USB_PHY_CTRL_OTG,
                .target = USB_PHY_TARGET_INT,
                .otg_mode = USB_OTG_MODE_DEVICE,
                .otg_speed = USB_PHY_SPEED_UNDEFINED,
                .ext_io_conf = nullptr,
                .otg_io_conf = nullptr,
            };
            ESP_ERROR_CHECK(usb_new_phy(&phy_config, &s_usb_phy));
        }
#endif
#if defined(ESP_PLATFORM)
        ESP_LOGI("loop_diag", "pre-tud");
#endif
        tud_init(TUD_OPT_RHPORT);
#if defined(ESP_PLATFORM)
        ESP_LOGI("loop_diag", "post-tud");
#endif
    }
    
	while (1) { // LOOP
#if defined(ESP_PLATFORM)
	// TEMP hardware probe (revert): loop-liveness + time-source witness.
	// If iters climbs but millis is frozen, hal::millis() is stuck (kills
	// debounce, heartbeat, and every deadline in one stroke).
	static uint32_t s_loopIters = 0;
	if ((++s_loopIters % 1000) == 0) {
		ESP_LOGI("loop_diag", "iters=%lu millis=%lu", (unsigned long)s_loopIters, (unsigned long)hal::millis());
	}
#endif
		this->getReinitGamepad(gamepad);

		memcpy(&prevState, &gamepad->state, sizeof(GamepadState));

		// Do any queued saves in StorageManager
		Storage::getInstance().performEnqueuedSaves();
		
		// Debounce
		debounceGpioGetAll();
		// Read Gamepad
		gamepad->read();

		checkRawState(prevState, gamepad->state);

		// Config Loop (Web-Config does not require gamepad)
		if (configMode == true) {
#if defined(PICO_BOARD)
			ConfigManager::getInstance().loop();
			rebootHotkeys.process(gamepad, configMode);
			continue;
#elif defined(ESP_PLATFORM)
			// S3: no webconfig until Phase 3 (configMode is never set);
			// fall through to the gamepad path.
#endif
		}

		// Process USB Host on Core0
#if defined(PICO_BOARD)
		USBHostManager::getInstance().process();
#elif defined(ESP_PLATFORM)
		// S3: no USB host until Phase 2.
#endif

		// Pre-Process add-ons for MPGS
		addons.PreprocessAddons(ADDON_PROCESS::CORE0_INPUT);

		gamepad->hotkey(); 	// check for MPGS hotkeys
		rebootHotkeys.process(gamepad, configMode);
		
		gamepad->process(); // process through MPGS

		// (Post) Process for add-ons
		addons.ProcessAddons(ADDON_PROCESS::CORE0_INPUT);

		checkProcessedState(processedGamepad->state, gamepad->state);

		// Copy Processed Gamepad for Core1 (race condition otherwise)
		memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

		// Process Input Driver
		inputDriver->process(gamepad);
		
		// Process USB Report Addons
		addons.ProcessAddons(ADDON_PROCESS::CORE0_USBREPORT);
		
    if (DriverManager::getInstance().getDriver()->usesUSB()) {
#if defined(ESP_PLATFORM)
        // S3/FreeRTOS: tud_task() blocks indefinitely when no USB events are
        // pending, wedging polled input in quiet modes (found on hardware
        // 2026-09-21: Switch idle never wakes it while XInput's traffic
        // does). Poll with zero timeout instead; the 1-tick yield keeps the
        // loop at ~100 Hz without starving IDLE.
        tud_task_ext(0, false);
        vTaskDelay(1);
#else
        tud_task(); // TinyUSB Task update
#endif
    }
	}
}

void GP2040::getReinitGamepad(Gamepad * gamepad) {
	// check if we should reinitialize the gamepad
	if (gamepad->userRequestedReinit) {
		// deinitialize the ordinary (non-reserved, non-addon) GPIO pins, since
		// we are moving off of them and onto potentially different pin assignments
		// we currently don't support ASSIGNED_TO_ADDON pins being reinitialized,
		// but if they were to be, that'd be the addon's duty, not ours
		this->deinitializeStandardGpio();

		// now we can load the latest configured profile, which will map the
		// new set of GPIOs to use...
		Storage::getInstance().setFunctionalPinMappings();

		// ...and initialize the pins again
		this->initializeStandardGpio();

		// now we can tell the gamepad that the new mappings are in place
		// and ready to use, and the pins are ready, so it should reinitialize itself
		gamepad->reinit();
		// ...and addons on this core, if they implemented reinit (just things
		// with simple GPIO pin usage, at time of writing)
		addons.ReinitializeAddons(ADDON_PROCESS::CORE0_INPUT);

		// and we're done
		gamepad->userRequestedReinit = false;
	}
}

GP2040::BootAction GP2040::getBootAction() {
	switch (System::takeBootMode()) {
		case System::BootMode::GAMEPAD: return BootAction::NONE;
		case System::BootMode::WEBCONFIG: return BootAction::ENTER_WEBCONFIG_MODE;
		case System::BootMode::USB: return BootAction::ENTER_USB_MODE;
		case System::BootMode::DEFAULT:
			{
				// Determine boot action based on gamepad state during boot
				Gamepad * gamepad = Storage::getInstance().GetGamepad();
				Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();
				
				debounceGpioGetAll();
				gamepad->read();

				// Pre-Process add-ons for MPGS
				addons.PreprocessAddons(ADDON_PROCESS::CORE0_INPUT);
				
		gamepad->process(); // process through MPGS
#if defined(ESP_PLATFORM)
		// TEMP hardware probe (revert): bisect loop-hang location. Prints
		// every 1000th MPGS pass; if absent while run() printed, the hang is
		// above (reinit/saves/debounce/read/hotkey/MPGS), else below.
		static uint32_t s_mpgsCount = 0;
		if ((++s_mpgsCount % 100) == 0) {
			ESP_LOGI("loop_diag", "mpgs-pass=%lu", (unsigned long)s_mpgsCount);
		}
#endif

				// (Post) Process for add-ons
				addons.ProcessAddons(ADDON_PROCESS::CORE0_INPUT);

				// Copy Processed Gamepad for Core1 (race condition otherwise)
				memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

                const ForcedSetupOptions& forcedSetupOptions = Storage::getInstance().getForcedSetupOptions();
                bool modeSwitchLocked = forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_MODE_SWITCH ||
                                        forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_BOTH;

                bool webConfigLocked  = forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_WEB_CONFIG ||
                                        forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_BOTH;

				if (gamepad->pressedS1() && gamepad->pressedS2() && gamepad->pressedUp()) {
					return BootAction::ENTER_USB_MODE;
				} else if (!webConfigLocked && gamepad->pressedS2()) {
					return BootAction::ENTER_WEBCONFIG_MODE;
                } else {
                    if (!modeSwitchLocked) {
                        if (auto search = bootActions.find(gamepad->state.buttons); search != bootActions.end()) {
                            switch (search->second) {
                                case INPUT_MODE_XINPUT: 
                                    return BootAction::SET_INPUT_MODE_XINPUT;
                                case INPUT_MODE_SWITCH: 
                                    return BootAction::SET_INPUT_MODE_SWITCH;
                                case INPUT_MODE_KEYBOARD: 
                                    return BootAction::SET_INPUT_MODE_KEYBOARD;
                                case INPUT_MODE_GENERIC:
                                    return BootAction::SET_INPUT_MODE_GENERIC;
                                case INPUT_MODE_PS3:
                                    return BootAction::SET_INPUT_MODE_PS3;
                                case INPUT_MODE_PS4: 
                                    return BootAction::SET_INPUT_MODE_PS4;
                                case INPUT_MODE_PS5: 
                                    return BootAction::SET_INPUT_MODE_PS5;
                                case INPUT_MODE_NEOGEO: 
                                    return BootAction::SET_INPUT_MODE_NEOGEO;
                                case INPUT_MODE_MDMINI: 
                                    return BootAction::SET_INPUT_MODE_MDMINI;
                                case INPUT_MODE_PCEMINI: 
                                    return BootAction::SET_INPUT_MODE_PCEMINI;
                                case INPUT_MODE_EGRET: 
                                    return BootAction::SET_INPUT_MODE_EGRET;
                                case INPUT_MODE_ASTRO: 
                                    return BootAction::SET_INPUT_MODE_ASTRO;
                                case INPUT_MODE_PSCLASSIC: 
                                    return BootAction::SET_INPUT_MODE_PSCLASSIC;
                                case INPUT_MODE_XBOXORIGINAL: 
                                    return BootAction::SET_INPUT_MODE_XBOXORIGINAL;
                                case INPUT_MODE_XBONE:
                                    return BootAction::SET_INPUT_MODE_XBONE;
                                default:
                                    return BootAction::NONE;
                            }
                        }
                    }
                }

				break;
			}
	}

	return BootAction::NONE;
}

GP2040::RebootHotkeys::RebootHotkeys() :
	active(false),
#if defined(PICO_BOARD)
	noButtonsPressedTimeout(nil_time),
	webConfigHotkeyMask(GAMEPAD_MASK_S2 | GAMEPAD_MASK_B3 | GAMEPAD_MASK_B4),
	bootselHotkeyMask(GAMEPAD_MASK_S1 | GAMEPAD_MASK_B3 | GAMEPAD_MASK_B4),
	rebootHotkeysHoldTimeout(nil_time) {
#elif defined(ESP_PLATFORM)
	noButtonsPressedDeadlineMs(0),
	webConfigHotkeyMask(GAMEPAD_MASK_S2 | GAMEPAD_MASK_B3 | GAMEPAD_MASK_B4),
	bootselHotkeyMask(GAMEPAD_MASK_S1 | GAMEPAD_MASK_B3 | GAMEPAD_MASK_B4),
	rebootHotkeysHoldDeadlineMs(0) {
#endif
}

void GP2040::RebootHotkeys::process(Gamepad* gamepad, bool configMode) {
#if defined(PICO_BOARD)
	// We only allow the hotkey to trigger after we observed no buttons pressed for a certain period of time.
	// We do this to avoid detecting buttons that are held during the boot process. In particular we want to avoid
	// oscillating between webconfig and default mode when the user keeps holding the hotkey buttons.
	if (!active) {
		if (gamepad->state.buttons == 0) {
			if (is_nil_time(noButtonsPressedTimeout)) {
				noButtonsPressedTimeout = make_timeout_time_us(REBOOT_HOTKEY_ACTIVATION_TIME_MS);
			}

			if (time_reached(noButtonsPressedTimeout)) {
				active = true;
			}
		} else {
			noButtonsPressedTimeout = nil_time;
		}
	} else {
		if (gamepad->state.buttons == webConfigHotkeyMask || gamepad->state.buttons == bootselHotkeyMask) {
			if (is_nil_time(rebootHotkeysHoldTimeout)) {
				rebootHotkeysHoldTimeout = make_timeout_time_ms(REBOOT_HOTKEY_HOLD_TIME_MS);
			}

			if (time_reached(rebootHotkeysHoldTimeout)) {
				if (gamepad->state.buttons == webConfigHotkeyMask) {
					// If we are in webconfig mode we go to gamepad mode and vice versa
					System::reboot(configMode ? System::BootMode::GAMEPAD : System::BootMode::WEBCONFIG);
				} else if (gamepad->state.buttons == bootselHotkeyMask) {
					System::reboot(System::BootMode::USB);
				}
			}
		} else {
			rebootHotkeysHoldTimeout = nil_time;
		}
	}
#elif defined(ESP_PLATFORM)
	// S3: same state machine on hal::millis() ms deadlines (0 = unset).
	// NOTE: Pico passes REBOOT_HOTKEY_ACTIVATION_TIME_MS (50) to
	// make_timeout_time_us, i.e. 50 us; S3 uses the nominal 50 ms instead —
	// still just an arm-after-idle gate, and the S3 value matches the name.
	uint32_t now = hal::millis();
	if (!active) {
		if (gamepad->state.buttons == 0) {
			if (noButtonsPressedDeadlineMs == 0) {
				noButtonsPressedDeadlineMs = now + REBOOT_HOTKEY_ACTIVATION_TIME_MS;
			}

			if ((int32_t)(now - noButtonsPressedDeadlineMs) >= 0) {
				active = true;
			}
		} else {
			noButtonsPressedDeadlineMs = 0;
		}
	} else {
		if (gamepad->state.buttons == webConfigHotkeyMask || gamepad->state.buttons == bootselHotkeyMask) {
			if (rebootHotkeysHoldDeadlineMs == 0) {
				rebootHotkeysHoldDeadlineMs = now + REBOOT_HOTKEY_HOLD_TIME_MS;
			}

			if ((int32_t)(now - rebootHotkeysHoldDeadlineMs) >= 0) {
				if (gamepad->state.buttons == webConfigHotkeyMask) {
					// If we are in webconfig mode we go to gamepad mode and vice versa
					System::reboot(configMode ? System::BootMode::GAMEPAD : System::BootMode::WEBCONFIG);
				} else if (gamepad->state.buttons == bootselHotkeyMask) {
					System::reboot(System::BootMode::USB);
				}
			}
		} else {
			rebootHotkeysHoldDeadlineMs = 0;
		}
	}
#endif
}

void GP2040::checkRawState(GamepadState prevState, GamepadState currState) {
    // buttons pressed
    if (
        ((currState.aux & ~prevState.aux) != 0) ||
        ((currState.dpad & ~prevState.dpad) != 0) ||
        ((currState.buttons & ~prevState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonDownEvent((currState.dpad & ~prevState.dpad), (currState.buttons & ~prevState.buttons), (currState.aux & ~prevState.aux)));
    }

    // buttons released
    if (
        ((prevState.aux & ~currState.aux) != 0) ||
        ((prevState.dpad & ~currState.dpad) != 0) ||
        ((prevState.buttons & ~currState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonUpEvent((prevState.dpad & ~currState.dpad), (prevState.buttons & ~currState.buttons), (prevState.aux & ~currState.aux)));
    }
}

void GP2040::checkProcessedState(GamepadState prevState, GamepadState currState) {
    // buttons pressed
    if (
        ((currState.aux & ~prevState.aux) != 0) ||
        ((currState.dpad & ~prevState.dpad) != 0) ||
        ((currState.buttons & ~prevState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonProcessedDownEvent((currState.dpad & ~prevState.dpad), (currState.buttons & ~prevState.buttons), (currState.aux & ~prevState.aux)));
    }

    // buttons released
    if (
        ((prevState.aux & ~currState.aux) != 0) ||
        ((prevState.dpad & ~currState.dpad) != 0) ||
        ((prevState.buttons & ~currState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonProcessedUpEvent((prevState.dpad & ~currState.dpad), (prevState.buttons & ~currState.buttons), (prevState.aux & ~currState.aux)));
    }

    if (
        (currState.lx != prevState.lx) ||
        (currState.ly != prevState.ly) ||
        (currState.rx != prevState.rx) ||
        (currState.ry != prevState.ry) ||
        (currState.lt != prevState.lt) ||
        (currState.rt != prevState.rt)
    ) {
        EventManager::getInstance().triggerEvent(new GPAnalogProcessedMoveEvent(currState.lx, currState.ly, currState.rx, currState.ry, currState.lt, currState.rt));
    }
}
