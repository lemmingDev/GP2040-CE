# Phase 1 S3 Integration + Device Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a compiling ESP32-S3 firmware (HID/XInput + all USB device drivers, storage, LEDs/audio, display) on a single-core-then-dual-core bring-up, with RP2040 still green.

**Architecture:** Single-core S3 bring-up first (core0 gamepad/USB loop only), then add the FreeRTOS core1 task with its LED/audio/display consumers; platform-select shared files with `#if defined(PICO_BOARD)` / `#elif defined(ESP_PLATFORM)` guards (Pico path byte-identical); pre-generated nanopb outputs committed under `esp32-s3/generated/` so S3 builds never invoke the Python venv.

**Tech Stack:** ESP-IDF v5.4 (`idf.py`, xtensa-esp32s3), Pico SDK 1.5.1 + ARM GCC 14.2 + CMake 3.28 (RP2040 regression), CPython 3.12 + protobuf 4.25 (proto regen only), host g++ (unit tests).

**Spec:** `docs/superpowers/plans/2026-09-19-esp32s3-poc.md` (Phase 0 plan) + `docs/superpowers/plans/2026-09-19-esp32s3-poc-exit.md` (carry-forward: R1 now closed locally; Task-7/8 TBDs; esp_hidd evaluation; pin-table items). Phase 1 scope = approved "device parity" (remaining USB device drivers, esp_partition storage, display + NeoPixel + buzzer/rumble on HAL) PLUS the Phase-0 leftover entry ticket (first S3 build green). Parked still: USB host/auth, webconfig, dual-transport, classic-ESP32.

## Global Constraints

- RP2040 Pico build must stay green — every task touching shared `src/`/`headers/`/`lib/` ends with a Pico regression rebuild.
- S3 pre-generated nanopb outputs are committed under `esp32-s3/generated/` and regenerated only when `proto/*.proto` changes, with the exact command in Environment.
- S3 guards use `#if defined(ESP_PLATFORM)` / `#if defined(PICO_BOARD)`; never alter Pico behavior inside a guard.
- No `gpio_get_all()` on S3 — per-pin `hal::gpioGet()` loops.
- BLE notify stays change-only (~133 Hz ceiling); no bonding UI, no dual-transport, no Classic BT.
- YAGNI: USB host, auth passthrough, and webconfig stay OUT (source-excluded, Phase 2/3).

---

## Environment (READ THIS FIRST — every task)

WSL Ubuntu holds all toolchains. The Windows worktree is the source of truth; S3 builds run on a plain-source copy (non-git dir, no venv needed there).

**Sync worktree → WSL build dir** (PowerShell, run after every edit batch before building):

```powershell
$src="C:\Users\PPSHS VR\Downloads\GP2040-CE\.worktrees\esp32s3-poc"
$dst="\\wsl$\Ubuntu\home\lemming\gp2040-s3build"
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item -Path "$src\*" -Destination $dst -Recurse -Force -Exclude .git,build,node_modules,.superpowers
```

**S3 build** (no inner double-quotes — the `wsl --exec` layer strips them; single-quote the whole program):

```powershell
wsl --exec bash -c 'bash ~/s3build.sh build'
```

`~/s3build.sh` exports IDF v5.4 and runs `idf.py` in `~/gp2040-s3build/esp32-s3`. Other targets: `reconfigure`, `flash`, `monitor`, `fullclean`.

**Pico regression** (WSL clone at `~/gp2040-wsl`, origin = Windows worktree path):

```powershell
wsl --exec bash -c 'cd ~/gp2040-wsl && git fetch origin && git reset --hard origin/feature/esp32s3-poc && export PATH="$HOME/tools/cmake-3.28.6-linux-x86_64/bin:$HOME/tools/arm-none-eabi/bin:$PATH" && echo TOOLCHAIN-NOTE'
```

NOTE: the ARM toolchain dir is `arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin` (verify with `ls ~/tools/` first — do not guess the path). Then per project CI: `PICO_SDK_PATH=~/sdks/pico-sdk-1.5.1 cmake -B build -DGP2040_BOARDCONFIG=Pico -DSKIP_WEBBUILD=TRUE` + `cmake --build build`. `lib/httpd/fsdata.c` and `build/venv` already exist in that clone (do not delete `build/`).

**Proto regen** (only when `proto/*.proto` changes; CPython 3.12 + pinned deps):

```powershell
wsl --exec bash -c 'cd ~/gp2040-wsl && build/venv/bin/python lib/nanopb/generator/nanopb_generator.py -q -D /tmp/pb -I proto -I lib/nanopb/generator/proto proto/enums.proto proto/config.proto'
```

Then copy `/tmp/pb/{config,enums}.pb.{c,h}` to `esp32-s3/generated/` and commit. If the venv is ever lost, rebuild it: standalone `~/tools/cpython-3.12/python/bin/python3 -m venv`, then `pip install setuptools==80.9.0 protobuf==4.25.8 grpcio-tools==1.62.3` (exact pins — newer protobuf drops `MakeClass`, Python 3.14 breaks old metaclasses).

**Host unit tests:** `wsl g++ -std=c++17 -Wall -Werror -I<dir> tests/host/x.cpp -o /tmp/x && /tmp/x` (single-file, no build system).

---

## File Structure

| File | Responsibility |
|---|---|
| `esp32-s3/CMakeLists.txt` | IDF project entry (`project.cmake` + `project()`) |
| `esp32-s3/main/CMakeLists.txt` (rewrite) | S3 component: triaged SRCS, `hal` + `hal_esp32s3` + `generated` includes, single TinyUSB require |
| `esp32-s3/generated/{config,enums}.pb.{c,h}` | Committed nanopb outputs for S3 builds |
| `src/main.cpp` (guard) | `app_main()` + single-core bring-up on S3; Pico `main()` untouched |
| `src/gp2040.cpp`, `src/gamepad.cpp`, `src/usbdriver.cpp` (guards) | S3 compile triage (clock/bootrom/time/ADC/poll-loop) |
| `src/drivermanager.cpp` (guard) | S3 switch trimmed to device drivers in scope |
| `src/system.cpp`, `src/storagemanager.cpp` (guards) | S3 reboot/flash/watchdog minimal mapping |
| `lib/FlashPROM/src/FlashPROM.{h,cpp}` + new `FlashPROM_esp32.cpp` | Same `FlashPROM` interface; Pico vs `esp_partition` backends |
| `hal_esp32s3/hal_pwm_s3.cpp` (extend) | Generalized `halPwmConfig()` + `halPwmTone()` wrapper for rumble/LEDs |
| `hal_esp32s3/hal_ws2812_s3.{h,cpp}` | RMT WS2812 backend matching NeoPico's used method set |
| `lib/PicoPeripherals/peripheral_i2c.*` + new S3 impl file | Same `PeripheralI2C` interface over IDF I2C master |
| `src/gp2040aux.cpp` (guard) | S3 core1 membership (LED/audio/display addons; host start excluded) |
| `src/addons/{buzzerspeaker,drv8833_rumble,reactiveleds,playerleds,neopicoleds}.cpp` (guards) | PWM→LEDC, NeoPico→RMT on S3 |
| `src/drivers/*` (SRCS only) | Remaining device drivers compiled for S3 |
| `docs/superpowers/plans/2026-09-20-phase1-exit.md` | Exit record + hardware validation procedures |

---

### Task 1: S3 project scaffolding + single-core entry + configure green

**Files:**
- Create: `esp32-s3/CMakeLists.txt`
- Modify: `src/main.cpp` (platform guard only)
- Modify: `esp32-s3/main/CMakeLists.txt` (rewrite SRCS/INCLUDE_DIRS/REQUIRES)

**Interfaces:**
- Consumes: `GP2040`/`GP2040Aux` classes, `hal::sleepMs`, FreeRTOS task API
- Produces: `idf.py reconfigure` green; `app_main()` single-core bring-up that Tasks 2+ build on

- [ ] **Step 1: Create `esp32-s3/CMakeLists.txt`** — verbatim:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gp2040-ce-s3)
```

`partitions.csv` in the project dir is picked up automatically; `sdkconfig.defaults` seeds new configs.

- [ ] **Step 2: Guard `src/main.cpp` for single-core S3 bring-up** — apply exactly:

Replace line 7 `#include "pico/multicore.h"` with:

```cpp
#if defined(PICO_BOARD)
#include "pico/multicore.h"
#elif defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
```

Replace the `core1()` body guard (line 28): wrap `multicore_lockout_victim_init();` in `#if defined(PICO_BOARD)` / `#endif`.

Replace `int main() {` (line 35) with:

```cpp
#if defined(ESP_PLATFORM)
extern "C" void app_main() {
#else
int main() {
#endif
```

Replace the core1 launch + ready barrier (lines 43–49):

```cpp
	// Create GP2040 Thread for Core1
#if defined(PICO_BOARD)
	multicore_launch_core1(core1);

	// Sync Core0 and Core1
	while(gp2040Core1->ready() == false ) {
		__asm volatile ("nop\n");
	}
#else
	// Phase 1 single-core bring-up: aux/display/LED task lands in Task 4.
	// Core1 objects exist but are not started yet.
	(void)gp2040Core1;
	hal::sleepMs(10);
#endif
	gp2040Core0->run();
```

And after `gp2040Core0->run();` add before the closing brace: `#if defined(ESP_PLATFORM)` + `vTaskDelete(nullptr);` + `#endif` (run() never returns; satisfies FreeRTOS task rules). Add `#include "hal_time.h"` at top (bare-name include per codebase convention) — needed for `hal::sleepMs`.

- [ ] **Step 3: Rewrite `esp32-s3/main/CMakeLists.txt`** — minimal Phase-1 set:

```cmake
idf_component_register(
    SRCS
        "../../src/main.cpp"
        "../../src/gp2040.cpp"
        "../../src/gamepad.cpp"
        "../../src/gamepad/GamepadState.cpp"
        "../../src/drivermanager.cpp"
        "../../src/drivers/hid/HIDDriver.cpp"
        "../../src/drivers/xinput/XInputDriver.cpp"
        "../../src/drivers/shared/xgip_protocol.cpp"
        "../../src/usbdriver.cpp"
        "../../src/storagemanager.cpp"
        "../../src/system.cpp"
        "../../src/peripheralmanager.cpp"
        "../../hal_esp32s3/hal_gpio_s3.cpp"
        "../../hal_esp32s3/hal_time_s3.cpp"
        "../../hal_esp32s3/hal_adc_s3.cpp"
        "../../hal_esp32s3/hal_pwm_s3.cpp"
        "../../esp32-s3/generated/config.pb.c"
        "../../esp32-s3/generated/enums.pb.c"
    INCLUDE_DIRS
        "../../headers"
        "../../hal"
        "../../hal_esp32s3"
        "../../esp32-s3/generated"
    REQUIRES tinyusb)
```

Keep the Task-7 OUT-scope comment block (host/auth/webconfig/display/addons/bluetooth excluded). REQUIRES starts with `tinyusb` only (resolves the duplication TBD; change only if reconfigure/build demands it, and report the change).

- [ ] **Step 4: Generate + commit nanopb outputs** — run the Environment regen command for `enums.proto` AND `config.proto`, copy the four outputs to `esp32-s3/generated/`, verify `enums.pb.h` contains `INPUT_MODE_BLUETOOTH = 15`.

- [ ] **Step 5: Sync + reconfigure** — run the Environment sync, then `wsl --exec bash -c 'bash ~/s3build.sh reconfigure'`. Expected: configure green (CMake + Kconfig). Build errors are EXPECTED at this task (Task 2 resolves them) — do not fix sources here; record the first-error list (max 10 lines) in your report for Task 2.

- [ ] **Step 6: Commit**

```bash
git add esp32-s3/CMakeLists.txt esp32-s3/main/CMakeLists.txt esp32-s3/generated src/main.cpp
git commit -m "feat(s3): project scaffolding with single-core entry"
```

---

### Task 2: Core-loop build green (HID + XInput USB device)

**Files:**
- Modify: `src/gp2040.cpp`, `src/gamepad.cpp`, `src/drivermanager.cpp`, `src/system.cpp`, `src/storagemanager.cpp`, `src/usbdriver.cpp` (S3 guards only — Pico bytes identical)
- Modify: `esp32-s3/main/CMakeLists.txt` (triage adjustments only)

**Interfaces:**
- Consumes: `hal::` gpio/time/adc/pwm, `TransportRouter`, Task-1 component
- Produces: `idf.py build` GREEN (zero errors); Pico regression GREEN

- [ ] **Step 1: Read the Task-1 first-error list** (from the Task-1 report file in `.superpowers/sdd/2026-09-20-phase1/` — the controller will point you at it) plus these known blockers; fix in this order:

(a) `src/gp2040.cpp`: guard `set_sys_clock_khz(120000)` (delete on S3 — RMT/LEDC take explicit clocks); guard `reset_usb_boot(0,0)` + `pico/bootrom.h` include (S3: no-op with `// S3: use BOOT+RESET into TinyUF2/DFU` comment); `adc_init()` global call → guard `PICO_BOARD` (ADC owned per-channel by `halAdcRead` on S3); `to_ms_since_boot(get_absolute_time())` → `hal::millis()`; the `~gpio_get_all()` input poll loop → per-pin `hal::gpioGet(pin)` loop over the same pin range with identical debounce handling; `NUM_BANK0_GPIOS` loops → keep the macro name but define the S3 count via the Task-0 board table (read `configs/ESP32S3DevKitC1/BoardConfig.h` for the reserved-pin rule before choosing the range).

(b) `src/gamepad.cpp`: `to_ms_since_boot`/`get_absolute_time` → `hal::millis()` (same swap, all sites).

(c) `src/drivermanager.cpp`: on S3, trim the switch to HID (`INPUT_MODE_GENERIC`), XInput, and Bluetooth cases; guard every other case (Net/XBOne/PS4/minis/keyboard return here in Task 6) with `#if defined(PICO_BOARD)` or `#else return;` — keep Pico switch byte-identical.

(d) `src/system.cpp` / `src/storagemanager.cpp`: guard `hardware/watchdog.h` + `watchdog_reboot(...)` → `esp_restart()` on S3; guard `flash_do_cmd`/JEDEC-ID capacity check (return a fixed 16 MiB note or defer to the storage task — comment it); guard `multicore_lockout_*` + `watchdog_hw->scratch` boot-mode word (S3: `// Phase 3 webconfig boot-mode: RTC-retain replacement` comment, default to gamepad mode).

(e) `src/usbdriver.cpp`: expected portable (TinyUSB APIs) — compile-driven; guard only what errors demand.

(f) `hal/` include wiring: if any S3 TU can't see `hal_gpio.h`, add `"../../hal"` is already listed — do not duplicate; fix the `#include` spelling instead.

- [ ] **Step 2: Iterate `s3build.sh build` to zero errors** — sync + build after each edit batch. Warnings are acceptable but must be listed (file:line + text, max 15) in your report for later cleanup. Do NOT silence warnings with flags.

- [ ] **Step 3: Pico regression** — fetch/reset `~/gp2040-wsl` to this branch head per Environment, rebuild the Pico target. Expected: green (proves all guards preserved Pico behavior).

- [ ] **Step 4: Commit**

```bash
git add src esp32-s3/main/CMakeLists.txt headers lib
git commit -m "feat(s3): core-loop usb-device build green"
```

(Stage only files you touched; never `git add -A`.)

---

### Task 3: Storage shim on `esp_partition` (layout preserved)

**Files:**
- Modify: `lib/FlashPROM/src/FlashPROM.h` (platform dispatch, same interface)
- Modify: `lib/FlashPROM/src/FlashPROM.cpp` (guard existing body `PICO_BOARD`)
- Create: `lib/FlashPROM/src/FlashPROM_esp32.cpp` (S3 backend)
- Modify: `esp32-s3/main/CMakeLists.txt` (add the S3 backend source)

**Interfaces:**
- Consumes: `FlashPROM` interface (`start/commit/reset`, `writeCache[0x4000]`, `EEPROM_SIZE_BYTES`, `EEPROM_WRITE_WAIT`) — read `lib/FlashPROM/src/FlashPROM.cpp` first (53 lines) to mirror its commit-coalescing semantics exactly
- Produces: identical persistence contract on S3; `ConfigUtils`/`StorageManager` UNTOUCHED (footer + CRC + nanopb byte-for-byte)

- [ ] **Step 1: Read `lib/FlashPROM/src/FlashPROM.cpp`** — mirror its semantics: `start()` memcpys flash→cache; `commit()` defers `EEPROM_WRITE_WAIT` ms then erase+program at the XIP offset; `reset()` zeroes + commits.

- [ ] **Step 2: Dispatch the header** — in `FlashPROM.h`, wrap the three Pico includes (`pico/lock_core.h`, `pico/multicore.h`, `hardware/flash.h`, `hardware/timer.h`) in `#if defined(PICO_BOARD)`; add `#elif defined(ESP_PLATFORM)` branch declaring the identical class (same methods, same `writeCache`, same defines; `EEPROM_ADDRESS_START` unused on S3 — keep the define with a comment pointing at the `gpconfig` partition). Guard `FlashPROM.cpp`'s entire body with `#if defined(PICO_BOARD)`.

- [ ] **Step 3: Write `lib/FlashPROM/src/FlashPROM_esp32.cpp`** — verbatim logic:

```cpp
#include "FlashPROM.h"

#if defined(ESP_PLATFORM)
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_log.h"

uint8_t FlashPROM::writeCache[EEPROM_SIZE_BYTES];

static const esp_partition_t *gpconfigPart() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x06, "gpconfig");
}

static void commitTimerCb(void *arg) {
    (void)arg;
    const esp_partition_t *p = gpconfigPart();
    if (p == nullptr) return;
    ESP_ERROR_CHECK(esp_partition_erase_range(p, 0, EEPROM_SIZE_BYTES));
    // 16 KiB = 4x 4 KiB sectors; sequential 256 B page writes.
    for (size_t off = 0; off < EEPROM_SIZE_BYTES; off += 256) {
        ESP_ERROR_CHECK(esp_partition_write(p, off, &FlashPROM::writeCache[off], 256));
    }
}

void FlashPROM::start() {
    const esp_partition_t *p = gpconfigPart();
    if (p == nullptr) return;
    ESP_ERROR_CHECK(esp_partition_read(p, 0, writeCache, EEPROM_SIZE_BYTES));
}

void FlashPROM::commit() {
    static esp_timer_handle_t t = nullptr;
    if (t == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = commitTimerCb;
        args.name = "gpconfig_commit";
        ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    }
    ESP_ERROR_CHECK(esp_timer_stop(t));
    ESP_ERROR_CHECK(esp_timer_start_once(t, (uint64_t)EEPROM_WRITE_WAIT * 1000ULL));
}

void FlashPROM::reset() {
    memset(writeCache, 0, EEPROM_SIZE_BYTES);
    commit();
}
#endif
```

If `esp_partition_write` alignment errors appear at build/first-run, adjust chunking (document what you changed and why — do not silently shrink the write). `partitions.csv` already reserves 32 KiB `gpconfig`; 16 KiB image fits with room for growth.

- [ ] **Step 4: Add the backend to S3 SRCS** — one line in `esp32-s3/main/CMakeLists.txt` (`../../lib/FlashPROM/src/FlashPROM_esp32.cpp`). Pico CMake (`lib/CMakeLists.txt`) is untouched — verify it still compiles only `FlashPROM.cpp` (it globs or lists explicitly; check and report, do not restructure).

- [ ] **Step 5: Verify both builds** — S3 `s3build.sh build` green; Pico regression green. Plus document the hardware round-trip procedure (for first-S3-hardware): change a setting → save → reboot → confirm persistence; append it to the exit file task (do not perform it — no hardware here).

- [ ] **Step 6: Commit**

```bash
git add lib/FlashPROM esp32-s3/main/CMakeLists.txt
git commit -m "feat(s3): esp_partition storage backend preserving layout"
```

---

### Task 4: LED/audio addons on HAL + core1 FreeRTOS task

**Files:**
- Modify: `src/addons/buzzerspeaker.cpp`, `src/addons/drv8833_rumble.cpp`, `src/addons/reactiveleds.cpp`, `src/addons/playerleds.cpp` (S3 PWM→LEDC paths)
- Modify: `hal_esp32s3/hal_pwm_s3.cpp` (add generalized helper below)
- Modify: `src/gp2040aux.cpp` (S3 membership guard)
- Modify: `src/main.cpp` (launch core1 task on S3)
- Modify: `esp32-s3/main/CMakeLists.txt` (add addon sources)

**Interfaces:**
- Consumes: `halPwmTone()` (Phase 0), `hal::gpio*`, FreeRTOS pinned tasks, `AddonManager::LoadAddon`
- Produces: working LED/audio path + `core1_task` that Task 5's display/NeoPixel join

- [ ] **Step 1: Generalize the LEDC helper** — append to `hal_esp32s3/hal_pwm_s3.cpp`:

```cpp
void halPwmConfig(uint8_t gpioPin, uint32_t freqHz, uint8_t dutyPct,
                  ledc_timer_t timer, ledc_channel_t channel) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = timer;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz = freqHz;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {};
    c.gpio_num = gpioPin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = channel;
    c.timer_sel = timer;
    c.duty = (1023u * dutyPct) / 100u;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
```

And refactor `halPwmTone()` to call `halPwmConfig(gpioPin, freqHz, dutyPct, LEDC_TIMER_0, LEDC_CHANNEL_0)`. Declare both in a new `hal_esp32s3/hal_pwm_s3.h` (create it; S3-only include). Assign: buzzer → TIMER_0/CHANNEL_0 (via `halPwmTone`, unchanged behavior); rumble motors → TIMER_1 + CHANNEL_0/1; reactive LEDs → TIMER_2 channels; player LEDs → TIMER_3 channels. Document the allocation in a comment at the top of `hal_pwm_s3.cpp`.

- [ ] **Step 2: Convert the four addons** — in each file, READ it first, then guard the `hardware/pwm.h` sections with `#if defined(PICO_BOARD)` and add `#elif defined(ESP_PLATFORM)` paths calling the Step-1 helpers with identical frequencies/duties/pins. Preserve every mode, threshold, and timing constant — only the register-write mechanism changes. `board_led.cpp` already uses HAL (no work).

- [ ] **Step 3: Launch the core1 task** — in `src/main.cpp`, replace the Phase-1 `(void)gp2040Core1; hal::sleepMs(10);` S3 block with:

```cpp
	xTaskCreatePinnedToCore(
		[](void *) {
			gp2040Core1->setup();
			gp2040Core1->run();
			vTaskDelete(nullptr);
		},
		"gp2040aux", 8192, nullptr, 5, nullptr, 1);
```

(FreeRTOS task instead of `multicore_launch_core1`; the old ready-spin is unnecessary — `GP2040::setup()` runs before task creation as today.)

- [ ] **Step 4: Guard `src/gp2040aux.cpp` membership for S3** — wrap `initUSB()` + `USBHostManager::start()` + auth-listener push in `#if defined(PICO_BOARD)` (host is Phase 2); wrap `LoadAddon(new DisplayAddon())` and `LoadAddon(new NeoPicoLEDAddon())` similarly (Task 5); KEEP BoardLed/PlayerLED/Buzzer/ReactiveLED/DRV8833 loads common. Pico path byte-identical.

- [ ] **Step 5: Add the four addon sources to S3 SRCS** + verify both builds green (S3 + Pico regression).

- [ ] **Step 6: Commit**

```bash
git add src/addons hal_esp32s3 src/gp2040aux.cpp src/main.cpp esp32-s3/main/CMakeLists.txt
git commit -m "feat(s3): led/audio addons on ledc with core1 task"
```

---

### Task 5: NeoPixel RMT + display I2C backend

**Files:**
- Create: `hal_esp32s3/hal_ws2812_s3.h`, `hal_esp32s3/hal_ws2812_s3.cpp`
- Modify: `src/addons/neopicoleds.cpp` (S3 path using Step-1 backend)
- Create: `lib/PicoPeripherals/peripheral_i2c_s3.cpp` (S3 `PeripheralI2C` implementation)
- Modify: `lib/PicoPeripherals/peripheral_i2c.h` (platform dispatch if needed)
- Modify: `src/gp2040aux.cpp` (enable Display + NeoPixel loads on S3)
- Modify: `esp32-s3/main/CMakeLists.txt` (add sources)

**Interfaces:**
- Consumes: `PeripheralI2C` interface (read `lib/PicoPeripherals/peripheral_i2c.h` first — same class, new backend file), NeoPico method set used by the addon (read `neopicoleds.cpp` first), `hal_esp32s3` RMT + I2C-master APIs
- Produces: lit pixels + OLED buttons screen on hardware (validation deferred to hardware)

- [ ] **Step 1: Read the two consumers first** — list every `NeoPico` method/constructor call in `src/addons/neopicoleds.cpp` and every `PeripheralI2C` method signature in `lib/PicoPeripherals/peripheral_i2c.h`. Your backends must cover exactly those sets — report both lists.

- [ ] **Step 2: RMT WS2812 backend** — implement `hal_ws2812_s3.h/.cpp` with the identical method set from Step 1, backed by the IDF `led_strip` component (`led_strip_new_rmt_device`, `led_strip_set_pixel`, `led_strip_refresh`, `led_strip_clear`). GRB order + 800 kHz timing are handled by the component; RGBW mode: map W channel onto brightness like the PIO `ws2812_parallel` path does, and document any deviation. Add `led_strip` to REQUIRES only if the build demands it (IDF managed component — report what you did).

- [ ] **Step 3: S3 `PeripheralI2C` backend** — new file `lib/PicoPeripherals/peripheral_i2c_s3.cpp` implementing the identical class interface over IDF I2C master (`i2c_new_master_bus`, `i2c_master_bus_add_device`, transmit/receive with the same timeouts the Pico blocking calls use). Guard the original `peripheral_i2c.cpp` body `PICO_BOARD`; dispatch in the header or the S3 CMake SRCS (prefer header dispatch so Pico CMake needs no edit — report your choice). Display/GPGFX/SSD1306 sources stay UNTOUCHED; add the needed `src/display/*` + `src/interfaces/i2c/*` sources to S3 SRCS by compile demand.

- [ ] **Step 4: Enable the loads** — in `src/gp2040aux.cpp`, extend the S3 guard from Task 4 to include `DisplayAddon` + `NeoPicoLEDAddon` loads.

- [ ] **Step 5: Verify both builds green** (S3 + Pico regression) and commit:

```bash
git add hal_esp32s3 lib/PicoPeripherals src/addons/neopicoleds.cpp src/gp2040aux.cpp esp32-s3/main/CMakeLists.txt src/display src/interfaces
git commit -m "feat(s3): neopixel rmt plus display i2c backend"
```

---

### Task 6: Remaining USB device drivers + esp_hidd evaluation + exit record

**Files:**
- Modify: `esp32-s3/main/CMakeLists.txt` (add driver sources + `mbedtls` require)
- Modify: `src/drivermanager.cpp` (enable cases on S3 per compile demand)
- Create: `docs/superpowers/plans/2026-09-20-phase1-exit.md`

**Interfaces:**
- Consumes: all Phase-1 tasks; `esp_hid_device` example (read-only reference)
- Produces: full device-driver parity compiling; documented esp_hidd verdict; exit record

- [ ] **Step 1: Add driver families one at a time, building after each** — Switch, PS3, Keyboard, PS4, XBOne, then minis (MDMini/NeoGeo/PCEngine/Egret/Astro/PSClassic). For each: add its `src/drivers/<name>/*.cpp` to SRCS, enable its `drivermanager.cpp` case on S3, sync + `s3build.sh build`, fix only what errors demand (guards, never rewrites). PS4 needs mbedTLS: add `mbedtls` to REQUIRES (IDF built-in component). XboxOriginal: include only if it compiles without the `xid` third-party quirks — else document exclusion with reasons and move on (YAGNI over heroics; record in exit file).

- [ ] **Step 2: Pico regression green** after all drivers compile on S3.

- [ ] **Step 3: esp_hidd evaluation (decision, not rewrite)** — read `~/sdks/esp-idf/examples/bluetooth/esp_hid_device/` (report map, GAP/advertising, `esp_hidd_dev_init` with NimBLE transport, gamepad appearance availability). Deliver a written verdict in your report: keep the hand-rolled GATT 0x1812 table OR adopt `esp_hidd` (with concrete migration sketch max 15 lines if the latter). Do not rewrite `BluetoothDriver.cpp` in this task either way — the verdict + sketch go to the exit file for the hardware-validation task.

- [ ] **Step 4: Write the exit file** `docs/superpowers/plans/2026-09-20-phase1-exit.md`:

```markdown
# Phase 1 exit (date)
- [ ] S3 USB HID enumerates, inputs verified (hardware)
- [ ] S3 USB XInput + all ported drivers enumerate (hardware)
- [ ] Config save→reboot→load round-trips on S3 flash (hardware)
- [ ] Display shows buttons screen; NeoPixel/buzzer/rumble respond (hardware)
- [ ] RP2040 Pico build green (local: DONE, paste SHA)
- [ ] `make -C tests/host test` green (local: DONE/notes)
- [ ] esp_hidd verdict: <keep-raw-gatt | adopt-esp_hidd + reasons>
- [ ] Parked still: USB host/auth, webconfig, dual-transport, classic-ESP32

## First-hardware validation procedures
<script the exact flash/monitor/pair/test steps for the S3 board here>
## Carry-forward (Phase 2 entry ticket)
<auth/host blockers found, if any>
```

Check ONLY boxes verified locally (builds/tests); hardware boxes stay open with the scripted procedures.

- [ ] **Step 5: Commit**

```bash
git add esp32-s3 src/drivermanager.cpp src/drivers docs/superpowers/plans/2026-09-20-phase1-exit.md
git commit -m "feat(s3): remaining usb device drivers plus exit record"
```

---

## Self-Review

- **Spec coverage:** approved Phase-1 scope (remaining device drivers → T6; storage shim → T3; display/NeoPixel/buzzer/rumble → T4+T5) all mapped; Phase-0 carry-forward (first S3 build → T1+T2; esp_hidd eval → T6-Step 3; pin-table items stay parked for hardware) mapped; parked items (host/auth/webconfig/dual-transport/classic-ESP32) explicitly excluded per task.
- **Placeholder scan:** no TBD/TODO; environment commands are exact (sync block, `s3build.sh`, venv pins, regen command); implementer judgment is bounded (compile-driven guards, named fallbacks like XboxOriginal exclusion and REQUIRES selection with report-back rules).
- **Type consistency:** `halPwmConfig(pin, freqHz, dutyPct, timer, channel)` signature fixed in T4-Step 1 and reused in T4-Step 2; `FlashPROM` interface (start/commit/reset/writeCache/0x4000/50 ms) identical between T3 steps; `core1` task shape fixed in T4-Step 3; `bleAxisFromRaw` untouched throughout; file lists per task match the File Structure table.

---

**Parked (do not build):** USB host + auth passthrough (Phase 2), webconfig over WiFi-AP (Phase 3), dual USB+BT transport, classic-ESP32 companion, S3 pin-table remap (needs hardware), L2/R2 bit-label decision (cross-driver, needs hardware testing).
