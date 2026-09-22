#include <optional>
#include <cstdint>

// GP2040 includes
#include "gp2040.h"
#if defined(PICO_BOARD)
#include "helper.h"
#endif
#include "system.h"
#include "enums.pb.h"

#if defined(PICO_BOARD)
#include "build_info.h"
#include "peripheralmanager.h"
#elif defined(ESP_PLATFORM)
// S3: PeripheralManager header is stub-safe (SPI/USB backends are headers
// only in Phase 1); peripheralmanager.cpp IS in the S3 SRCS for initI2C
// (display probe, also called from gp2040aux on core1).
#include "peripheralmanager.h"
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
#include "addons/i2canalog1115.h"
#include "addons/i2canalog1219.h"
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
#include "addons/he_trigger.h"
#include "addons/tg16_input.h"
#include "addons/slider_profile.h"
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#if defined(PICO_BOARD)
#include "rndis.h"
#endif

#if defined(ESP_PLATFORM)
// S3 webconfig bring-up (src/webconfig_s3.cpp, S3-only TU): WiFi AP + HTTP
// server lifecycle, called from GP2040::setup().
bool startWifiAP();
bool startWifiS3(bool apWanted, bool webconfigSessionActive);
bool s3_sta_wanted(bool webconfigSessionActive);
void startWebconfigServer();

// L1-hold WiFi-config session flag: set by getButtonMappedBootAction(),
// consumed once by GP2040::setup(). Session-only, never saved.
static bool s3WifiConfigSession = false;
#endif

// TinyUSB
#include "tusb.h"

// USB Input Class Drivers
#include "drivermanager.h"

static const uint32_t REBOOT_HOTKEY_ACTIVATION_TIME_MS = 50;
static const uint32_t REBOOT_HOTKEY_HOLD_TIME_MS = 4000;

const static uint32_t rebootDelayMs = 500;
static absolute_time_t rebootDelayTimeout = nil_time;


void GP2040::setup() {
	Storage::getInstance().init();

#if defined(PICO_BOARD)
	PeripheralManager::getInstance().initUSB();

	// I2C & SPI rely on the system clock
	PeripheralManager::getInstance().initSPI();
	PeripheralManager::getInstance().initI2C();
#elif defined(ESP_PLATFORM)
	// S3: I2C only (display probe; core1 calls it too). SPI has no S3
	// backend and USB needs no PIO/host init in Phase 1.
	PeripheralManager::getInstance().initI2C();
#endif

	Gamepad * gamepad = new Gamepad();
	Gamepad * processedGamepad = new Gamepad();
	Storage::getInstance().SetGamepad(gamepad);
	Storage::getInstance().SetProcessedGamepad(processedGamepad);

	BootModeOptions& bootModeOptions = Storage::getInstance().getBootModeOptions();
	BootAction bootAction;

	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
	uint32_t prevProfile = gamepadOptions.profileNumber;
	bool profileChanged = false;

	if (bootModeOptions.enabled) {
		bootAction = getGpioMappedBootAction();
		profileChanged = bootAction.profileNumber != prevProfile;
		gamepadOptions.profileNumber = bootAction.profileNumber;
	}

	// Set pin mappings for all GPIO functions
	Storage::getInstance().setFunctionalPinMappings();

	// power up...
	gamepad->auxState.power.pluggedIn = true;
	gamepad->auxState.power.charging = false;
	gamepad->auxState.power.level = GAMEPAD_AUX_MAX_POWER;

	// Setup Gamepad
	gamepad->setup();

	// now we can load the latest configured profile, which will map the
	// new set of GPIOs to use...
  this->initializeStandardGpio();

	// Initialize our ADC (various add-ons)
#if defined(PICO_BOARD)
	adc_init();
#elif defined(ESP_PLATFORM)
	// S3: ADC is owned per-channel by halAdcRead; no global init.
#endif

	// Setup Add-ons
#if defined(PICO_BOARD)
	addons.LoadUSBAddon(new KeyboardHostAddon());
	addons.LoadUSBAddon(new GamepadUSBHostAddon());
	addons.LoadAddon(new AnalogInput());
	addons.LoadAddon(new HETriggerAddon());
	addons.LoadAddon(new BootselButtonAddon());
	addons.LoadAddon(new DualDirectionalInput());
	addons.LoadAddon(new FocusModeAddon());
	addons.LoadAddon(new I2CAnalog1115Input());
	addons.LoadAddon(new I2CAnalog1219Input());
	addons.LoadAddon(new SPIAnalog1256Input());
	addons.LoadAddon(new WiiExtensionInput());
	addons.LoadAddon(new SNESpadInput());
	addons.LoadAddon(new SliderSOCDInput());
	addons.LoadAddon(new SliderProfileInput());
	addons.LoadAddon(new TiltInput());
	addons.LoadAddon(new RotaryEncoderInput());
	addons.LoadAddon(new PCF8575Addon());
	addons.LoadAddon(new TG16padInput());

	// Input override addons
	addons.LoadAddon(new ReverseInput());
	addons.LoadAddon(new TurboInput()); // Turbo overrides button states and should be close to the end
	addons.LoadAddon(new InputMacro());
#elif defined(ESP_PLATFORM)
	// S3: no addons in the core loop (LED/audio/NeoPixel/display live on the
	// aux core; USB-host addons wait for Phase 2).
#endif

	// Use the old method of selecting input mode via mapped button, i.e. AFTER initializing GPIO
	// pins with the currently active profile. Calling this even if the GPIO-mapped selection is
	// used to make sure the same gamepad and add-on initialization steps still happen;
	BootAction altBootAction = getButtonMappedBootAction();

	if (!bootModeOptions.enabled) {
		bootAction = altBootAction;
	}

#if defined(ESP_PLATFORM)
	// Consume the L1-hold WiFi-config session flag (set inside
	// getButtonMappedBootAction above). Deliberately NOT gated on
	// bootModeOptions.enabled: a physical hold at boot is an explicit
	// override, and gating it locks out webconfig entirely when mappings
	// are on but no webConfig pin is set (seen on hardware 2026-09).
	bool s3WifiSession = s3WifiConfigSession;
	s3WifiConfigSession = false; // consume once
#endif

	// Initialize last reinit profile to current so we don't reinit on first loop
	gamepad->lastReinitProfileNumber = bootAction.profileNumber;

	if (bootAction.type == BootActionType::ENTER_USB_MODE) {
#if defined(PICO_BOARD)
		reset_usb_boot(0, 0);
		return;
#elif defined(ESP_PLATFORM)
		// S3: no USB-boot reboot; fall through and boot as gamepad.
#endif
	}

	InputMode inputMode = bootAction.inputMode;
#if defined(ESP_PLATFORM)
	// S3 webconfig bring-up: CONFIG is served over WiFi-AP (the
	// INPUT_MODE_CONFIG→GENERIC demotion is gone). The AP comes up when
	// requested — L1-hold WiFi-config session, saved apEnabled toggle, or a
	// CONFIG boot with the WIFI transport pref — followed by the HTTP server.
	// The STA client joins through the same bring-up (startWifiS3 mode
	// matrix) per staMode. There is no CONFIG-mode (NetDriver-equivalent)
	// USB driver on S3, so a
	// CONFIG boot keeps the saved gamepad mode live instead: WiFi-config
	// never parks gameplay. (Contrast: the later USB-config phase will mirror
	// Pico and park the gamepad while in CONFIG mode — spec §4 note. A CONFIG
	// boot with the USB pref therefore comes up as the saved gamepad with no
	// AP/server until that phase lands.)
	WebConfigOptions &webConfigOptions = Storage::getInstance().getConfig().webConfigOptions;
	bool s3ConfigBoot = (bootAction.inputMode == INPUT_MODE_CONFIG);
	bool s3ApRequested = webConfigOptions.apEnabled || s3WifiSession ||
		(s3ConfigBoot && webConfigOptions.webconfigTransport == WEBCONFIG_TRANSPORT_WIFI);
	// STA-Task 2: STA joins at boot when Always-on, or in any webconfig
	// session (L1-hold, toggle, CONFIG+WiFi-pref — exactly s3ApRequested)
	// when Webconfig-only; Off (or empty SSID) never joins. The server
	// follows WiFi up on any interface (AP and/or STA) so the LAN UI works
	// over STA too (live join verified in STA-Task 4 end-to-end).
	bool s3StaWanted = s3_sta_wanted(s3ApRequested);
	if ((s3ApRequested || s3StaWanted) && startWifiS3(s3ApRequested, s3ApRequested)) {
		startWebconfigServer();
	}
	if (s3ConfigBoot) {
		inputMode = gamepadOptions.inputMode;
	}
#endif

	// Setup USB Driver
	DriverManager::getInstance().setup(inputMode);

	if (inputMode != INPUT_MODE_CONFIG) {
		bool inputModeChanged = inputMode != gamepadOptions.inputMode;
		if (inputModeChanged) gamepad->setInputMode(inputMode);

		// save to match user expectations on choosing mode at boot, and this is
		// before USB host will be used so we can force it to ignore the check
		if (inputModeChanged || profileChanged) Storage::getInstance().save(true);
	}

	// register system event handlers
	EventManager::getInstance().registerEventHandler(GP_EVENT_STORAGE_SAVE, GPEVENT_CALLBACK(this->handleStorageSave(event)));
	EventManager::getInstance().registerEventHandler(GP_EVENT_RESTART, GPEVENT_CALLBACK(this->handleSystemReboot(event)));
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
			buttonGpios |= Mask_t{1} << pin;    // mark this pin as mattering for GPIO debouncing
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
	Mask_t pressedGpios = ~gpio_get_all64() & buttonGpios;
#elif defined(ESP_PLATFORM)
	// S3: no gpio_get_all() — sample each button pin per-pin (active-low,
	// matching the Pico ~gpio_get_all() polarity: bit = 1 means pressed).
	Mask_t pressedGpios = 0;
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		Mask_t pin_mask = Mask_t{1} << pin;
		if ((buttonGpios & pin_mask) && !hal::gpioGet((uint8_t)pin)) {
			pressedGpios |= pin_mask;
		}
	}
#endif
	Gamepad* gamepad = Storage::getInstance().GetGamepad();

	// Return if state isn't different than the actual
	if (gamepad->debouncedGpio == (pressedGpios)) return;

	uint32_t debounceDelay = Storage::getInstance().getGamepadOptions().debounceDelay;
	// Abort if no delay is configured
	if (debounceDelay == 0) {
		gamepad->debouncedGpio = pressedGpios;
		return;
	}

	Mask_t debounceChange = (pressedGpios ^ gamepad->debouncedGpio) & buttonGpios;

	if (debounceChange == 0) {
		gamepad->debouncedGpio = pressedGpios;
		return;
	}

	uint32_t now = getMillis();

	// Check only changed button use case GPIO for state
	while (debounceChange != 0) {
		Pin_t pin = __builtin_ctzll(debounceChange);
		Mask_t pin_mask = Mask_t{1} << pin;
		
		// Allow debouncer to change state if button state changed and debounce delay threshold met
		if ((now - gpioDebounceTime[pin]) >= debounceDelay) {
			gamepad->debouncedGpio ^= pin_mask;
			gpioDebounceTime[pin] = now;
		}
		debounceChange &= debounceChange - 1;
	}
}

void GP2040::run() {
	bool configMode = DriverManager::getInstance().isConfigMode();
	GPDriver * inputDriver = DriverManager::getInstance().getDriver();
	Gamepad * gamepad = Storage::getInstance().GetGamepad();
	Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();
	GamepadState prevState;
    
    // Start the TinyUSB Device functionality
    if (DriverManager::getInstance().getDriver()->usesUSB()) {
#if defined(ESP_PLATFORM)
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
        tud_init(TUD_OPT_RHPORT);
    }

	// Initialize our USB manager
#if defined(PICO_BOARD)
	USBHostManager::getInstance().start();
#elif defined(ESP_PLATFORM)
	// S3: no USB host until Phase 2.
#endif

	if (configMode == true ) {
#if defined(PICO_BOARD)
		rndis_init(WEB_CONFIG_HOSTNAME);
#elif defined(ESP_PLATFORM)
		// S3: no RNDIS/webconfig until Phase 3.
#endif
	}
	while (1) { // LOOP
		this->getReinitGamepad(gamepad);

		memcpy(&prevState, &gamepad->state, sizeof(GamepadState));

		// Debounce
		debounceGpioGetAll();
		// Read Gamepad
		gamepad->read();

		checkRawState(prevState, gamepad->state);

		// Process USB Host on Core0
#if defined(PICO_BOARD)
		USBHostManager::getInstance().process();
#elif defined(ESP_PLATFORM)
		// S3: no USB host until Phase 2.
#endif

		// Config Loop (Web-Config skips Core0 add-ons)
		if (configMode == true) {
			inputDriver->process(gamepad);
			rebootHotkeys.process(gamepad, configMode);
			checkSaveRebootState();
			continue;
		}

		// Pre-Process add-ons for MPGS
		addons.PreprocessAddons();

		gamepad->process(); // process through MPGS

		// (Post) Process for add-ons
		addons.ProcessAddons();

		gamepad->hotkey(); 	// check for MPGS hotkeys
		rebootHotkeys.process(gamepad, configMode);

		checkProcessedState(processedGamepad->state, gamepad->state);

		// Copy Processed Gamepad for Core1 (race condition otherwise)
		memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

		// Process Input Driver
		bool processed = inputDriver->process(gamepad);

#if defined(ESP_PLATFORM)
		// S3/FreeRTOS: tud_task() blocks indefinitely when no USB events are
		// pending, wedging polled input in quiet modes (found on hardware
		// 2026-09-21: Switch idle never wakes it while XInput's traffic
		// does). Poll with zero timeout instead; the 1-tick yield keeps the
		// loop at ~1000 Hz without starving IDLE (CONFIG_FREERTOS_HZ=1000).
		tud_task_ext(0, false);
		vTaskDelay(1);
#else
		// TinyUSB Task update
		tud_task();
#endif

		// Post-Process Add-ons with USB Report Processed Sent
		addons.PostprocessAddons(processed);

		// Check if we have a pending save
		checkSaveRebootState();
	}
}

void GP2040::getReinitGamepad(Gamepad * gamepad) {
	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

	// Check if profile has changed since last reinit
	if (gamepad->lastReinitProfileNumber != gamepadOptions.profileNumber) {
		uint32_t previousProfile = gamepad->lastReinitProfileNumber;
		uint32_t currentProfile = gamepadOptions.profileNumber;

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
		addons.ReinitializeAddons();

		// Update the last reinit profile
		gamepad->lastReinitProfileNumber = currentProfile;

		// Trigger the profile change event now that reinit is complete
		EventManager::getInstance().triggerEvent(new GPProfileChangeEvent(previousProfile, currentProfile));
	}
}

GP2040::BootAction GP2040::getButtonMappedBootAction() {
	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
	// Initialized to the current mode and profile
	BootAction bootAction = {
		BootActionType::SET_INPUT_MODE,
		gamepadOptions.inputMode,
		gamepadOptions.profileNumber
	};

	switch (System::takeBootMode()) {
		case System::BootMode::GAMEPAD:
			return bootAction;
		case System::BootMode::WEBCONFIG:
			bootAction.inputMode = InputMode::INPUT_MODE_CONFIG;
			return bootAction;
		case System::BootMode::USB:
			bootAction.type = BootActionType::ENTER_USB_MODE;
			return bootAction;
		case System::BootMode::DEFAULT:
			break;
	}
	// Determine boot action based on gamepad state during boot
	Gamepad * gamepad = Storage::getInstance().GetGamepad();
	Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();

	debounceGpioGetAll();
	gamepad->read();

	// Pre-Process add-ons for MPGS
	addons.PreprocessAddons();

	gamepad->process(); // process through MPGS

	// Process for add-ons
	addons.ProcessAddons();

	// Copy Processed Gamepad for Core1 (race condition otherwise)
	memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

	const ForcedSetupOptions& forcedSetupOptions = Storage::getInstance().getForcedSetupOptions();
	bool modeSwitchLocked = forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_MODE_SWITCH ||
													forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_BOTH;

	bool webConfigLocked  = forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_WEB_CONFIG ||
													forcedSetupOptions.mode == FORCED_SETUP_MODE_LOCK_BOTH;

	if (gamepad->pressedS1() && gamepad->pressedS2() && gamepad->pressedUp()) {
		bootAction.type = BootActionType::ENTER_USB_MODE;
		return bootAction;
	}
	if (!webConfigLocked && gamepad->pressedS2()) {
		bootAction.inputMode =  InputMode::INPUT_MODE_CONFIG;
		return bootAction;
	}
	// input mask, action
	std::map<uint32_t, int32_t> bootActions;

	// check setup options and add modes to the list
	bootActions.insert({GAMEPAD_MASK_B1, gamepadOptions.inputModeB1});
	bootActions.insert({GAMEPAD_MASK_B2, gamepadOptions.inputModeB2});
	bootActions.insert({GAMEPAD_MASK_B3, gamepadOptions.inputModeB3});
	bootActions.insert({GAMEPAD_MASK_B4, gamepadOptions.inputModeB4});
	bootActions.insert({GAMEPAD_MASK_L1, gamepadOptions.inputModeL1});
	bootActions.insert({GAMEPAD_MASK_L2, gamepadOptions.inputModeL2});
	bootActions.insert({GAMEPAD_MASK_R1, gamepadOptions.inputModeR1});
	bootActions.insert({GAMEPAD_MASK_R2, gamepadOptions.inputModeR2});

	if (!modeSwitchLocked) {
#if defined(ESP_PLATFORM)
		// S3 boot-action guards (checked BEFORE the generic lookup: an
		// unmapped (-1) hold matches its map entry below and would return
		// the placeholder as a mode, poisoning the stored input mode
		// (found on hardware 2026-09: L1-hold stored -1, WiFi never came
		// up). L1-unmapped boots WiFi-config (session-only, never saved);
		// L2/R1-unmapped holds are ignored (normal boot) — L2 is reserved
		// for the USB-config phase.
		if (!webConfigLocked && gamepad->state.buttons == GAMEPAD_MASK_L1 &&
				gamepadOptions.inputModeL1 < 0) {
			bootAction.inputMode = InputMode::INPUT_MODE_CONFIG;
			s3WifiConfigSession = true;
			return bootAction;
		}
		if (gamepad->state.buttons == GAMEPAD_MASK_L2 && gamepadOptions.inputModeL2 < 0) {
			return bootAction;
		}
		if (gamepad->state.buttons == GAMEPAD_MASK_R1 && gamepadOptions.inputModeR1 < 0) {
			return bootAction;
		}
#endif
		if (auto search = bootActions.find(gamepad->state.buttons); search != bootActions.end()) {
			bootAction.inputMode = static_cast<InputMode>(search->second);
			return bootAction;
		}
	}
#if defined(ESP_PLATFORM)
	// S3: L2-hold with a VALID stored mapping falls through to the lookup
	// above (honored); reaching here means no hold matched or switching is
	// locked — nothing S3-specific left to do (L1/L2/R1-unmapped handled
	// before the lookup).
#endif
	return bootAction;
}

/**
 * @brief Get input mode and profile to load at startup via mapped GPIO pins
 *
 * GPIO pins are initialized by constructing a temporary, virtual profile using BootModeOptions and
 * de-initialized before returning the BootAction.
 */
GP2040::BootAction GP2040::getGpioMappedBootAction() {
	Storage::getInstance().setBootModeFunctionalPinMappings();
	initializeStandardGpio();

	const GamepadOptions& gamepad = Storage::getInstance().getGamepadOptions();
	const BootModeOptions& bootModeOptions = Storage::getInstance().getBootModeOptions();
	// Initialized to the current mode and profile
	BootAction action = { BootActionType::SET_INPUT_MODE, gamepad.inputMode, gamepad.profileNumber };

	switch (System::takeBootMode()) {
		case System::BootMode::GAMEPAD:
			return action;
		case System::BootMode::WEBCONFIG:
			action.inputMode = InputMode::INPUT_MODE_CONFIG;
			return action;
		case System::BootMode::USB:
			action.type = BootActionType::ENTER_USB_MODE;
			return action;
		default:
			break;
	}
#if defined(PICO_BOARD)
	Mask_t gpio = ~gpio_get_all64() & buttonGpios;
#elif defined(ESP_PLATFORM)
	// S3: no gpio_get_all(); per-pin poll, active-low, same polarity as
	// ~gpio_get_all() (bit = 1 means pressed). Pins are input+pullup from
	// initializeStandardGpio above, like the Pico path.
	Mask_t gpio = 0;
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		Mask_t pin_mask = Mask_t{1} << pin;
		if ((buttonGpios & pin_mask) && !hal::gpioGet((uint8_t)pin)) {
			gpio |= pin_mask;
		}
	}
#endif

	if (gpio == bootModeOptions.usbModePinMask) {
		action.type = BootActionType::ENTER_USB_MODE;
		return action;
	}

	if (gpio == bootModeOptions.webConfigPinMask) {
		action.inputMode = InputMode::INPUT_MODE_CONFIG;
		return action;
	}

	for (size_t i = 0; i < bootModeOptions.inputModeMappings_count; i++) {
		InputModeMapping m = bootModeOptions.inputModeMappings[i];
		if (m.pinMask == UINT64_MAX) // disabled mapping
			continue;
		if (gpio == m.pinMask) {
			action.inputMode = static_cast<InputMode>(m.inputMode);
			if (m.profileNumber > 0) {
				action.profileNumber = m.profileNumber;
			}
			break;
		}
	}

	deinitializeStandardGpio();
	return action;
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

void GP2040::checkRawState(const GamepadState& prevState, const GamepadState& currState) {
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

void GP2040::checkProcessedState(const GamepadState& prevState, const GamepadState& currState) {
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

void GP2040::checkSaveRebootState() {
	if (saveRequested) {
		saveRequested = false;
		Storage::getInstance().save(forceSave);
	}

	if (rebootRequested) {
		rebootRequested = false;
		rebootDelayTimeout = make_timeout_time_ms(rebootDelayMs);
	}

	if (!is_nil_time(rebootDelayTimeout) && time_reached(rebootDelayTimeout)) {
		System::reboot(rebootMode);
	}
}

void GP2040::handleStorageSave(GPEvent* e) {
	saveRequested = true;
	forceSave = ((GPStorageSaveEvent*)e)->forceSave;
	rebootRequested = ((GPStorageSaveEvent*)e)->restartAfterSave;
	rebootMode = System::BootMode::DEFAULT;
}

void GP2040::handleSystemReboot(GPEvent* e) {
	rebootRequested = true;
	rebootMode = ((GPRestartEvent*)e)->bootMode;
}
