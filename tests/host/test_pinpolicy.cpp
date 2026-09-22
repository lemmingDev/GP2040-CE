#include <cassert>
#include <cstdio>
#include "BoardConfig.h"

static_assert(NUM_BANK0_GPIOS == 49,
    "S3 48-pin policy: NUM_BANK0_GPIOS must be 49 (pins 0-48 inclusive)");
static_assert(sizeof(PIN_NOTES) / sizeof(PIN_NOTES[0]) == 49,
    "S3 48-pin policy: PIN_NOTES must hold exactly 49 entries (index = GPIO number)");

int main() {
    // Unassigned / nonexistent pins stay unmapped.
    // 3/45/46 freed like GPIO0: strapping, usable, warn-at-reset.
    assert(GPIO_PIN_03 == GpioAction::NONE);
    assert(GPIO_PIN_22 == GpioAction::NONE);
    assert(GPIO_PIN_23 == GpioAction::NONE);
    assert(GPIO_PIN_24 == GpioAction::NONE);
    assert(GPIO_PIN_25 == GpioAction::NONE);
    assert(GPIO_PIN_26 == GpioAction::NONE);
    assert(GPIO_PIN_27 == GpioAction::NONE);
    assert(GPIO_PIN_28 == GpioAction::NONE);
    assert(GPIO_PIN_29 == GpioAction::NONE);
    assert(GPIO_PIN_30 == GpioAction::NONE);
    assert(GPIO_PIN_31 == GpioAction::NONE);
    assert(GPIO_PIN_32 == GpioAction::NONE);
    assert(GPIO_PIN_38 == GpioAction::NONE);
    assert(GPIO_PIN_39 == GpioAction::NONE);
    assert(GPIO_PIN_40 == GpioAction::NONE);
    assert(GPIO_PIN_47 == GpioAction::NONE);
    assert(GPIO_PIN_48 == GpioAction::NONE);

    // Reserved: native USB, octal flash/PSRAM, UART0 console, strapping.
    assert(GPIO_PIN_19 == GpioAction::RESERVED);
    assert(GPIO_PIN_20 == GpioAction::RESERVED);
    assert(GPIO_PIN_33 == GpioAction::RESERVED);
    assert(GPIO_PIN_34 == GpioAction::RESERVED);
    // 35-37 freed for quad-flash modules (owner-verified routed 2026-09-22).
    assert(GPIO_PIN_35 == GpioAction::NONE);
    assert(GPIO_PIN_36 == GpioAction::NONE);
    assert(GPIO_PIN_37 == GpioAction::NONE);
    assert(GPIO_PIN_43 == GpioAction::RESERVED);
    assert(GPIO_PIN_44 == GpioAction::RESERVED);
    // 45/46 freed like GPIO0 (strapping, usable, warn-at-reset).
    assert(GPIO_PIN_45 == GpioAction::NONE);
    assert(GPIO_PIN_46 == GpioAction::NONE);

    // Kept assignments: I2C0 addons, TURBO button.
    assert(GPIO_PIN_41 == GpioAction::ASSIGNED_TO_ADDON);
    assert(GPIO_PIN_42 == GpioAction::ASSIGNED_TO_ADDON);
    assert(GPIO_PIN_14 == GpioAction::BUTTON_PRESS_TURBO);

    // PIN_NOTES: non-empty exactly on boot/strapping/USB/flash/UART pins.
    int noted = 0;
    for (int i = 0; i < NUM_BANK0_GPIOS; i++) {
        bool want = (i == 0 || i == 3 || i == 19 || i == 20 ||
                     i == 33 || i == 34 || i == 35 || i == 36 || i == 37 ||
                     i == 43 || i == 44 || i == 45 || i == 46);
        if (want) {
            assert(PIN_NOTES[i][0] != '\0');
            noted++;
        } else {
            assert(PIN_NOTES[i][0] == '\0');
        }
    }
    assert(noted == 13);

    printf("pinpolicy: all assertions passed\n");
    return 0;
}
