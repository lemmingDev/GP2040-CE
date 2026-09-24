// extras/gp-link/tests/test_gplink_codec.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gplink.h"
#include "CRC32.h"

static bool feed_all(gplink_decoder *dec, const uint8_t *wire, size_t n, gplink_frame *out) {
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (gplink_feed(dec, wire[i], out)) got = true;
    }
    return got;
}

static void test_heartbeat_roundtrip() {
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x03, nullptr, 0, wire);
    assert(n > 4);
    assert(wire[0] == 0x00 && wire[n - 1] == 0x00);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (gplink_feed(&dec, wire[i], &f)) got = true;
    }
    assert(got && f.type == 0x03 && f.len == 0);
}

static void test_allzeros_payload_roundtrip() {
    uint8_t payload[16] = {0};
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x02, payload, sizeof(payload), wire);
    assert(n > 0);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (gplink_feed(&dec, wire[i], &f)) got = true;
    }
    assert(got && f.type == 0x02 && f.len == 16);
    assert(memcmp(f.payload, payload, 16) == 0);
}

static void test_oversize_rejected() {
    uint8_t big[241] = {0};
    uint8_t wire[300] = {0};
    assert(gplink_encode(0x02, big, sizeof(big), wire) == 0);
}

static void test_trailing_zero_crc_msb_roundtrip() {
    // CRC32 over this header+payload is 0x000ab41c: raw ends in 0x00, which
    // the pre-fix COBS encoder silently dropped (~1/256 frames lost).
    uint8_t payload[1] = {93};
    uint8_t hdr[5] = {GPLINK_VERSION_BYTE, 0x02, 0x00, 0x01, 93};
    assert((CRC32::calculate(hdr, (uint16_t)sizeof(hdr)) >> 24) == 0x00);
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x02, payload, sizeof(payload), wire);
    assert(n > 0);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    assert(feed_all(&dec, wire, n, &f));
    assert(f.type == 0x02 && f.len == 1 && f.payload[0] == 93);
}

static void test_trailing_zero_payload_roundtrip() {
    // Payload itself ends in a zero byte, and CRC32 over header+payload is
    // 0x00aaf378 so raw also ends in 0x00: both trailing-zero paths at once.
    uint8_t payload[2] = {192, 0};
    uint8_t hdr[6] = {GPLINK_VERSION_BYTE, 0x02, 0x00, 0x02, 192, 0};
    assert((CRC32::calculate(hdr, (uint16_t)sizeof(hdr)) >> 24) == 0x00);
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x02, payload, sizeof(payload), wire);
    assert(n > 0);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    assert(feed_all(&dec, wire, n, &f));
    assert(f.type == 0x02 && f.len == 2);
    assert(memcmp(f.payload, payload, 2) == 0);
}

// Independent standard-COBS encoder for building wire vectors by hand
// (cross-checks the real decoder without reusing gplink_encode).
static size_t test_cobs_wrap(const uint8_t *raw, uint16_t rawLen, uint8_t *out) {
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
    if (rawLen > 0 && raw[rawLen - 1] == 0) out[w++] = 0x01;
    out[w++] = 0x00;
    return w;
}

static void test_bad_version_rejected() {
    // Same body with a future version byte and a VALID CRC must be rejected
    // by the version gate (not merely by CRC).
    uint8_t raw[9] = {0x20, 0x02, 0x00, 0x01, 0x55, 0, 0, 0, 0};
    uint32_t crc = CRC32::calculate(raw, 5);
    raw[5] = (uint8_t)(crc & 0xFF);
    raw[6] = (uint8_t)((crc >> 8) & 0xFF);
    raw[7] = (uint8_t)((crc >> 16) & 0xFF);
    raw[8] = (uint8_t)((crc >> 24) & 0xFF);
    uint8_t wire[64] = {0};
    size_t n = test_cobs_wrap(raw, sizeof(raw), wire);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    assert(!feed_all(&dec, wire, n, &f));
    // Positive control: identical frame with the correct version decodes.
    raw[0] = GPLINK_VERSION_BYTE;
    crc = CRC32::calculate(raw, 5);
    raw[5] = (uint8_t)(crc & 0xFF);
    raw[6] = (uint8_t)((crc >> 8) & 0xFF);
    raw[7] = (uint8_t)((crc >> 16) & 0xFF);
    raw[8] = (uint8_t)((crc >> 24) & 0xFF);
    n = test_cobs_wrap(raw, sizeof(raw), wire);
    gplink_decoder_init(&dec);
    assert(feed_all(&dec, wire, n, &f));
    assert(f.type == 0x02 && f.len == 1 && f.payload[0] == 0x55);
}

int main() {
    test_heartbeat_roundtrip();
    test_allzeros_payload_roundtrip();
    test_oversize_rejected();
    test_trailing_zero_crc_msb_roundtrip();
    test_trailing_zero_payload_roundtrip();
    test_bad_version_rejected();
    printf("codec: all assertions passed\n");
    return 0;
}
