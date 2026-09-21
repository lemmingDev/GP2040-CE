/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef ESP32S3DEVKITC1_BOARD_CONFIG_H_
#define ESP32S3DEVKITC1_BOARD_CONFIG_H_

#include "enums.pb.h"
#include "class/hid/hid.h"

#define BOARD_CONFIG_LABEL "ESP32S3DevKitC1"

// S3: Pico SDK's NUM_BANK0_GPIOS (30) doesn't exist outside the SDK; the
// port binds every pin table, 32-bit mask and per-pin array to 30 (see
// below), so define it identically. functional buttons MUST stay below 30.
#define NUM_BANK0_GPIOS 30

// Main pin mapping Configuration
// HARDWARE REALITY (verified vs DevKitC-1 header pinout 2026-09-20): header
// exposes GPIO 0-21 and 35-48, but 19/20 are native USB D-/D+, 0/3/45/46 are
// strapping, 35-37 are module-dependent (octal flash), and 22-34 DO NOT EXIST
// on this package. The core poll loop and all pin tables are bound to
// NUM_BANK0_GPIOS (30) with 32-bit `1 << pin` masks, so every functional
// button MUST live below pin 30. Consequences, all deliberate:
//  - DOWN lives on GPIO 1 (the Pico-template I2C0 slot; S3 I2C0 moved to
//    41/42, so 1 was a placeholder, not a bus).
//  - R3 lives on GPIO 15 (was an addon placeholder; TURBO_LED moves to 39).
//  - A1 (Guide) lives on GPIO 0, the BOOT button: do NOT hold it across a
//    reset or the chip enters download mode. Same caveat as the Pico
//    BOOTSEL-as-button addon, and Guide is the least-pressed button.
//  - GPIO 28 never existed here: it is NONE, not an addon slot. Addon
//    placeholders that remain: 41/42 (I2C0 bus).
//                                                  // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define GPIO_PIN_02 GpioAction::BUTTON_PRESS_UP     // UP     | UP     | UP      | UP       | UP     | UP     |
#define GPIO_PIN_01 GpioAction::BUTTON_PRESS_DOWN   // DOWN   | DOWN   | DOWN    | DOWN     | DOWN   | DOWN   |
#define GPIO_PIN_04 GpioAction::BUTTON_PRESS_RIGHT  // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define GPIO_PIN_05 GpioAction::BUTTON_PRESS_LEFT   // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define GPIO_PIN_06 GpioAction::BUTTON_PRESS_B1     // B1     | A      | B       | Cross    | 2      | K1     |
#define GPIO_PIN_07 GpioAction::BUTTON_PRESS_B2     // B2     | B      | A       | Circle   | 3      | K2     |
#define GPIO_PIN_08 GpioAction::BUTTON_PRESS_R2     // R2     | RT     | ZR      | R2       | 8      | K3     |
#define GPIO_PIN_09 GpioAction::BUTTON_PRESS_L2     // L2     | LT     | ZL      | L2       | 7      | K4     |
#define GPIO_PIN_10 GpioAction::BUTTON_PRESS_B3     // B3     | X      | Y       | Square   | 1      | P1     |
#define GPIO_PIN_11 GpioAction::BUTTON_PRESS_B4     // B4     | Y      | X       | Triangle | 4      | P2     |
#define GPIO_PIN_12 GpioAction::BUTTON_PRESS_R1     // R1     | RB     | R       | R1       | 6      | P3     |
#define GPIO_PIN_13 GpioAction::BUTTON_PRESS_L1     // L1     | LB     | L       | L1       | 5      | P4     |
#define GPIO_PIN_16 GpioAction::BUTTON_PRESS_S1     // S1     | Back   | Minus   | Select   | 9      | Coin   |
#define GPIO_PIN_17 GpioAction::BUTTON_PRESS_S2     // S2     | Start  | Plus    | Start    | 10     | Start  |
#define GPIO_PIN_18 GpioAction::BUTTON_PRESS_L3     // L3     | LS     | LS      | L3       | 11     | LS     |
#define GPIO_PIN_15 GpioAction::BUTTON_PRESS_R3     // R3     | RS     | RS      | R3       | 12     | RS     |
#define GPIO_PIN_00 GpioAction::BUTTON_PRESS_A1     // A1     | Guide  | Home    | PS       | 13     | ~      |
#define GPIO_PIN_21 GpioAction::BUTTON_PRESS_A2     // A2     | ~      | Capture | ~        | 14     | ~      |

// Reserved pins: native USB (19/20) and strapping (3/45/46). Never assign
// inputs here. GPIO 0 is intentionally NOT reserved (see A1 note above).
// (GPIO_PIN_30+ macros are not consumed by the 30-pin board table;
// S3 HAL tasks never extended it — functional buttons must stay below 30.)
#define GPIO_PIN_03 GpioAction::RESERVED
#define GPIO_PIN_19 GpioAction::RESERVED
#define GPIO_PIN_20 GpioAction::RESERVED
#define GPIO_PIN_45 GpioAction::RESERVED
#define GPIO_PIN_46 GpioAction::RESERVED

// Unassigned pins inside the 30-entry table (Phase 1 core loop iterates
// pins 0-29): NONE so the stock table compiles and maps nothing here.
#define GPIO_PIN_22 GpioAction::NONE
#define GPIO_PIN_23 GpioAction::NONE
#define GPIO_PIN_24 GpioAction::NONE
#define GPIO_PIN_25 GpioAction::NONE
#define GPIO_PIN_26 GpioAction::NONE
#define GPIO_PIN_27 GpioAction::NONE
#define GPIO_PIN_29 GpioAction::NONE

// Setting GPIO pins to assigned by add-on
// NOTE: I2C0 moved off strapping pin GPIO 0 to GPIO 41/42; both are
// addon-reserved here. GPIO 28 does not exist on this package (see above),
// so it is NONE. GPIO 15 stopped being an addon slot (it is R3 now).
#define GPIO_PIN_28 GpioAction::NONE
#define GPIO_PIN_41 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_42 GpioAction::ASSIGNED_TO_ADDON

// Keyboard Mapping Configuration
//                                            // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define KEY_DPAD_UP     HID_KEY_ARROW_UP      // UP     | UP     | UP      | UP       | UP     | UP     |
#define KEY_DPAD_DOWN   HID_KEY_ARROW_DOWN    // DOWN   | DOWN   | DOWN    | DOWN     | DOWN   | DOWN   |
#define KEY_DPAD_RIGHT  HID_KEY_ARROW_RIGHT   // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define KEY_DPAD_LEFT   HID_KEY_ARROW_LEFT    // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define KEY_BUTTON_B1   HID_KEY_SHIFT_LEFT    // B1     | A      | B       | Cross    | 2      | K1     |
#define KEY_BUTTON_B2   HID_KEY_Z             // B2     | B      | A       | Circle   | 3      | K2     |
#define KEY_BUTTON_R2   HID_KEY_X             // R2     | RT     | ZR      | R2       | 8      | K3     |
#define KEY_BUTTON_L2   HID_KEY_V             // L2     | LT     | ZL      | L2       | 7      | K4     |
#define KEY_BUTTON_B3   HID_KEY_CONTROL_LEFT  // B3     | X      | Y       | Square   | 1      | P1     |
#define KEY_BUTTON_B4   HID_KEY_ALT_LEFT      // B4     | Y      | X       | Triangle | 4      | P2     |
#define KEY_BUTTON_R1   HID_KEY_SPACE         // R1     | RB     | R       | R1       | 6      | P3     |
#define KEY_BUTTON_L1   HID_KEY_C             // L1     | LB     | L       | L1       | 5      | P4     |
#define KEY_BUTTON_S1   HID_KEY_5             // S1     | Back   | Minus   | Select   | 9      | Coin   |
#define KEY_BUTTON_S2   HID_KEY_1             // S2     | Start  | Plus    | Start    | 10     | Start  |
#define KEY_BUTTON_L3   HID_KEY_EQUAL         // L3     | LS     | LS      | L3       | 11     | LS     |
#define KEY_BUTTON_R3   HID_KEY_MINUS         // R3     | RS     | RS      | R3       | 12     | RS     |
#define KEY_BUTTON_A1   HID_KEY_9             // A1     | Guide  | Home    | PS       | 13     | ~      |
#define KEY_BUTTON_A2   HID_KEY_F2            // A2     | ~      | Capture | ~        | 14     | ~      |
#define KEY_BUTTON_FN   -1                    // Hotkey Function                                        |

#define TURBO_ENABLED 1
#define GPIO_PIN_14 GpioAction::BUTTON_PRESS_TURBO
// Turbo LED cannot stay on 15 (that pin is R3 now); 39 is a free plain GPIO.
#define TURBO_LED_PIN 39

// Onboard RGB pixel defaulted OFF (-1 = unassigned) until the pixel story
// resumes. Proven on hardware: the die is on GPIO 38 (v1.1 board), RMT
// backend healthy (chase observed). Re-enable by setting this to 38.
// (BoardLedAddon + NeoPixel both treat -1 as absent; hal rejects >48.)
#define BOARD_LEDS_PIN -1
#define LED_BRIGHTNESS_MAXIMUM 100
#define LED_BRIGHTNESS_STEPS 5
#define LED_FORMAT LED_FORMAT_GRB
#define LEDS_PER_PIXEL 1

#define LEDS_DPAD_LEFT   0
#define LEDS_DPAD_DOWN   1
#define LEDS_DPAD_RIGHT  2
#define LEDS_DPAD_UP     3
#define LEDS_BUTTON_B3   4
#define LEDS_BUTTON_B4   5
#define LEDS_BUTTON_R1   6
#define LEDS_BUTTON_L1   7
#define LEDS_BUTTON_B1   8
#define LEDS_BUTTON_B2   9
#define LEDS_BUTTON_R2   10
#define LEDS_BUTTON_L2   11
#define LEDS_BUTTON_A1   12
#define LEDS_BUTTON_L3   13
#define LEDS_BUTTON_R3   14
#define LEDS_BUTTON_A2   15

// No PIO-USB on S3: USB device runs on the native USB peripheral instead.
#define USB_PERIPHERAL_ENABLED 0

#define HAS_I2C_DISPLAY 1
// peripheral_i2c.h (via GPGFX_types.h) may already carry brackets-defaults
// (0/-1/-1) when gamepad.h pulls the display chain before this file; undef
// first so the board values win without -Wmacro-redefined noise. S3 board
// file only — no Pico TU includes it.
#undef I2C0_ENABLED
#define I2C0_ENABLED 1
#undef I2C0_PIN_SDA
#define I2C0_PIN_SDA 41
#undef I2C0_PIN_SCL
#define I2C0_PIN_SCL 42
#define BUTTON_LAYOUT BUTTON_LAYOUT_STICKLESS
#define BUTTON_LAYOUT_RIGHT BUTTON_LAYOUT_STICKLESSB

#endif
