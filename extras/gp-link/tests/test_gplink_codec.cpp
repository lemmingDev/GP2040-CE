// extras/gp-link/tests/test_gplink_codec.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gplink.h"

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

int main() {
    test_heartbeat_roundtrip();
    test_allzeros_payload_roundtrip();
    test_oversize_rejected();
    printf("codec: all assertions passed\n");
    return 0;
}
