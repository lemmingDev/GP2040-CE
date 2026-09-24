// extras/gp-link/tests/test_gplink_link.cpp
// Host tests for GP-Link RP2040 UART transport policy (gplink_link.cpp):
// UART1 pin validation, baud tolerance, and link supervision timing.
#include "gplink_link.h"
#include "gplink.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main() {
    // UART1 mux table (RP2040 datasheet; GP24 excluded: Pico onboard LED)
    CHECK(gplink_uart1_pins_valid(20, 21));
    CHECK(gplink_uart1_pins_valid(4, 5));
    CHECK(gplink_uart1_pins_valid(8, 9));
    CHECK(gplink_uart1_pins_valid(20, 5));   // any TX/RX cross-combination
    CHECK(!gplink_uart1_pins_valid(24, 25)); // reserved onboard pins
    CHECK(!gplink_uart1_pins_valid(0, 1));   // UART0 pair, not UART1
    CHECK(!gplink_uart1_pins_valid(21, 20)); // swapped: RX pin as TX
    CHECK(!gplink_uart1_pins_valid(20, 20)); // TX == RX
    CHECK(!gplink_uart1_pins_valid(20, 22)); // 22 has no UART function

    // UART0 mux table (TX in {0,12,16,28}, RX in {1,13,17}; 29 excluded: VSYS ADC)
    CHECK(gplink_uart0_pins_valid(0, 1));   // Advanced Breakout expansion header
    CHECK(gplink_uart0_pins_valid(12, 13));
    CHECK(gplink_uart0_pins_valid(16, 17));
    CHECK(gplink_uart0_pins_valid(28, 13)); // any TX/RX cross-combination
    CHECK(!gplink_uart0_pins_valid(28, 29)); // 29 is VSYS/ADC3 on Pico boards
    CHECK(!gplink_uart0_pins_valid(4, 5));   // UART1 pair, not UART0
    CHECK(!gplink_uart0_pins_valid(1, 0));   // swapped: RX pin as TX
    CHECK(!gplink_uart0_pins_valid(0, 0));

    // Instance-generic validator: the selectable pair set per UART instance
    CHECK(gplink_uart_pins_valid(0, 0, 1));
    CHECK(gplink_uart_pins_valid(1, 20, 21));
    CHECK(!gplink_uart_pins_valid(0, 20, 21)); // UART1 pins on UART0
    CHECK(!gplink_uart_pins_valid(1, 0, 1));   // UART0 pins on UART1
    CHECK(!gplink_uart_pins_valid(2, 0, 1));   // no such instance

    // Baud tolerance: accept exact and small divisor rounding, reject drift
    CHECK(gplink_baud_ok(2000000, 2000000));
    CHECK(gplink_baud_ok(2040000, 2000000));   // +2%
    CHECK(gplink_baud_ok(1960000, 2000000));   // -2%
    CHECK(!gplink_baud_ok(2100000, 2000000));  // +5%
    CHECK(!gplink_baud_ok(921600, 2000000));   // wrong rate entirely
    CHECK(!gplink_baud_ok(2000000, 0));        // degenerate request

    // Link supervision: alive / timeout boundary (2 s silence = link down)
    gplink_link l;
    gplink_link_init(&l, 1000);
    CHECK(gplink_link_alive(&l, 1000));
    CHECK(gplink_link_alive(&l, 3000));   // exactly 2000 ms still alive
    CHECK(!gplink_link_alive(&l, 3001));  // 2001 ms = down
    gplink_link_on_rx(&l, 7, 5000);
    CHECK(gplink_link_alive(&l, 7000));
    // uint32_t millis wrap-around stays correct (0x100 + 0x600 = 1792 ms)
    gplink_link_init(&l, 0xFFFFFF00u);
    CHECK(gplink_link_alive(&l, 0x00000600u));
    CHECK(!gplink_link_alive(&l, 0x000006D1u)); // 0x100 + 0x6D1 = 2001 ms

    // Heartbeat: due every 500 ms of TX silence
    gplink_link_init(&l, 0);
    CHECK(!gplink_link_heartbeat_due(&l, 499));
    CHECK(gplink_link_heartbeat_due(&l, 500));
    gplink_link_on_tx(&l, 500);
    CHECK(!gplink_link_heartbeat_due(&l, 999));
    CHECK(gplink_link_heartbeat_due(&l, 1000));

    // Sequence order: first frame accepted, +1 accepted, gaps/duplicates flagged
    gplink_link_init(&l, 0);
    CHECK(gplink_link_seq_in_order(&l, 44)); // no history: accept
    gplink_link_on_rx(&l, 44, 10);
    CHECK(gplink_link_seq_in_order(&l, 45));
    CHECK(!gplink_link_seq_in_order(&l, 47)); // gap
    CHECK(!gplink_link_seq_in_order(&l, 44)); // duplicate
    gplink_link_on_rx(&l, 0xFF, 20);
    CHECK(gplink_link_seq_in_order(&l, 0x00)); // wrap 0xFF -> 0x00 is in order
    CHECK(!gplink_link_seq_in_order(&l, 0x02));

    // Sequence numbers ride the wire: encode_seq stamps raw[2], decode exposes it
    {
        uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
        uint8_t wire[GPLINK_ENCODED_MAX + 8];
        size_t n = gplink_encode_seq(0x02, payload, sizeof(payload), 0x5A, wire);
        CHECK(n > 0 && n <= (size_t)(GPLINK_ENCODED_MAX + 8));
        gplink_decoder dec;
        gplink_decoder_init(&dec);
        gplink_frame f;
        bool got = false;
        for (size_t i = 0; i < n && !got; i++) got = gplink_feed(&dec, wire[i], &f);
        CHECK(got);
        CHECK(f.type == 0x02);
        CHECK(f.seq == 0x5A);
        CHECK(f.len == sizeof(payload));
        // legacy encode still stamps seq 0
        n = gplink_encode(0x03, nullptr, 0, wire);
        gplink_decoder_init(&dec);
        got = false;
        for (size_t i = 0; i < n && !got; i++) got = gplink_feed(&dec, wire[i], &f);
        CHECK(got && f.seq == 0);
    }

    if (failures == 0) printf("test_gplink_link PASS\n");
    return failures ? 1 : 0;
}
