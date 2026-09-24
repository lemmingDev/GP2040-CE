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

## 7. Link supervision

`HEARTBEAT` every 500 ms when idle; 2 s silence = link down. On link
down each side parks outputs at neutral (sticks center, buttons clear,
rumble off) — the failsafe. Reconnect re-runs `HELLO`; no bonded state
is kept on the wire (bonding stays in the BT stack).

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
