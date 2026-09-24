// extras/gp-link/gplink_link.cpp
#include "gplink_link.h"

bool gplink_uart0_pins_valid(uint8_t tx, uint8_t rx) {
    bool tx_ok = (tx == 0 || tx == 12 || tx == 16 || tx == 28);
    bool rx_ok = (rx == 1 || rx == 13 || rx == 17);
    return tx_ok && rx_ok && tx != rx;
}

bool gplink_uart1_pins_valid(uint8_t tx, uint8_t rx) {
    bool tx_ok = (tx == 4 || tx == 8 || tx == 20);
    bool rx_ok = (rx == 5 || rx == 9 || rx == 21);
    return tx_ok && rx_ok && tx != rx;
}

bool gplink_uart_pins_valid(uint8_t uart, uint8_t tx, uint8_t rx) {
    if (uart == 0) return gplink_uart0_pins_valid(tx, rx);
    if (uart == 1) return gplink_uart1_pins_valid(tx, rx);
    return false;
}

bool gplink_baud_ok(uint32_t actual, uint32_t requested) {
    if (requested == 0 || actual == 0) return false;
    uint32_t lo = requested - requested / 33; // ~-3%
    uint32_t hi = requested + requested / 33; // ~+3%
    return actual >= lo && actual <= hi;
}

void gplink_link_init(gplink_link *l, uint32_t now_ms) {
    l->last_rx_ms = now_ms;
    l->last_tx_ms = now_ms;
    l->last_seq = 0;
    l->have_seq = false;
}

void gplink_link_on_rx(gplink_link *l, uint8_t seq, uint32_t now_ms) {
    l->last_rx_ms = now_ms;
    l->last_seq = seq;
    l->have_seq = true;
}

void gplink_link_on_tx(gplink_link *l, uint32_t now_ms) {
    l->last_tx_ms = now_ms;
}

bool gplink_link_alive(const gplink_link *l, uint32_t now_ms) {
    return (now_ms - l->last_rx_ms) <= GPLINK_LINK_TIMEOUT_MS;
}

bool gplink_link_heartbeat_due(const gplink_link *l, uint32_t now_ms) {
    return (now_ms - l->last_tx_ms) >= GPLINK_HEARTBEAT_INTERVAL_MS;
}

bool gplink_link_seq_in_order(const gplink_link *l, uint8_t seq) {
    if (!l->have_seq) return true;
    return (uint8_t)(seq - l->last_seq) == 1;
}
