# ESP32 companion (GP-Link bridge)

Gives radio-less RP2040 boards Bluetooth via a companion ESP32 over the
GP-Link protocol (`docs/superpowers/specs/2026-09-24-gp-link-design.md`).

## M1 (built): UART link + GPIO expander

- Answers `HELLO` (caps `COMPANION_GPIO`) and `PIN_CAPS_REQ` with the
  Classic-ESP32 pin table (`src/companion_pins.cpp`, §6c capability bits).
- Honors `GPIO_CONFIG` (validates pins, NAKs violations: `1`=not-a-pin,
  `2`=output-on-input-only), pushes `GPIO_READ` masks on change at
  500 Hz, applies `GPIO_WRITE` levels, heartbeats symmetrically.
- Console log on USB serial @115200 (boot banner, link UP/DOWN, NAKs).

Console commands (type a letter + Enter): `t` toggles the input
self-test (synthetic press/release on the first configured input pin
every second — proves the ESP32→main-board path with no wiring);
`g` sends one manual GPIO_READ frame with its pack/encode/write sizes
printed (isolates the send path itself).

M2 adds Bluepad32 → `INPUT_STATE` (wireless pads). Device mode (BT HID
output) and the HTTP tunnel profile (§10) are specified but deferred.

## Build (PlatformIO, Arduino framework)

```sh
cd extras/esp32-companion
python3 -m platformio run -e devkit   # regular classic-ESP32 dev board
python3 -m platformio run -e r32      # ESPDUINO-32 / Wemos D1 R32
```

`platform = espressif32@7.0.1` is pinned; the platform, framework and
toolchain are cached, so the build works offline. The shared codec
(`../gp-link`) and CRC32 (`../../lib/CRC32`) are consumed in place via
`file://` lib deps — one source of truth, no copies, no dialect drift
(the same files compile under Pico SDK `-fno-exceptions`, ESP-IDF and
Arduino).

## Wire to the RP2040 main board

Link UART defaults (override with `-D` in `platformio.ini`):

| Companion (ESP32) | RP2040 main board |
|---|---|
| GPIO16 (RX) | GP-Link TX pin (default GP8) |
| GPIO17 (TX) | GP-Link RX pin (default GP9) |
| GND | GND |

Baud 2 Mbaud 8N1 both ends. Enable the GPLink addon in webconfig with
the matching pair, reboot both boards to gamepad mode, and watch the
companion console: `GPLink: link UP`, then `GPIO_CONFIG` lines as the
main board configures each mapped pin.

## Flash

```sh
python3 -m platformio run -e devkit -t upload   # + --upload-port /dev/ttyUSB0
python3 -m platformio device monitor -e devkit  # console @115200
```

## Host tests (pin table)

```sh
bash extras/esp32-companion/tests/run_tests.sh
```

Validates the PIN_CAPS table against the researched per-pin rules
(reserved absent, strapping/ADC/input-only verdicts, sorted order).

## Loopback self-check (documented procedure, no extra code)

Proves a wire between any two companion pins with no main board involved:

1. Jumper the two pins (e.g. GPIO25 → GPIO32).
2. Drive one side: send `TEST_CONFIGURE` (hold high / toggle cycle), or
   for a DAC pin use console `d` (DAC sweep on GPIO25).
3. Watch the other side: its `GPIO_READ` mask bits (digital) or
   `ANALOG_READ` values (ADC pin) must follow. With a companion console
   open, `x` lists active tests and the `GPLink: TX/RX` frame tap shows
   both directions.
4. Stop with `TEST_CONFIGURE` function `0xFF` (or console toggles off);
   all tests also abort automatically if the link drops.

## Self-test protocol (TEST_CONFIGURE 0x16 / TEST_RESULT 0x17)

Session-only pin exerciser (never persisted, never runs while link is
down; every test aborts silently on link drop). One test per pin,
max 8 concurrent; re-configuring a pin replaces silently; over-cap
answers busy. Simulate family (0x00 hold low, 0x01 hold high, 0x02
toggle cycle, 0x03/0x04/0x05 sweep up/down/triangle) feeds the normal
`GPIO_READ`/`ANALOG_READ` pipelines (analog sweeps are synthetic, so
every board qualifies — no DAC needed); drive family (0x0A/0x0B/0x0C
low/high/cycle, 0x0D PWM via LEDC with timer bit-bang fallback) owns
the pin electrically. Analog functions require the ADC bit, drive
requires OUTPUT (same NAK vocabulary as GPIO/ANALOG_CONFIG); reserved
and link-UART pins always refuse. `0xFF` stops one test (or all with
testId `0xFF`). Results carry running/done/aborted/bad-pin/
unsupported/busy status; completion reports once, long runners are
observed through the normal streams. Identity (`FEATURE_REQ 0x0B` /
`FEATURE_ACK 0x0C`, feature `0x01`) reports firmware version +
radio byte (bit0 WiFi, bit1 BT, M2-owned); the main board queries on
every HELLO and the companion re-announces on radio change.
