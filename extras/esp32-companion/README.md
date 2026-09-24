# ESP32 companion (GP-Link bridge)

Gives radio-less RP2040 boards (and the ESP32-S3 port) Bluetooth via a
companion ESP32 over the GP-Link protocol (`docs/superpowers/specs/
2026-09-24-gp-link-design.md`).

Modes (one active at a time; Classic needs one role per chip, BLE may
dual-role — see spec §8):
- **host**: Bluepad32 reads commercial pads → GP-Link INPUT_STATE →
  main board (RP2040 USB output, Pico-W, S3).
- **device**: main board's processed state → GP-Link → BT HID to
  console/PC (Classic on classic ESP32; S3 radio is BLE-only).

v1 scope: host mode on classic ESP32 at 2 Mbaud UART + a minimal status
page. Full board webconfig stays on RP2040 USB; the HTTP tunnel profile
(§10) is specified but deferred.
