// extras/gp-link/tests/test_gplink_messages.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gplink.h"

static bool feed_all(gplink_decoder *dec, const uint8_t *wire, size_t n, gplink_frame *out) {
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (gplink_feed(dec, wire[i], out)) got = true;
    }
    return got;
}

static void wire_roundtrip(uint8_t type, const uint8_t *payload, uint8_t len, gplink_frame *out) {
    uint8_t wire[512] = {0};
    size_t n = gplink_encode(type, payload, len, wire);
    assert(n > 0);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    assert(feed_all(&dec, wire, n, out));
    assert(out->type == type);
    assert(out->len == len);
    if (len > 0) assert(memcmp(out->payload, payload, len) == 0);
}

static void test_input_state() {
    uint8_t payload[32] = {0};
    size_t n = gplink_pack_input_state(3, 0xDEADBEEF, 7, 1000, 2000, 3000, 4000, 255, 128, 5, payload);
    assert(n == 17);
    // LE spot checks: buttons then dpad then lx
    assert(payload[0] == 3);
    assert(payload[1] == 0xEF && payload[2] == 0xBE && payload[3] == 0xAD && payload[4] == 0xDE);
    assert(payload[5] == 7);
    assert(payload[6] == (1000 & 0xFF) && payload[7] == (1000 >> 8));
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_INPUT_STATE, payload, (uint8_t)n, &f);
    uint8_t devid = 0, dpad = 0, lt = 0, rt = 0, aux = 0;
    uint32_t buttons = 0;
    uint16_t lx = 0, ly = 0, rx = 0, ry = 0;
    assert(gplink_unpack_input_state(&f, &devid, &buttons, &dpad, &lx, &ly, &rx, &ry, &lt, &rt, &aux));
    assert(devid == 3 && buttons == 0xDEADBEEF && dpad == 7);
    assert(lx == 1000 && ly == 2000 && rx == 3000 && ry == 4000);
    assert(lt == 255 && rt == 128 && aux == 5);
    // wrong-type reject
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_input_state(&wrong, &devid, &buttons, &dpad, &lx, &ly, &rx, &ry, &lt, &rt, &aux));
    // truncated reject
    gplink_frame trunc = f;
    trunc.len = 16;
    assert(!gplink_unpack_input_state(&trunc, &devid, &buttons, &dpad, &lx, &ly, &rx, &ry, &lt, &rt, &aux));
}

static void test_hello() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_hello(1, 0, 0xFFFFFFFFu, 4, payload);
    assert(n == 7);
    assert(payload[0] == 1 && payload[1] == 0);
    assert(payload[2] == 0xFF && payload[3] == 0xFF && payload[4] == 0xFF && payload[5] == 0xFF);
    assert(payload[6] == 4);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_HELLO, payload, (uint8_t)n, &f);
    uint8_t major = 0, minor = 0, devcount = 0;
    uint32_t caps = 0;
    assert(gplink_unpack_hello(&f, &major, &minor, &caps, &devcount));
    assert(major == 1 && minor == 0 && caps == 0xFFFFFFFFu && devcount == 4);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_INPUT_STATE;
    assert(!gplink_unpack_hello(&wrong, &major, &minor, &caps, &devcount));
    gplink_frame trunc = f;
    trunc.len = 6;
    assert(!gplink_unpack_hello(&trunc, &major, &minor, &caps, &devcount));
}

static void test_gpio_config() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_gpio_config(1, 13, 1, 1, GPLINK_GPIO_FLAG_INVERTED, payload);
    assert(n == 5);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_GPIO_CONFIG, payload, (uint8_t)n, &f);
    uint8_t devid = 0, pin = 0, dir = 0, pull = 0, flags = 0;
    assert(gplink_unpack_gpio_config(&f, &devid, &pin, &dir, &pull, &flags));
    assert(devid == 1 && pin == 13 && dir == 1 && pull == 1);
    assert(flags == GPLINK_GPIO_FLAG_INVERTED);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_gpio_config(&wrong, &devid, &pin, &dir, &pull, &flags));
    gplink_frame trunc = f;
    trunc.len = 3;
    assert(!gplink_unpack_gpio_config(&trunc, &devid, &pin, &dir, &pull, &flags));
    // Legacy 4-byte form still unpacks with flags defaulted to 0.
    uint8_t legacy[4] = {2, 7, 0, 1};
    gplink_frame lf;
    wire_roundtrip(GPLINK_TYPE_GPIO_CONFIG, legacy, sizeof(legacy), &lf);
    assert(gplink_unpack_gpio_config(&lf, &devid, &pin, &dir, &pull, &flags));
    assert(devid == 2 && pin == 7 && dir == 0 && pull == 1 && flags == 0);
}

static void test_gpio_mask() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_gpio_mask(2, 0x8000000000000001ULL, payload);
    assert(n == 9);
    assert(payload[0] == 2);
    assert(payload[1] == 0x01 && payload[8] == 0x80);
    for (int k = 2; k < 8; k++) assert(payload[k] == 0x00);
    // GPIO_READ
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_GPIO_READ, payload, (uint8_t)n, &f);
    uint8_t devid = 0;
    uint64_t mask = 0;
    assert(gplink_unpack_gpio_mask(&f, &devid, &mask));
    assert(devid == 2 && mask == 0x8000000000000001ULL);
    // GPIO_WRITE shares the layout
    wire_roundtrip(GPLINK_TYPE_GPIO_WRITE, payload, (uint8_t)n, &f);
    assert(gplink_unpack_gpio_mask(&f, &devid, &mask));
    assert(devid == 2 && mask == 0x8000000000000001ULL);
    // wrong-type reject (not READ/WRITE)
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_gpio_mask(&wrong, &devid, &mask));
    gplink_frame trunc = f;
    trunc.len = 8;
    assert(!gplink_unpack_gpio_mask(&trunc, &devid, &mask));
}

static void test_gpio_nak() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_gpio_nak(0, 9, 2, payload);
    assert(n == 3);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_GPIO_NAK, payload, (uint8_t)n, &f);
    uint8_t devid = 0, pin = 0, code = 0;
    assert(gplink_unpack_gpio_nak(&f, &devid, &pin, &code));
    assert(devid == 0 && pin == 9 && code == 2);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_gpio_nak(&wrong, &devid, &pin, &code));
    gplink_frame trunc = f;
    trunc.len = 2;
    assert(!gplink_unpack_gpio_nak(&trunc, &devid, &pin, &code));
}

static void test_analog_config() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_analog_config(0, 32, 1, payload);
    assert(n == 3);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_ANALOG_CONFIG, payload, (uint8_t)n, &f);
    uint8_t devid = 0, pin = 0, enable = 0;
    assert(gplink_unpack_analog_config(&f, &devid, &pin, &enable));
    assert(devid == 0 && pin == 32 && enable == 1);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_analog_config(&wrong, &devid, &pin, &enable));
    gplink_frame trunc2 = f;
    trunc2.len = 2;
    assert(!gplink_unpack_analog_config(&trunc2, &devid, &pin, &enable));
}

static void test_analog_read() {
    uint8_t pins[3] = {32, 33, 26};
    uint16_t values[3] = {0, 32768, 65535};
    uint8_t payload[32] = {0};
    size_t n = gplink_pack_analog_read(0, 3, pins, values, payload);
    assert(n == 11);
    assert(payload[0] == 0 && payload[1] == 3);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_ANALOG_READ, payload, (uint8_t)n, &f);
    uint8_t devid = 0, count = 0;
    uint8_t pinsOut[8] = {0};
    uint16_t valuesOut[8] = {0};
    assert(gplink_unpack_analog_read(&f, &devid, &count, pinsOut, valuesOut, 8));
    assert(devid == 0 && count == 3);
    for (int i = 0; i < 3; i++) {
        assert(pinsOut[i] == pins[i]);
        assert(valuesOut[i] == values[i]);
    }
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_analog_read(&wrong, &devid, &count, pinsOut, valuesOut, 8));
    // caller buffer too small: rejected, no overrun
    assert(!gplink_unpack_analog_read(&f, &devid, &count, pinsOut, valuesOut, 2));
    // empty report round-trips
    n = gplink_pack_analog_read(1, 0, pins, values, payload);
    assert(n == 2);
    wire_roundtrip(GPLINK_TYPE_ANALOG_READ, payload, (uint8_t)n, &f);
    assert(gplink_unpack_analog_read(&f, &devid, &count, pinsOut, valuesOut, 8));
    assert(devid == 1 && count == 0);
}

static void test_rumble() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_rumble(1, 100, 200, 1000, payload);
    assert(n == 5);
    assert(payload[0] == 1 && payload[1] == 100 && payload[2] == 200);
    assert(payload[3] == (1000 & 0xFF) && payload[4] == (1000 >> 8));
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_RUMBLE_SET, payload, (uint8_t)n, &f);
    uint8_t devid = 0, weak = 0, strong = 0;
    uint16_t dur = 0;
    assert(gplink_unpack_rumble(&f, &devid, &weak, &strong, &dur));
    assert(devid == 1 && weak == 100 && strong == 200 && dur == 1000);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_rumble(&wrong, &devid, &weak, &strong, &dur));
    gplink_frame trunc = f;
    trunc.len = 4;
    assert(!gplink_unpack_rumble(&trunc, &devid, &weak, &strong, &dur));
}

static void test_player_led() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_player_led(2, 0x0A, payload);
    assert(n == 2);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_PLAYER_LED_SET, payload, (uint8_t)n, &f);
    uint8_t devid = 0, mask = 0;
    assert(gplink_unpack_player_led(&f, &devid, &mask));
    assert(devid == 2 && mask == 0x0A);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_player_led(&wrong, &devid, &mask));
    gplink_frame trunc = f;
    trunc.len = 1;
    assert(!gplink_unpack_player_led(&trunc, &devid, &mask));
}

static void test_battery() {
    uint8_t payload[16] = {0};
    size_t n = gplink_pack_battery(0, 100, 1, payload);
    assert(n == 3);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_BATTERY_REPORT, payload, (uint8_t)n, &f);
    uint8_t devid = 0, pct = 0, charging = 0;
    assert(gplink_unpack_battery(&f, &devid, &pct, &charging));
    assert(devid == 0 && pct == 100 && charging == 1);
    gplink_frame wrong = f;
    wrong.type = GPLINK_TYPE_HELLO;
    assert(!gplink_unpack_battery(&wrong, &devid, &pct, &charging));
    gplink_frame trunc = f;
    trunc.len = 2;
    assert(!gplink_unpack_battery(&trunc, &devid, &pct, &charging));
}

static void test_pin_caps() {
    // REQ: empty payload roundtrip
    uint8_t req[8] = {0};
    assert(gplink_pack_pin_caps_req(req) == 0);
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_PIN_CAPS_REQ, nullptr, 0, &f);

    // RSP roundtrip: name "UNO", pins {2,3,19}, caps {0x0B,0x0B,0x03}
    const uint8_t pins[3] = {2, 3, 19};
    const uint8_t caps[3] = {0x0B, 0x0B, 0x03};
    uint8_t payload[GPLINK_MAX_PAYLOAD] = {0};
    size_t n = gplink_pack_pin_caps_rsp("UNO", 3, 3, pins, caps, payload);
    assert(n == 1 + 3 + 1 + 2 * 3);
    assert(payload[0] == 3);
    assert(payload[1] == 'U' && payload[2] == 'N' && payload[3] == 'O');
    assert(payload[4] == 3);
    gplink_frame rsp;
    wire_roundtrip(GPLINK_TYPE_PIN_CAPS_RSP, payload, (uint8_t)n, &rsp);
    char name[33] = {0};
    uint8_t name_len = 0, count = 0;
    uint8_t pins_out[70] = {0};
    uint8_t caps_out[70] = {0};
    assert(gplink_unpack_pin_caps_rsp(&rsp, name, &name_len, &count, pins_out, caps_out));
    assert(name_len == 3 && count == 3);
    assert(memcmp(name, "UNO", 3) == 0 && name[3] == '\0');
    assert(pins_out[0] == 2 && pins_out[1] == 3 && pins_out[2] == 19);
    assert(caps_out[0] == 0x0B && caps_out[1] == 0x0B && caps_out[2] == 0x03);

    // truncated RSP rejected
    gplink_frame trunc = rsp;
    trunc.len = (uint8_t)(rsp.len - 1);
    assert(!gplink_unpack_pin_caps_rsp(&trunc, name, &name_len, &count, pins_out, caps_out));
    // wrong-type rejected
    gplink_frame wrong = rsp;
    wrong.type = GPLINK_TYPE_PIN_CAPS_REQ;
    assert(!gplink_unpack_pin_caps_rsp(&wrong, name, &name_len, &count, pins_out, caps_out));
    // bounds rejected at pack time
    assert(gplink_pack_pin_caps_rsp("x", 33, 0, pins, caps, payload) == 0);
    assert(gplink_pack_pin_caps_rsp("x", 1, 71, pins, caps, payload) == 0);
}

static void test_raw_types_roundtrip() {
    // Types without dedicated pack/unpack: encode->feed must still roundtrip.
    gplink_frame f;
    wire_roundtrip(GPLINK_TYPE_HEARTBEAT, nullptr, 0, &f);
    const uint8_t dbg[] = {1, 'h', 'i'};
    wire_roundtrip(GPLINK_TYPE_DEBUG_TEXT, dbg, sizeof(dbg), &f);
    const uint8_t feat[] = {0x01, 0x02};
    wire_roundtrip(GPLINK_TYPE_FEATURE_REQ, feat, sizeof(feat), &f);
    wire_roundtrip(GPLINK_TYPE_FEATURE_ACK, feat, sizeof(feat), &f);
    const uint8_t http[] = {0x07, 0x08, 0x09};
    wire_roundtrip(GPLINK_TYPE_HTTP_REQ, http, sizeof(http), &f);
    wire_roundtrip(GPLINK_TYPE_HTTP_RESP, http, sizeof(http), &f);
    wire_roundtrip(GPLINK_TYPE_HTTP_FRAG, http, sizeof(http), &f);
}

int main() {
    test_input_state();
    test_hello();
    test_gpio_config();
    test_gpio_mask();
    test_rumble();
    test_player_led();
    test_battery();
    test_gpio_nak();
    test_analog_config();
    test_analog_read();
    test_pin_caps();
    test_raw_types_roundtrip();
    printf("messages: all assertions passed\n");
    return 0;
}
