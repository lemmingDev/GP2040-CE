# S3 USB Webconfig Design (2026-09-22)

Second webconfig transport for the ESP32-S3 port: the existing HTTP
backend served over USB (RNDIS) in addition to WiFi (AP+STA). Same
endpoints, same UI, second address. Opt-in only: real game consoles do
strict USB enumeration and a surprise interface could break them.

## 1. Architecture

One `esp_http_server` instance, up to three transports. Today it binds
the AP+STA esp_netif pair; we add a third esp_netif backed by a TinyUSB
RNDIS class driver (`tud_network_*` pumped from the USB task). All
existing handlers stay transport-agnostic.

New code:
- `src/webconfig_s3.cpp`: RNDIS control plane (init/reset/deinit, link
  status into the boot flow).
- New small TU `hal_usb_net_s3` (name TBD at plan time): TinyUSB<->netif
  packet path, kept out of the HTTP backend file.

When USB networking is off, descriptors are byte-identical to today
(the RNDIS interface is appended at runtime only).

## 2. Descriptors + compatibility guard

Each gamepad-mode driver appends the RNDIS interface (bulk IN/OUT +
interrupt endpoint, standard RNDIS class/subclass/protocol codes) if
and only if USB networking is active for this boot. Resolution order:

1. Boot-hold override (this session only, not persisted).
2. Stored `usbNetworkMode` (Off / Always-on / Config-mode-only).
3. Config-mode boots imply enabled regardless of the toggle.

Default is Off everywhere, so console enumeration is provably unchanged
unless the user opted in. The existing per-mode E2E (enumerate + input)
re-runs with the toggle off and on.

## 3. Settings + boot override

- `usbNetworkMode` in WebConfigOptions: Off=0 (default), AlwaysOn=1,
  ConfigModeOnly=2. Surfaced in Settings -> Network beside the STA
  controls; included in backup/restore; existing configs pick up Off
  via INIT_UNSET (no wipe).
- Boot-hold override: hold S1+S2 during boot (no clash with the L1-WiFi
  and L2-USB holds) to enable USB networking for that session only.
- Applies on next boot (same rule as the STA mode toggle); stated
  inline in the UI.

## 4. Networking + configurable subnets

- Defaults: AP `192.168.4.0/24` (board `.1`), USB `192.168.5.0/24`
  (board `.1`), USB side runs DHCP for the host.
- Both subnets user-configurable (`apSubnet`, `usbSubnet` in
  WebConfigOptions, `/24` assumed, shown as network addresses).
  Motivation: multiple boards on one PC must not share a subnet.
- Validation at save: valid IPv4 private ranges, and the two must
  differ. STA stays DHCP; the UI notes that a LAN collision means
  changing ours. Applies on reboot.
- All three interfaces (AP, STA, USB) coexist; each keeps its address,
  no NAT between them. The UI keeps listing all addresses as today.
- OS coverage: Windows (inbox RNDIS), Linux, macOS (standard gadget).

## 5. Testing

- Per gamepad mode x toggle off/on: enumerate + input on hardware.
  Toggle-off cases must be byte-identical to today's descriptors.
- RNDIS bring-up on Windows, Linux, macOS.
- Subnet validation matrix as host tests (no toolchain): equal subnets
  rejected, non-private rejected, defaults applied on old configs.
- Multi-board: two boards with distinct USB subnets configurable
  simultaneously from one PC.
- Full LED/webconfig E2E re-run (HTTP behavior unchanged, new
  transport only).

## Non-goals

- No ECM/NCM (Windows driver story is worse; RNDIS is plug-and-play).
- No custom serial protocol (a second config API to maintain forever).
- No Pico changes; S3-gated throughout, Pico builds stay green.
