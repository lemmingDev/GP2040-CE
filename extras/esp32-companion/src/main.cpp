// extras/esp32-companion/src/main.cpp
// GP-Link ESP32 companion, M1: UART link + HELLO/PIN_CAPS + GPIO expander.
// Framework: Arduino (ESP32). Link UART defaults to Serial2 16/17 @ 2 Mbaud;
// console stays on Serial (USB). See platformio.ini / README.
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "gplink.h"
#include "gplink_link.h"
#include "companion_pins.h"

#ifndef COMPANION_BOARD_NAME
#define COMPANION_BOARD_NAME "ESP32-DevKit"
#endif
#ifndef COMPANION_FW_VERSION
#define COMPANION_FW_VERSION "1.0.0-dev"
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
// ESP32 Arduino analogRead is 12-bit). Kept above the test engine: sweep
// steps and RESULT normalization share the same full-scale.
#define COMPANION_ADC_MAX 4095
#define COMPANION_ADC_DEADBAND 8

// ---- GP-Link test engine (session-only pin exerciser) ----
// Wire contract (payloads packed by hand; the shared codec does not define
// these types yet): TEST_CONFIGURE 0x16 [testId pin fn p1LE p2LE],
// TEST_RESULT 0x17 [testId status valueLE countLE], FEATURE_REQ 0x0B
// [feature], FEATURE_ACK 0x0C [feature verLen ver radio]. RESULT value is
// normalized u16 for analog sweeps (same scale as ANALOG_READ) and 0/1 for
// digital families; PWM reports normalized duty (65535*duty/100) since a
// 0/1 level is meaningless for it.
// Simulate family (0x00-0x05) feeds the existing per-pin pipelines and never
// touches hardware. Drive family (0x0A-0x0D) owns the pin (s_dir=OUTPUT,
// session only). Tests never persist to NVS and never emit while down.
#ifndef GPLINK_TYPE_TEST_CONFIGURE
#define GPLINK_TYPE_TEST_CONFIGURE 0x16
#endif
#ifndef GPLINK_TYPE_TEST_RESULT
#define GPLINK_TYPE_TEST_RESULT 0x17
#endif
// FEATURE_REQ (0x0B) / FEATURE_ACK (0x0C) already live in gplink.h.
#define GPLINK_TEST_FN_HOLD_LOW 0x00
#define GPLINK_TEST_FN_HOLD_HIGH 0x01
#define GPLINK_TEST_FN_TOGGLE 0x02
#define GPLINK_TEST_FN_SWEEP_UP 0x03
#define GPLINK_TEST_FN_SWEEP_DOWN 0x04
#define GPLINK_TEST_FN_TRIANGLE 0x05
#define GPLINK_TEST_FN_DRIVE_LOW 0x0A
#define GPLINK_TEST_FN_DRIVE_HIGH 0x0B
#define GPLINK_TEST_FN_DRIVE_CYCLE 0x0C
#define GPLINK_TEST_FN_DRIVE_PWM 0x0D
#define GPLINK_TEST_FN_STOP 0xFF
#define GPLINK_TEST_STATUS_RUNNING 0
#define GPLINK_TEST_STATUS_DONE 1
#define GPLINK_TEST_STATUS_ABORTED 2
#define GPLINK_TEST_STATUS_BAD_PIN 3
#define GPLINK_TEST_STATUS_UNSUPPORTED 4
#define GPLINK_TEST_STATUS_BUSY 5
#define GPLINK_FEATURE_IDENTITY 0x01
#define GPLINK_TEST_VER_MAX 24
#define GPLINK_TEST_MAX_SLOTS 8
// Sweep step in native ADC units: max(1, ADC_MAX/256) == max(1, 4095/256).
#define GPLINK_TEST_SWEEP_STEP 15
// Bit-bang PWM ceiling for future non-LEDC ports; the ESP32 serves PWM via
// LEDC and has no practical cap (any nonzero u16 freq is accepted).
#define GPLINK_TEST_PWM_BITBANG_MAX_HZ 1000

struct GplinkTestSlot {
    bool active;
    uint8_t testId;
    uint8_t pin;
    uint8_t function;
    uint16_t param1;   // toggle: full-cycle period ms / sweep: step ms / pwm: Hz
    uint16_t param2;   // toggle/cycle: count (0=inf) / sweep: repeats (0=inf) / pwm: duty %
    bool level;        // current digital level (digital families)
    uint16_t value;    // current native-unit level (analog sweep family)
    int8_t dir;        // triangle direction (+1/-1)
    uint32_t nextMs;   // next step/toggle deadline (millis)
    uint16_t done;     // completed full cycles / sweep repeats
    bool pwmPhase;     // bit-bang fallback state (future ports only)
    uint32_t pwmNextUs;
    uint32_t pwmOnUs;
    uint32_t pwmOffUs;
};
static GplinkTestSlot s_tests[GPLINK_TEST_MAX_SLOTS];
// Last RESULT sent (any status), for the 'x' console dump.
static bool s_testHaveLast = false;
static uint8_t s_testLastId = 0, s_testLastStatus = 0;
static uint16_t s_testLastValue = 0, s_testLastCount = 0;
// M2 owns Bluetooth; bit1 of the radio byte stays 0 until then.
static bool s_btActive = false;
// ESP32 serves PWM via LEDC; the micros() bit-bang path stays compiled for
// future ports without LEDC (flip this to select it).
static const bool kTestPwmUseLedc = true;

static uint32_t testHalfPeriod(uint16_t fullMs) {
    uint32_t h = (uint32_t)fullMs / 2;
    return (h < 1) ? 1 : h;
}

static uint32_t testStepInterval(uint16_t stepMs) {
    return (stepMs < COMPANION_SAMPLE_MS) ? COMPANION_SAMPLE_MS : stepMs;
}

static uint16_t testNormNative(uint16_t native) {
    return (uint16_t)(((uint32_t)native * 65535 + COMPANION_ADC_MAX / 2) / COMPANION_ADC_MAX);
}

static GplinkTestSlot *testFindId(uint8_t testId) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (s_tests[i].active && s_tests[i].testId == testId) return &s_tests[i];
    }
    return nullptr;
}

static GplinkTestSlot *testFindPin(uint8_t pin) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (s_tests[i].active && s_tests[i].pin == pin) return &s_tests[i];
    }
    return nullptr;
}

static GplinkTestSlot *testFreeSlot() {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (!s_tests[i].active) return &s_tests[i];
    }
    return nullptr;
}

static bool testDriveActive(uint8_t pin) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (!s_tests[i].active || s_tests[i].pin != pin) continue;
        uint8_t fn = s_tests[i].function;
        if (fn >= GPLINK_TEST_FN_DRIVE_LOW && fn <= GPLINK_TEST_FN_DRIVE_PWM) return true;
    }
    return false;
}

// Current RESULT value for a slot (normalized analog / 0-1 digital /
// normalized duty for PWM).
static uint16_t testSlotValue(const GplinkTestSlot *s) {
    if (s->function >= GPLINK_TEST_FN_SWEEP_UP && s->function <= GPLINK_TEST_FN_TRIANGLE) {
        return testNormNative(s->value);
    }
    if (s->function == GPLINK_TEST_FN_DRIVE_PWM) {
        uint8_t d = (s->param2 > 100) ? 100 : (uint8_t)s->param2;
        return (uint16_t)((uint32_t)d * 65535 / 100);
    }
    return s->level ? 1 : 0;
}

static void sendTestResult(uint8_t testId, uint8_t status, uint16_t value, uint16_t count, uint32_t now) {
    if (!gplink_link_alive(&s_link, now)) return; // never queue while down
    uint8_t payload[6];
    payload[0] = testId;
    payload[1] = status;
    payload[2] = (uint8_t)(value & 0xFF);
    payload[3] = (uint8_t)((value >> 8) & 0xFF);
    payload[4] = (uint8_t)(count & 0xFF);
    payload[5] = (uint8_t)((count >> 8) & 0xFF);
    if (!sendFrame(GPLINK_TYPE_TEST_RESULT, payload, sizeof(payload))) return;
    s_testHaveLast = true;
    s_testLastId = testId;
    s_testLastStatus = status;
    s_testLastValue = value;
    s_testLastCount = count;
}

static void testPwmStart(GplinkTestSlot *s, uint8_t idx, uint32_t nowUs) {
    uint16_t freqHz = s->param1;
    uint8_t duty = (s->param2 > 100) ? 100 : (uint8_t)s->param2; // clamp, documented
    if (kTestPwmUseLedc) {
        uint32_t d = (uint32_t)duty * 255 / 100;
        ledcSetup(idx, freqHz, 8);
        ledcAttachPin(s->pin, idx);
        ledcWrite(idx, d);
        return;
    }
    if (duty == 0) {
        digitalWrite(s->pin, LOW);
    } else if (duty >= 100) {
        digitalWrite(s->pin, HIGH);
    } else {
        uint32_t periodUs = 1000000UL / freqHz;
        s->pwmOnUs = periodUs * duty / 100;
        s->pwmOffUs = periodUs - s->pwmOnUs;
        digitalWrite(s->pin, HIGH);
        s->pwmPhase = true;
        s->pwmNextUs = nowUs + s->pwmOnUs;
    }
}

static void testPwmBitbangTick(GplinkTestSlot *s, uint32_t nowUs) {
    uint8_t duty = (s->param2 > 100) ? 100 : (uint8_t)s->param2;
    if (duty == 0 || duty >= 100) return; // flat level, nothing to tick
    if ((int32_t)(nowUs - s->pwmNextUs) >= 0) {
        if (s->pwmPhase) {
            digitalWrite(s->pin, LOW);
            s->pwmNextUs = nowUs + s->pwmOffUs;
        } else {
            digitalWrite(s->pin, HIGH);
            s->pwmNextUs = nowUs + s->pwmOnUs;
        }
        s->pwmPhase = !s->pwmPhase;
    }
}

// Stop one slot's hardware with no RESULT (RESULT policy is the caller's:
// silent for replace/link-down, status-2 for commanded stop). Drive pins go
// back to INPUT via the s_dir table (session only, never NVS); the player
// LED pin is exempt so a test there cannot steal it from the LED path.
static void testReleaseSilent(GplinkTestSlot *s) {
    uint8_t pin = s->pin;
    uint8_t fn = s->function;
    if (pin < 64) {
        if (fn == GPLINK_TEST_FN_DRIVE_PWM && kTestPwmUseLedc) {
            uint8_t idx = (uint8_t)(s - s_tests);
            ledcWrite(idx, 0);
            ledcDetachPin(pin);
        }
        if (fn >= GPLINK_TEST_FN_DRIVE_LOW && fn <= GPLINK_TEST_FN_DRIVE_PWM &&
                pin != COMPANION_PLAYER_LED_PIN) {
            s_dir[pin] = 0;
            applyPinMode(pin);
        }
    }
    s->active = false;
}

// Drop every slot silently (link-down path). Returns the drop count for logs.
static uint8_t testAbortAll() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (!s_tests[i].active) continue;
        testReleaseSilent(&s_tests[i]);
        n++;
    }
    return n;
}

static void handleTestConfigure(const gplink_frame *frame, uint32_t now) {
    if (frame->len != 7) return; // malformed: ignore silently
    uint8_t testId = frame->payload[0];
    uint8_t pin = frame->payload[1];
    uint8_t fn = frame->payload[2];
    uint16_t p1 = (uint16_t)((uint16_t)frame->payload[3] | ((uint16_t)frame->payload[4] << 8));
    uint16_t p2 = (uint16_t)((uint16_t)frame->payload[5] | ((uint16_t)frame->payload[6] << 8));

    // Stop family (pin ignored).
    if (fn == GPLINK_TEST_FN_STOP) {
        if (testId == 0xFF) {
            uint8_t n = 0;
            for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
                if (!s_tests[i].active) continue;
                uint8_t id = s_tests[i].testId;
                uint16_t v = testSlotValue(&s_tests[i]);
                uint16_t c = s_tests[i].done;
                testReleaseSilent(&s_tests[i]);
                sendTestResult(id, GPLINK_TEST_STATUS_ABORTED, v, c, now);
                n++;
            }
            Serial.printf("%lu GPLink: tests stop-all (%u)\n", (unsigned long)now, n);
            return;
        }
        GplinkTestSlot *s = testFindId(testId);
        if (!s) {
            Serial.printf("%lu GPLink: test stop id %u ignored (idle)\n", (unsigned long)now, testId);
            return;
        }
        uint16_t v = testSlotValue(s);
        uint16_t c = s->done;
        testReleaseSilent(s);
        sendTestResult(testId, GPLINK_TEST_STATUS_ABORTED, v, c, now);
        Serial.printf("%lu GPLink: test id %u aborted\n", (unsigned long)now, testId);
        return;
    }

    // Pin validation: bounds first, then the link-UART exclusion (mirrors
    // sendPinCaps), then the table. Status 3 covers every unknown-pin case.
    if (pin >= 64 || pin == GPLINK_UART_RX || pin == GPLINK_UART_TX) {
        sendTestResult(testId, GPLINK_TEST_STATUS_BAD_PIN, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject pin %u status 3\n", (unsigned long)now, testId, pin);
        return;
    }
    const CompanionPin *entry = companionPinLookup(pin);
    if (!entry) {
        sendTestResult(testId, GPLINK_TEST_STATUS_BAD_PIN, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject pin %u status 3\n", (unsigned long)now, testId, pin);
        return;
    }
    // Strapping pins are fair game post-boot (boot sampled them long ago).
    bool isSim = (fn <= GPLINK_TEST_FN_TRIANGLE); // 0x00-0x05
    bool isDrive = (fn >= GPLINK_TEST_FN_DRIVE_LOW && fn <= GPLINK_TEST_FN_DRIVE_PWM);
    if (!isSim && !isDrive) {
        sendTestResult(testId, GPLINK_TEST_STATUS_UNSUPPORTED, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject fn %02X status 4\n", (unsigned long)now, testId, fn);
        return;
    }
    if (fn >= GPLINK_TEST_FN_SWEEP_UP && fn <= GPLINK_TEST_FN_TRIANGLE &&
            !(entry->caps & GPLINK_PINCAP_ADC)) {
        sendTestResult(testId, GPLINK_TEST_STATUS_UNSUPPORTED, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject pin %u no-ADC status 4\n",
                      (unsigned long)now, testId, pin);
        return;
    }
    if (isDrive && !(entry->caps & GPLINK_PINCAP_OUTPUT)) {
        sendTestResult(testId, GPLINK_TEST_STATUS_UNSUPPORTED, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject pin %u no-OUT status 4\n",
                      (unsigned long)now, testId, pin);
        return;
    }
    if (fn == GPLINK_TEST_FN_DRIVE_PWM) {
        if (p1 == 0 || (!kTestPwmUseLedc && p1 > GPLINK_TEST_PWM_BITBANG_MAX_HZ)) {
            sendTestResult(testId, GPLINK_TEST_STATUS_UNSUPPORTED, 0, 0, now);
            Serial.printf("%lu GPLink: test id %u reject pwm %u Hz status 4\n",
                          (unsigned long)now, testId, p1);
            return;
        }
    }

    // Per-pin exclusive: same testId or same pin re-CONFIGURED replaces the
    // old slot silently (hardware torn down, no RESULT for the old test).
    GplinkTestSlot *old = testFindId(testId);
    if (old) testReleaseSilent(old);
    old = testFindPin(pin);
    if (old) testReleaseSilent(old);
    GplinkTestSlot *s = testFreeSlot();
    if (!s) {
        sendTestResult(testId, GPLINK_TEST_STATUS_BUSY, 0, 0, now);
        Serial.printf("%lu GPLink: test id %u reject busy status 5\n", (unsigned long)now, testId);
        return;
    }
    s->active = true;
    s->testId = testId;
    s->pin = pin;
    s->function = fn;
    s->param1 = p1;
    s->param2 = p2;
    s->done = 0;
    s->dir = 1;
    s->pwmPhase = false;
    s->pwmNextUs = 0;
    s->pwmOnUs = 0;
    s->pwmOffUs = 0;
    switch (fn) {
        case GPLINK_TEST_FN_HOLD_LOW:
            s->level = false;
            break;
        case GPLINK_TEST_FN_HOLD_HIGH:
            s->level = true;
            break;
        case GPLINK_TEST_FN_TOGGLE:
            s->level = true; // start HIGH: immediate visible edge
            s->nextMs = now + testHalfPeriod(p1);
            break;
        case GPLINK_TEST_FN_SWEEP_UP:
            s->value = 0;
            s->nextMs = now + testStepInterval(p1);
            break;
        case GPLINK_TEST_FN_SWEEP_DOWN:
            s->value = COMPANION_ADC_MAX;
            s->nextMs = now + testStepInterval(p1);
            break;
        case GPLINK_TEST_FN_TRIANGLE:
            s->value = 0;
            s->nextMs = now + testStepInterval(p1);
            break;
        case GPLINK_TEST_FN_DRIVE_LOW:
        case GPLINK_TEST_FN_DRIVE_HIGH:
            s->level = (fn == GPLINK_TEST_FN_DRIVE_HIGH);
            s_dir[pin] = 1; // driven; session only, never persisted
            applyPinMode(pin);
            digitalWrite(pin, s->level ? HIGH : LOW);
            break;
        case GPLINK_TEST_FN_DRIVE_CYCLE:
            s->level = true;
            s->nextMs = now + testHalfPeriod(p1);
            s_dir[pin] = 1; // driven; session only, never persisted
            applyPinMode(pin);
            digitalWrite(pin, HIGH);
            break;
        case GPLINK_TEST_FN_DRIVE_PWM: {
            s_dir[pin] = 1; // driven; session only, never persisted
            applyPinMode(pin);
            uint8_t idx = (uint8_t)(s - s_tests); // LEDC channel = slot index
            testPwmStart(s, idx, micros());
            break;
        }
        default:
            break; // unreachable: validated above
    }
    sendTestResult(testId, GPLINK_TEST_STATUS_RUNNING, testSlotValue(s), 0, now);
    Serial.printf("%lu GPLink: test id %u pin %u fn %02X p1=%u p2=%u\n",
                  (unsigned long)now, testId, pin, fn, p1, p2);
}

// Radio byte for FEATURE_ACK identity: bit0 = WiFi active (STA connected or
// soft-AP mode on; getMode/status are non-blocking state reads only),
// bit1 = BT (M2 owns it; always 0 here).
static uint8_t testRadioByte() {
    uint8_t radio = 0;
    wifi_mode_t mode = WiFi.getMode();
    if (mode & WIFI_MODE_AP) {
        radio |= 0x01;
    } else if ((mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
        radio |= 0x01;
    }
    if (s_btActive) radio |= 0x02;
    return radio;
}

static void handleFeatureReq(const gplink_frame *frame, uint32_t now) {
    if (frame->len != 1) return;
    if (frame->payload[0] != GPLINK_FEATURE_IDENTITY) return; // unknown: ignore silently
    const char *ver = COMPANION_FW_VERSION;
    size_t vl = strlen(ver);
    if (vl > GPLINK_TEST_VER_MAX) vl = GPLINK_TEST_VER_MAX;
    uint8_t payload[1 + 1 + GPLINK_TEST_VER_MAX + 1];
    payload[0] = GPLINK_FEATURE_IDENTITY;
    payload[1] = (uint8_t)vl;
    memcpy(&payload[2], ver, vl);
    payload[2 + vl] = testRadioByte();
    sendFrame(GPLINK_TYPE_FEATURE_ACK, payload, 2 + vl + 1);
    Serial.printf("%lu GPLink: FEATURE_ACK identity ver %s radio %u\n",
                  (unsigned long)now, ver, payload[2 + vl]);
}

// Analog sweep one-step: returns true on a repeat boundary (top for up,
// bottom for down, return-to-zero for triangle).
static bool testSweepStep(GplinkTestSlot *s) {
    int v = (int)s->value;
    if (s->function == GPLINK_TEST_FN_SWEEP_UP) {
        v += GPLINK_TEST_SWEEP_STEP;
        if (v >= (int)COMPANION_ADC_MAX) {
            s->value = COMPANION_ADC_MAX;
            return true;
        }
        s->value = (uint16_t)v;
        return false;
    }
    if (s->function == GPLINK_TEST_FN_SWEEP_DOWN) {
        v -= GPLINK_TEST_SWEEP_STEP;
        if (v <= 0) {
            s->value = 0;
            return true;
        }
        s->value = (uint16_t)v;
        return false;
    }
    v += GPLINK_TEST_SWEEP_STEP * (int)s->dir;
    if (v >= (int)COMPANION_ADC_MAX) {
        v = COMPANION_ADC_MAX;
        s->dir = -1;
    } else if (v <= 0) {
        v = 0;
        s->dir = 1;
        s->value = 0;
        return true;
    }
    s->value = (uint16_t)v;
    return false;
}

// Native-unit peek for pumpAnalog: per-pin test wins over global synth/known
// for that pin only (checked first at the top of the per-pin section).
static bool testAnalogPeek(uint8_t pin, uint16_t *rawOut) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (!s_tests[i].active || s_tests[i].pin != pin) continue;
        uint8_t fn = s_tests[i].function;
        if (fn >= GPLINK_TEST_FN_SWEEP_UP && fn <= GPLINK_TEST_FN_TRIANGLE) {
            *rawOut = s_tests[i].value;
            return true;
        }
    }
    return false;
}

// Digital simulate override, applied to the sampled mask AFTER
// sampleConfiguredInputs() (and the console self-test) in the 2 ms block.
// Drive pins never synthesize: they read s_dir=OUTPUT so the sampler skips
// them and this override only knows the 0x00-0x02 family.
static uint64_t testDigitalOverride(uint64_t mask) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        if (!s_tests[i].active || s_tests[i].pin >= 64) continue;
        if (s_tests[i].function > GPLINK_TEST_FN_TOGGLE) continue;
        if (s_tests[i].level) mask |= (1ULL << s_tests[i].pin);
        else mask &= ~(1ULL << s_tests[i].pin);
    }
    return mask;
}

// Non-blocking slot evolution (millis pattern, never delay): toggle/cycle
// flips, sweep steps, PWM bit-bang ticks. Holds do nothing. Finite tests
// complete here with a single status-1 RESULT (gated on link, like all
// RESULTs); infinite ones run until stopped or the link drops.
static void pumpTests(uint32_t now) {
    for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
        GplinkTestSlot *s = &s_tests[i];
        if (!s->active) continue;
        switch (s->function) {
            case GPLINK_TEST_FN_TOGGLE:
            case GPLINK_TEST_FN_DRIVE_CYCLE: {
                if ((int32_t)(now - s->nextMs) < 0) break;
                s->nextMs = now + testHalfPeriod(s->param1);
                s->level = !s->level;
                if (s->function == GPLINK_TEST_FN_DRIVE_CYCLE) {
                    digitalWrite(s->pin, s->level ? HIGH : LOW);
                }
                // A full cycle closes on return to the start level (HIGH).
                if (s->level) {
                    s->done++;
                    if (s->param2 > 0 && s->done >= s->param2) {
                        uint8_t id = s->testId;
                        uint16_t v = testSlotValue(s);
                        uint16_t c = s->done;
                        testReleaseSilent(s);
                        sendTestResult(id, GPLINK_TEST_STATUS_DONE, v, c, now);
                        Serial.printf("%lu GPLink: test id %u done value %u count %u\n",
                                      (unsigned long)now, id, v, c);
                    }
                }
                break;
            }
            case GPLINK_TEST_FN_SWEEP_UP:
            case GPLINK_TEST_FN_SWEEP_DOWN:
            case GPLINK_TEST_FN_TRIANGLE: {
                if ((int32_t)(now - s->nextMs) < 0) break;
                s->nextMs = now + testStepInterval(s->param1);
                if (!testSweepStep(s)) break;
                s->done++;
                if (s->param2 > 0 && s->done >= s->param2) {
                    uint8_t id = s->testId;
                    uint16_t v = testSlotValue(s);
                    uint16_t c = s->done;
                    testReleaseSilent(s);
                    sendTestResult(id, GPLINK_TEST_STATUS_DONE, v, c, now);
                    Serial.printf("%lu GPLink: test id %u done value %u count %u\n",
                                  (unsigned long)now, id, v, c);
                } else if (s->function == GPLINK_TEST_FN_SWEEP_UP) {
                    s->value = 0; // wrap for the next repeat
                } else if (s->function == GPLINK_TEST_FN_SWEEP_DOWN) {
                    s->value = COMPANION_ADC_MAX;
                }
                break;
            }
            case GPLINK_TEST_FN_DRIVE_PWM:
                if (!kTestPwmUseLedc) testPwmBitbangTick(s, micros());
                break;
            default:
                break; // holds: level set once at accept
        }
    }
}

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
// Known-value mode ('k'): every configured channel reports a constant
// mid-scale RAW reading (2048 ≈ 32776 normalized). Any display movement
// under a constant source is definitively downstream (wire/decode/apply/
// USB/tester), never source noise.
static bool s_analogKnown = false;
#define COMPANION_KNOWN_RAW 2048

static void pumpAnalog(uint32_t now) {
    static uint32_t lastPushMs = 0;
    // Synthetic triangle advances here so every configured channel shares
    // one phase. 2 ms steps (not 20 ms): coarser steps beat against a 60 Hz
    // display and read as judder, which looks exactly like link jitter.
    if (s_analogSynth && (now - s_synthMs) >= 2) {
        s_synthMs = now;
        int v = (int)s_synthRaw + 3 * (int)s_synthDir;
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
        // Per-pin test stimulus wins here (before known/synth/ADC): the same
        // EMA/deadband/normalization below still shapes it, and only pins
        // with a sweep test join without ANALOG_CONFIG.
        uint16_t testRaw = 0;
        bool inTest = testAnalogPeek(pin, &testRaw);
        if (!s_analog[pin] && !inTest) continue;
        uint16_t raw;
        if (inTest) {
            raw = testRaw;
        } else if (s_analogKnown) {
            raw = COMPANION_KNOWN_RAW;
        } else if (s_analogSynth) {
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
        if (testDriveActive(pin)) continue; // test-owned: GPIO_WRITE must not stomp it
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
            case GPLINK_TYPE_TEST_CONFIGURE:
                handleTestConfigure(&frame, now);
                break;
            case GPLINK_TYPE_FEATURE_REQ:
                handleFeatureReq(&frame, now);
                break;
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
    Serial.println("GPLink: console keys: t = input self-test, a = analog synth, k = known value, d = DAC sweep, g = frame probe, l = LED");
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
            if (s_analogSynth) s_analogKnown = false;
            if (!s_analogSynth && !s_analogKnown) memset(s_analogSm, 0xff, sizeof(s_analogSm));
            Serial.printf("GPLink: analog synth %s\n", s_analogSynth ? "ON" : "OFF");
        } else if (c == 'k' || c == 'K') {
            s_analogKnown = !s_analogKnown;
            if (s_analogKnown) s_analogSynth = false;
            if (!s_analogSynth && !s_analogKnown) memset(s_analogSm, 0xff, sizeof(s_analogSm));
            Serial.printf("GPLink: known-value %s\n", s_analogKnown ? "ON" : "OFF");
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
        } else if (c == 'x' || c == 'X') {
            uint8_t n = 0;
            for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
                if (s_tests[i].active) n++;
            }
            Serial.printf("GPLink: tests %u active\n", n);
            for (uint8_t i = 0; i < GPLINK_TEST_MAX_SLOTS; i++) {
                const GplinkTestSlot *s = &s_tests[i];
                if (!s->active) continue;
                Serial.printf("  id %u pin %u fn %02X p1=%u p2=%u lvl=%u val=%u done=%u\n",
                              s->testId, s->pin, s->function, s->param1, s->param2,
                              s->level ? 1 : 0, s->value, s->done);
            }
            if (s_testHaveLast) {
                Serial.printf("GPLink: last RESULT id %u status %u value %u count %u\n",
                              s_testLastId, s_testLastStatus, s_testLastValue, s_testLastCount);
            } else {
                Serial.println("GPLink: no RESULT yet");
            }
        }
    }
    pumpLink();
    pumpTests(now); // advance slots first: fresh sweep levels feed pumpAnalog below
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
        // Simulate family (0x00-0x02) overrides AFTER sampling (and the
        // console self-test above) so tests take precedence; drive pins are
        // s_dir=OUTPUT and never reach this mask except through a test.
        mask = testDigitalOverride(mask);
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
        if (!alive) {
            // Link-down abort: drop every test silently (slots cleared, LEDC
            // detached, driven pins back to INPUT). No RESULT may be queued
            // while down, so this console line is the only trace.
            uint8_t dropped = testAbortAll();
            if (dropped > 0) {
                Serial.printf("%lu GPLink: tests aborted (link DOWN, %u)\n",
                              (unsigned long)now, dropped);
            }
        }
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
    // Radio-change HELLO: the peer re-pushes configs on HELLO, so this
    // propagates WiFi/AP changes for free. One extra HELLO per 5 s max.
    static bool s_radioInit = false;
    static uint8_t s_lastRadioSent = 0;
    static uint32_t s_lastExtraHelloMs = 0;
    uint8_t radioNow = testRadioByte();
    if (!s_radioInit) {
        s_radioInit = true;
        s_lastRadioSent = radioNow;
    } else if (radioNow != s_lastRadioSent && (now - s_lastExtraHelloMs) >= 5000) {
        s_lastExtraHelloMs = now;
        s_lastRadioSent = radioNow;
        sendHello();
        Serial.printf("%lu GPLink: HELLO re-push (radio %u)\n", (unsigned long)now, radioNow);
    }
}
