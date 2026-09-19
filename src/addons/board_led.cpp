#include "addons/board_led.h"
#include "drivermanager.h"
#include "drivers/ps4/PS4Driver.h"
#include "usbdriver.h"
#include "helper.h"
#include "config.pb.h"
#include "hal_gpio.h"

bool BoardLedAddon::available() {
    const OnBoardLedOptions& options = Storage::getInstance().getAddonOptions().onBoardLedOptions;
    return options.enabled && options.mode != OnBoardLedMode::ON_BOARD_LED_MODE_OFF; // Available only when it's not set to off
}

void BoardLedAddon::setup() {
    const OnBoardLedOptions& options = Storage::getInstance().getAddonOptions().onBoardLedOptions;
    onBoardLedMode = options.mode;
    isConfigMode = Storage::getInstance().GetConfigMode();
    timeSinceBlink = getMillis();
    prevState = -1;

    hal::gpioInit(BOARD_LED_PIN);
    hal::gpioSetOutput(BOARD_LED_PIN);
}

void BoardLedAddon::process() {
    bool state = 0;
    Gamepad * processedGamepad;
    uint16_t joystickMid = GAMEPAD_JOYSTICK_MID;
    if ( DriverManager::getInstance().getDriver() != nullptr ) {
        joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
    }
    switch (onBoardLedMode) {
        case OnBoardLedMode::ON_BOARD_LED_MODE_INPUT_TEST: // Blinks on input
            processedGamepad = Storage::getInstance().GetProcessedGamepad();
            state =    (processedGamepad->state.buttons != 0)
                    || (processedGamepad->state.dpad    != 0)
                    || (processedGamepad->state.lx      != joystickMid)
                    || (processedGamepad->state.rx      != joystickMid)
                    || (processedGamepad->state.ly      != joystickMid)
                    || (processedGamepad->state.ry      != joystickMid)
                    || (processedGamepad->state.lt      != 0)
                    || (processedGamepad->state.rt      != 0)
                    || (processedGamepad->state.aux     != 0);
            if (prevState != state) {
                hal::gpioPut(BOARD_LED_PIN, state ? 1 : 0);
            }
            prevState = state;
            break;
        case OnBoardLedMode::ON_BOARD_LED_MODE_MODE_INDICATOR: // Blinks based on USB state and config mode
            if (!get_usb_mounted()) { // USB not mounted
                uint32_t millis = getMillis();
                if ((millis - timeSinceBlink) > BLINK_INTERVAL_USB_UNMOUNTED) {
                    hal::gpioPut(BOARD_LED_PIN, prevState ? 1 : 0);
                    timeSinceBlink = millis;
                    prevState = !prevState;
                }
            } else {
                if (isConfigMode) { // Current mode is config
                    uint32_t millis = getMillis();
                    if ((millis - timeSinceBlink) > BLINK_INTERVAL_CONFIG_MODE) {
                        hal::gpioPut(BOARD_LED_PIN, prevState ? 1 : 0);
                        timeSinceBlink = millis;
                        prevState = !prevState;
                    }
                } else { // Regular mode and functional
                    if (prevState != 1) {
                        hal::gpioPut(BOARD_LED_PIN, 1);
                        prevState = 1;
                    }
                }
            }
            break;
        case OnBoardLedMode::ON_BOARD_LED_MODE_PS_AUTH:
            processedGamepad = Storage::getInstance().GetProcessedGamepad();
            if(processedGamepad->getOptions().inputMode == INPUT_MODE_PS4 ||
                processedGamepad->getOptions().inputMode == INPUT_MODE_PS5) {
                state = ((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent() == true;
            }
            if (prevState != state) {
                hal::gpioPut(BOARD_LED_PIN, state ? 1 : 0);
            }
            prevState = state;
            break;
        case OnBoardLedMode::ON_BOARD_LED_MODE_OFF:
        default:
            break;
    }
}
