# GP-Link: inter-board pad-data link (design)

Version: 0.1 (draft for review). Covers RP2040 ↔ ESP32-companion (UART now,
SPI later), S3 in-tree use, Pico-W and classic-ESP32 standalone later.
Direction-agnostic: any board can source inputs, sink outputs, or both.

## 1. Goals / non-goals

Goals: carry processed pad state board-to-board at BT-usable rates
(≥250 Hz, 64 B reports), plus a low-rate back-channel (rumble, player
LEDs, battery), over 2-wire UART today and SPI tomorrow. One protocol
for all topologies: companion-host (Bluepad32 → RP2040/S3), companion-
device (RP2040/S3 → BT HID), S3/Pico-W/standalone in-tree links.

Non-goals: Bluetooth pairing/bonding itself (lives in the BT stack:
Bluepad32, NimBLE, BTstack); wire security (physical link = trusted;
integrity via CRC only); audio/video.

## 2. Transports

- UART default: **2 Mbaud 8N1**, RTS/CTS optional, configurable down
  to 921600 on noisy wiring. Rate choice: RP2040 `clk_peri` is 48 MHz,
  so divisor `48M / (16 × 2M) = 1.5` is exact (0% error); 921600 gives
  +0.16% (also fine) but 4× less headroom. Budget at the operated
  500 Hz with ~74 B framed reports: ~296 kbps vs 2 Mbaud (~200 KB/s)
  ≈ 6.7× headroom; even full 1 kHz loop rate (~592 kbps) clears 3×.
  Link pacing never throttles the local 1 kHz gamepad loop — reports
  are sampled outbound, never blocking input.
- WiFi webconfig is unaffected: the link uses no network resources,
  so AP/STA/USB webconfig, STA mode and the ADC2/WiFi caveat all behave
  exactly as today with the link active.
- SPI later: RP2040 master, ESP32 slave + IRQ line, multi-MHz for
  sub-ms jitter. Framing identical; only the byte pipe changes.
- All multi-byte fields little-endian. Max frame 256 B (fits UART
  FIFOs and one lwIP pbuf if ever tunneled over TCP for testing).

## 3. Framing (transport-independent)

`[0x00] [COBS-encoded(header + payload + CRC32)] [0x00]` — COBS keeps
0x00 as unambiguous frame delimiter; CRC32 (our vendored lib/CRC32)
covers header + payload. Header (4 B): `ver(1) | type(1) | seq(1) |
len(1)`. `seq` wraps; gaps are informational only (see §7).

## 4. Versioning + capability handshake

- Protocol `major.minor` in every header `ver` nibble-pair (high 4 bits
  major, low 4 minor). Minor additions must be ignorable by older peers
  (unknown types are dropped, never NAKed); major bumps may break wire.
- On link-up each side sends   `HELLO(major, minor, caps-bitmask,
  device-id-count)`. Caps bits below are append-only; new bits get the
  next free number plus one line here. `RUMBLE=0`, `PLAYER_LEDS=1`,
  `BATTERY=2`, `IMU=3`, `MULTI_PAD=4`, `DISPLAY_HINTS=5` (reserved),
  `HTTP_TUNNEL=6` (§10), `COMPANION_GPIO=7` (§6b). Intersection =
  active feature set; a side never sends what the peer didn't advertise.
- `device-id-count` (1–4) declares how many pads this link can carry;
  Bluepad32 multi-pad arrives without re-specifying the protocol.

## 5. Message catalog (type byte)

v1 core (all bidirectional-aware, each carries a 2-bit device id 0–3):
`INPUT_STATE`, `RUMBLE_SET`, `PLAYER_LED_SET`, `BATTERY_REPORT`,
`HELLO`, `HEARTBEAT`, `FEATURE_REQ/FEATURE_ACK` (for future negotiated
extensions, e.g. IMU streams), `DEBUG_TEXT` (human-readable, never in
production timing paths). Types 0x80–0xFF reserved for experimental
extensions; 0xF0–0xFF for transport testing (ping/throughput).

Assigned v1 type numbers (backported from `extras/gp-link/gplink.h`):
`HELLO=0x01`, `INPUT_STATE=0x02`, `HEARTBEAT=0x03`,
`RUMBLE_SET=0x04`, `PLAYER_LED_SET=0x05`, `BATTERY_REPORT=0x06`,
`GPIO_CONFIG=0x07`, `GPIO_READ=0x08`, `GPIO_WRITE=0x09`,
`DEBUG_TEXT=0x0A`, `FEATURE_REQ=0x0B`, `FEATURE_ACK=0x0C`,
`PIN_CAPS_REQ=0x0D`, `PIN_CAPS_RSP=0x0E`, `HTTP_REQ=0x10`,
`HTTP_RESP=0x11`, `HTTP_FRAG=0x12`, `GPIO_NAK=0x13` (codes:
`1`=not-a-pin, `2`=output-on-input-only, `3`=not-adc-capable,
`4`=input-on-output-only).

Caps: max payload 240 B (`GPLINK_MAX_PAYLOAD`), max encoded frame
256 B (`GPLINK_ENCODED_MAX`).

## 6. Report payloads

`INPUT_STATE` v1 payload mirrors the already-processed `GamepadState`
(post-GPIO, post-addon): `buttons u32`, `dpad u8`, `lx,ly,rx,ry u16`,
`lt,rt u8`, `aux u8`. Deliberately display/input-pipeline agnostic —
GPIO mapping, profiles, macros and turbo all resolve upstream, so BT
output inherits every webconfig feature free. Future axes/sensors ride
in `FEATURE_REQ`-negotiated extension blocks, never by widening v1.

## 6b. Companion-local GPIO (extra pins on the ESP32)

Companion GPIO integrates like the existing IO-expander addons
(PCF8575 pattern), not as a special case: a companion-GPIO addon on
the main board owns virtual pins with direction/action config, and the
core loop reads them exactly like expander pins. BT pads (which only
ever produce pad-state) keep using §6 virtual-pad `INPUT_STATE`s.

- New types: `GPIO_CONFIG` (main→companion: pin, direction, pull),
  `GPIO_READ` (companion→main: pin-level bitmask per device, poll
  rate), `GPIO_WRITE` (main→companion: output levels, e.g. LEDs).
  Companion ADC sticks later ride the same addon as axis reads (new
  types only if v1's `INPUT_STATE` axes prove insufficient — prefer
  reuse).
- The main board debounces and action-maps companion pins in its own
  pipeline (same as PCF8575), so profiles, SOCD, hotkeys, macros and
  turbo apply with no companion-side input logic beyond sampling.
- Pin numbering stays source-local: companion pins are qualified by
  device id and never merged into the main board's pin numbers (no
  `Mask_t` collisions, per-board validity checks intact). The classic
  ESP32's ~34 GPIOs and 18 ADC channels are all eligible.
- Advertised via the `COMPANION_GPIO` caps bit; webconfig (§9) shows
  companion pins as a separate source section when present, configured
  like the PCF8575 pin table.
- Pure-GPIO mode is valid with no BT role at all: a companion that
  advertises only `COMPANION_GPIO` is just an expander. BT host/device
  roles are independent caps, never prerequisites.
- Classic-ESP32 pin capabilities (companion enforces; NAK violations
  with new `GPIO_NAK(pin, code)` type `0x13`, codes: `1`=not-a-pin,
  `2`=output-on-input-only, `3`=not-adc-capable):
  - ADC1 (works with WiFi): GPIO32–39 — i.e. 8 channels, and the easy
    way to add a lot more analog to a controller.
  - ADC2 (blocked while wireless runs — WiFi, and Classic BT once the
    companion hosts pads — so these carry ADC_CAPABLE without
    ADC_RADIO_SAFE): GPIO0,2,4,12–15,25–27 (10 channels).
  - Input-only, no internal pullup/down: GPIO34,35,36,39 — output
    direction rejected; inputs need external pull resistors.
- Strapping (sampled at reset only; normal GPIO after boot —
  Espressif boot-mode-selection + hardware-design docs). All five are
  usable with per-pin rules the companion enforces as warnings
  (never hard NAKs — the board already booted, so misuse risks only
  the *next* reset/flash):
  - GPIO0: LOW at reset = download mode. BOOT button territory on
    nearly every devkit — ideal as an input button to GND; keep the
    pullup. Never externally held LOW (kills normal boot).
  - GPIO2: must float/be LOW only while entering the bootloader
    (ignored in normal boot). Full GPIO + ADC2 after boot. Watch the
    onboard blue LED (DOIT V1, NodeMCU-32S): repurpose it, don't fight
    it — and a HIGH-driving peripheral here breaks *flashing*, not
    booting.
  - GPIO5: must be HIGH at reset (LOW alters SDIO-slave timing, may
    block boot). Keep pulled HIGH; never drive LOW at reset. No ADC
    on this pin — digital only.
  - GPIO12 (MTDI): the dangerous one. HIGH at reset selects 1.8 V
    flash voltage → brownout boot-loop on 3.3 V-flash modules
    (internal pulldown saves floating pins). Rule: outputs fine
    (sampled before your code runs), inputs only with pullDOWN —
    never a pullup/button-to-3V3. Also ADC2 + JTAG.
  - GPIO15 (MTDO): LOW merely silences ROM boot messages (internal
    pullup = noisy default, harmless). Emits a PWM burst at boot —
    fine for LEDs, never servos/relays. Also ADC2 + JTAG.
  - Note all of 0/2/12/15 are ADC2: with WiFi up they're digital-only
    on the companion too (same WiFi rule as S3).
- Common-board conflicts (check the schematic; clones vary): DOIT
  DEVKIT V1 + NodeMCU-32S put the blue LED on GPIO2 and BOOT on GPIO0;
  38-pin layouts (NodeMCU-32S) break out flash pins 6–11 — driving
  those kills the chip's own flash access (hard trap, not a strap
  issue); WROVER modules spend GPIO16/17 on PSRAM; integrated boards
  (e.g. M5Stack: buttons on 37/38/39, LCD over SPI) pre-consume pins
  before you start.
  - Reserved: GPIO6–11 (SPI flash), GPIO1/3 (USB-serial console).
  - DAC outputs (not ADC inputs): GPIO25,26.
  - Everything else commonly broken out (4,5,13,14,16–19,21–23,27,
    32,33) is full digital IO with pullups.

## 6c. Pin capability advertisement (PIN_CAPS)

`PIN_CAPS_REQ` (empty payload) asks the companion to describe its GPIO.
`PIN_CAPS_RSP` answers: `name_len u8`, `name[name_len]` (≤32 B, not
NUL-terminated), `count u8` (≤70), then per pin `pin u8`, `caps u8`.
Pin numbering stays source-local (companion GPIO numbers, qualified by
device id — never merged into main-board numbering). The main board
renders only reported pins in its pin table; unreported companions fall
back to manual slot configuration.

Each `caps` byte (bit set = capable; `0x00` = unknown, never filter on it):

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | INPUT | Usable as digital input |
| 1 | OUTPUT | Usable as digital output (clear = input-only, e.g. ESP32 GPIO34–39) |
| 2 | PULL | Internal pull up/down available (else the main board must assume external) |
| 3 | ADC_CAPABLE | Has an ADC channel (future analog reads; no v1 wire change) |
| 4 | PWM_CAPABLE | Hardware PWM/LEDC (future LED/motor outputs) |
| 5 | STRAPPING | Boot-strapping pin — UI warns against driving it at boot |
| 6 | FIVE_VOLT_TOLERANT | 5 V-safe input (AVR yes, ESP32 no — wiring note) |
| 7 | ADC_RADIO_SAFE | Analog channel stays usable with wireless on (ESP32 ADC1; ADC2-class channels go blind while WiFi or Classic BT runs, so they carry ADC_CAPABLE without this bit) |

Advertisement mirrors enforcement: `GPIO_NAK` codes are the same
vocabulary in the other direction (`1`=not-a-pin, `2`=output-on-input-only,
`3`=not-adc-capable). The companion enforces; the main board filters and
warns but never hard-blocks (a miscabled pin NAKs at `GPIO_CONFIG` time).

## 7. Link supervision

`HEARTBEAT` every 500 ms when idle; 2 s silence = link down. On link
down each side parks outputs at neutral (sticks center, buttons clear,
rumble off) — the failsafe. Reconnect re-runs `HELLO`; no bonded state
is kept on the wire (bonding stays in the BT stack).

Two hard-won handshake rules (both learned from a real HELLO storm):
inbound `HELLO` is answered with a fresh `GPIO_CONFIG` round, never
with a `HELLO` reply (mutual hello-replies ping-pong forever); and the
main board re-applies its sticky companion-input mask every poll rather
than only on frame arrival, because its own pipeline rebuilds button
state from physical pins each iteration (same reason PCF8575 re-reads
its expander every call).

## 8. Roles / topologies (all served by §§3–7)

- Companion-host: ESP32 runs Bluepad32, forwards pad INPUT_STATEs.
- Companion-device: RP2040/S3 sends processed state, ESP32 presents
  BT HID (Classic one-role-per-chip; BLE may dual-role central+
  peripheral on one chip — firmware switch, not protocol work).
- In-tree (S3 NimBLE-device, Pico-W BTstack, classic-ESP32 standalone):
  same messages over an internal queue instead of UART (transport
  abstraction, same codec).
- Classic + BLE pads coexist: transport field in HELLO notes which
  radio each device id arrived on (info for latency budgets).
- S3 + classic-ESP32 companion for Classic BT: the S3 radio is BLE-only,
  so Classic pads (PS4, Xbox One) and Classic device output always route
  through a classic-ESP32 companion over GP-Link, while the S3 serves BLE
  directly. This is also the primary Bluepad32 host deployment (classic
  silicon, best-supported) feeding every board type.

## 9. Webconfig touchpoints (future, additive)

Webconfig is full-featured native on every board with its own radio —
Pico-W (CYW43), ESP32-S3 (this port) and classic-ESP32 standalone each
serve the shared bundle + API directly. The §10 tunnel exists only for
radio-less main boards (plain RP2040) reached through a companion.

Transport indicator (link type/rate/errors, like the network status
line), pairing UI (scan/pair/unpair + bonded list, modeled on the WiFi
scan picker), per-device-id mapping, and a companion-pins source
section (§6b) when `COMPANION_GPIO` is advertised. No config-universe
fork: BT output is a driver; pairing state is BT-stack data surfaced
read-mostly.

Companion v1 serves only its own status page (link state, paired pads,
BT output mode); full configuration stays on RP2040 USB webconfig.

## 10. HTTP tunnel profile (deferred; specified now)

Lets a companion's WiFi AP serve the main board's full webconfig UI +
API with zero UI changes. Gated by the `HTTP_TUNNEL` caps bit; peers
without it drop these types per the unknown-type rule.

- Types: `HTTP_REQ=0x10`, `HTTP_RESP=0x11`, `HTTP_FRAG=0x12`.
- `HTTP_REQ`: `reqId u16`, `method u8` (0=GET, 1=POST), `uriLen u8`,
  `uri bytes`, `body bytes…`. `HTTP_RESP`: `reqId u16`, `status u16`,
  `body bytes…`. Bodies here are raw HTTP bodies (no chunked encoding).
- Fragmentation (required: API payloads reach ~4 KB, the bundle ~1.3 MB,
  frames cap at 256 B): payloads larger than one frame use `HTTP_FRAG`
  (`reqId u16`, `fragIdx u8`, `fragTotal u8`, `chunk bytes…`). The first
  fragment of a request also carries method+uri (same layout as
  `HTTP_REQ` after the frag header); response fragments carry body
  chunks in order. Reassembly timeout 2 s → drop silently.
- One outstanding tunneled request per link (serialized; device id
  still carried for future pipelining). At 2 Mbaud a 3 KB API round
  trip costs ~15 ms; bundle loads are one-time and browser-cacheable
  (companion-side bundle caching is an allowed optimization).
- Security: the tunnel exposes the full config API over WiFi, so the
  companion AP follows the same WPA2 rule as the S3 AP.

## 11. Test plan

Host-side codec unit tests (encode/decode vectors, COBS edge cases,
CRC fault injection, unknown-type drop, v1-extension ignore), loopback
throughput test at 500 Hz over a virtual UART, plus hardware E2E:
pad → companion → RP2040 USB output latency budget (<5 ms link share).
Add `tests/host/test_gplink.cpp` + a `check-gplink-guards.sh` entry.

## 12. Open questions (resolve before plan)

1. ~~UART default rate~~ — decided: 2 Mbaud, 921600 fallback (§2).
2. ~~Companion firmware home~~ — decided: `extras/esp32-companion/`
   in this fork (same review loop as protocol + RP2040 side).
3. S3 Bluepad32 silicon support: verify in the spike before promising
   in-tree host mode. (Confirmed by Bluepad32 FAQ: S3/C3/C6/H2 are
   BLE-only; Classic needs ESP32-classic or Pico-W.)
