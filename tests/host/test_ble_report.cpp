#include <cassert>
#include <cstdio>
#include "ble_gamepad_report.h"

int main() {
    // A(0) + RB(5) + Guide(10) pressed, half left trigger, sticks at extremes
    BleGamepadReport r = buildBleGamepadReport(
        (1u << 0) | (1u << 5) | (1u << 10), 128, 0, -32768, 32767, 0, 0);
    assert(r.buttons == 0x421u);
    assert(r.leftTrigger == 128 && r.rightTrigger == 0);
    assert(r.leftX == -32768 && r.leftY == 32767);
    // Button bits above bit10 are masked out
    BleGamepadReport m = buildBleGamepadReport(0xFFFFu, 0, 0, 0, 0, 0, 0);
    assert(m.buttons == 0x07FFu);
    // Task 8 Step 0: raw GamepadState stick units (uint16 0..0xFFFF, center
    // GAMEPAD_JOYSTICK_MID 0x7FFF) -> signed HID units. Center maps to -1,
    // not 0: the uint16 range has no exact signed zero and 0x7FFF sits one
    // below the 0x8000 pivot — the same off-by-one XInputDriver
    // (static_cast<int16_t>(raw) + INT16_MIN) and HIDDriver (0x7F vs 0x80)
    // already accept.
    assert(bleAxisFromRaw(0x7FFFu) == -1);
    assert(bleAxisFromRaw(0u) == -32768);
    assert(bleAxisFromRaw(0xFFFFu) == 32767);
    // End-to-end: converted center sticks round-trip through the builder.
    BleGamepadReport c = buildBleGamepadReport(0, 0, 0,
        bleAxisFromRaw(0x7FFFu), bleAxisFromRaw(0x7FFFu),
        bleAxisFromRaw(0x7FFFu), bleAxisFromRaw(0x7FFFu));
    assert(c.leftX == -1 && c.leftY == -1 && c.rightX == -1 && c.rightY == -1);
    printf("ble_report: all assertions passed\n");
    return 0;
}
