# Upstream issue draft — GP-Link Companion (do not commit as-is; paste to GitHub)

Title: New add-on: GP-Link Companion (UART GPIO/analog expander + tester)

Body:

Proposing a new add-on that pairs the RP2040 board with an ESP32
companion over UART (2 Mbaud, COBS framing, CRC32, heartbeat
supervision). The companion acts as a GPIO expander (discovery + pin
table), analog front-end (sticks, triggers, deadzones, smoothing,
calibration), and self-test target (hold/toggle/sweeps/PWM with a web
Tester). Companion firmware is a separate PlatformIO project. Transport
is versioned and extensible — the intent is to extend GPLink with other
features in the future.

On companion boards: the protocol is board-agnostic and most boards can
serve as a companion (Mega, Pico/W, Blue Pill, Uno with caveats — see
the hardware notes), but nearly all testing so far has been done with
an ESP32 DevKit. Other boards are porting targets, not tested claims.

Planned PR: a single add-on PR covering transport, digital/discovery,
analog + triggers, tester UI + results endpoints, and the docs page,
since it is one feature (companion expander) end to end. If you would
rather review it in pieces, it splits cleanly into (1) transport +
digital/discovery, (2) analog + triggers, (3) tester UI + results
endpoints — just say the word. Tested on RP2040 Advanced Breakout +
ESP32-DevKit with hardware loopback, DAC sweeps, and link-down failsafe
(neutral outputs on sustained loss). Open questions for maintainers:
acceptable home for the companion project (extras/ vs separate repo?),
and whether the docs page belongs in this PR or the website repo.
