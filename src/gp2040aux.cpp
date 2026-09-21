// GP2040 includes
#include "gp2040aux.h"
#include "gamepad.h"

#include "drivermanager.h"
#include "storagemanager.h"
#if defined(PICO_BOARD)
#include "usbhostmanager.h"
#endif

#include "addons/board_led.h"  // Add-Ons
#include "addons/buzzerspeaker.h"
#if defined(PICO_BOARD) || defined(ESP_PLATFORM)
// S3: Display + NeoPixel join in Task 5 (I2C/RMT backends).
#include "addons/display.h"
#endif
#include "addons/pleds.h"
#if defined(PICO_BOARD) || defined(ESP_PLATFORM)
#include "addons/neopicoleds.h"
#endif
#include "addons/reactiveleds.h"
#include "addons/drv8833_rumble.h"
#if defined(ESP_PLATFORM)
// S3: peripheralmanager.cpp is an S3 source (I2C backend); the Pico build
// reaches it transitively.
#include "peripheralmanager.h"
#endif

#include <iterator>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Note: initializer order matches member declaration order in gp2040aux.h
// (S3 builds with -Werror=reorder).
GP2040Aux::GP2040Aux() : inputDriver(nullptr), isReady(false) {
}

GP2040Aux::~GP2040Aux() {
}

// GP2040Aux will always come after GP2040 setup(), so we can rely on the
// GP2040 setup function for certain setup functions.
void GP2040Aux::setup() {
#if defined(PICO_BOARD)
	PeripheralManager::getInstance().initI2C();
	PeripheralManager::getInstance().initSPI();
	PeripheralManager::getInstance().initUSB();
#elif defined(ESP_PLATFORM)
	// S3: I2C only (Task 5 display backend). SPI has no S3 backend in
	// Phase 1 and USB host is Phase 2, so those stay Pico-only.
	// (peripheralmanager.cpp is in the S3 SRCS for exactly this call.)
	PeripheralManager::getInstance().initI2C();
#endif

	// Initialize our input driver's auxilliary functions
	inputDriver = DriverManager::getInstance().getDriver();
	if ( inputDriver != nullptr ) {
		inputDriver->initializeAux();

#if defined(PICO_BOARD)
		// Check if we have a USB listener
		USBListener * listener = inputDriver->get_usb_auth_listener();
		if (listener != nullptr) {
			USBHostManager::getInstance().pushListener(listener);
		}
#else
		// S3: USB-host auth passthrough is Phase 2 — no listener to push.
#endif
	}

	// Setup Add-ons
	addons.LoadAddon(new DisplayAddon());
	addons.LoadAddon(new NeoPicoLEDAddon());
	addons.LoadAddon(new PlayerLEDAddon());
	addons.LoadAddon(new BoardLedAddon());
	addons.LoadAddon(new BuzzerSpeakerAddon());
	addons.LoadAddon(new DRV8833RumbleAddon());
	addons.LoadAddon(new ReactiveLEDAddon());

	// Ready to sync Core0 and Core1
	isReady = true;
}

void GP2040Aux::run() {
	while (1) {
		// Pre, Process, and Post
		addons.PreprocessAddons();
		addons.ProcessAddons();

		// Run auxiliary functions for input driver on Core1
		if ( inputDriver != nullptr ) {
			inputDriver->processAux();
		}
#if defined(ESP_PLATFORM)
		// FreeRTOS: a never-blocking loop starves IDLE1 and trips the task
		// watchdog (Pico core1 spins bare-metal, no watchdog). Delay exactly
		// one RTOS tick — NOT pdMS_TO_TICKS(1): below a 1000 Hz tick rate
		// that truncates to 0 ticks (a mere yield, still starves IDLE).
		vTaskDelay(1);
#endif
	}
}
