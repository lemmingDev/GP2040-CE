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
    printf("ble_report: all assertions passed\n");
    return 0;
}
