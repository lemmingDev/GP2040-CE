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
#if defined(PICO_BOARD)
// S3: Display + NeoPixel loads join in Task 5 (RMT/I2C backends).
#include "addons/display.h"
#endif
#include "addons/pleds.h"
#if defined(PICO_BOARD)
#include "addons/neopicoleds.h"
#endif
#include "addons/reactiveleds.h"
#include "addons/drv8833_rumble.h"

#include <iterator>

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
#else
	// S3: I2C/SPI peripheral backends land in Task 5 and USB host is Phase 2,
	// so no peripheral init runs on the aux core yet. (peripheralmanager.cpp
	// is not in the S3 SRCS, which is also why these calls are guarded out.)
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
#if defined(PICO_BOARD)
	addons.LoadAddon(new DisplayAddon(), CORE1_LOOP);
	addons.LoadAddon(new NeoPicoLEDAddon(), CORE1_LOOP);
#endif
	addons.LoadAddon(new PlayerLEDAddon(), CORE1_LOOP);
	addons.LoadAddon(new BoardLedAddon(), CORE1_LOOP);
	addons.LoadAddon(new BuzzerSpeakerAddon(), CORE1_LOOP);
	addons.LoadAddon(new DRV8833RumbleAddon(), CORE1_LOOP);
	addons.LoadAddon(new ReactiveLEDAddon(), CORE1_LOOP);

#if defined(PICO_BOARD)
	// Initialize our USB manager
	USBHostManager::getInstance().start();
#else
	// S3: USB host stack is Phase 2 — nothing to start on the aux core yet.
#endif

	// Ready to sync Core0 and Core1
	isReady = true;
}

void GP2040Aux::run() {
	while (1) {
		addons.ProcessAddons(CORE1_LOOP);

		// Run auxiliary functions for input driver on Core1
		if ( inputDriver != nullptr ) {
			inputDriver->processAux();
		}
	}
}
