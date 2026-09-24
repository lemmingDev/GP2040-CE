// extras/esp32-companion/src/main.cpp
// GP-Link ESP32 companion, M1: UART link + HELLO/PIN_CAPS + GPIO expander.
// Framework: Arduino (ESP32). Link UART defaults to Serial2 16/17 @ 2 Mbaud;
// console stays on Serial (USB). See platformio.ini / README.
#include <Arduino.h>
#include "gplink.h"
#include "gplink_link.h"
#include "companion_pins.h"

#ifndef COMPANION_BOARD_NAME
#define COMPANION_BOARD_NAME "ESP32-DevKit"
#endif
#ifndef GPLINK_UART_RX
#define GPLINK_UART_RX 16
#endif
#ifndef GPLINK_UART_TX
#define GPLINK_UART_TX 17
#endif
#ifndef GPLINK_BAUD
#define GPLINK_BAUD 2000000
#endif

// GPIO sample/push cadence mirrors the main-board report floor (500 Hz).
#define COMPANION_SAMPLE_MS 2
#define COMPANION_HEARTBEAT_MS GPLINK_HEARTBEAT_INTERVAL_MS

static gplink_decoder s_dec;
static gplink_link s_link;
static uint8_t s_txSeq = 0;
static uint32_t s_lastTxMs = 0;
static uint32_t s_lastSampleMs = 0;
static uint64_t s_lastMask = 0;
static bool s_linkWasAlive = false;
// Configured direction per companion GPIO number (0xff = unconfigured).
static uint8_t s_dir[64];
static uint8_t s_pull[64];

static bool sendFrame(uint8_t type, const uint8_t *payload, size_t len) {
    if (len > GPLINK_MAX_PAYLOAD) return false;
    uint8_t wire[GPLINK_ENCODED_MAX + 8];
    size_t n = gplink_encode_seq(type, payload, (uint8_t)len, s_txSeq, wire);
    if (n == 0 || n > (size_t)Serial2.availableForWrite()) return false;
    size_t w = Serial2.write(wire, n);
    if (w != n) return false;
    s_txSeq++;
    s_lastTxMs = millis();
    return true;
}

static void sendHello() {
    uint8_t payload[8];
    size_t len = gplink_pack_hello(GPLINK_VERSION_MAJOR, GPLINK_VERSION_MINOR,
                                   GPLINK_CAP_COMPANION_GPIO, 0, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_HELLO, payload, len);
}

static void sendHeartbeat() {
    sendFrame(GPLINK_TYPE_HEARTBEAT, nullptr, 0);
}

static void sendPinCaps() {
    size_t count = 0;
    const CompanionPin *table = companionPins(&count);
    char name[40];
    snprintf(name, sizeof(name), "%s", COMPANION_BOARD_NAME);
    size_t nameLen = strlen(name);
    if (nameLen > 32) nameLen = 32;
    uint8_t pinArr[64], capsArr[64];
    uint8_t n = 0;
    for (size_t i = 0; i < count && n < 64; i++) {
        pinArr[n] = table[i].gpio;
        capsArr[n] = table[i].caps;
        n++;
    }
    uint8_t payload[128];
    size_t len = gplink_pack_pin_caps_rsp(name, (uint8_t)nameLen, n, pinArr, capsArr, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_PIN_CAPS_RSP, payload, len);
}

static void sendNak(uint8_t devid, uint8_t pin, uint8_t code) {
    uint8_t payload[8];
    size_t len = gplink_pack_gpio_nak(devid, pin, code, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_GPIO_NAK, payload, len);
    Serial.printf("GPLink: NAK dev %u pin %u code %u\n", devid, pin, code);
}

static void handleGpioConfig(uint8_t devid, uint8_t pin, uint8_t dir, uint8_t pull) {
    const CompanionPin *entry = companionPinLookup(pin);
    if (!entry) {
        sendNak(devid, pin, 1); // not-a-pin
        return;
    }
    if (dir == 1 && !(entry->caps & GPLINK_PINCAP_OUTPUT)) {
        sendNak(devid, pin, 2); // output-on-input-only
        return;
    }
    if (dir == 1) {
        pinMode(pin, OUTPUT);
    } else {
        if (pull == 2) pinMode(pin, INPUT_PULLDOWN);
        else if (pull == 1) pinMode(pin, INPUT_PULLUP);
        else pinMode(pin, INPUT);
    }
    s_dir[pin] = dir;
    s_pull[pin] = pull;
    Serial.printf("GPLink: GPIO_CONFIG dev %u pin %u %s pull %u\n", devid, pin,
                  dir ? "out" : "in", pull);
}

static void handleGpioWrite(uint8_t devid, uint64_t mask) {
    for (uint8_t pin = 0; pin < 64; pin++) {
        if (s_dir[pin] != 1) continue;
        digitalWrite(pin, (mask & (1ULL << pin)) ? HIGH : LOW);
    }
    (void)devid;
}

// Pressed semantics match the main board (bit = pressed): pull-up inputs
// invert (pressed = LOW), everything else reads level as-is.
static uint64_t sampleConfiguredInputs() {
    uint64_t mask = 0;
    for (uint8_t pin = 0; pin < 64; pin++) {
        if (s_dir[pin] != 0) continue;
        int level = digitalRead(pin);
        bool pressed = (s_pull[pin] == 1) ? (level == LOW) : (level == HIGH);
        if (pressed) mask |= (1ULL << pin);
    }
    return mask;
}

static void pumpLink() {
    while (Serial2.available() > 0) {
        int byte = Serial2.read();
        if (byte < 0) break;
        gplink_frame frame;
        if (!gplink_feed(&s_dec, (uint8_t)byte, &frame)) continue;
        uint32_t now = millis();
        if (!gplink_link_seq_in_order(&s_link, frame.seq)) {
            Serial.printf("GPLink: seq gap (last %u got %u)\n", s_link.last_seq, frame.seq);
        }
        gplink_link_on_rx(&s_link, frame.seq, now);
        switch (frame.type) {
            case GPLINK_TYPE_HELLO:
                sendHello();
                break;
            case GPLINK_TYPE_HEARTBEAT:
                sendHeartbeat();
                break;
            case GPLINK_TYPE_PIN_CAPS_REQ:
                sendPinCaps();
                break;
            case GPLINK_TYPE_GPIO_CONFIG: {
                uint8_t devid, pin, dir, pull;
                if (gplink_unpack_gpio_config(&frame, &devid, &pin, &dir, &pull)) {
                    handleGpioConfig(devid, pin, dir, pull);
                }
                break;
            }
            case GPLINK_TYPE_GPIO_WRITE: {
                uint8_t devid;
                uint64_t mask;
                if (gplink_unpack_gpio_mask(&frame, &devid, &mask)) handleGpioWrite(devid, mask);
                break;
            }
            default:
                // M1 scope: INPUT_STATE/RUMBLE/LED/HTTP arrive only with
                // future phases; unknown minor-version types are ignorable.
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.setRxBufferSize(1024);
    Serial2.begin(GPLINK_BAUD, SERIAL_8N1, GPLINK_UART_RX, GPLINK_UART_TX);
    memset(s_dir, 0xff, sizeof(s_dir));
    memset(s_pull, 0, sizeof(s_pull));
    gplink_decoder_init(&s_dec);
    gplink_link_init(&s_link, millis());
    s_lastTxMs = millis();
    s_lastSampleMs = millis();
    Serial.printf("GPLink companion %s up (UART2 %d/%d @ %d)\n", COMPANION_BOARD_NAME,
                  GPLINK_UART_RX, GPLINK_UART_TX, GPLINK_BAUD);
    sendHello();
}

void loop() {
    uint32_t now = millis();
    pumpLink();
    if (now - s_lastSampleMs >= COMPANION_SAMPLE_MS) {
        s_lastSampleMs = now;
        uint64_t mask = sampleConfiguredInputs();
        if (mask != s_lastMask) {
            s_lastMask = mask;
            uint8_t payload[16];
            size_t len = gplink_pack_gpio_mask(0, mask, payload);
            if (len > 0) sendFrame(GPLINK_TYPE_GPIO_READ, payload, len);
        }
    }
    if (now - s_lastTxMs >= COMPANION_HEARTBEAT_MS) sendHeartbeat();
    bool alive = gplink_link_alive(&s_link, now);
    if (alive != s_linkWasAlive) {
        s_linkWasAlive = alive;
        Serial.printf("GPLink: link %s\n", alive ? "UP" : "DOWN");
    }
}
