# GP-Link Companion

Purpose: This add-on turns a second microcontroller (an ESP32 "companion" board) into a GPIO and analog expander for GP2040-CE. The main (RP2040) board talks to the companion over UART using the GP-Link protocol: the companion reports button inputs and analog readings, and the main board mirrors outputs (player LEDs, rumble) back. This is useful when you run out of pins on the main board, want analog sticks and triggers from external ADCs, or want to exercise companion pins with built-in self-tests.

GP-Link is a transport. It carries digital GPIO, analog channels, and test sessions today; it is designed to carry more (such as a future wireless host) later without changing the wire protocol's framing.

The reference companion is the ESP32 — nearly all testing has been done on ESP32 DevKit hardware. The protocol itself is board-agnostic and other boards (Mega, Pico/W, Blue Pill, Uno, each with caveats) are porting targets; see the companion hardware notes for the board matrix.

![GP2040-CE Configurator - GP-Link Companion](assets/images/gpc-add-ons-gplink-companion.png)

The companion side is a separate PlatformIO project with its own firmware, console, and pin table. Everything below describes the main-board web configurator; flashing and wiring the companion is covered under Hardware.

## Web Configurator Options

The add-on has three sections: Digital, Analog, and Tester. Each has its own enable switch.

### Digital

- `UART Instance` - Which RP2040 UART talks to the companion (UART0 shares GPIO 0/1 with the debug console; UART1 is recommended).
- `GP-Link TX GPIO Pin` / `GP-Link RX GPIO Pin` - Only pin pairs with a hardware UART function can be selected.
- `Baud Rate` - 2000000 by default, 921600 as a fallback for marginal wiring. Both sides must match.
- `Test Link & Discover Pins` - Checks TX/RX continuity and asks the companion for its pin capabilities. Discovered pins can be assigned below or entered manually.
- Pin table - Assign a gamepad action, direction (input/output), pull, and invert flag per companion pin. Tags: ADC = analog-capable with wireless on, ADC* = analog needs wireless off, in-only / out-only = digital direction limits, strapping pins are marked and can prevent boot if pulled the wrong way.
- `Save Pin Assignments` - Persists the table.

Gamepad outputs (buttons, sticks, rumble/LED mirror) only run in gamepad mode. The link stays up in webconfig, so live values and discovery keep working there.

### Analog

Maps companion ADC pins (by companion GPIO number, -1 = none) to sticks and triggers. Values arrive normalized full-range.

- Stick pins (`Left/Right Stick X/Y Companion Pin`), per-axis invert switches, and `Calibrate Center` (leave sticks untouched, capture, then save).
- Axis deadzones: per-axis inner/outer deadzone sliders, radial per-stick deadzone, and force-circularity.
- Per-axis Min/Max with one-click capture (`Set ... Min/Max` samples the live value).
- Per-stick smoothing (EMA factor) for noisy ADCs.
- Triggers tab: LT/RT companion pins (disabled = -1), rest/full calibration (`Set Rest` at rest, `Set Full` at full press, then save), per-trigger invert. Triggers drive LT/RT on analog-trigger-capable drivers (XInput, PS4, PS3); other drivers treat any nonzero value as pressed.
- `Save Analog Settings` - Persists everything on this tab.

Live raw and shaped values are shown per stick and trigger while the link is up. If the feed is unreachable, check the link first.

### Tester

Runs pin self-tests on the companion: hold, toggle, sweeps, triangle, drive levels, and PWM, plus stop. Tests are session-only: nothing persists, running tests keep going in gamepad mode, and everything stops if the companion resets, power-cycles, or the link drops for more than about 2 seconds.

- `Pin` (0-63; pins 16/17 are the link UART itself and cannot be tested), `Function` dropdown, and two parameter boxes that relabel per function (`Period ms` + `Count` for toggle, `Step ms` + `Repeats` for sweeps, `Frequency Hz` + `Duty %` for PWM; a count/repeat of 0 means forever).
- Presets (`Slow sweep`, `Quick toggle`, `PWM half`) fill in function and params; the pin is always your choice, and Run stays disabled until one is picked.
- `Run test` / `Stop` (per row) / `Stop all` / `Clear` (empties the results table).
- Results table shows test id, pin, function, status (`Running`, `Done`, `Aborted`, `Bad pin`, `Unsupported`, `Busy`), last value, and count.

## Hardware

### Requirements

- An ESP32 dev board flashed with the companion firmware (PlatformIO targets: `devkit` at 2 Mbaud, `devkit-921600` fallback for marginal wiring, `r32` for ESPDUINO-32 / Wemos D1 R32 Analogue boards).
- Three wires minimum: companion GPIO16 to main-board RX, GPIO17 to main-board TX, and GND to GND. A common ground is required.
- Level shifting for 5 V companions (Uno, Mega): RP2040 GPIO is **not** 5 V tolerant, so the companion-TX to main-RX line must be divided down — 1 kΩ series + 1.2 kΩ to GND (≈2.73 V), kept off GPIO26–29. The RP2040-TX to companion-RX direction usually works direct into AVR (3.0 V threshold) but with thin margin — add a 74AHCT125/TXU gate for noisy builds. 3.3 V companions (ESP32, Pico, Blue Pill UART pins) connect direct, no divider needed. Full board matrix, verified 2 Mbaud figures, and what to avoid (BSS138, TXB shifters) live in [the companion hardware notes](../gplink-companion-hardware.md).
- Matching baud rate on both sides.

### Installation

1. Flash the companion with the PlatformIO project and confirm its USB console shows the boot banner and link chatter.
2. Wire TX/RX/GND as above and select the matching UART instance, pins, and baud on the GP-Link Companion - Digital tab.
3. Run `Test Link & Discover Pins`: expect PASS with continuity and a pin count.
4. Assign pins, map analog channels, and save. Saved settings apply after reboot.

## Miscellaneous Notes

- If the link drops for more than about 2 seconds while in gamepad mode, the main board releases everything the companion was driving: buttons released, sticks centered, triggers to 0. Short blips below the timeout hold last-known values rather than glitching.
- The companion keeps no configuration across its own reset or power cycle except the persisted pin table; tests never survive a companion reboot by design.
- Unmapped stick pins read center, unmapped trigger pins read released.
- A trigger pin shared with a stick axis cannot satisfy both at once; sticks win in the link-down failsafe. Prefer one pin per function.
