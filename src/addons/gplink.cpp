#include "addons/gplink.h"

#include "gamepad.h"
#include "storagemanager.h"
#include "helper.h"
#include "config.pb.h"
#include "gplink.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <math.h>

// Axis flag start (lx,ly,rx,ry order), same convention as ADS1115_CHANNEL_FLAG_START.
#define GPLINK_ANALOG_AXIS_FLAG_START 0b1000

// Report at most every 2 ms (500 Hz, spec section 2); heartbeats cover the idle.
#define GPLINK_MIN_REPORT_INTERVAL_MS 2

// In-protocol telemetry beacon: readable in gamepad mode (where webconfig is
// down) via the companion console. 5 s cadence, ~60 B, negligible load.
#define GPLINK_DEBUG_INTERVAL_MS 5000

// Companion reboots wipe its pin config (RAM-only) at any time, so refresh
// ours on every link rise plus a 30 s backstop. Cheap, idempotent.
#define GPLINK_CONFIG_REFRESH_MS 30000

static GPLinkAddon *s_instance = nullptr;

GPLinkAddon *GPLink_GetAddon() {
    return s_instance;
}

GPLinkAddon::GPLinkAddon() : started(false), uartInst(GPLINK_UART_INSTANCE),
        txPin(GPLINK_TX_PIN), rxPin(GPLINK_RX_PIN),
        processCalls(0), rxBytes(0), lastDebugMs(0), wasAlive(false), lastConfigMs(0),
        txSeq(0), haveLastSent(false), ignoredFrames(0), handledFrames(0), seqGaps(0),
        lastMask(0), lastOutputMask(0), lastOutputMs(0),
        lastLedMask(0), lastWeak(0), lastStrong(0), lastActMs(0) {
    s_instance = this;
    compFw[0] = '\0';
    compRadio = 0;
}

void GPLinkAddon::getStatus(GPLinkStatus &out) {
    uint32_t now = getMillis();
    out.started = started;
    out.linkAlive = started && gplink_link_alive(&link, now);
    out.seqGaps = seqGaps;
    out.ignoredFrames = ignoredFrames;
    out.handledFrames = handledFrames;
    out.txSeq = txSeq;
    uart_inst_t *inst = (uartInst == 0) ? uart0 : uart1;
    out.txFuncOk = started && (gpio_get_function(txPin) == GPIO_FUNC_UART);
    out.rxFuncOk = started && (gpio_get_function(rxPin) == GPIO_FUNC_UART);
    out.uartFr = started ? uart_get_hw(inst)->fr : 0;
    out.processCalls = processCalls;
    out.rxBytes = rxBytes;
    out.uptimeS = now / 1000;
    strncpy(out.fwVersion, compFw, sizeof(out.fwVersion) - 1);
    out.fwVersion[sizeof(out.fwVersion) - 1] = '\0';
    out.radioFlags = compRadio;
}

bool GPLinkAddon::requestCaps() {
    if (!started) return false;
    uint8_t payload[8];
    size_t len = gplink_pack_pin_caps_req(payload);
    // Empty REQ payload packs to 0 length; a zero-length frame is still valid.
    return sendFrame(GPLINK_TYPE_PIN_CAPS_REQ, payload, len);
}

uint16_t GPLinkAddon::getAnalogPinValue(uint8_t pin) {
    if (pin >= GPLINK_PIN_COUNT) return GAMEPAD_JOYSTICK_MID;
    return analogValues[pin];
}

bool GPLinkAddon::available() {
    const AddonOptions& options = Storage::getInstance().getAddonOptions();
    if (!options.gplinkOptions.enabled && !options.gplinkAnalogOptions.enabled) return false;
    return gplink_uart_pins_valid((uint8_t)options.gplinkOptions.uartInstance,
                                  (uint8_t)options.gplinkOptions.txPin,
                                  (uint8_t)options.gplinkOptions.rxPin);
}

void GPLinkAddon::setup() {
    const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
    started = false;
    // wasAlive starts true so setup's own CONFIG round isn't immediately
    // duplicated by the rise detector; genuine rises still refresh (silence
    // flips it false first).
    wasAlive = true;
    lastConfigMs = getMillis();
    uartInst = (uint8_t)options.uartInstance;
    txPin = (uint8_t)options.txPin;
    rxPin = (uint8_t)options.rxPin;
    if (!gplink_uart_init(uartInst, txPin, rxPin, options.baudRate)) {
        return;
    }
    uint32_t now = getMillis();
    gplink_decoder_init(&decoder);
    gplink_link_init(&link, now);
    // This board's rumble output path is the companion: enable the haptic
    // actuators (same contract as DRV8833 for its motors) so USB drivers
    // actually populate active/intensity instead of dropping reports.
    Gamepad *processed = Storage::getInstance().GetProcessedGamepad();
    processed->auxState.haptics.leftActuator.enabled = true;
    processed->auxState.haptics.rightActuator.enabled = true;
    txSeq = 0;
    haveLastSent = false;
    ignoredFrames = 0;
    handledFrames = 0;
    seqGaps = 0;
    lastMask = 0;
    lastOutputMask = 0;
    lastOutputMs = 0;
    lastLedMask = 0;
    lastWeak = 0;
    lastStrong = 0;
    lastActMs = 0;
    for (uint8_t i = 0; i < GPLINK_PIN_COUNT; i++) analogValues[i] = GAMEPAD_JOYSTICK_MID;
    started = true;
    sendHello();
    sendGpioConfigs();
    sendAnalogConfigs();
}

// Tell the companion which ADC pins feed our sticks and triggers (by
// companion GPIO number, -1 = unused). Values arrive normalized full-range
// u16. Trigger pins must be included or the companion never samples them
// (unsampled pins read back constant MID).
void GPLinkAddon::sendAnalogConfigs() {
    const GPLinkAnalogOptions& options = Storage::getInstance().getAddonOptions().gplinkAnalogOptions;
    if (!options.enabled) return;
    const int32_t pins[6] = {options.lxPin, options.lyPin, options.rxPin, options.ryPin,
                             options.ltPin, options.rtPin};
    for (uint8_t i = 0; i < 6; i++) {
        if (pins[i] < 0 || pins[i] >= GPLINK_PIN_COUNT) continue;
        bool dup = false;
        for (uint8_t j = 0; j < i; j++) {
            if (pins[j] == pins[i]) { dup = true; break; }
        }
        if (dup) continue;
        uint8_t payload[8];
        size_t len = gplink_pack_analog_config(/*devid=*/0, (uint8_t)pins[i], /*enable=*/1, payload);
        if (len > 0) sendFrame(GPLINK_TYPE_ANALOG_CONFIG, payload, len);
    }
}

void GPLinkAddon::applyAnalogPin(uint8_t pin, uint16_t value) {
    if (pin >= GPLINK_PIN_COUNT) return;
    analogValues[pin] = value;
}

static uint16_t gplinkMagnitudeXY(uint16_t channelX, uint16_t channelY) {
    int16_t xOffset = channelX - GAMEPAD_JOYSTICK_MID;
    int16_t yOffset = channelY - GAMEPAD_JOYSTICK_MID;
    return (uint16_t)sqrt((xOffset * xOffset) + (yOffset * yOffset));
}

// Map a raw companion trigger value through a calibrated [min, max] window
// to 0-255, clamping outside. A degenerate window falls back to identity.
static uint8_t gplinkTriggerValue(uint16_t raw, uint32_t vmin, uint32_t vmax) {
    if (vmax <= vmin) return (uint8_t)(raw >> 8);
    int32_t t = ((int32_t)raw - (int32_t)vmin) * 255 / ((int32_t)vmax - (int32_t)vmin);
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    return (uint8_t)t;
}

// Modifier pipeline mirroring I2CAnalog1115Input: per-axis inner/outer
// deadzones, invert, then radial per-stick deadzones. Runs every poll over
// the stored channel values so shaping holds between frames (same sticky
// philosophy as the GPIO mask re-apply).
void GPLinkAddon::applyAnalogAxes() {
    const GPLinkAnalogOptions& options = Storage::getInstance().getAddonOptions().gplinkAnalogOptions;
    if (!options.enabled) return;
    const int32_t pins[4] = {options.lxPin, options.lyPin, options.rxPin, options.ryPin};
    bool anyMapped = false;
    for (uint8_t i = 0; i < 4; i++) {
        if (pins[i] >= 0 && pins[i] < GPLINK_PIN_COUNT) {
            anyMapped = true;
            break;
        }
    }
    // Triggers without sticks still need the tail of the pipeline (the
    // trigger block runs after the stick section below).
    if (!anyMapped) {
        if ((options.ltPin >= 0 && options.ltPin < GPLINK_PIN_COUNT) ||
            (options.rtPin >= 0 && options.rtPin < GPLINK_PIN_COUNT)) {
            anyMapped = true;
        }
    }
    if (!anyMapped) return; // enabled but nothing mapped: leave state alone
    const uint32_t innerDz[4] = {options.axis0InnerDeadzone, options.axis1InnerDeadzone,
                                 options.axis2InnerDeadzone, options.axis3InnerDeadzone};
    const uint32_t outerDz[4] = {options.axis0OuterDeadzone, options.axis1OuterDeadzone,
                                 options.axis2OuterDeadzone, options.axis3OuterDeadzone};
    uint16_t axis[4];
    const uint32_t centers[4] = {options.lxCenter, options.lyCenter,
                                 options.rxCenter, options.ryCenter};
    const uint32_t axisMins[4] = {options.lxMin, options.lyMin,
                                  options.rxMin, options.ryMin};
    const uint32_t axisMaxs[4] = {options.lxMax, options.lyMax,
                                  options.rxMax, options.ryMax};
    for (uint8_t i = 0; i < 4; i++) {
        if (pins[i] >= 0 && pins[i] < GPLINK_PIN_COUNT) {
            uint16_t raw = analogValues[pins[i]];
            uint32_t c = centers[i], lo = axisMins[i], hi = axisMaxs[i];
            int32_t centered;
            // Asymmetric min/center/max map mirroring official readPin: a
            // valid window maps each side to full range; otherwise the
            // legacy centers-only mapping applies (identity at defaults).
            if (lo < c && c < hi) {
                float delta = (float)raw - (float)c;
                float span = (delta < 0.0f) ? (float)(c - lo) : (float)(hi - c);
                float fv = (float)GAMEPAD_JOYSTICK_MID
                         + (float)GAMEPAD_JOYSTICK_MID * (delta / span);
                centered = (int32_t)std::clamp(fv, (float)GAMEPAD_JOYSTICK_MIN,
                                               (float)GAMEPAD_JOYSTICK_MAX);
            } else {
                centered = (int32_t)raw - (int32_t)c + GAMEPAD_JOYSTICK_MID;
            }
            axis[i] = (uint16_t)std::clamp(centered, (int32_t)GAMEPAD_JOYSTICK_MIN,
                                           (int32_t)GAMEPAD_JOYSTICK_MAX);
        } else {
            axis[i] = GAMEPAD_JOYSTICK_MID;
        }
        int32_t offset = (int32_t)axis[i] - GAMEPAD_JOYSTICK_MID;
        uint32_t inner = innerDz[i] * (1 << 16) / 100;
        uint32_t outer = outerDz[i] * (1 << 16) / 100;
        // Per-axis inner snap (self-arming: nonzero percent enables it; the
        // innerDeadzoneEnabled bitmask is unused). Snap boundary inclusive,
        // mirroring official (<=).
        if (inner > 0 && abs(offset) <= (int32_t)inner) {
            axis[i] = GAMEPAD_JOYSTICK_MID;
        } else {
            // Outer rescale mirrors the official Analog addon: the [inner,
            // outer] window maps to full output, saturating beyond (percent
            // of full range, same units as ADS1115; outer=100 = linear).
            int32_t den = (int32_t)outer - (int32_t)inner;
            if (den > 0) {
                uint32_t dist = (uint32_t)(offset >= 0 ? offset : -offset);
                uint32_t mag = (dist - inner) * (uint32_t)GAMEPAD_JOYSTICK_MAX / (uint32_t)den;
                if (mag > (uint32_t)GAMEPAD_JOYSTICK_MAX) mag = (uint32_t)GAMEPAD_JOYSTICK_MAX;
                int32_t out = (int32_t)GAMEPAD_JOYSTICK_MID + (offset >= 0 ? (int32_t)mag : -(int32_t)mag);
                axis[i] = (uint16_t)std::clamp(out, (int32_t)GAMEPAD_JOYSTICK_MIN, (int32_t)GAMEPAD_JOYSTICK_MAX);
            }
        }
        if (options.invertEnabled & (GPLINK_ANALOG_AXIS_FLAG_START >> i)) {
            axis[i] = GAMEPAD_JOYSTICK_MAX - axis[i];
        }
        // EMA smoothing mirrors official AnalogInput: strength scale 0-10
        // with alpha = 10^(-strength/2); out-of-range strength disables.
        // First sample seeds history (no ramp from a corner). Alpha is
        // cached per stick and recomputed only when the factor changes.
        uint8_t s = (i < 2) ? 0 : 1;
        bool smooth = ((i < 2) ? options.smoothingEnabled : options.smoothingEnabled2);
        uint32_t factor = (i < 2) ? options.smoothingFactor : options.smoothingFactor2;
        bool useSmooth = smooth && factor >= 1 && factor <= 10;
        if (useSmooth) {
            if (emaAlphaFactor[s] != factor) {
                emaAlphaFactor[s] = factor;
                emaAlpha[s] = powf(10.0f, -0.5f * (float)factor);
            }
            float norm = (float)axis[i] / (float)GAMEPAD_JOYSTICK_MAX;
            if (!emaReady[i]) {
                analogEma[i] = norm;
                emaReady[i] = true;
            } else {
                analogEma[i] = analogEma[i] + emaAlpha[s] * (norm - analogEma[i]);
            }
            axis[i] = (uint16_t)(analogEma[i] * (float)GAMEPAD_JOYSTICK_MAX);
        }
        // TODO apply auto calibration (also TODO upstream)
    }
    if (options.leftStickDeadzoneEnabled) {
        uint32_t dz = options.leftStickDeadzone * (1 << 16) / 100;
        if (gplinkMagnitudeXY(axis[0], axis[1]) < dz) {
            axis[0] = GAMEPAD_JOYSTICK_MID;
            axis[1] = GAMEPAD_JOYSTICK_MID;
        }
    }
    if (options.rightStickDeadzoneEnabled) {
        uint32_t dz = options.rightStickDeadzone * (1 << 16) / 100;
        if (gplinkMagnitudeXY(axis[2], axis[3]) < dz) {
            axis[2] = GAMEPAD_JOYSTICK_MID;
            axis[3] = GAMEPAD_JOYSTICK_MID;
        }
    }
    // Forced circularity: clamp each stick's offset vector to the inscribed
    // circle so diagonals can't reach the square corners (official caps the
    // radial scaling factor at center for the same effect).
    const bool circular[2] = {options.forcedCircularity, options.forcedCircularity2};
    for (uint8_t s = 0; s < 2; s++) {
        if (!circular[s]) continue;
        float dx = (float)((int32_t)axis[2 * s] - GAMEPAD_JOYSTICK_MID);
        float dy = (float)((int32_t)axis[2 * s + 1] - GAMEPAD_JOYSTICK_MID);
        float mag = sqrtf(dx * dx + dy * dy);
        if (mag > (float)GAMEPAD_JOYSTICK_MID && mag > 0.0f) {
            float k = (float)GAMEPAD_JOYSTICK_MID / mag;
            axis[2 * s] = (uint16_t)((float)GAMEPAD_JOYSTICK_MID + dx * k);
            axis[2 * s + 1] = (uint16_t)((float)GAMEPAD_JOYSTICK_MID + dy * k);
        }
    }
    Gamepad *gamepad = Storage::getInstance().GetGamepad();
    gamepad->state.lx = axis[0];
    gamepad->state.ly = axis[1];
    gamepad->state.rx = axis[2];
    gamepad->state.ry = axis[3];
    shapedState[0] = axis[0];
    shapedState[1] = axis[1];
    shapedState[2] = axis[2];
    shapedState[3] = axis[3];
    // Analog triggers mirror the ADS1256 addon: normalized full-range
    // companion values mapped straight to lt/rt, asserted every poll (our
    // addon runs last, so this wins over addons that clear the flag).
    // An unmapped pin (-1) disables that trigger; nothing is written.
    bool anyTrigger = false;
    if (options.ltPin >= 0 && options.ltPin < GPLINK_PIN_COUNT) {
        uint8_t lt = gplinkTriggerValue(analogValues[options.ltPin],
                                        options.ltMin, options.ltMax);
        lt = (options.triggerInvert & 0x01) ? (uint8_t)(255 - lt) : lt;
        gamepad->state.lt = lt;
        shapedState[4] = lt;
        anyTrigger = true;
    }
    if (options.rtPin >= 0 && options.rtPin < GPLINK_PIN_COUNT) {
        uint8_t rt = gplinkTriggerValue(analogValues[options.rtPin],
                                        options.rtMin, options.rtMax);
        rt = (options.triggerInvert & 0x02) ? (uint8_t)(255 - rt) : rt;
        gamepad->state.rt = rt;
        shapedState[5] = rt;
        anyTrigger = true;
    }
    if (anyTrigger) gamepad->hasAnalogTriggers = true;
    shapedRuns++;
}

// Tell the companion which of its pins we use (inputs and outputs), with
// each pin's electrical config (pull + invert flag). Mask bits read back
// as 1 mean pressed (companion maps levels per these settings).
void GPLinkAddon::sendGpioConfigs() {
    const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
    uint32_t count = options.gplinkPins_count;
    if (count > GPLINK_PIN_COUNT) count = GPLINK_PIN_COUNT;
    for (uint8_t i = 0; i < count; i++) {
        const GpioMappingInfo &pin = options.gplinkPins[i];
        if (pin.action == GpioAction::NONE || pin.action == GpioAction::RESERVED ||
                pin.action == GpioAction::ASSIGNED_TO_ADDON) continue;
        uint8_t dir = (pin.direction == GpioDirection::GPIO_DIRECTION_OUTPUT) ? 1 : 0;
        uint8_t pull = (pin.pull <= GPLINK_GPIO_PULL_DOWN) ? (uint8_t)pin.pull : GPLINK_GPIO_PULL_UP;
        uint8_t flags = pin.inverted ? GPLINK_GPIO_FLAG_INVERTED : 0;
        uint8_t payload[8];
        size_t len = gplink_pack_gpio_config(/*devid=*/0, i, dir, pull, flags, payload);
        if (len > 0) sendFrame(GPLINK_TYPE_GPIO_CONFIG, payload, len);
    }
}

// Level of a mapped action in the current state (for mirrored outputs).
static bool gplinkActionLevel(GpioAction action, const GamepadState &state) {
    switch (action) {
        case GpioAction::BUTTON_PRESS_UP:    return (state.dpad & GAMEPAD_MASK_UP) != 0;
        case GpioAction::BUTTON_PRESS_DOWN:  return (state.dpad & GAMEPAD_MASK_DOWN) != 0;
        case GpioAction::BUTTON_PRESS_LEFT:  return (state.dpad & GAMEPAD_MASK_LEFT) != 0;
        case GpioAction::BUTTON_PRESS_RIGHT: return (state.dpad & GAMEPAD_MASK_RIGHT) != 0;
        case GpioAction::BUTTON_PRESS_B1:    return (state.buttons & GAMEPAD_MASK_B1) != 0;
        case GpioAction::BUTTON_PRESS_B2:    return (state.buttons & GAMEPAD_MASK_B2) != 0;
        case GpioAction::BUTTON_PRESS_B3:    return (state.buttons & GAMEPAD_MASK_B3) != 0;
        case GpioAction::BUTTON_PRESS_B4:    return (state.buttons & GAMEPAD_MASK_B4) != 0;
        case GpioAction::BUTTON_PRESS_L1:    return (state.buttons & GAMEPAD_MASK_L1) != 0;
        case GpioAction::BUTTON_PRESS_R1:    return (state.buttons & GAMEPAD_MASK_R1) != 0;
        case GpioAction::BUTTON_PRESS_L2:    return (state.buttons & GAMEPAD_MASK_L2) != 0;
        case GpioAction::BUTTON_PRESS_R2:    return (state.buttons & GAMEPAD_MASK_R2) != 0;
        case GpioAction::BUTTON_PRESS_S1:    return (state.buttons & GAMEPAD_MASK_S1) != 0;
        case GpioAction::BUTTON_PRESS_S2:    return (state.buttons & GAMEPAD_MASK_S2) != 0;
        case GpioAction::BUTTON_PRESS_L3:    return (state.buttons & GAMEPAD_MASK_L3) != 0;
        case GpioAction::BUTTON_PRESS_R3:    return (state.buttons & GAMEPAD_MASK_R3) != 0;
        case GpioAction::BUTTON_PRESS_A1:    return (state.buttons & GAMEPAD_MASK_A1) != 0;
        case GpioAction::BUTTON_PRESS_A2:    return (state.buttons & GAMEPAD_MASK_A2) != 0;
        case GpioAction::BUTTON_PRESS_A3:    return (state.buttons & GAMEPAD_MASK_A3) != 0;
        case GpioAction::BUTTON_PRESS_A4:    return (state.buttons & GAMEPAD_MASK_A4) != 0;
        case GpioAction::BUTTON_PRESS_E1:    return (state.buttons & GAMEPAD_MASK_E1) != 0;
        case GpioAction::BUTTON_PRESS_E2:    return (state.buttons & GAMEPAD_MASK_E2) != 0;
        case GpioAction::BUTTON_PRESS_E3:    return (state.buttons & GAMEPAD_MASK_E3) != 0;
        case GpioAction::BUTTON_PRESS_E4:    return (state.buttons & GAMEPAD_MASK_E4) != 0;
        case GpioAction::BUTTON_PRESS_E5:    return (state.buttons & GAMEPAD_MASK_E5) != 0;
        case GpioAction::BUTTON_PRESS_E6:    return (state.buttons & GAMEPAD_MASK_E6) != 0;
        case GpioAction::BUTTON_PRESS_E7:    return (state.buttons & GAMEPAD_MASK_E7) != 0;
        case GpioAction::BUTTON_PRESS_E8:    return (state.buttons & GAMEPAD_MASK_E8) != 0;
        case GpioAction::BUTTON_PRESS_E9:    return (state.buttons & GAMEPAD_MASK_E9) != 0;
        case GpioAction::BUTTON_PRESS_E10:   return (state.buttons & GAMEPAD_MASK_E10) != 0;
        case GpioAction::BUTTON_PRESS_E11:   return (state.buttons & GAMEPAD_MASK_E11) != 0;
        case GpioAction::BUTTON_PRESS_E12:   return (state.buttons & GAMEPAD_MASK_E12) != 0;
        case GpioAction::BUTTON_PRESS_FN:    return (state.aux & AUX_MASK_FUNCTION) != 0;
        default: break;
    }
    return false;
}

void GPLinkAddon::sendOutputMask(uint64_t mask) {
    uint8_t payload[16];
    size_t len = gplink_pack_gpio_mask(/*devid=*/0, mask, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_GPIO_WRITE, payload, len);
}

void GPLinkAddon::applyGpioMask(uint64_t mask) {
    const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
    uint32_t count = options.gplinkPins_count;
    if (count > GPLINK_PIN_COUNT) count = GPLINK_PIN_COUNT;
    Gamepad *gamepad = Storage::getInstance().GetGamepad();
    for (uint8_t i = 0; i < count; i++) {
        // Mask bit set = pressed: OR the action. Bit clear = released: clear
        // it back out (a set-only mask would stick buttons on forever).
        bool pressed = (mask & (1ULL << i)) != 0;
        switch (options.gplinkPins[i].action) {
            case GpioAction::BUTTON_PRESS_UP:
                if (pressed) { gamepad->state.dpad |= GAMEPAD_MASK_UP; gamepad->state.dpadOriginal |= GAMEPAD_MASK_UP; }
                else { gamepad->state.dpad &= ~GAMEPAD_MASK_UP; gamepad->state.dpadOriginal &= ~GAMEPAD_MASK_UP; }
                break;
            case GpioAction::BUTTON_PRESS_DOWN:
                if (pressed) { gamepad->state.dpad |= GAMEPAD_MASK_DOWN; gamepad->state.dpadOriginal |= GAMEPAD_MASK_DOWN; }
                else { gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN; gamepad->state.dpadOriginal &= ~GAMEPAD_MASK_DOWN; }
                break;
            case GpioAction::BUTTON_PRESS_LEFT:
                if (pressed) { gamepad->state.dpad |= GAMEPAD_MASK_LEFT; gamepad->state.dpadOriginal |= GAMEPAD_MASK_LEFT; }
                else { gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT; gamepad->state.dpadOriginal &= ~GAMEPAD_MASK_LEFT; }
                break;
            case GpioAction::BUTTON_PRESS_RIGHT:
                if (pressed) { gamepad->state.dpad |= GAMEPAD_MASK_RIGHT; gamepad->state.dpadOriginal |= GAMEPAD_MASK_RIGHT; }
                else { gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT; gamepad->state.dpadOriginal &= ~GAMEPAD_MASK_RIGHT; }
                break;
            case GpioAction::BUTTON_PRESS_B1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_B1  : gamepad->state.buttons &= ~GAMEPAD_MASK_B1; break;
            case GpioAction::BUTTON_PRESS_B2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_B2  : gamepad->state.buttons &= ~GAMEPAD_MASK_B2; break;
            case GpioAction::BUTTON_PRESS_B3:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_B3  : gamepad->state.buttons &= ~GAMEPAD_MASK_B3; break;
            case GpioAction::BUTTON_PRESS_B4:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_B4  : gamepad->state.buttons &= ~GAMEPAD_MASK_B4; break;
            case GpioAction::BUTTON_PRESS_L1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_L1  : gamepad->state.buttons &= ~GAMEPAD_MASK_L1; break;
            case GpioAction::BUTTON_PRESS_R1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_R1  : gamepad->state.buttons &= ~GAMEPAD_MASK_R1; break;
            case GpioAction::BUTTON_PRESS_L2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_L2  : gamepad->state.buttons &= ~GAMEPAD_MASK_L2; break;
            case GpioAction::BUTTON_PRESS_R2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_R2  : gamepad->state.buttons &= ~GAMEPAD_MASK_R2; break;
            case GpioAction::BUTTON_PRESS_S1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_S1  : gamepad->state.buttons &= ~GAMEPAD_MASK_S1; break;
            case GpioAction::BUTTON_PRESS_S2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_S2  : gamepad->state.buttons &= ~GAMEPAD_MASK_S2; break;
            case GpioAction::BUTTON_PRESS_L3:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_L3  : gamepad->state.buttons &= ~GAMEPAD_MASK_L3; break;
            case GpioAction::BUTTON_PRESS_R3:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_R3  : gamepad->state.buttons &= ~GAMEPAD_MASK_R3; break;
            case GpioAction::BUTTON_PRESS_A1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_A1  : gamepad->state.buttons &= ~GAMEPAD_MASK_A1; break;
            case GpioAction::BUTTON_PRESS_A2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_A2  : gamepad->state.buttons &= ~GAMEPAD_MASK_A2; break;
            case GpioAction::BUTTON_PRESS_A3:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_A3  : gamepad->state.buttons &= ~GAMEPAD_MASK_A3; break;
            case GpioAction::BUTTON_PRESS_A4:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_A4  : gamepad->state.buttons &= ~GAMEPAD_MASK_A4; break;
            case GpioAction::BUTTON_PRESS_E1:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E1  : gamepad->state.buttons &= ~GAMEPAD_MASK_E1; break;
            case GpioAction::BUTTON_PRESS_E2:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E2  : gamepad->state.buttons &= ~GAMEPAD_MASK_E2; break;
            case GpioAction::BUTTON_PRESS_E3:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E3  : gamepad->state.buttons &= ~GAMEPAD_MASK_E3; break;
            case GpioAction::BUTTON_PRESS_E4:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E4  : gamepad->state.buttons &= ~GAMEPAD_MASK_E4; break;
            case GpioAction::BUTTON_PRESS_E5:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E5  : gamepad->state.buttons &= ~GAMEPAD_MASK_E5; break;
            case GpioAction::BUTTON_PRESS_E6:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E6  : gamepad->state.buttons &= ~GAMEPAD_MASK_E6; break;
            case GpioAction::BUTTON_PRESS_E7:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E7  : gamepad->state.buttons &= ~GAMEPAD_MASK_E7; break;
            case GpioAction::BUTTON_PRESS_E8:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E8  : gamepad->state.buttons &= ~GAMEPAD_MASK_E8; break;
            case GpioAction::BUTTON_PRESS_E9:    pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E9  : gamepad->state.buttons &= ~GAMEPAD_MASK_E9; break;
            case GpioAction::BUTTON_PRESS_E10:   pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E10 : gamepad->state.buttons &= ~GAMEPAD_MASK_E10; break;
            case GpioAction::BUTTON_PRESS_E11:   pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E11 : gamepad->state.buttons &= ~GAMEPAD_MASK_E11; break;
            case GpioAction::BUTTON_PRESS_E12:   pressed ? gamepad->state.buttons |= GAMEPAD_MASK_E12 : gamepad->state.buttons &= ~GAMEPAD_MASK_E12; break;
            case GpioAction::BUTTON_PRESS_FN:    pressed ? gamepad->state.aux |= AUX_MASK_FUNCTION    : gamepad->state.aux &= ~AUX_MASK_FUNCTION; break;
            case GpioAction::ANALOG_DIRECTION_LS_X_NEG: gamepad->state.lx = pressed ? GAMEPAD_JOYSTICK_MIN : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_LS_X_POS: gamepad->state.lx = pressed ? GAMEPAD_JOYSTICK_MAX : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: gamepad->state.ly = pressed ? GAMEPAD_JOYSTICK_MIN : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_LS_Y_POS: gamepad->state.ly = pressed ? GAMEPAD_JOYSTICK_MAX : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_RS_X_NEG: gamepad->state.rx = pressed ? GAMEPAD_JOYSTICK_MIN : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_RS_X_POS: gamepad->state.rx = pressed ? GAMEPAD_JOYSTICK_MAX : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: gamepad->state.ry = pressed ? GAMEPAD_JOYSTICK_MIN : GAMEPAD_JOYSTICK_MID; break;
            case GpioAction::ANALOG_DIRECTION_RS_Y_POS: gamepad->state.ry = pressed ? GAMEPAD_JOYSTICK_MAX : GAMEPAD_JOYSTICK_MID; break;
            default: break;
        }
    }
}

void GPLinkAddon::reinit() {
    if (started) {
        uart_deinit(uartInst == 0 ? uart0 : uart1);
        started = false;
    }
    setup();
}

bool GPLinkAddon::sendFrame(uint8_t type, const uint8_t *payload, size_t payloadLen) {
    if (payloadLen > GPLINK_MAX_PAYLOAD) return false;
    uint8_t wire[GPLINK_ENCODED_MAX + 8];
    size_t n = gplink_encode_seq(type, payload, (uint8_t)payloadLen, txSeq, wire);
    if (n == 0) return false;
    size_t written = gplink_uart_write(wire, n);
    if (written != n) return false; // FIFO filled mid-frame: drop, decoder resyncs
    txSeq++;
    gplink_link_on_tx(&link, getMillis());
    return true;
}

void GPLinkAddon::sendHello() {
    uint8_t payload[8];
    size_t len = gplink_pack_hello(GPLINK_VERSION_MAJOR, GPLINK_VERSION_MINOR,
                                   /*caps=*/0, /*devcount=*/1, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_HELLO, payload, len);
}

void GPLinkAddon::sendInputState(const GamepadState &state) {
    uint8_t payload[32];
    size_t len = gplink_pack_input_state(/*devid=*/0, state.buttons, state.dpad,
                                         state.lx, state.ly, state.rx, state.ry,
                                         state.lt, state.rt,
                                         (uint8_t)(state.aux & 0xFF), payload);
    if (len > 0) sendFrame(GPLINK_TYPE_INPUT_STATE, payload, len);
}

void GPLinkAddon::sendHeartbeat() {
    sendFrame(GPLINK_TYPE_HEARTBEAT, nullptr, 0);
}

void GPLinkAddon::pumpRx(uint32_t now) {
    int byte;
    while ((byte = gplink_uart_read()) >= 0) {
        rxBytes++;
        gplink_frame frame;
        if (!gplink_feed(&decoder, (uint8_t)byte, &frame)) continue;
        // v1 scope: track liveness and sequence health; parse-but-ignore commands
        // (rumble/LED actuation arrives with companion hardware).
        if (!gplink_link_seq_in_order(&link, frame.seq)) seqGaps++;
        gplink_link_on_rx(&link, frame.seq, now);
        switch (frame.type) {
            case GPLINK_TYPE_GPIO_READ: {
                uint8_t devid;
                uint64_t mask;
                if (frame.len >= 9 && gplink_unpack_gpio_mask(&frame, &devid, &mask) && devid == 0) {
                    lastMask = mask;
                    applyGpioMask(mask);
                    handledFrames++;
                } else {
                    ignoredFrames++;
                }
                break;
            }
            case GPLINK_TYPE_GPIO_WRITE:
                // Companion output reports (echo/ack path): parsed, no local action in v1.
                ignoredFrames++;
                break;
            case GPLINK_TYPE_ANALOG_READ: {
                uint8_t devid, count;
                uint8_t pins[16];
                uint16_t values[16];
                if (gplink_unpack_analog_read(&frame, &devid, &count, pins, values, 16) && devid == 0) {
                    for (uint8_t i = 0; i < count; i++) applyAnalogPin(pins[i], values[i]);
                    handledFrames++;
                } else {
                    ignoredFrames++;
                }
                break;
            }
            case GPLINK_TYPE_ANALOG_CONFIG:
                // Companion echo of our channel config (or future requests).
                ignoredFrames++;
                break;
            case GPLINK_TYPE_HELLO:
                // A HELLO means the peer (re)booted and lost RAM state:
                // re-push our pin config so its sampler comes alive in
                // milliseconds. Deliberately NO hello reply here: the peer
                // answers hellos itself, and replying would ping-pong
                // forever, flooding the link with handshake chatter.
                sendGpioConfigs();
                sendAnalogConfigs();
                // A fresh boot (or radio change, which the companion also
                // announces via HELLO) is the moment to (re)query identity:
                // version + radio byte for the status readout.
                {
                    uint8_t payload[4];
                    size_t len = gplink_pack_feature_req(GPLINK_FEATURE_IDENTITY, payload);
                    if (len > 0) sendFrame(GPLINK_TYPE_FEATURE_REQ, payload, len);
                }
                ignoredFrames++;
                break;
            case GPLINK_TYPE_FEATURE_ACK: {
                // Companion identity (version string + radio byte). Session
                // state, re-queried on every HELLO; unknown features ignored.
                char ver[GPLINK_TEST_VER_MAX + 1] = {0};
                uint8_t feature = 0, verLen = 0, radio = 0;
                if (gplink_unpack_feature_ack_identity(&frame, &feature, ver, &verLen,
                                                       GPLINK_TEST_VER_MAX, &radio)) {
                    strncpy(compFw, ver, sizeof(compFw) - 1);
                    compFw[sizeof(compFw) - 1] = '\0';
                    compRadio = radio;
                    handledFrames++;
                } else {
                    ignoredFrames++;
                }
                break;
            }
            case GPLINK_TYPE_HEARTBEAT:
            case GPLINK_TYPE_RUMBLE_SET:
            case GPLINK_TYPE_PLAYER_LED_SET:
            case GPLINK_TYPE_BATTERY_REPORT:
            case GPLINK_TYPE_PIN_CAPS_REQ:
            case GPLINK_TYPE_HTTP_REQ:
            case GPLINK_TYPE_HTTP_FRAG:
                ignoredFrames++;
                break;
            default:
                // Unknown minor-version additions: ignorable by design (spec 4).
                ignoredFrames++;
                break;
        }
    }
}

static bool gplinkStateChanged(const GamepadState &a, const GamepadState &b) {
    return a.dpad != b.dpad || a.buttons != b.buttons || a.aux != b.aux ||
           a.lx != b.lx || a.ly != b.ly || a.rx != b.rx || a.ry != b.ry ||
           a.lt != b.lt || a.rt != b.rt;
}

void GPLinkAddon::pollConfigMode() {
    if (!started) return;
    pumpRx(getMillis());
    // Run the shaping pipeline too so the shaped-value endpoint mirrors
    // gamepad mode exactly (same code path, not a reimplementation).
    // Writes gamepad state the config driver ignores; harmless here.
    applyAnalogAxes();
}

// Input application lives here (pre-MPGS) so companion dpad gets SOCD /
// invert / 4-way / dpad-mode treatment like USB and keyboard pads, and
// companion buttons participate in turbo and macro detection downstream.
// Output mirror, INPUT_STATE TX and actuation stay in process(), where the
// final post-pipeline state exists (we load last).
void GPLinkAddon::preprocess() {
    if (!started) return;
    pumpRx(getMillis());
    // Sticky re-apply every poll: gamepad->read() rebuilds state from
    // physical pins each iteration (same rationale as before; only moved).
    applyGpioMask(lastMask);
    applyAnalogAxes();
}

void GPLinkAddon::process() {
    if (!started) return;
    processCalls++;
    uint32_t now = getMillis();

    bool alive = gplink_link_alive(&link, now);
    if ((alive && !wasAlive) || (now - lastConfigMs) >= GPLINK_CONFIG_REFRESH_MS) {
        lastConfigMs = now;
        sendGpioConfigs();
        sendAnalogConfigs();
    }
    wasAlive = alive;

    // Final state: we load last, so GetGamepad already reflects every addon.
    // (Injected companion inputs above land in the same object.)
    Gamepad *gamepad = Storage::getInstance().GetGamepad();
    // Re-assert analog axes here as well as in preprocess(): MPGS dpad-mode
    // conversion (LS/RS) overwrites sticks after preprocess, so without this
    // companion analog sticks would die in those modes. Idempotent pure
    // function of stored channel values; the dpad half stays preprocess-only
    // (re-asserting it here would undo SOCD cleaning).
    applyAnalogAxes();
    const GamepadState &state = gamepad->state;
    // Mirror mapped output pins to the companion (change-driven + 5 s
    // backstop so a lost frame can't stick an LED).
    {
        const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
        uint32_t count = options.gplinkPins_count;
        if (count > GPLINK_PIN_COUNT) count = GPLINK_PIN_COUNT;
        uint64_t outMask = 0;
        for (uint8_t i = 0; i < count; i++) {
            const GpioMappingInfo &pin = options.gplinkPins[i];
            if (pin.direction != GpioDirection::GPIO_DIRECTION_OUTPUT) continue;
            if (gplinkActionLevel(pin.action, state)) outMask |= (1ULL << i);
        }
        if (outMask != lastOutputMask || (now - lastOutputMs) >= 5000) {
            lastOutputMask = outMask;
            lastOutputMs = now;
            sendOutputMask(outMask);
        }
    }
    if (!haveLastSent || gplinkStateChanged(state, lastSent)) {
        if (!haveLastSent || (now - link.last_tx_ms) >= GPLINK_MIN_REPORT_INTERVAL_MS) {
            sendInputState(state);
            lastSent = state;
            haveLastSent = true;
        }
    } else if (gplink_link_heartbeat_due(&link, now)) {
        sendHeartbeat();
    }

    // Console-driven actuation for the companion (dedicated messages, never
    // the pin table): player LED mask from player ID, rumble intensities
    // from haptic actuators. Change-driven + 5 s backstop like outputs.
    // NOTE: auxState lives on the PROCESSED pad (drivers write it during
    // inputDriver->process, after addons run — same object DRV8833 reads);
    // buttons/sticks above intentionally stay on the raw pad (final state).
    {
        Gamepad *processed = Storage::getInstance().GetProcessedGamepad();
        uint8_t ledMask = (uint8_t)(processed->auxState.playerID.ledValue & 0xFF);
        const auto &hap = processed->auxState.haptics;
        // Intensities are already 0-255 (see motorToDuty); active gates them.
        uint8_t weak = (hap.leftActuator.active && hap.leftActuator.enabled)
                           ? (uint8_t)(hap.leftActuator.intensity > 255 ? 255 : hap.leftActuator.intensity)
                           : 0;
        uint8_t strong = (hap.rightActuator.active && hap.rightActuator.enabled)
                             ? (uint8_t)(hap.rightActuator.intensity > 255 ? 255 : hap.rightActuator.intensity)
                             : 0;
        if (ledMask != lastLedMask || weak != lastWeak || strong != lastStrong ||
                (now - lastActMs) >= 5000) {
            lastLedMask = ledMask;
            lastWeak = weak;
            lastStrong = strong;
            lastActMs = now;
            uint8_t payload[16];
            size_t n = gplink_pack_player_led(/*devid=*/0, ledMask, payload);
            if (n > 0) sendFrame(GPLINK_TYPE_PLAYER_LED_SET, payload, n);
            n = gplink_pack_rumble(/*devid=*/0, weak, strong, /*duration_ms=*/0, payload);
            if (n > 0) sendFrame(GPLINK_TYPE_RUMBLE_SET, payload, n);
        }
    }

    if ((now - lastDebugMs) >= GPLINK_DEBUG_INTERVAL_MS) {
        lastDebugMs = now;
        char text[96];
        int n = snprintf(text, sizeof(text), "rp2040 proc=%lu tx=%u rx=%lu h=%lu g=%lu",
                         (unsigned long)processCalls, txSeq,
                         (unsigned long)rxBytes,
                         (unsigned long)handledFrames, (unsigned long)seqGaps);
        if (n > 0) sendFrame(GPLINK_TYPE_DEBUG_TEXT, (const uint8_t *)text, (size_t)n);
    }
}
