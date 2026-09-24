// extras/gp-link/gplink_uart_rp2040.cpp
// Firmware-only GP-Link UART backend for RP2040 (hardware UART, either instance).
// NOT compiled by the host tests (requires the Pico SDK); validate on device.
//
// Transport policy: fully non-blocking. Writes return the bytes accepted;
// a short write means the TX FIFO filled mid-frame, in which case the caller
// drops the frame — the receiver's COBS decoder resynchronises on the next
// 0x00 delimiter and the CRC rejects the runt, so the link self-heals.
// Reads are polled single bytes (-1 when the RX FIFO is empty); feed each
// byte to gplink_feed() from the codec.
#include "gplink_link.h"

#ifdef PICO_ON_DEVICE
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define GPLINK_UART_INST s_uart
static uart_inst_t *s_uart = NULL;
static uint8_t s_tx = 0;
static uint8_t s_rx = 0;

bool gplink_uart_init(uint8_t uart, uint8_t tx, uint8_t rx, uint32_t baud) {
    if (!gplink_uart_pins_valid(uart, tx, rx)) return false;
    s_uart = (uart == 0) ? uart0 : uart1;
    s_tx = tx;
    s_rx = rx;
    uint actual = uart_init(GPLINK_UART_INST, baud);
    if (!gplink_baud_ok(actual, baud)) {
        uart_deinit(GPLINK_UART_INST);
        return false;
    }
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rx, GPIO_FUNC_UART);
    uart_set_format(GPLINK_UART_INST, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(GPLINK_UART_INST, false, false);
    uart_set_fifo_enabled(GPLINK_UART_INST, true);
    return true;
}

size_t gplink_uart_write(const uint8_t *data, size_t len) {
    if (s_uart == NULL) return 0;
    size_t n = 0;
    while (n < len && uart_is_writable(GPLINK_UART_INST)) {
        uart_putc(GPLINK_UART_INST, (char)data[n]);
        n++;
    }
    return n;
}

int gplink_uart_read(void) {
    if (s_uart == NULL || !uart_is_readable(GPLINK_UART_INST)) return -1;
    return uart_getc(GPLINK_UART_INST);
}

bool gplink_uart_loopback_test(void) {
    if (s_uart == NULL || s_tx == s_rx) return false;
    // Drop both pins to SIO: TX push-pull out, RX in with pull-down so an
    // unconnected pin reads a stable 0. A jumper must pull RX up with TX.
    gpio_init(s_tx);
    gpio_init(s_rx);
    gpio_set_dir(s_tx, GPIO_OUT);
    gpio_set_dir(s_rx, GPIO_IN);
    gpio_pull_down(s_rx);
    gpio_put(s_tx, 0);
    busy_wait_us(50);
    bool low_ok = (gpio_get(s_rx) == 0);
    gpio_put(s_tx, 1);
    busy_wait_us(50);
    bool high_ok = (gpio_get(s_rx) != 0);
    // Restore the UART mux (peripheral config is untouched, only pins moved).
    gpio_disable_pulls(s_rx);
    gpio_set_function(s_tx, GPIO_FUNC_UART);
    gpio_set_function(s_rx, GPIO_FUNC_UART);
    return low_ok && high_ok;
}
#endif
