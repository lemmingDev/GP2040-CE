// extras/gp-link/tests/test_gplink_throughput.cpp
#include <cassert>
#include <chrono>
#include <cstdio>
#include "gplink.h"

int main() {
    // 500 INPUT_STATE frames (worst-case 64 B-class payload is 17 B here;
    // pad to 64 B to simulate the largest v1 traffic) looped pack ->
    // encode -> feed -> unpack, must finish far inside the 2 s budget
    // (i.e. codec cost is negligible next to the 500 Hz wire budget).
    uint8_t payload[64];
    for (int i = 0; i < 64; i++) payload[i] = (uint8_t)i;
    uint8_t wire[512] = {0};
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 500; k++) {
        size_t n = gplink_encode(0x02, payload, 64, wire);
        assert(n > 0 && n <= 256);
        gplink_decoder dec;
        gplink_decoder_init(&dec);
        gplink_frame f;
        bool got = false;
        for (size_t i = 0; i < n; i++) {
            if (gplink_feed(&dec, wire[i], &f)) got = true;
        }
        assert(got && f.len == 64);
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    printf("throughput: 500 frames in %lld ms\n", (long long)ms);
    assert(ms < 2000);
    return 0;
}
