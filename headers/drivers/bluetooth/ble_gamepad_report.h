#ifndef _BLE_GAMEPAD_REPORT_H_
#define _BLE_GAMEPAD_REPORT_H_

#include <stdint.h>

// Xbox/Series-X control set over a standard HID gamepad report:
// 11 buttons (A,B,X,Y,LB,RB,Back,Start,LS,RS,Guide), 2 analog triggers, 4 stick axes.
struct BleGamepadReport {
    uint16_t buttons; // bit0=A bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Back bit7=Start bit8=LS bit9=RS bit10=Guide
    uint8_t  leftTrigger;
    uint8_t  rightTrigger;
    int16_t  leftX;
    int16_t  leftY;
    int16_t  rightX;
    int16_t  rightY;
};

inline BleGamepadReport buildBleGamepadReport(uint16_t buttons, uint8_t lt, uint8_t rt,
                                              int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    BleGamepadReport r;
    r.buttons = buttons & 0x07FFu;
    r.leftTrigger = lt;
    r.rightTrigger = rt;
    r.leftX = lx;
    r.leftY = ly;
    r.rightX = rx;
    r.rightY = ry;
    return r;
}

// Map raw GamepadState stick units (uint16_t 0..0xFFFF, center
// GAMEPAD_JOYSTICK_MID = 0x7FFF) to signed HID report units (int16_t,
// center ~0). Same numeric convention as XInputDriver::process()
// (static_cast<int16_t>(raw) + INT16_MIN): raw 0 -> -32768,
// raw 0x7FFF -> -1 (center; off-by-one shared with HIDDriver's 0x7F-vs-0x80),
// raw 0xFFFF -> 32767. Computed in int32_t so every result is in int16_t
// range before the single narrowing cast (identical results to the XInput
// form on two's-complement targets, without relying on signed overflow).
// No Y inversion: HOGP follows HIDDriver (ly >> 8, direct), not XInput.
inline int16_t bleAxisFromRaw(uint16_t raw) {
    return static_cast<int16_t>(static_cast<int32_t>(raw) + INT16_MIN);
}

#endif
