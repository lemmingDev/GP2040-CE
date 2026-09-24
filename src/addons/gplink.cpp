#include "addons/gplink.h"

#include "gamepad.h"
#include "storagemanager.h"
#include "helper.h"
#include "config.pb.h"
#include "gplink.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

// Report at most every 2 ms (500 Hz, spec section 2); heartbeats cover the idle.
#define GPLINK_MIN_REPORT_INTERVAL_MS 2

static GPLinkAddon *s_instance = nullptr;

GPLinkAddon *GPLink_GetAddon() {
    return s_instance;
}

GPLinkAddon::GPLinkAddon() : started(false), uartInst(GPLINK_UART_INSTANCE),
        txPin(GPLINK_TX_PIN), rxPin(GPLINK_RX_PIN), loopTest(0),
        processCalls(0), rxBytes(0),
        txSeq(0), haveLastSent(false), ignoredFrames(0), seqGaps(0) {
    s_instance = this;
}

void GPLinkAddon::getStatus(GPLinkStatus &out) {
    uint32_t now = getMillis();
    out.started = started;
    out.linkAlive = started && gplink_link_alive(&link, now);
    out.seqGaps = seqGaps;
    out.ignoredFrames = ignoredFrames;
    out.txSeq = txSeq;
    uart_inst_t *inst = (uartInst == 0) ? uart0 : uart1;
    out.txFuncOk = started && (gpio_get_function(txPin) == GPIO_FUNC_UART);
    out.rxFuncOk = started && (gpio_get_function(rxPin) == GPIO_FUNC_UART);
    out.uartFr = started ? uart_get_hw(inst)->fr : 0;
    out.loopTest = loopTest;
    out.processCalls = processCalls;
    out.rxBytes = rxBytes;
    out.uptimeS = now / 1000;
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
    loopTest = 0;
    uartInst = (uint8_t)options.uartInstance;
    txPin = (uint8_t)options.txPin;
    rxPin = (uint8_t)options.rxPin;
    if (!gplink_uart_init(uartInst, txPin, rxPin, options.baudRate)) {
        return;
    }
    // Continuity check before first HELLO: proves the jumper electrically,
    // independent of the UART mux table. Re-run by rebooting.
    loopTest = gplink_uart_loopback_test() ? 1 : 2;
    uint32_t now = getMillis();
    gplink_decoder_init(&decoder);
    gplink_link_init(&link, now);
    txSeq = 0;
    haveLastSent = false;
    ignoredFrames = 0;
    seqGaps = 0;
    started = true;
    sendHello();
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
            case GPLINK_TYPE_HELLO:
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

    Gamepad *gamepad = Storage::getInstance().GetProcessedGamepad();
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
}
