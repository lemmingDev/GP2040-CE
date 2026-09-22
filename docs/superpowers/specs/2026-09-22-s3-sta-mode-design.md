# S3 WiFi STA Mode — Design

Date: 2026-09-22. Status: sections §1–§5 approved; STA posture questions
resolved (concurrent AP+STA; configurable scope with ADC2 warning).
Follows the webconfig spec (`2026-09-21-s3-webconfig-design.md`).

## Context

WiFi-AP webconfig works on hardware (E2E passed). AP has no uplink:
version checks fail, and the UI is only reachable from the AP itself.
STA mode joins a home network for uplink + LAN-side UI access, running
concurrently with the AP.

## Decisions (locked with human partner)

- Concurrent AP+STA (never STA-replaces-AP).
- STA scope configurable: Off / Webconfig-only / Always-on (default
  Off), with a plain-English ADC2 warning in the UI (analog on GPIO
  11–20 unavailable while any WiFi runs).
- Follow-up (not this spec): WiFi config on Pico W — strictly easier
  there (existing lwIP-httpd backend + CYW43 netif join, no port), but a
  different board/driver/hardware validation. Recorded, not scoped.

## §1 Schema

Extend `WebConfigOptions` (`proto/config.proto`), all `optional`
(Pico ignores): `staSSID` (string, max 32), `staPassphrase` (string,
max 64), `staMode` enum (`STA_OFF = 0` default, `STA_WEBCONFIG_ONLY =
1`, `STA_ALWAYS_ON = 2`). Defaults block mirrors the AP pattern
(`DEFAULT_STA_SSID ""`, empty passphrase, mode off). Nanopb regen both
toolchains.

## §2 Lifecycle

Beside the AP bring-up (untouched): when STA is wanted,
`esp_wifi_set_mode(WIFI_MODE_APSTA)`,
`esp_wifi_set_config(WIFI_IF_STA, …)`, `esp_wifi_connect()`; default
STA netif runs the DHCP client. A lightweight supervisor retries with
exponential backoff (capped); AP lifecycle fully independent — STA
failure/retry never disturbs it. Connection state (connected SSID + IP,
or `connecting…`/`failed`) exposed for the UI status line. Disable →
STA disconnects; AP follows its own rules.

## §3 Boot integration

STA joins at boot when mode is Always-on, or in any webconfig session
(L1-hold, toggle, CONFIG+WiFi-pref) when Webconfig-only; Off never
joins. Gamepad/USB loop untouched (same concurrency as AP).

## §4 UI

"Home Network" section beside AP settings (Settings page): SSID +
passphrase fields (maxLength 32/64, Formik/yup patterns), mode select,
persistent ADC2 trade-off note, status line (SSID + LAN IP or state).
English labels; other locales fall back. Save via existing settings
POST; no new endpoints (reads/writes ride the gamepad-options pair +
full-config round-trip, which cover new keys automatically like the AP
keys).

## §5 Testing

Builds (S3 + Pico, new fields optional); guards (STA defaults pinned,
no STA passphrase in logs); host tests untouched. Hardware: configure
via AP UI → reboot → joins home network (serial shows IP) → UI
reachable via LAN IP → AP-router down → backoff retries, AP
unaffected → clear creds → clean boot with no STA.
