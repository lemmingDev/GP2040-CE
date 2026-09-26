// extras/gp-link/gplink.cpp
#include "gplink.h"
#include "CRC32.h"

size_t gplink_encode(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out) {
    return gplink_encode_seq(type, payload, payload_len, 0, out);
}

size_t gplink_encode_seq(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t seq, uint8_t *out) {
    if (payload == NULL && payload_len > 0) return 0;
    if (payload_len > GPLINK_MAX_PAYLOAD) return 0;
    uint8_t raw[4 + GPLINK_MAX_PAYLOAD + 4];
    raw[0] = GPLINK_VERSION_BYTE;
    raw[1] = type;
    raw[2] = seq;
    raw[3] = payload_len;
    for (uint8_t i = 0; i < payload_len; i++) raw[4 + i] = payload[i];
    uint32_t crc = CRC32::calculate(raw, (uint16_t)(4 + payload_len));
    raw[4 + payload_len + 0] = (uint8_t)(crc & 0xFF);
    raw[4 + payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    raw[4 + payload_len + 2] = (uint8_t)((crc >> 16) & 0xFF);
    raw[4 + payload_len + 3] = (uint8_t)((crc >> 24) & 0xFF);
    const uint16_t rawLen = (uint16_t)(8 + payload_len);
    // COBS encode raw[0..rawLen) between 0x00 delimiters
    size_t w = 0;
    out[w++] = 0x00;
    uint16_t i = 0;
    while (i < rawLen) {
        uint16_t codeIdx = (uint16_t)w++;
        uint8_t code = 1;
        while (i < rawLen && raw[i] != 0 && code < 0xFF) {
            out[w++] = raw[i++];
            code++;
        }
        out[codeIdx] = code;
        if (i < rawLen && raw[i] == 0) i++;
    }
    // Standard COBS termination: a trailing zero byte in the raw input must
    // close with an empty final group, otherwise the decoder drops it.
    if (rawLen > 0 && raw[rawLen - 1] == 0) out[w++] = 0x01;
    out[w++] = 0x00;
    return w;
}

void gplink_decoder_init(gplink_decoder *d) {
    d->len = 0;
}

static bool gplink_decode_frame(const uint8_t *cobs, uint16_t cobsLen, gplink_frame *out) {
    if (cobsLen < 8) return false;
    uint8_t raw[4 + GPLINK_MAX_PAYLOAD + 4];
    uint16_t r = 0;
    uint16_t i = 0;
    while (i < cobsLen) {
        uint8_t code = cobs[i++];
        if (code == 0 || i + code - 1 > cobsLen) return false;
        for (uint8_t k = 1; k < code; k++) {
            if (r >= sizeof(raw)) return false;
            raw[r++] = cobs[i++];
        }
        if (code < 0xFF && i < cobsLen) {
            if (r >= sizeof(raw)) return false;
            raw[r++] = 0;
        }
    }
    if (r < 8) return false;
    uint8_t payloadLen = raw[3];
    if ((uint16_t)(8 + payloadLen) != r) return false;
    if (payloadLen > GPLINK_MAX_PAYLOAD) return false;
    if (raw[0] != GPLINK_VERSION_BYTE) return false;
    uint32_t crc = (uint32_t)raw[4 + payloadLen] |
                   ((uint32_t)raw[5 + payloadLen] << 8) |
                   ((uint32_t)raw[6 + payloadLen] << 16) |
                   ((uint32_t)raw[7 + payloadLen] << 24);
    if (CRC32::calculate(raw, (uint16_t)(4 + payloadLen)) != crc) return false;
    out->type = raw[1];
    out->seq = raw[2];
    out->len = payloadLen;
    for (uint8_t k = 0; k < payloadLen; k++) out->payload[k] = raw[4 + k];
    return true;
}

bool gplink_feed(gplink_decoder *d, uint8_t byte, gplink_frame *out) {
    if (byte == 0x00) {
        bool ok = false;
        if (d->len > 0) ok = gplink_decode_frame(d->buf, d->len, out);
        d->len = 0;
        return ok;
    }
    if (d->len < sizeof(d->buf)) d->buf[d->len++] = byte;
    else d->len = 0; // overrun: drop and resync on next delimiter
    return false;
}

// v1 message payload pack/unpack (little-endian, no allocation)

static void gplink_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void gplink_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t gplink_get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t gplink_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t gplink_pack_input_state(uint8_t devid, uint32_t buttons, uint8_t dpad, uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry, uint8_t lt, uint8_t rt, uint8_t aux, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    gplink_put_u32(&payload_out[1], buttons);
    payload_out[5] = dpad;
    gplink_put_u16(&payload_out[6], lx);
    gplink_put_u16(&payload_out[8], ly);
    gplink_put_u16(&payload_out[10], rx);
    gplink_put_u16(&payload_out[12], ry);
    payload_out[14] = lt;
    payload_out[15] = rt;
    payload_out[16] = aux;
    return 17;
}

size_t gplink_pack_hello(uint8_t major, uint8_t minor, uint32_t caps, uint8_t devcount, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = major;
    payload_out[1] = minor;
    gplink_put_u32(&payload_out[2], caps);
    payload_out[6] = devcount;
    return 7;
}

size_t gplink_pack_gpio_config(uint8_t devid, uint8_t pin, uint8_t dir, uint8_t pull, uint8_t flags, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = pin;
    payload_out[2] = dir;
    payload_out[3] = pull;
    payload_out[4] = flags;
    return 5;
}

size_t gplink_pack_gpio_mask(uint8_t devid, uint64_t mask, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    for (uint8_t i = 0; i < 8; i++) payload_out[1 + i] = (uint8_t)((mask >> (8 * i)) & 0xFF);
    return 9;
}

size_t gplink_pack_gpio_nak(uint8_t devid, uint8_t pin, uint8_t code, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = pin;
    payload_out[2] = code;
    return 3;
}

size_t gplink_pack_analog_config(uint8_t devid, uint8_t pin, uint8_t enable, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = pin;
    payload_out[2] = enable ? 1 : 0;
    return 3;
}

size_t gplink_pack_analog_read(uint8_t devid, uint8_t count, const uint8_t *pins, const uint16_t *values, uint8_t *payload_out) {
    if (!payload_out || !pins || !values) return 0;
    if ((size_t)2 + (size_t)3 * count > GPLINK_MAX_PAYLOAD) return 0;
    payload_out[0] = devid;
    payload_out[1] = count;
    for (uint8_t i = 0; i < count; i++) {
        payload_out[2 + 3 * i] = pins[i];
        gplink_put_u16(&payload_out[3 + 3 * i], values[i]);
    }
    return (size_t)2 + (size_t)3 * count;
}

size_t gplink_pack_rumble(uint8_t devid, uint8_t weak, uint8_t strong, uint16_t duration_ms, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = weak;
    payload_out[2] = strong;
    gplink_put_u16(&payload_out[3], duration_ms);
    return 5;
}

size_t gplink_pack_player_led(uint8_t devid, uint8_t mask, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = mask;
    return 2;
}

size_t gplink_pack_battery(uint8_t devid, uint8_t pct, uint8_t charging, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = devid;
    payload_out[1] = pct;
    payload_out[2] = charging;
    return 3;
}

bool gplink_unpack_input_state(const gplink_frame *f, uint8_t *devid, uint32_t *buttons, uint8_t *dpad, uint16_t *lx, uint16_t *ly, uint16_t *rx, uint16_t *ry, uint8_t *lt, uint8_t *rt, uint8_t *aux) {
    if (!f || f->type != GPLINK_TYPE_INPUT_STATE || f->len != 17) return false;
    if (!devid || !buttons || !dpad || !lx || !ly || !rx || !ry || !lt || !rt || !aux) return false;
    *devid = f->payload[0];
    *buttons = gplink_get_u32(&f->payload[1]);
    *dpad = f->payload[5];
    *lx = gplink_get_u16(&f->payload[6]);
    *ly = gplink_get_u16(&f->payload[8]);
    *rx = gplink_get_u16(&f->payload[10]);
    *ry = gplink_get_u16(&f->payload[12]);
    *lt = f->payload[14];
    *rt = f->payload[15];
    *aux = f->payload[16];
    return true;
}

bool gplink_unpack_hello(const gplink_frame *f, uint8_t *major, uint8_t *minor, uint32_t *caps, uint8_t *devcount) {
    if (!f || f->type != GPLINK_TYPE_HELLO || f->len != 7) return false;
    if (!major || !minor || !caps || !devcount) return false;
    *major = f->payload[0];
    *minor = f->payload[1];
    *caps = gplink_get_u32(&f->payload[2]);
    *devcount = f->payload[6];
    return true;
}

bool gplink_unpack_gpio_config(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *dir, uint8_t *pull, uint8_t *flags) {
    if (!f || f->type != GPLINK_TYPE_GPIO_CONFIG || (f->len != 4 && f->len != 5)) return false;
    if (!devid || !pin || !dir || !pull || !flags) return false;
    *devid = f->payload[0];
    *pin = f->payload[1];
    *dir = f->payload[2];
    *pull = f->payload[3];
    *flags = (f->len == 5) ? f->payload[4] : 0;
    return true;
}

bool gplink_unpack_gpio_mask(const gplink_frame *f, uint8_t *devid, uint64_t *mask) {
    if (!f || (f->type != GPLINK_TYPE_GPIO_READ && f->type != GPLINK_TYPE_GPIO_WRITE) || f->len != 9) return false;
    if (!devid || !mask) return false;
    *devid = f->payload[0];
    uint64_t m = 0;
    for (uint8_t i = 0; i < 8; i++) m |= ((uint64_t)f->payload[1 + i]) << (8 * i);
    *mask = m;
    return true;
}

bool gplink_unpack_gpio_nak(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *code) {
    if (!f || f->type != GPLINK_TYPE_GPIO_NAK || f->len != 3) return false;
    if (!devid || !pin || !code) return false;
    *devid = f->payload[0];
    *pin = f->payload[1];
    *code = f->payload[2];
    return true;
}

bool gplink_unpack_analog_config(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *enable) {
    if (!f || f->type != GPLINK_TYPE_ANALOG_CONFIG || f->len != 3) return false;
    if (!devid || !pin || !enable) return false;
    *devid = f->payload[0];
    *pin = f->payload[1];
    *enable = f->payload[2] ? 1 : 0;
    return true;
}

bool gplink_unpack_analog_read(const gplink_frame *f, uint8_t *devid, uint8_t *count, uint8_t *pins_out, uint16_t *values_out, uint8_t maxCount) {
    if (!f || f->type != GPLINK_TYPE_ANALOG_READ || f->len < 2) return false;
    if (!devid || !count || !pins_out || !values_out) return false;
    uint8_t c = f->payload[1];
    if (c > maxCount) return false;
    if (f->len != (uint8_t)(2 + 3 * c)) return false;
    *devid = f->payload[0];
    *count = c;
    for (uint8_t i = 0; i < c; i++) {
        pins_out[i] = f->payload[2 + 3 * i];
        values_out[i] = gplink_get_u16(&f->payload[3 + 3 * i]);
    }
    return true;
}

bool gplink_unpack_rumble(const gplink_frame *f, uint8_t *devid, uint8_t *weak, uint8_t *strong, uint16_t *duration_ms) {
    if (!f || f->type != GPLINK_TYPE_RUMBLE_SET || f->len != 5) return false;
    if (!devid || !weak || !strong || !duration_ms) return false;
    *devid = f->payload[0];
    *weak = f->payload[1];
    *strong = f->payload[2];
    *duration_ms = gplink_get_u16(&f->payload[3]);
    return true;
}

bool gplink_unpack_player_led(const gplink_frame *f, uint8_t *devid, uint8_t *mask) {
    if (!f || f->type != GPLINK_TYPE_PLAYER_LED_SET || f->len != 2) return false;
    if (!devid || !mask) return false;
    *devid = f->payload[0];
    *mask = f->payload[1];
    return true;
}

bool gplink_unpack_battery(const gplink_frame *f, uint8_t *devid, uint8_t *pct, uint8_t *charging) {
    if (!f || f->type != GPLINK_TYPE_BATTERY_REPORT || f->len != 3) return false;
    if (!devid || !pct || !charging) return false;
    *devid = f->payload[0];
    *pct = f->payload[1];
    *charging = f->payload[2];
    return true;
}

size_t gplink_pack_pin_caps_req(uint8_t *payload_out) {
    (void)payload_out;
    return 0;
}

size_t gplink_pack_pin_caps_rsp(const char *name, uint8_t name_len, uint8_t count, const uint8_t *pins, const uint8_t *caps, uint8_t *payload_out) {
    if (name_len > 32 || count > 70) return 0;
    size_t total = (size_t)1 + name_len + 1 + (size_t)2 * count;
    if (total > GPLINK_MAX_PAYLOAD) return 0;
    if (!payload_out) return 0;
    if (name_len > 0 && !name) return 0;
    if (count > 0 && (!pins || !caps)) return 0;
    payload_out[0] = name_len;
    for (uint8_t i = 0; i < name_len; i++) payload_out[1 + i] = (uint8_t)name[i];
    payload_out[1 + name_len] = count;
    for (uint8_t i = 0; i < count; i++) {
        payload_out[1 + name_len + 1 + 2 * i] = pins[i];
        payload_out[1 + name_len + 1 + 2 * i + 1] = caps[i];
    }
    return total;
}

bool gplink_unpack_pin_caps_rsp(const gplink_frame *f, char *name_out, uint8_t *name_len, uint8_t *count, uint8_t *pins_out, uint8_t *caps_out) {
    if (!f || f->type != GPLINK_TYPE_PIN_CAPS_RSP || f->len < 2) return false;
    if (!name_out || !name_len || !count || !pins_out || !caps_out) return false;
    uint8_t nl = f->payload[0];
    if (nl > 32) return false;
    if (f->len < (uint8_t)(1 + nl + 1)) return false;
    uint8_t c = f->payload[1 + nl];
    if (c > 70) return false;
    size_t total = (size_t)1 + nl + 1 + (size_t)2 * c;
    if (total != f->len) return false;
    for (uint8_t i = 0; i < nl; i++) name_out[i] = (char)f->payload[1 + i];
    name_out[nl] = '\0';
    *name_len = nl;
    *count = c;
    for (uint8_t i = 0; i < c; i++) {
        pins_out[i] = f->payload[1 + nl + 1 + 2 * i];
        caps_out[i] = f->payload[1 + nl + 1 + 2 * i + 1];
    }
    return true;
}

size_t gplink_pack_test_configure(uint8_t testId, uint8_t pin, uint8_t function, uint16_t param1, uint16_t param2, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = testId;
    payload_out[1] = pin;
    payload_out[2] = function;
    gplink_put_u16(&payload_out[3], param1);
    gplink_put_u16(&payload_out[5], param2);
    return 7;
}

bool gplink_unpack_test_configure(const gplink_frame *f, uint8_t *testId, uint8_t *pin, uint8_t *function, uint16_t *param1, uint16_t *param2) {
    if (!f || f->type != GPLINK_TYPE_TEST_CONFIGURE || f->len != 7) return false;
    if (!testId || !pin || !function || !param1 || !param2) return false;
    *testId = f->payload[0];
    *pin = f->payload[1];
    *function = f->payload[2];
    *param1 = gplink_get_u16(&f->payload[3]);
    *param2 = gplink_get_u16(&f->payload[5]);
    return true;
}

size_t gplink_pack_test_result(uint8_t testId, uint8_t status, uint16_t value, uint16_t count, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = testId;
    payload_out[1] = status;
    gplink_put_u16(&payload_out[2], value);
    gplink_put_u16(&payload_out[4], count);
    return 6;
}

bool gplink_unpack_test_result(const gplink_frame *f, uint8_t *testId, uint8_t *status, uint16_t *value, uint16_t *count) {
    if (!f || f->type != GPLINK_TYPE_TEST_RESULT || f->len != 6) return false;
    if (!testId || !status || !value || !count) return false;
    *testId = f->payload[0];
    *status = f->payload[1];
    *value = gplink_get_u16(&f->payload[2]);
    *count = gplink_get_u16(&f->payload[4]);
    return true;
}

size_t gplink_pack_feature_req(uint8_t feature, uint8_t *payload_out) {
    if (!payload_out) return 0;
    payload_out[0] = feature;
    return 1;
}

size_t gplink_pack_feature_ack_identity(const char *version, uint8_t verLen, uint8_t radio, uint8_t *payload_out) {
    if (!payload_out) return 0;
    if (verLen > GPLINK_TEST_VER_MAX) return 0;
    if (verLen > 0 && !version) return 0;
    payload_out[0] = GPLINK_FEATURE_IDENTITY;
    payload_out[1] = verLen;
    for (uint8_t i = 0; i < verLen; i++) payload_out[2 + i] = (uint8_t)version[i];
    payload_out[2 + verLen] = radio;
    return (size_t)3 + verLen;
}

bool gplink_unpack_feature_ack_identity(const gplink_frame *f, uint8_t *feature, char *ver_out, uint8_t *ver_len, uint8_t verMax, uint8_t *radio) {
    if (!f || f->type != GPLINK_TYPE_FEATURE_ACK || f->len < 3) return false;
    if (!feature || !ver_out || !ver_len || !radio) return false;
    if (f->payload[0] != GPLINK_FEATURE_IDENTITY) return false;
    uint8_t vl = f->payload[1];
    if (vl > GPLINK_TEST_VER_MAX || vl > verMax) return false;
    if (f->len != (uint8_t)(3 + vl)) return false;
    *feature = f->payload[0];
    for (uint8_t i = 0; i < vl; i++) ver_out[i] = (char)f->payload[2 + i];
    ver_out[vl] = '\0';
    *ver_len = vl;
    *radio = f->payload[2 + vl];
    return true;
}
