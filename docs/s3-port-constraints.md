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
- **Root cause:** `pdMS_TO_TICKS(1)` truncates to **0 ticks** below a
  1000 Hz tick rate — a mere yield that still starves IDLE. (At the old
  100 Hz default it was likewise 0.)
- **Rule:** one-tick waits spell `vTaskDelay(1)` (exactly one tick at any
  rate). Never use `pdMS_TO_TICKS(1)`.
- **Verify:** guard check 5; hardware: no watchdog, IDLE runs.

## 8. Gamepad loop runs at 1000 Hz

- **Symptom:** up to 10 ms input latency despite a 1 ms USB poll rate.
- **Root cause:** the S3 core loop yields exactly one RTOS tick per
  iteration (`vTaskDelay(1)`); at the IDF default 100 Hz tick that quantum
  is 10 ms, so reports were generated at ~100 Hz while the host polled at
  1000 Hz.
- **Rule:** `esp32-s3/sdkconfig.defaults` pins `CONFIG_FREERTOS_HZ=1000`,
  making the yield 1 ms (~1000 Hz loop, hardware-measured stable). All
  other S3 waits are millisecond-based (`pdMS_TO_TICKS(ms)`) and scale
  automatically; only the two intentional one-tick yields depend on this.
- **Verify:** hardware: 1000 loops measure exactly 1000000 µs; inputs flow
  in XInput/PS3/SwitchPro/SInput with no watchdog trips.

## 6. FlashPROM commit must tolerate a cold timer

- **Symptom:** boot abort loop on first flash (`ESP_ERR_INVALID_STATE`).
- **Root cause:** `esp_timer_stop()` on a created-but-never-started timer
  fails; `ESP_ERROR_CHECK` turns it into an abort on the very first save.
- **Rule:** the stop stays `esp_timer_is_active()`-guarded in
  `lib/FlashPROM/src/FlashPROM_esp32.cpp`.
- **Verify:** guard check 6; hardware: first boot with fresh flash saves cleanly.

## 7. PS3 interrupt reports must be exactly the descriptor length (49)

- **Symptom:** PS3 mode enumerates (VID_054C, "working properly") but no
  input ever registers. `tud_hid_ready()` reads false on ~99% of loop
  iterations and IN transfers never complete — even with the host actively
  polling (joy.cpl panel open).
- **Root cause:** `PS3Report` measures 51 bytes (`sizeof`) but the HID
  descriptor declares 49 input bytes for Report ID 1; the struct's trailing
  `reserved4` overflows it. Sending 51 wedges the S3 IN endpoint
  (completions stop arriving); sending fewer (32 tried) flows at USB level
  but the host drops short reports — dead either way.
- **Rule:** the interrupt send uses `PS3_INPUT_REPORT_LEN` (49,
  `headers/drivers/ps3/PS3Descriptors.h`), never `sizeof(PS3Report)`.
  Change-detection (`memcmp` vs `last_report`) still uses the full struct.
- **Verify:** guard check 7; hardware: B1/directions register in joy.cpl
  and in WebHID (joypad.ai) in PS3 mode (commit `8b08e171`).
- **Evidence trail (Sept 2026):** boundary counters proved gamepad state
  reached the driver with correct bytes (`0x40` = South); a PS4 control
  test passed (shared HID path healthy, bug is PS3-specific); 32-byte
  sends completed without registering; 49-byte sends fixed inputs.

## Validated USB modes on S3 hardware (post-merge, Sept 2026)

- XInput (`VID_045E:028E`), Pokken (`VID_0F0D:0092`), PS4, PS3 (`VID_054C`,
  needs §7 fix), SInput (`VID_2E8A:10C6`, struct is exactly the declared
  64 bytes — no PS3-style mismatch). Keyboard validated pre-merge.
- Boot-select defaults: B1=Switch (GPIO6), B2=XInput (GPIO7), B3=PS3
  (GPIO10), B4=PS4 (GPIO11), R2=Keyboard; L1/L2/R1 unmapped (`-1`).
  No default button selects SInput/SwitchPro/P5General/minis — SInput was
  validated via a temporary L1 mapping, reverted after.
- Button-hold mode switching works both directions and persists across
  reboot (exercises input path + config save).
- SwitchPro (`VID_057E:2009`): enumerated but inputs "cycled in binary"
  with nothing touched — the running timestamp byte was parsed as buttons
  1-8 (descriptor omitted the timer + conn/batt bytes, shifting every
  field). Fixed by declaring those 2 bytes const and shrinking trailing
  pad 52→50 (array 203→156 bytes, both `wDescriptorLength`s updated);
  dpad stays buttons (no hat nibble — joy.cpl POV reads const-pad zero).
- P5General (`VID_28B1:0101`): enumerates, no inputs — by design on S3
  (input path requires USB-host auth dongle, Phase 2; stubs force
  available()->false).

## 9. S3 flash layout must fit 8 MB variants

- **Symptom:** firmware flashes but the `www` (web UI) mount or OTA
  region silently overlaps/truncates on 8 MB modules; config and web
  assets corrupt each other with no build error.
- **Root cause:** the custom partition table (`esp32-s3/partitions.csv`,
  §3) is hand-sized; adding entries (e.g. the 4 MB `www` SPIFFS region)
  can push the layout past the smallest supported flash without any
  build-time complaint.
- **Rule:** the Size column of `esp32-s3/partitions.csv` must sum to at
  most `0x800000` (current: nvs `0x6000` + phy `0x1000` + factory
  `0x1E0000` + gpconfig `0x8000` + www `0x400000` = `0x5F8000`).
- **Verify:** guard check 9 sums the table in CI; hardware: AP serves the
  full web UI and save → reboot → setting persists (§3).

## 10. Webconfig transport defaults to USB; S2-hold boots USB-config

- **Symptom:** a CONFIG-booted board unexpectedly joins/starts WiFi (or a
  host-side script finds no AP when it expected one) after a default
  change.
- **Root cause:** the AP + HTTP server only start when the transport
  preference selects WiFi (L1-hold session, saved `apEnabled`, or CONFIG
  boot with the WiFi pref). USB-config is the later phase that will park
  gameplay like Pico; until then the safe default keeps radio off.
- **Rule:** `DEFAULT_WEBCONFIG_TRANSPORT` stays `WEBCONFIG_TRANSPORT_USB`
  in `src/config_utils.cpp`. S2-hold (and RTC-WEBCONFIG with the default
  pref) therefore boots USB-config by design — currently the saved
  gamepad mode stays live with no AP/server, never WiFi.
- **Verify:** guard check 10; hardware: S2-hold boot shows no AP and the
  gamepad enumerates in the saved mode.

## 11. Webconfig bring-up: AP + server live under WiFi, gamepad stays live

- **Symptom:** AP appears but the UI never loads, or inputs die while the
  AP session runs; alternatively the passphrase leaks into USB/serial
  logs captured during bring-up.
- **Root cause:** the S3 AP lifecycle needs the exact IDF sequence
  (`nvs_flash_init` + erase-retry, `esp_netif_init`,
  `esp_event_loop_create_default` tolerating `ESP_ERR_INVALID_STATE`,
  default-AP netif with DHCP, `esp_wifi_init`, bounded SSID/passphrase
  copies, WPA2/open select, `set_mode`/`set_config`/`esp_wifi_start`) —
  and, unlike Pico's USB-config which parks the gamepad, WiFi-config
  keeps the normal input loop running (no NetDriver on S3 to park).
- **Rule:** credentials come from `WebConfigOptions` (`apSSID` default
  `"GP2040-CE"`, empty SSID falls back to it; empty passphrase = OPEN,
  1–7 chars fail `set_config` loudly, never silently downgraded); the
  documented default passphrase literal lives ONLY in the
  `DEFAULT_AP_PASSPHRASE` define; log lines carry lengths only, never
  `apPassphrase`. L1-hold forces a session-only WiFi-config boot;
  gamepad inputs stay live under WiFi-config and will park only under
  the future USB-config.
- **Verify:** guard checks 11 (passphrase confined to the define; no
  `apPassphrase` in `webconfig_s3.cpp` printf/`ESP_LOG` lines); hardware:
  L1-boot → AP `GP2040-CE` appears → UI at `http://192.168.4.1/` →
  save → reboot → persists → inputs live during the AP session.

## Open items (observed, not guard-enforced)
- **PS3 Feature 0x01 response over-read (upstream bug, not ours).**
  `PS3Driver::get_report`, `PS3_FEATURE_01` case: copies a 48-byte host
  request from the 8-byte `output_ps3_alt_0x01` table (31 bytes past the
  end), and the GAMEPAD/alt branches look inverted against the table
  comments ("for non DS3 controllers"). Present upstream, tolerated by the
  host on S3. Flag for an upstream report; deliberately not fixed here.
- **Boot-select mode persistence retest.** A B3-hold boot entered PS3 for
  the session but a later reset came up in stored Pokken; XInput↔Pokken
  switches persisted earlier, so saving itself works. Needs a dedicated
  save → reset → recheck pass to rule out a merge regression in the
  boot-action save path.
