#include "addons/gplink.h"

#include "gamepad.h"
#include "storagemanager.h"
#include "helper.h"
#include "config.pb.h"
#include "gplink.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include <cstdio>

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
        lastMask(0) {
    s_instance = this;
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
}

bool GPLinkAddon::requestCaps() {
    if (!started) return false;
    uint8_t payload[8];
    size_t len = gplink_pack_pin_caps_req(payload);
    // Empty REQ payload packs to 0 length; a zero-length frame is still valid.
    return sendFrame(GPLINK_TYPE_PIN_CAPS_REQ, payload, len);
}

bool GPLinkAddon::available() {
    const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
    if (!options.enabled) return false;
    return gplink_uart_pins_valid((uint8_t)options.uartInstance,
                                  (uint8_t)options.txPin, (uint8_t)options.rxPin);
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
    txSeq = 0;
    haveLastSent = false;
    ignoredFrames = 0;
    handledFrames = 0;
    seqGaps = 0;
    lastMask = 0;
    started = true;
    sendHello();
    sendGpioConfigs();
}

// Tell the companion which of its pins we use as button inputs (dir 0=input,
// pull 1=up). Mask bits read back as 1 mean pressed (active-high semantics).
void GPLinkAddon::sendGpioConfigs() {
    const GPLinkOptions& options = Storage::getInstance().getAddonOptions().gplinkOptions;
    uint32_t count = options.gplinkPins_count;
    if (count > GPLINK_PIN_COUNT) count = GPLINK_PIN_COUNT;
    for (uint8_t i = 0; i < count; i++) {
        const GpioMappingInfo &pin = options.gplinkPins[i];
        if (pin.action == GpioAction::NONE || pin.action == GpioAction::RESERVED ||
                pin.action == GpioAction::ASSIGNED_TO_ADDON) continue;
        if (pin.direction != GpioDirection::GPIO_DIRECTION_INPUT) continue; // outputs: later phase
        uint8_t payload[8];
        size_t len = gplink_pack_gpio_config(/*devid=*/0, i, /*dir=*/0, /*pull=*/1, payload);
        if (len > 0) sendFrame(GPLINK_TYPE_GPIO_CONFIG, payload, len);
    }
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
            case GpioAction::BUTTON_PRESS_UP:    pressed ? gamepad->state.dpad |= GAMEPAD_MASK_UP     : gamepad->state.dpad &= ~GAMEPAD_MASK_UP; break;
            case GpioAction::BUTTON_PRESS_DOWN:  pressed ? gamepad->state.dpad |= GAMEPAD_MASK_DOWN   : gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN; break;
            case GpioAction::BUTTON_PRESS_LEFT:  pressed ? gamepad->state.dpad |= GAMEPAD_MASK_LEFT   : gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT; break;
            case GpioAction::BUTTON_PRESS_RIGHT: pressed ? gamepad->state.dpad |= GAMEPAD_MASK_RIGHT  : gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT; break;
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
            case GPLINK_TYPE_HELLO:
                // A HELLO means the peer (re)booted and lost RAM state:
                // re-push our pin config so its sampler comes alive in
                // milliseconds. Deliberately NO hello reply here: the peer
                // answers hellos itself, and replying would ping-pong
                // forever, flooding the link with handshake chatter.
                sendGpioConfigs();
                ignoredFrames++;
                break;
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

void GPLinkAddon::process() {
    if (!started) return;
    processCalls++;
    uint32_t now = getMillis();
    pumpRx(now);

    bool alive = gplink_link_alive(&link, now);
    if ((alive && !wasAlive) || (now - lastConfigMs) >= GPLINK_CONFIG_REFRESH_MS) {
        lastConfigMs = now;
        sendGpioConfigs();
    }
    wasAlive = alive;

    // Final state: we load last, so GetGamepad already reflects every addon.
    // (Injected companion inputs above land in the same object.)
    Gamepad *gamepad = Storage::getInstance().GetGamepad();
    // Re-assert the sticky companion mask every poll: the core pipeline
    // rebuilds button state from physical pins each iteration, which would
    // otherwise wipe injected inputs ~1 poll after they arrive (PCF8575
    // survives this by re-reading its expander every call; we re-apply).
    applyGpioMask(lastMask);
    const GamepadState &state = gamepad->state;
    if (!haveLastSent || gplinkStateChanged(state, lastSent)) {
        if (!haveLastSent || (now - link.last_tx_ms) >= GPLINK_MIN_REPORT_INTERVAL_MS) {
            sendInputState(state);
            lastSent = state;
            haveLastSent = true;
        }
    } else if (gplink_link_heartbeat_due(&link, now)) {
        sendHeartbeat();
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
