# ESP32-S3 Port: Hardware-Proven Constraints

This file records invariants of the ESP32-S3 port that were each proven by
real hardware failures during bring-up (Sept 2026). Every item below is
enforced by `tests/host/check-s3-guards.sh`, which runs in CI
(`.github/workflows/s3-guards.yml`) without any toolchain. If a guard fails,
read the matching section here before touching the code.

## 1. Gamepad loop must pump USB without blocking

- **Symptom:** device enumerates but goes silent in USB-quiet modes (Switch,
  HID minis); XInput keeps working, masking the fault for weeks.
- **Root cause:** `tud_task()` on FreeRTOS blocks indefinitely when no USB
  events are pending. Called inline in the gamepad loop, it wedges polled
  input whenever the host is quiet. XInput survives only because its own
  endpoint traffic keeps waking the pump.
- **Rule:** the S3 loop pumps with `tud_task_ext(0, false)` (zero timeout)
  plus `vTaskDelay(1)` (~100 Hz loop, IDLE still runs). Bare `tud_task()`
  is correct on Pico (no RTOS) and forbidden on S3 outside `PICO_BOARD`
  guards.
- **Verify:** guard checks 1–2; hardware: Switch-mode inputs flow with the
  host completely idle.

## 2. OTG PHY must be initialized before `tud_init()`

- **Symptom:** device silently never enumerates; USB-Serial/JTAG keeps the
  pins and everything looks alive.
- **Root cause:** raw TinyUSB does not enable the S3 USB peripheral (clocks,
  internal PHY, GPIO19/20 mux). No error is raised anywhere.
- **Rule:** `usb_new_phy()` with internal-PHY device config runs before
  `tud_init()` in `GP2040::run()` (S3-guarded).
- **Verify:** guard check 2; hardware: gamepad enumerates on first plug.

## 3. Build must select the custom partition table

- **Symptom:** config save/load silently no-ops (RAM works, flash doesn't).
- **Root cause:** without `CONFIG_PARTITION_TABLE_CUSTOM=y`, IDF builds the
  default single-app table, so the `gpconfig` partition never exists and
  every partition lookup returns NULL (handled silently by design).
- **Rule:** `esp32-s3/sdkconfig.defaults` pins `CONFIG_PARTITION_TABLE_CUSTOM=y`
  alongside `esp32-s3/partitions.csv`.
- **Verify:** guard check 3; hardware: save → reboot → setting persists.

## 4. Pin validity covers S3 routable GPIOs (0–48 minus USB 19/20)

- **Symptom:** onboard NeoPixel (GPIO38/48) never initializes; no error.
- **Root cause:** `isValidPin()` kept the RP2040-era `< 30` window, so every
  high-pin peripheral was constructed as a dummy.
- **Rule:** S3 validation is `0 <= pin <= 48` except 19/20. Companion rule:
  every table indexed by pin (`actions[]`, `gpioMappingsSets[].pins[]`)
  stays 30 entries, so migration code must additionally bound writes to
  `< NUM_BANK0_GPIOS` (I2C 41/42 wrote 11 past the end and corrupted input
  state — found on hardware).
- **Verify:** guard check 4; hardware: pixel lights, taps register.

## 5. One-tick waits spell `vTaskDelay(1)`

- **Symptom:** task watchdog fires on an apparently yielding task.
- **Root cause:** `pdMS_TO_TICKS(1)` truncates to **0 ticks** at the default
  100 Hz tick rate — a mere yield that still starves IDLE.
- **Rule:** one-tick waits spell `vTaskDelay(1)` (exactly one tick at any
  rate). Never use `pdMS_TO_TICKS(1)`.
- **Verify:** guard check 5; hardware: no watchdog, IDLE runs.

## 6. FlashPROM commit must tolerate a cold timer

- **Symptom:** boot abort loop on first flash (`ESP_ERR_INVALID_STATE`).
- **Root cause:** `esp_timer_stop()` on a created-but-never-started timer
  fails; `ESP_ERROR_CHECK` turns it into an abort on the very first save.
- **Rule:** the stop stays `esp_timer_is_active()`-guarded in
  `lib/FlashPROM/src/FlashPROM_esp32.cpp`.
- **Verify:** guard check 6; hardware: first boot with fresh flash saves cleanly.
