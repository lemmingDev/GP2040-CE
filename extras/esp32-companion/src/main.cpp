// extras/esp32-companion/src/main.cpp
// GP-Link ESP32 companion, M1: UART link + HELLO/PIN_CAPS + GPIO expander.
// Framework: Arduino (ESP32). Link UART defaults to Serial2 16/17 @ 2 Mbaud;
// console stays on Serial (USB). See platformio.ini / README.
#include <Arduino.h>
#include <Preferences.h>
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

#ifndef COMPANION_PLAYER_LED_PIN
#define COMPANION_PLAYER_LED_PIN 2 // devkit blue LED; R32 overrides per board
#endif

// GPIO sample/push cadence mirrors the main-board report floor (500 Hz).
#define COMPANION_SAMPLE_MS 2
#define COMPANION_HEARTBEAT_MS GPLINK_HEARTBEAT_INTERVAL_MS

static gplink_decoder s_dec;
static gplink_link s_link;
static uint8_t s_txSeq = 0;
// Passive frame tap: every byte crossing Serial2 in either direction is fed
// through these decoders purely to log complete frames to the USB console.
// Short headers keep console load far below 115200 even at 500 Hz bursts.
static gplink_decoder s_tapRx;
static gplink_decoder s_tapTx;

static void tapFeed(gplink_decoder *tap, uint8_t byte, bool outgoing) {
    gplink_frame frame;
    if (!gplink_feed(tap, byte, &frame)) return;
    Serial.printf("%lu GPLink %s %02X s=%u len=%u\n", (unsigned long)millis(),
                  outgoing ? "TX" : "RX", frame.type, frame.seq, frame.len);
}
static uint32_t s_lastTxMs = 0;
static uint32_t s_lastSampleMs = 0;
static uint64_t s_lastMask = 0;
static bool s_linkWasAlive = false;
// Synthetic input self-test (console 't' toggles): alternates a press on
// the first configured input pin every second, ORed with real inputs.
// Proves the ESP32->main-board path with no wiring involved. Default off.
static bool s_selftest = false;
static uint32_t s_selftestMs = 0;
static bool s_selftestOn = false;
// Configured direction per companion GPIO number (0xff = unconfigured).
static uint8_t s_dir[64];
static uint8_t s_pull[64];
static uint8_t s_invert[64];
// Analog-enabled channels (ANALOG_CONFIG), persisted like pin config.
static uint8_t s_analog[64];
static uint16_t s_analogLast[64];
// EMA smoothing state per channel (0xFFFF = unseeded). ESP32 ADC noise
// (±tens of LSB) walks straight through a deadband alone; the HE-trigger
// addon smooths the same way.
static uint16_t s_analogSm[64];
static uint32_t s_analogMs = 0;
static Preferences s_prefs;

// Pin config is user configuration: persist to NVS so serial-monitor opens
// (which reset the chip) and brownouts don't silently disable inputs.
// Writes only happen on actual change (CONFIG bursts repeat identically).
static void persistPinConfig() {
    s_prefs.begin("gplink", false);
    s_prefs.putBytes("dir", s_dir, sizeof(s_dir));
    s_prefs.putBytes("pull", s_pull, sizeof(s_pull));
    s_prefs.putBytes("inv", s_invert, sizeof(s_invert));
    s_prefs.putBytes("ana", s_analog, sizeof(s_analog));
    s_prefs.end();
}

static void applyPinMode(uint8_t pin) {
    if (s_dir[pin] == 1) pinMode(pin, OUTPUT);
    else if (s_pull[pin] == GPLINK_GPIO_PULL_DOWN) pinMode(pin, INPUT_PULLDOWN);
    else if (s_pull[pin] == GPLINK_GPIO_PULL_UP) pinMode(pin, INPUT_PULLUP);
    else pinMode(pin, INPUT);
}

static void restorePinConfig() {
    memset(s_dir, 0xff, sizeof(s_dir));
    memset(s_pull, 0, sizeof(s_pull));
    memset(s_invert, 0, sizeof(s_invert));
    memset(s_analog, 0, sizeof(s_analog));
    memset(s_analogSm, 0xff, sizeof(s_analogSm));
    s_prefs.begin("gplink", true);
    size_t n = s_prefs.getBytes("dir", s_dir, sizeof(s_dir));
    size_t m = s_prefs.getBytes("pull", s_pull, sizeof(s_pull));
    size_t k = s_prefs.getBytes("inv", s_invert, sizeof(s_invert));
    size_t a = s_prefs.getBytes("ana", s_analog, sizeof(s_analog));
    s_prefs.end();
    if (n != sizeof(s_dir) || m != sizeof(s_pull) || k != sizeof(s_invert) ||
            a != sizeof(s_analog)) {
        memset(s_dir, 0xff, sizeof(s_dir));
        memset(s_pull, 0, sizeof(s_pull));
        memset(s_invert, 0, sizeof(s_invert));
        memset(s_analog, 0, sizeof(s_analog));
        return;
    }
    for (uint8_t pin = 0; pin < 64; pin++) {
        if (s_dir[pin] == 0xff) continue;
        if (!companionPinLookup(pin)) {
            s_dir[pin] = 0xff;
            continue;
        }
        applyPinMode(pin);
    }
    Serial.println("GPLink: restored pin config from NVS");
}

static bool sendFrame(uint8_t type, const uint8_t *payload, size_t len) {
    if (len > GPLINK_MAX_PAYLOAD) return false;
    uint8_t wire[GPLINK_ENCODED_MAX + 8];
    size_t n = gplink_encode_seq(type, payload, (uint8_t)len, s_txSeq, wire);
    if (n == 0) return false;
    // No availableForWrite pre-check: it proved unreliable as a gate (small
    // frames passed while larger ones never left). write() paces itself on
    // the 128 B HW FIFO; a short write is logged so send failures stay
    // visible instead of silent.
    size_t w = Serial2.write(wire, n);
    if (w != n) {
        Serial.printf("%lu GPLink: TX short type %02X %u/%u\n",
                      (unsigned long)millis(), type, (unsigned)w, (unsigned)n);
        return false;
    }
    for (size_t i = 0; i < n; i++) tapFeed(&s_tapTx, wire[i], true);
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
        // Never advertise the link UART itself as GPIO.
        if (table[i].gpio == GPLINK_UART_RX || table[i].gpio == GPLINK_UART_TX) continue;
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

static void handleGpioConfig(uint8_t devid, uint8_t pin, uint8_t dir, uint8_t pull, uint8_t flags) {
    const CompanionPin *entry = companionPinLookup(pin);
    if (!entry) {
        sendNak(devid, pin, 1); // not-a-pin
        return;
    }
    if (dir == 1 && !(entry->caps & GPLINK_PINCAP_OUTPUT)) {
        sendNak(devid, pin, 2); // output-on-input-only
        return;
    }
    if (dir == 0 && !(entry->caps & GPLINK_PINCAP_INPUT)) {
        sendNak(devid, pin, 4); // input-on-output-only
        return;
    }
    bool inverted = (flags & GPLINK_GPIO_FLAG_INVERTED) != 0;
    if (s_dir[pin] != dir || s_pull[pin] != pull || s_invert[pin] != (uint8_t)inverted) {
        s_dir[pin] = dir;
        s_pull[pin] = pull;
        s_invert[pin] = (uint8_t)inverted;
        applyPinMode(pin);
        persistPinConfig();
    }
    Serial.printf("%lu GPLink: GPIO_CONFIG dev %u pin %u %s pull %u\n",
                  (unsigned long)millis(), devid, pin, dir ? "out" : "in", pull);
}

static void handleAnalogConfig(uint8_t devid, uint8_t pin, uint8_t enable) {
    const CompanionPin *entry = companionPinLookup(pin);
    if (!entry) {
        sendNak(devid, pin, 1); // not-a-pin
        return;
    }
    if (enable && !(entry->caps & GPLINK_PINCAP_ADC)) {
        sendNak(devid, pin, 3); // not-adc-capable
        return;
    }
    uint8_t on = enable ? 1 : 0;
    if (s_analog[pin] != on) {
        s_analog[pin] = on;
        persistPinConfig();
    }
    Serial.printf("%lu GPLink: ANALOG_CONFIG dev %u pin %u %s\n",
                  (unsigned long)millis(), devid, pin, on ? "on" : "off");
}

// Normalized full-range u16 ADC read (companion scales its native width;
// ESP32 Arduino analogRead is 12-bit).
#define COMPANION_ADC_MAX 4095
#define COMPANION_ADC_DEADBAND 8

// DAC sweep test aid: triangular 0-255 on GPIO25 (~2.5 s period) for the
// ADC loopback check (jumper GPIO25 to an ADC pin mapped to a stick).
// Console 'd' toggles. Proves modulation end to end with no pots.
static bool s_dacSweep = false;
static uint32_t s_dacMs = 0;
static uint8_t s_dacVal = 0;
static int8_t s_dacDir = 1;

static void pumpDacSweep(uint32_t now) {
    if (!s_dacSweep || (now - s_dacMs) < 20) return;
    s_dacMs = now;
    dacWrite(25, s_dacVal);
    int v = (int)s_dacVal + 4 * s_dacDir;
    if (v >= 255) {
        v = 255;
        s_dacDir = -1;
    } else if (v <= 0) {
        v = 0;
        s_dacDir = 1;
    }
    s_dacVal = (uint8_t)v;
}

// Synthetic analog stimulus ('a' toggles): triangular 12-bit wave fed into
// configured channels INSTEAD of ADC reads. Bisects cleanly against the DAC
// loopback: smooth motion here + jitter there = source-side physics.
static bool s_analogSynth = false;
static uint32_t s_synthMs = 0;
static uint16_t s_synthRaw = 0;
static int8_t s_synthDir = 1;

static void pumpAnalog(uint32_t now) {
    static uint32_t lastPushMs = 0;
    // Synthetic triangle advances here so every configured channel shares
    // one phase (~5 s period, full 12-bit swing).
    if (s_analogSynth && (now - s_synthMs) >= 20) {
        s_synthMs = now;
        int v = (int)s_synthRaw + 32 * (int)s_synthDir;
        if (v >= (int)COMPANION_ADC_MAX) {
            v = COMPANION_ADC_MAX;
            s_synthDir = -1;
        } else if (v <= 0) {
            v = 0;
            s_synthDir = 1;
        }
        s_synthRaw = (uint16_t)v;
    }
    bool force = (now - lastPushMs) >= 1000;
    uint8_t pins[8];
    uint16_t values[8];
    uint8_t count = 0;
    for (uint8_t pin = 0; pin < 64 && count < 8; pin++) {
        if (!s_analog[pin]) continue;
        uint16_t raw;
        if (s_analogSynth) {
            raw = s_synthRaw;
        } else {
            // Oversampled read: 8 conversions averaged (~80 us) to kill white
            // noise spikes a single conversion catches whole. Then EMA (1/8)
            // plus deadband, same shaping family as the HE-trigger addon.
            uint32_t acc = 0;
            for (uint8_t k = 0; k < 8; k++) acc += analogRead(pin);
            raw = (uint16_t)(acc >> 3);
        }
        if (s_analogSm[pin] == 0xFFFF) {
            // Seed baseline silently (no glitch frame on enable).
            s_analogSm[pin] = raw;
            s_analogLast[pin] = raw;
            continue;
        }
        int step = (int)raw - (int)s_analogSm[pin];
        s_analogSm[pin] = (uint16_t)((int)s_analogSm[pin] + ((step + (step >= 0 ? 4 : -4)) >> 3));
        uint16_t sm = s_analogSm[pin];
        uint16_t last = s_analogLast[pin];
        uint16_t diff = (sm > last) ? (uint16_t)(sm - last) : (uint16_t)(last - sm);
        if (force || diff >= COMPANION_ADC_DEADBAND) {
            s_analogLast[pin] = sm;
            pins[count] = pin;
            values[count] = (uint16_t)(((uint32_t)sm * 65535 + COMPANION_ADC_MAX / 2) / COMPANION_ADC_MAX);
            count++;
        }
    }
    if (count == 0) return;
    if (force) lastPushMs = now;
    uint8_t payload[32];
    size_t len = gplink_pack_analog_read(0, count, pins, values, payload);
    if (len > 0) sendFrame(GPLINK_TYPE_ANALOG_READ, payload, len);
}

static void handleGpioWrite(uint8_t devid, uint64_t mask) {
    (void)devid; // single virtual device in M1; devid carried for future use
    for (uint8_t pin = 0; pin < 64; pin++) {
        if (s_dir[pin] != 1) continue;
        bool level = (mask & (1ULL << pin)) != 0;
        if (s_invert[pin]) level = !level;
        digitalWrite(pin, level ? HIGH : LOW);
    }
}

// Pressed semantics match the main board (bit = pressed): pull-up inputs
// invert (pressed = LOW), everything else reads level as-is; the per-pin
// invert flag flips the result for active-high wiring.
static uint64_t sampleConfiguredInputs() {
    uint64_t mask = 0;
    for (uint8_t pin = 0; pin < 64; pin++) {
        if (s_dir[pin] != 0) continue;
        int level = digitalRead(pin);
        bool pressed = (s_pull[pin] == GPLINK_GPIO_PULL_UP) ? (level == LOW) : (level == HIGH);
        if (s_invert[pin]) pressed = !pressed;
        if (pressed) mask |= (1ULL << pin);
    }
    return mask;
}

static void pumpLink() {
    while (Serial2.available() > 0) {
        int byte = Serial2.read();
        if (byte < 0) break;
        tapFeed(&s_tapRx, (uint8_t)byte, false);
        gplink_frame frame;
        if (!gplink_feed(&s_dec, (uint8_t)byte, &frame)) continue;
        uint32_t now = millis();
        if (!gplink_link_seq_in_order(&s_link, frame.seq)) {
            Serial.printf("%lu GPLink: seq gap (last %u got %u)\n",
                          (unsigned long)now, s_link.last_seq, frame.seq);
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
                uint8_t devid, pin, dir, pull, flags;
                if (gplink_unpack_gpio_config(&frame, &devid, &pin, &dir, &pull, &flags)) {
                    handleGpioConfig(devid, pin, dir, pull, flags);
                }
                break;
            }
            case GPLINK_TYPE_GPIO_WRITE: {
                uint8_t devid;
                uint64_t mask;
                if (gplink_unpack_gpio_mask(&frame, &devid, &mask)) handleGpioWrite(devid, mask);
                break;
            }
            case GPLINK_TYPE_ANALOG_CONFIG: {
                uint8_t devid, pin, enable;
                if (gplink_unpack_analog_config(&frame, &devid, &pin, &enable)) {
                    handleAnalogConfig(devid, pin, enable);
                }
                break;
            }
            case GPLINK_TYPE_PLAYER_LED_SET: {
                uint8_t devid, mask;
                if (gplink_unpack_player_led(&frame, &devid, &mask)) {
                    digitalWrite(COMPANION_PLAYER_LED_PIN, (mask & 0x01) ? HIGH : LOW);
                    Serial.printf("%lu GPLink: player LED %s\n",
                                  (unsigned long)now, (mask & 0x01) ? "on" : "off");
                }
                break;
            }
            case GPLINK_TYPE_RUMBLE_SET: {
                uint8_t devid, weak, strong;
                uint16_t duration;
                if (gplink_unpack_rumble(&frame, &devid, &weak, &strong, &duration)) {
                    // No motor pins profiled yet: log receipt (proves the
                    // path live) until motor hardware lands.
                    if (weak || strong) {
                        Serial.printf("%lu GPLink: rumble weak %u strong %u dur %u\n",
                                      (unsigned long)now, weak, strong, duration);
                    }
                }
                break;
            }
            case GPLINK_TYPE_DEBUG_TEXT: {
                char text[241];
                size_t n = frame.len;
                if (n > 240) n = 240;
                memcpy(text, frame.payload, n);
                text[n] = '\0';
                Serial.printf("%lu GPLink DBG: %s\n", (unsigned long)millis(), text);
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
    restorePinConfig();
    // Player LED owns its pin regardless of GPIO table use (devkit GPIO2
    // can't be an input anyway); applied after restore so it always wins.
    // s_dir is forced too, or a stale NVS input entry would keep sampling
    // the LED pin and emitting ghost GPIO_READs.
    pinMode(COMPANION_PLAYER_LED_PIN, OUTPUT);
    s_dir[COMPANION_PLAYER_LED_PIN] = 1;
    gplink_decoder_init(&s_dec);
    gplink_decoder_init(&s_tapRx);
    gplink_decoder_init(&s_tapTx);
    gplink_link_init(&s_link, millis());
    s_lastTxMs = millis();
    s_lastSampleMs = millis();
    Serial.printf("GPLink companion %s up (UART2 %d/%d @ %d) fw " __DATE__ " " __TIME__ "\n",
                  COMPANION_BOARD_NAME, GPLINK_UART_RX, GPLINK_UART_TX, GPLINK_BAUD);
    Serial.println("GPLink: console keys: t = input self-test, a = analog synth, d = DAC sweep, g = frame probe, l = LED");
    sendHello();
}

void loop() {
    uint32_t now = millis();
    if (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == 't' || c == 'T') {
            s_selftest = !s_selftest;
            Serial.printf("GPLink: input self-test %s\n", s_selftest ? "ON" : "OFF");
        } else if (c == 'l' || c == 'L') {
            // Player-LED hardware proof: drives the LED pin directly,
            // bypassing the protocol (which currently only ever delivers
            // mask 0 — the host never assigns a nonzero player LED here).
            static bool ledOn = false;
            ledOn = !ledOn;
            digitalWrite(COMPANION_PLAYER_LED_PIN, ledOn ? HIGH : LOW);
            Serial.printf("GPLink: player LED direct %s\n", ledOn ? "ON" : "OFF");
        } else if (c == 'd' || c == 'D') {
            s_dacSweep = !s_dacSweep;
            Serial.printf("GPLink: DAC sweep %s (GPIO25)\n", s_dacSweep ? "ON" : "OFF");
        } else if (c == 'a' || c == 'A') {
            s_analogSynth = !s_analogSynth;
            if (!s_analogSynth) memset(s_analogSm, 0xff, sizeof(s_analogSm));
            Serial.printf("GPLink: analog synth %s\n", s_analogSynth ? "ON" : "OFF");
        } else if (c == 'g' || c == 'G') {
            // Manual GPIO_READ probe: pack + encode + write a fixed mask
            // frame with every intermediate value printed. Bypasses the
            // sampler/mask-change logic to isolate the send path itself.
            uint8_t p[16];
            size_t L = gplink_pack_gpio_mask(0, 0x04, p);
            uint8_t wire[GPLINK_ENCODED_MAX + 8];
            size_t n = gplink_encode_seq(GPLINK_TYPE_GPIO_READ, p, 9, s_txSeq, wire);
            size_t w = (n == 0) ? 0 : Serial2.write(wire, n);
            Serial.printf("GPLink: manual mask probe len=%u n=%u w=%u\n",
                          (unsigned)L, (unsigned)n, (unsigned)w);
            if (n > 0 && w == n) {
                for (size_t i = 0; i < n; i++) tapFeed(&s_tapTx, wire[i], true);
                s_txSeq++;
                s_lastTxMs = millis();
            }
        }
    }
    pumpLink();
    pumpAnalog(now);
    pumpDacSweep(now);
    if (now - s_lastSampleMs >= COMPANION_SAMPLE_MS) {
        s_lastSampleMs = now;
        uint64_t mask = sampleConfiguredInputs();
        if (s_selftest && (now - s_selftestMs) >= 1000) {
            s_selftestMs = now;
            s_selftestOn = !s_selftestOn;
            uint8_t pin = 2;
            for (uint8_t p = 0; p < 64; p++) {
                if (s_dir[p] == 0) {
                    pin = p;
                    break;
                }
            }
            if (s_selftestOn) mask |= (1ULL << pin);
            Serial.printf("GPLink: self-test pin %u %s (sample=%llu last=%llu dir2=%u)\n",
                          pin, s_selftestOn ? "press" : "release",
                          (unsigned long long)sampleConfiguredInputs(),
                          (unsigned long long)s_lastMask, s_dir[2]);
        }
        if (mask != s_lastMask) {
            s_lastMask = mask;
            uint8_t payload[16];
            size_t len = gplink_pack_gpio_mask(0, mask, payload);
            if (len > 0) sendFrame(GPLINK_TYPE_GPIO_READ, payload, len);
        }
    }
    if (now - s_lastTxMs >= COMPANION_HEARTBEAT_MS) sendHeartbeat();
    bool alive = gplink_link_alive(&s_link, now);
    // Gate transitions on seen traffic: link_init stamps last_rx=now, which
    // would otherwise print a spurious UP at every boot. Transitions are
    // also rate-limited: a flapping link must not flood the console (which
    // would stall this loop on a full Serial buffer and make everything
    // worse); flaps between prints are counted into the status instead.
    static uint32_t lastLinkPrintMs = 0;
    static uint32_t linkFlaps = 0;
    if (alive != s_linkWasAlive && s_link.have_seq) {
        s_linkWasAlive = alive;
        if ((now - lastLinkPrintMs) >= 10000) {
            lastLinkPrintMs = now;
            Serial.printf("%lu GPLink: link %s", (unsigned long)now, alive ? "UP" : "DOWN");
            if (linkFlaps > 0) Serial.printf(" (%lu flaps suppressed)", (unsigned long)linkFlaps);
            Serial.printf("\n");
            linkFlaps = 0;
        } else {
            linkFlaps++;
        }
    }
}
