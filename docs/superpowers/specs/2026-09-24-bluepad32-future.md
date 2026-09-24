# Bluepad32 for S3 Bluetooth input (future)

Deferred 2026-09-24. Do not implement until analog phase (a–c) lands.

## What it is

https://github.com/ricardoquesada/bluepad32 — Bluetooth gamepad-host
library for ESP32/ESP32-S3 (Bluedroid-based). Speaks Xbox One/Series,
PS4/PS5, Switch Pro, Wii and more over BT Classic/BLE: pairing, button/
stick reports, rumble, LEDs. The jocover `esp32s3_xbox_adapter` findings
(BLE Xbox → PS4-over-OTG on S3) are a subset of what Bluepad32 already
productizes.

## Why it fits us

- Gives `INPUT_MODE_BLUETOOTH` a real backend on S3 with zero USB
  involvement — sidesteps the single-OTG-port host problem entirely
  (same conclusion as the jocover notes: wireless input needs no USB).
- Controller coverage matches exactly what users pair for passthrough/
  auth use cases today.
- S3 has the RAM for it (Bluedroid is hungry; we have 512 KB HP SRAM
  + 8 MB PSRAM, but verify heap with webconfig + WiFi + BT all up).

## Related work (2026-09-24 survey)

- **Bluepad32 AirLift precedent**: Bluepad32 already ships as an ESP32
  *co-processor* (SPI, NINA protocol) for a main MCU — our companion
  architecture is proven. We still define GP-Link (UART-first, rumble
  back-channel, multi-pad, HTTP tunnel) rather than adopting NINA.
- **Jamaica-Sound/Esp32-lowlatency-wireless-gp2040ce-controller**:
  ESP32 master → ESP-NOW → ESP32 slave → UART → Pico running a
  GP2040-CE-UART fork. Validates UART-into-RP2040; study its UART
  protocol/baud and the fork's serial-ingest path.
- **WilliamLewww/GP2040-CE-bluetooth** (upstream draft PR #1575,
  +2893/-38): Pico-W BT *device* (Switch + Generic HID) via BTstack,
  with auto-reconnect, deep sleep and a BT status webconfig page. If
  upstream merges it, rebase Pico-W work onto it; copy the status-page
  pattern for pairing UI either way.
- Upstream issue #247 (BT bounty): BTstack ships RPi-licensed for
  Pico-W products (incl. commercial); other controllers need a
  BlueKitchen agreement. Bluepad32 itself is Apache 2.0 but depends on
  BTstack (free for open-source, licensed for closed-source). Fine for
  our open work; note for any commercial use.
- Market gap: Doio discontinued the only wireless GP2040 kits; no
  prebuilt wireless devices (latency paranoia cited). XInput-over-BT
  is impossible (Windows requires USB) — standard HID only.

## Risks / open questions

- **License check required** before vendoring or linking (see above:
  Apache 2.0 + BTstack dependency; confirm against our tree).
- **BT + WiFi coexistence**: webconfig normally keeps WiFi up; validate
  Amsterdam-style coexistence (timeshare), link stability, and input
  latency with both radios active. ADC2/WiFi conflict is orthogonal
  (ADC, not radio) but compounds testing.
- **BT Classic vs BLE**: Xbox Series is BLE; older pads are Classic —
  confirm our S3 board config enables both.
- Footprint: Bluedroid + app task stacks vs our 12288-byte httpd workers
  and TinyUSB task; watch `getMemoryReport` minimum-free-heap.

## When to pick up

After analog phase (S3 ADC backend → Analog → Turbo → HE). Entry point:
spike task — build Bluepad32 as a managed component in a scratch branch,
pair one Xbox Series pad, dump reports over UART. No UI changes until
the spike reports.
