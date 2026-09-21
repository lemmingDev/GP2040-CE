# S3 Webconfig: HTTP Backend + WiFi-AP (USB deferred) — Design

Date: 2026-09-21. Status: approved sections §1–§6, awaiting spec review.
Approach A (port endpoints to `esp_http_server`); USB RNDIS is a
follow-up spec, not work here.

## Context

GP2040-CE's web UI (stock React app) talks to `src/webconfig.cpp`
(173 KB, lwIP-httpd callbacks + ArduinoJson), parked out of the S3
build in Phase 1. ESP-IDF's lwIP ships no HTTPD app, so the backend
must be re-expressed on `esp_http_server` (present and standard).
TinyUSB fork ships CDC-device and ECM/RNDIS-device classes (spike,
2026-09-21). ArduinoJson + mbedtls already compile in the S3 build.
16 MB flash on dev board; **8 MB variants must fit** (hard requirement).

## Decisions (locked with human partner)

- Sequence: shared REST backend → WiFi-AP → USB-RNDIS later.
- AP posture: saved toggle (default off) + force-on via boot hold.
- Boot buttons: L1-hold = WiFi-config boot, L2-hold = USB-config boot
  (both currently unmapped `-1`; S2-hold keeps meaning CONFIG).
- S2-hold default transport: USB (Pico parity); L1/L2 are session-only
  overrides (no save).
- AP security: WPA2 + documented default passphrase, changeable in UI.
- AP parameters are intentionally easy to revisit (SSID/pass, toggle
  semantics not load-bearing elsewhere).

## §1 Architecture & phases

Three layers, dependency order: (1) config schema, (2) HTTP backend,
(3) transports. One `esp_http_server` instance binds all netifs, so
WiFi and (later) USB share it with no extra HTTP work. Pico's
`webconfig.cpp` stays byte-untouched (S3-only TUs keep merges clean).

## §2 Config schema

New `WebConfigOptions` message in `proto/config.proto`, all fields
`optional` (Pico ignores; existing configs migrate via
`INIT_UNSET_PROPERTY` defaults in `src/config_utils.cpp`):

- `apEnabled` bool, default false (saved toggle).
- `apSSID` string, default `"GP2040-CE"`.
- `apPassphrase` string, default documented (e.g. `"gp2040config"`),
  plaintext in `gpconfig` like all Pico settings today.
- `webconfigTransport` enum (`USB` default, `WIFI`) — S2-hold default.

Minimal React additions (same spec): AP enable + SSID + passphrase +
transport pref fields on existing settings pages, English labels, other
locales fall back. Nanopb regen on both toolchains; S3 `generated/`
committed per convention.

## §3 HTTP backend + bundle + partitions

- New S3-only TU (e.g. `src/webconfig_s3.cpp`, S3 SRCS only).
  `esp_http_server` URI handlers implement the exact GET/POST contract
  the stock React app expects; `webconfig.cpp` remains the contract
  source of truth. ArduinoJson over S3-clean `ConfigUtils`.
- Save/reboot map to `Storage::save` + `esp_restart`. Handlers run on
  the server task: shared-state access follows existing Storage locking.
- Static bundle in a SPIFFS `www` partition (~4 MB), proper MIME types.
  Factory untouched. Partition total ≈ 6 MB < 8 MB (verified fit).
  `gpconfig`/NVS untouched.

## §4 WiFi AP bring-up

S3-only TU starts IDF WiFi AP mode (SSID/passphrase from §2, WPA2) +
DHCP + §3 server when requested; tears down when disabled. Request
priority: L1-hold at boot, else saved `apEnabled`. Gamepad/USB loop
untouched and concurrent (WiFi-config never parks gameplay). First-boot:
L1-hold → AP → UI → set toggle/password → save → honored thereafter.
(Contrast: USB-config, later phase, mirrors Pico — gamepad parked while
in CONFIG mode. The two transports intentionally differ here.)

## §5 USB phase scope (note, not work)

Later spec: L2-hold boots USB-config; CONFIG-mode TinyUSB descriptor
set (RNDIS/ECM classes); §3 server binds the USB netif unchanged.
Nothing in §§1–4 assumes WiFi-only (transport is a boot flag).

## §6 Testing

- S3 green + Pico green (shared proto/config_utils touched).
- Guards: extend `check-s3-guards.sh` (concrete checks at plan time;
  candidates: 1 kHz tick pinned, transport defaults, no creds in logs).
- Hardware: L1-boot → AP appears → join → UI loads → change setting →
  save → reboot → persists; toggle-on boot without hold; gamepad inputs
  live during AP session; exact 8 MB-fit partition table flashed.
- Pico regression unchanged (new proto fields optional).
