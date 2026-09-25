// extras/gp-link/gplink_link.h
// GP-Link UART transport policy for RP2040 (UART1, hardware UART).
// This file is portable (no Pico SDK dependency) so host tests can cover it;
// the hardware register poking lives in gplink_uart_rp2040.cpp (firmware-only).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Link defaults (spec section 2): UART1, TX=GP8/RX=GP9, 2 Mbaud 8N1.
#define GPLINK_UART_DEFAULT_INST 1
#define GPLINK_UART_DEFAULT_TX 8
#define GPLINK_UART_DEFAULT_RX 9
#define GPLINK_UART_DEFAULT_BAUD 2000000u
#define GPLINK_UART_FALLBACK_BAUD 921600u

// Liveness (spec section 7): heartbeat every 500 ms idle, 2 s silence down.
#define GPLINK_HEARTBEAT_INTERVAL_MS 500u
#define GPLINK_LINK_TIMEOUT_MS 2000u

// RP2040 GPIO-mux tables (datasheet; GP24/29 excluded, Pico onboard use).
// UART0: TX in {0,12,16,28}, RX in {1,13,17} (29 is VSYS/ADC3).
// UART1: TX in {4,8,20}, RX in {5,9,21} (24 is the onboard LED).
// UART0 pins 0/1 are broken out on the Advanced Breakout expansion header.
bool gplink_uart0_pins_valid(uint8_t tx, uint8_t rx);
bool gplink_uart1_pins_valid(uint8_t tx, uint8_t rx);
// Instance-generic validator: uart is 0 or 1; the selectable pair set.
bool gplink_uart_pins_valid(uint8_t uart, uint8_t tx, uint8_t rx);

// Baud-rate tolerance: fractional-divider rounding (a few %) is fine,
// anything beyond +/-3% risks interop failure at 2 Mbaud.
bool gplink_baud_ok(uint32_t actual, uint32_t requested);

struct gplink_link {
    uint32_t last_rx_ms;
    uint32_t last_tx_ms;
    uint8_t last_seq;
    bool have_seq;
};

void gplink_link_init(gplink_link *l, uint32_t now_ms);
void gplink_link_on_rx(gplink_link *l, uint8_t seq, uint32_t now_ms);
void gplink_link_on_tx(gplink_link *l, uint32_t now_ms);
bool gplink_link_alive(const gplink_link *l, uint32_t now_ms);
bool gplink_link_heartbeat_due(const gplink_link *l, uint32_t now_ms);
// True when seq is the accepted next value (or first frame ever seen).
bool gplink_link_seq_in_order(const gplink_link *l, uint8_t seq);

// Hardware backend (gplink_uart_rp2040.cpp, firmware-only / Pico SDK).
// Writes are non-blocking and return bytes accepted; a short write means
// the caller drops the frame (the COBS decoder resynchronises on the next
// delimiter and the CRC rejects the runt). Reads return -1 when empty.
// NOTE: selecting UART0 shares pins with the default debug console (GP0/GP1);
// the firmware must keep stdio off the chosen pins (wiring task).
bool gplink_uart_init(uint8_t uart, uint8_t tx, uint8_t rx, uint32_t baud);
size_t gplink_uart_write(const uint8_t *data, size_t len);
int gplink_uart_read(void);
// Jumper continuity self-test: momentarily drops both pins to SIO, wiggles
// TX against a pulled-down RX, then restores the UART mux. Returns 1 when
// the pins are connected, 0 when open (RX never follows high), 2 when the
// line is externally driven (a live peer idles high — the loopback case
// this test was built for doesn't apply). -1 when never initialised.
int gplink_uart_loopback_test(void);
