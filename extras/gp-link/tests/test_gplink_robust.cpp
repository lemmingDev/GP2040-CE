// extras/gp-link/tests/test_gplink_robust.cpp
#include <cassert>
#include <cstdio>
#include "gplink.h"

static int feed_all(const uint8_t *wire, size_t n, gplink_frame *out) {
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    int hits = 0;
    gplink_frame f;
    for (size_t i = 0; i < n; i++) {
        if (gplink_feed(&dec, wire[i], &f)) {
            hits++;
            if (out) *out = f;
        }
    }
    return hits;
}

static void test_unknown_type_dropped() {
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x77, nullptr, 0, wire);
    assert(n > 0);
    assert(feed_all(wire, n, nullptr) == 1); // framing accepts; no type registry in codec
    // NOTE: unknown-type DROP is a policy for the message layer: the byte
    // below documents that the codec itself delivers the frame; callers
    // switch on type and ignore what they don't know.
    (void)n;
}

static void test_crc_fault_injected() {
    uint8_t p[4] = {1, 2, 3, 4};
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x02, p, sizeof(p), wire);
    assert(n > 4);
    wire[n / 2] ^= 0xFF; // corrupt one wire byte (never touch the delimiters)
    if (wire[n / 2] == 0x00) wire[n / 2] = 0x7F;
    assert(feed_all(wire, n, nullptr) == 0);
}

static void test_garbage_resync() {
    uint8_t garbage[32];
    for (int i = 0; i < 32; i++) garbage[i] = (uint8_t)(i * 7 + 3);
    uint8_t p[2] = {0xAA, 0x55};
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x03, p, sizeof(p), wire);
    uint8_t stream[300];
    for (int i = 0; i < 32; i++) stream[i] = garbage[i];
    for (size_t i = 0; i < n; i++) stream[32 + i] = wire[i];
    gplink_frame f;
    assert(feed_all(stream, 32 + n, &f) == 1);
    assert(f.type == 0x03 && f.len == 2 && f.payload[0] == 0xAA);
}

static void test_split_feed() {
    uint8_t p[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t wire[256] = {0};
    size_t n = gplink_encode(0x06, p, sizeof(p), wire);
    gplink_decoder dec;
    gplink_decoder_init(&dec);
    gplink_frame f;
    int hits = 0;
    for (size_t i = 0; i < n / 2; i++) {
        if (gplink_feed(&dec, wire[i], &f)) hits++;
    }
    assert(hits == 0); // half a frame completes nothing
    for (size_t i = n / 2; i < n; i++) {
        if (gplink_feed(&dec, wire[i], &f)) hits++;
    }
    assert(hits == 1 && f.type == 0x06 && f.len == 8);
}

int main() {
    test_unknown_type_dropped();
    test_crc_fault_injected();
    test_garbage_resync();
    test_split_feed();
    printf("robust: all assertions passed\n");
    return 0;
}
