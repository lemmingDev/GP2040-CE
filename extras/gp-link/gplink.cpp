// extras/gp-link/gplink.cpp
#include "gplink.h"
#include "CRC32.h"

size_t gplink_encode(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out) {
    if (payload_len > GPLINK_MAX_PAYLOAD) return 0;
    uint8_t raw[4 + GPLINK_MAX_PAYLOAD + 4];
    raw[0] = GPLINK_VERSION_BYTE;
    raw[1] = type;
    raw[2] = 0; // seq: managed by transport users, 0 here
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
