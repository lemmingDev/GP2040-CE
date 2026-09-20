// Task-2 link shims for the S3 core-loop build (RAM-only, no persistence).
//
// Task 3 replaced the FlashPROM section below with the real esp_partition
// backend (lib/FlashPROM/src/FlashPROM_esp32.cpp, in SRCS). What remains is
// the ConfigUtils fresh-defaults stub: the real src/config_utils.cpp reads
// flash via an XIP-mapped EEPROM_ADDRESS_START dereference (config_utils.cpp
// loadConfigInner) and Pico-only get_core_num(), so it cannot compile on S3
// untouched — a later task owns that port. Delete the corresponding stub
// section when each real backend lands; do NOT copy these into the Pico build
// (this file is S3-SRCS-only and is never touched by the Pico CMake).

#include "config_utils.h"
#include "config.pb.h"
#include "enums.pb.h"
#include "BoardConfig.h"
#include "FlashPROM.h"
#include "types.h"

#include <cstring>
#include <string>

#if defined(ESP_PLATFORM)

// ---- FlashPROM: REAL backend (Task 3: lib/FlashPROM/src/FlashPROM_esp32.cpp) ----
// (Task-2 RAM-only start/commit/reset stub removed; no stub path remains for
// FlashPROM on S3.)

// ---- ConfigUtils: fresh-board defaults stub (real nanopb load/save deferred ----
// to the ConfigUtils port task: loadConfigInner XIP-dereferences
// EEPROM_ADDRESS_START and save() asserts Pico get_core_num() == 0) ----
static void setFreshGamepadDefaults(Config& config) {
    GamepadOptions& go = config.gamepadOptions;
    go.inputMode = INPUT_MODE_XINPUT;
    go.dpadMode = DPAD_MODE_DIGITAL;
    go.socdMode = SOCD_MODE_NEUTRAL;
    go.invertXAxis = false;
    go.invertYAxis = false;
    go.fourWayMode = false;
    go.profileNumber = 1;
    go.debounceDelay = 5;
    go.inputModeB1 = INPUT_MODE_SWITCH;
    go.inputModeB2 = INPUT_MODE_XINPUT;
    go.inputModeB3 = INPUT_MODE_PS3;
    go.inputModeB4 = INPUT_MODE_PS4;
    go.inputModeL1 = (InputMode)-1;
    go.inputModeL2 = (InputMode)-1;
    go.inputModeR1 = (InputMode)-1;
    go.inputModeR2 = INPUT_MODE_KEYBOARD;
    go.ps4AuthType = INPUT_MODE_AUTH_TYPE_NONE;
    go.ps5AuthType = INPUT_MODE_AUTH_TYPE_NONE;
    go.xinputAuthType = INPUT_MODE_AUTH_TYPE_NONE;
    go.lockHotkeys = false;
}

void ConfigUtils::load(Config& config) {
    memset(&config, 0, sizeof(Config));
    setFreshGamepadDefaults(config);
    // Fresh-board pin table straight from BoardConfig.h (mirrors the
    // fromBoardConfig pass in config_utils.cpp for a board with no profile).
    // RESERVED/ASSIGNED pins stay non-positive and are never init'd, so the
    // USB (19/20) and strapping (0/3) pins are safe by construction.
    static const GpioAction boardDefaults[NUM_BANK0_GPIOS] = {
        GPIO_PIN_00, GPIO_PIN_01, GPIO_PIN_02,
        GPIO_PIN_03, GPIO_PIN_04, GPIO_PIN_05,
        GPIO_PIN_06, GPIO_PIN_07, GPIO_PIN_08,
        GPIO_PIN_09, GPIO_PIN_10, GPIO_PIN_11,
        GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14,
        GPIO_PIN_15, GPIO_PIN_16, GPIO_PIN_17,
        GPIO_PIN_18, GPIO_PIN_19, GPIO_PIN_20,
        GPIO_PIN_21, GPIO_PIN_22, GPIO_PIN_23,
        GPIO_PIN_24, GPIO_PIN_25, GPIO_PIN_26,
        GPIO_PIN_27, GPIO_PIN_28, GPIO_PIN_29,
    };
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        config.gpioMappings.pins[pin].has_action = true;
        config.gpioMappings.pins[pin].action = boardDefaults[pin];
    }
    config.gpioMappings.pins_count = NUM_BANK0_GPIOS;
}

bool ConfigUtils::save(Config& config) {
    (void)config;
    // RAM stub: nothing persists until Task 3; report success so the core
    // loop (boot-mode saves, hotkey saves) proceeds normally.
    return true;
}

void ConfigUtils::initUnsetPropertiesWithDefaults(Config& config) {
    setFreshGamepadDefaults(config);
}

std::string ConfigUtils::toJSON(const Config& config) {
    (void)config;
    return "{}";
}

bool ConfigUtils::fromJSON(Config& config, const char* data, size_t dataLen) {
    (void)config;
    (void)data;
    (void)dataLen;
    return false;
}

bool ConfigUtils::fromLegacyStorage(Config& config) {
    (void)config;
    return false;
}

#endif // defined(ESP_PLATFORM)
