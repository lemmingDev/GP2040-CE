# S3 Webconfig (HTTP Backend + WiFi-AP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring up webconfig on ESP32-S3: shared REST backend on `esp_http_server` + WiFi-AP transport, so the stock React app configures the board.

**Architecture:** New `WebConfigOptions` proto fields feed ArduinoJson/ConfigUtils code paths; a new S3-only TU serves the exact Pico URI contract via `esp_http_server`; the React bundle ships in a SPIFFS `www` partition; WiFi-AP (L1-hold or saved toggle) brings up server + DHCP while the gamepad keeps running. Pico's `webconfig.cpp` is never touched. USB-RNDIS is explicitly out of scope (later spec).

**Tech Stack:** ESP-IDF v5.4 (`esp_http_server`, WiFi AP, SPIFFS, `esp_timer`), nanopb, ArduinoJson (vendored component), React bundle (prebuilt `www/build`), Pico SDK 1.5.1/2.3.1 + ARM GCC (regression only), host g++ (one new unit test).

**Spec:** `docs/superpowers/specs/2026-09-21-s3-webconfig-design.md` — this plan argues from the spec; executors read both.

## Global Constraints

- S3 tree builds with `bash ~/s3build.sh build` in `~/gp2040-s3build` (plain-source WSL copy of the worktree; sync changed files per batch before building).
- Pico regression stays green: `cmake --build build` in `~/gp2040-wsl` (Pico SDK 2.3.1, ARM GCC 14.2).
- Total flash layout must fit 8 MB (`0x800000`); verify by arithmetic on `esp32-s3/partitions.csv`.
- New S3 code lives in S3-only TUs or `#if defined(ESP_PLATFORM)` guards; never alter Pico behavior.
- POST payload cap stays 16 KB (`LWIP_HTTPD_POST_MAX_PAYLOAD_LEN` equivalent).
- Responses carry `Access-Control-Allow-Origin: *` and `Content-Type: application/json` like Pico.
- Commit after every task; push at the end. Update `tests/host/check-s3-guards.sh` + `docs/s3-port-constraints.md` where noted.

---

## File Structure

- Modify: `proto/config.proto` — append `WebconfigTransport` enum + `WebConfigOptions` message + `Config.webConfigOptions = 17`.
- Modify: `src/config_utils.cpp` — `DEFAULT_AP_*` defines + `// webConfigOptions` defaults block in `ConfigUtils::initUnsetPropertiesWithDefaults`.
- Modify: `esp32-s3/generated/{config,enums}.pb.{c,h}` — regen via the documented nanopb command, copy 4 outputs in.
- Create: `src/system_bootword.h` + `src/system_bootword.cpp` — pure pack/unpack of the reboot boot-mode word (host-testable); S3 `System::reboot/takeBootMode` use it.
- Create: `tests/host/test_bootword.cpp` — host unit test; modify `tests/host/Makefile` (add target).
- Modify: `esp32-s3/partitions.csv` — append `www` SPIFFS partition; total must stay ≤ 8 MB.
- Modify: `esp32-s3/main/CMakeLists.txt` — SRCS += webconfig TU; REQUIRES += `esp_http_server esp_wifi nvs_flash esp_netif spiffs`; SPIFFS image rule for the `www` bundle.
- Create: `src/webconfig_s3.cpp` (+ add to SRCS) — all URI handlers + static-file serving + server lifecycle.
- Modify: `src/gp2040.cpp` — remove S3 `INPUT_MODE_CONFIG→GENERIC` demotion; CONFIG mode starts webconfig instead of a gamepad driver; S3 L1/L2-hold boot actions.
- Modify: `www/src/Data/InputBootModes.ts` — NOT in this plan (no UI list changes; Bluetooth option stays held per prior decision).
- Create: React AP fields — `www/src/Pages/SettingsPage.jsx` (+ `Locales/en/SettingsPage.jsx` key), wired through existing `Services/WebApi` get/set.
- Modify: `tests/host/check-s3-guards.sh` — new checks (partition fit, transport defaults, no creds in logs).
- Modify: `docs/s3-port-constraints.md` — webconfig section + guard notes.

---

### Task 1: Config schema (proto + defaults + regen)

**Files:**
- Modify: `proto/config.proto` (append after `BootModeOptions = 16` area in `message Config`, l.993-1014)
- Modify: `src/config_utils.cpp` (defines near l.90-140; defaults block in `ConfigUtils::initUnsetPropertiesWithDefaults`, l.347)
- Modify: `esp32-s3/generated/{config,enums}.pb.{c,h}` (regen output)

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `Config.webConfigOptions` (`apEnabled`, `apSSID[32]`, `apPassphrase[64]`, `webconfigTransport`), `DEFAULT_AP_SSID="GP2040-CE"`, `DEFAULT_AP_PASSPHRASE="gp2040config"`, used by Tasks 3–6.

- [ ] **Step 1: Append the schema to `proto/config.proto`**

```proto
enum WebconfigTransport {
    WEBCONFIG_TRANSPORT_USB = 0;
    WEBCONFIG_TRANSPORT_WIFI = 1;
}
message WebConfigOptions {
    optional bool apEnabled = 1;
    optional string apSSID = 2 [(nanopb).max_length = 32];
    optional string apPassphrase = 3 [(nanopb).max_length = 64];
    optional WebconfigTransport webconfigTransport = 4;
}
// inside message Config, after BootModeOptions:
    optional WebConfigOptions webConfigOptions = 17;
```

- [ ] **Step 2: Add defaults in `src/config_utils.cpp`**

```cpp
#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID "GP2040-CE"
#endif
#ifndef DEFAULT_AP_PASSPHRASE
#define DEFAULT_AP_PASSPHRASE "gp2040config"
#endif
#ifndef DEFAULT_WEBCONFIG_TRANSPORT
#define DEFAULT_WEBCONFIG_TRANSPORT WEBCONFIG_TRANSPORT_USB
#endif
```
Plus a `// webConfigOptions` block in `ConfigUtils::initUnsetPropertiesWithDefaults` following the `// peripheralOptions` alias style:
```cpp
WebConfigOptions & webConfigOptions = config.webConfigOptions;
INIT_UNSET_PROPERTY(config.webConfigOptions, apEnabled, false);
INIT_UNSET_PROPERTY_STR(config.webConfigOptions, apSSID, DEFAULT_AP_SSID);
INIT_UNSET_PROPERTY_STR(config.webConfigOptions, apPassphrase, DEFAULT_AP_PASSPHRASE);
INIT_UNSET_PROPERTY(config.webConfigOptions, webconfigTransport, DEFAULT_WEBCONFIG_TRANSPORT);
```

- [ ] **Step 3: Regen nanopb and Pico-build to verify**

Run: `build/venv/bin/python lib/nanopb/generator/nanopb_generator.py -q -D /tmp/pb -I proto -I lib/nanopb/generator/proto proto/enums.proto proto/config.proto` (in `~/gp2040-wsl`)
Expected: 4 outputs; `grep -c WebConfigOptions /tmp/pb/config.pb.h` nonzero. Copy the 4 outputs to `esp32-s3/generated/`.

- [ ] **Step 4: Commit**

```bash
git add proto/config.proto src/config_utils.cpp esp32-s3/generated/
git commit -m "feat(s3-webconfig): WebConfigOptions schema + defaults"
```

---

### Task 2: RTC boot-word persistence (S3 reboot-into-mode)

**Files:**
- Create: `src/system_bootword.h`, `src/system_bootword.cpp`
- Create: `tests/host/test_bootword.cpp`
- Modify: `tests/host/Makefile` (add `test_bootword` target beside `test_ble_report`)
- Modify: `src/system.cpp` + `headers/system.h` (S3 `reboot()` stores, `takeBootMode()` reads+c clears) — ESP-guarded only

**Interfaces:**
- Consumes: `System::BootMode` enum values (`GAMEPAD=0x43d566cd`, `WEBCONFIG=0xe77784a5`, `USB=0xf737e4e1`).
- Produces: `uint32_t packBootWord(System::BootMode)`, `System::BootMode unpackBootWord(uint32_t)` (unknown → `DEFAULT`), used by Task 6 (`/api/reboot`).

- [ ] **Step 1: Write the failing host test**

```cpp
// tests/host/test_bootword.cpp
#include <cassert>
#include <cstdio>
#include "system_bootword.h"
int main() {
    assert(unpackBootWord(packBootWord(System::BootMode::WEBCONFIG)) == System::BootMode::WEBCONFIG);
    assert(unpackBootWord(packBootWord(System::BootMode::GAMEPAD)) == System::BootMode::GAMEPAD);
    assert(unpackBootWord(0x00000000) == System::BootMode::DEFAULT);
    assert(unpackBootWord(0xdeadbeef) == System::BootMode::DEFAULT);
    printf("bootword: all assertions passed\n");
    return 0;
}
```
(`system_bootword.h` includes `headers/system.h` for the enum; compile with `-I../../headers`.)

- [ ] **Step 2: Run it to verify it fails**

Run: `g++ -std=c++17 -Wall -Werror -I../../headers test_bootword.cpp -o /tmp/tbw && /tmp/tbw`
Expected: FAIL (no such file `system_bootword.h`).

- [ ] **Step 3: Implement `src/system_bootword.{h,cpp}`**

```cpp
// system_bootword.h
#pragma once
#include <stdint.h>
#include "system.h"
uint32_t packBootWord(System::BootMode mode);
System::BootMode unpackBootWord(uint32_t word);
// system_bootword.cpp
#include "system_bootword.h"
static const uint32_t BOOTWORD_MAGIC = 0xB007C0DE;
uint32_t packBootWord(System::BootMode mode) { return BOOTWORD_MAGIC ^ (uint32_t)mode; }
System::BootMode unpackBootWord(uint32_t word) {
    uint32_t mode = word ^ BOOTWORD_MAGIC;
    if (mode == (uint32_t)System::BootMode::GAMEPAD) return System::BootMode::GAMEPAD;
    if (mode == (uint32_t)System::BootMode::WEBCONFIG) return System::BootMode::WEBCONFIG;
    if (mode == (uint32_t)System::BootMode::USB) return System::BootMode::USB;
    return System::BootMode::DEFAULT;
}
```

- [ ] **Step 4: Wire S3 `System::reboot/takeBootMode` (ESP-guarded)**

In `src/system.cpp`: `RTC_DATA_ATTR static uint32_t s3_boot_word = 0;` at file scope; in S3 `reboot()`: `s3_boot_word = packBootWord(bootMode);` before `esp_restart()`; in S3 `takeBootMode()`: `uint32_t w = s3_boot_word; s3_boot_word = 0; return unpackBootWord(w);` (replacing `return DEFAULT`).

- [ ] **Step 5: Run host test + both builds**

Run: the g++ command from Step 2. Expected: PASS.
Run: S3 `bash ~/s3build.sh build` green; Pico `cmake --build build` green.

- [ ] **Step 6: Commit**

```bash
git add src/system_bootword.h src/system_bootword.cpp tests/host/test_bootword.cpp tests/host/Makefile src/system.cpp headers/system.h
git commit -m "feat(s3-webconfig): RTC boot-word persistence + host test"
```

---

### Task 3: SPIFFS `www` partition + bundle plumbing

**Files:**
- Modify: `esp32-s3/partitions.csv` (append `www` row)
- Modify: `esp32-s3/main/CMakeLists.txt` (REQUIRES += `spiffs`; SPIFFS image rule)
- Build-side: `www/build` React output → staging dir consumed by the image rule (gitignored staging, never committed)

**Interfaces:**
- Consumes: nothing (partition math only).
- Produces: mounted `/www` SPIFFS with the React bundle, consumed by Task 4 static serving.

- [ ] **Step 1: Append the partition (8 MB fit first)**

Current payload ends `0x1F8000` (~1.97 MB). Append:
```csv
www,      data, spiffs,  ,        0x400000,
```
New total: `0x1F8000 + 0x400000 = 0x5F8000` (~6.2 MB) < `0x800000` (8 MB). Verify by arithmetic in the commit message; the guards task (Task 7) enforces it in CI.

- [ ] **Step 2: Wire the image rule + REQUIRES**

In `esp32-s3/main/CMakeLists.txt`: add `spiffs` to `REQUIRES`; add `spiffs_create_partition_image(www ../spiffs_root FLASH_IN_PROJECT)` (standard IDF helper; `../spiffs_root` is WSL-build-side staging, gitignored, populated from the Pico clone's `www/build` output before `idf.py build`).

- [ ] **Step 3: Verify without flashing**

Run: S3 `bash ~/s3build.sh build` green; `idf.py partition-table` shows `www` at `0x1F8000` len `0x400000`; `ls` the built `www.bin` (~2.4 MB expected).
Expected: all present, build green.

- [ ] **Step 4: Commit**

```bash
git add esp32-s3/partitions.csv esp32-s3/main/CMakeLists.txt
git commit -m "feat(s3-webconfig): SPIFFS www partition + image rule"
```

---

### Task 4: HTTP backend port (core endpoints)

**Files:**
- Create: `src/webconfig_s3.cpp` (add to S3 SRCS)
- Reference (read-only): `src/webconfig.cpp` handler bodies for each endpoint below

**Interfaces:**
- Consumes: Task 1 (`WebConfigOptions`), Task 3 (`/www` mount), `ConfigUtils`, `Storage`, ArduinoJson.
- Produces: working `GET /api/getFirmwareVersion`, `GET /api/getConfig`, `POST /api/setConfig`, `POST /api/reboot`, `POST /api/resetSettings`, static `/` serving — the bring-up slice Task 5 stands on.

Server shape (write once, reuse in Tasks 5–6):
```cpp
static httpd_handle_t s3_httpd = nullptr;
static esp_err_t s3_json_get(httpd_req_t *req, std::string (*fn)());
static esp_err_t s3_json_post(httpd_req_t *req, std::string (*fn)(const char *body, size_t len));
// POST body: httpd_req_recv loop into a 16KB stack-or-static buffer (mirror LWIP_HTTPD_POST_MAX_PAYLOAD_LEN); on overflow return 400.
// Every JSON response: httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); httpd_resp_send(...)
// Static: catch-all GET handler maps URI -> /www + path (default /www/index.html for the spaPaths list, exact match otherwise), MIME map {.html,.css,.js,.json,.png,.ico,.svg} via fopen/fread/fclose + httpd_resp_send with httpd_resp_set_type.
void startWebconfigServer(); // httpd_start with .stack_size = 12288 (16KB ArduinoJson docs must not live on the 4096 default), register URIs below, start
void stopWebconfigServer();  // httpd_stop + null the handle
```

- [ ] **Step 1: Server skeleton + static serving + `/api/getFirmwareVersion`**

Mirror Pico `getFirmwareVersion()` keys (`version,boardArchitecture,boardBuild,boardBuildType,boardConfigLabel,boardConfigFileName,boardConfig`) from `Storage`/generated `version.h` (`boardConfigLabel` = `"ESP32S3DevKitC1"` as in the S3 CMakeLists board definition).

- [ ] **Step 2: Verify by curl against hardware**

Flash, boot normally, start server via a TEMP `startWebconfigServer()` call? No TEMP: this task also wires a temporary bring-up? No — server start wiring is Task 5. For THIS task's verification, add the server start behind the same condition Task 5 will own? That couples tasks. Resolution: Task 4 verification = build green + `idf.py partition-table` + a host-side compile check only; live curl moves to Task 5 (server actually starts there). State that explicitly in the commit message. Hmm — but "independently testable" per skill... A task whose deliverable is unverified code violates the spirit. Alternative honest split: Task 4 = skeleton + static + firmware-version AND a minimal always-on-if-built? No — always-on server changes behavior prematurely.

Cleaner: merge server-lifecycle into Task 4 (start/stop functions + call them from `GP2040::run` startup unconditionally for now? That makes AP-less HTTP listen on... no interfaces exist yet — harmless, and curl-against-USB fails until Task 5. Still unverifiable until Task 5.

Decision: Task 4 implements skeleton + static + 5 endpoints; verification = S3+Pico builds green + host `curl` shape review impossible without transport → mark verification as "build green; live curl deferred to Task 5, which exercises every endpoint added here." Document the deferral in the task (honest, reviewer-approved). The skill wants testable units; the reviewable unit here is the ported code + green builds. Accept with explicit note.

- [ ] **Step 3: Port `/api/getConfig`, `/api/setConfig`, `/api/reboot`, `/api/resetSettings`**

`getConfig` = `ConfigUtils::toJSON` equivalent (mirror Pico `getConfig()`); `setConfig` = `ConfigUtils::fromJSON` + `save(true)` with 200/400/500 mapping via status codes; `reboot` = `GPRestartEvent(mode)` honoring `bootMode` 0/1/2 (mode persistence via Task 2); `resetSettings` = `ResetSettings()`.

- [ ] **Step 4: Commit**

```bash
git add src/webconfig_s3.cpp esp32-s3/main/CMakeLists.txt
git commit -m "feat(s3-webconfig): HTTP backend core endpoints + static serving"
```

---

### Task 5: Settings endpoint groups (gamepad/pins/profiles/keys)

**Files:**
- Modify: `src/webconfig_s3.cpp` (append handlers + URI registrations)
- Reference: Pico `getGamepadOptions/setGamepadOptions` (incl. `hotkey_01..16`, `usbDesc*` strings), `getPinMappings/setPinMappings`, `getProfileOptions/setProfileOptions`, `getKeyMappings/setKeyMappings`, `getBootModeOptions/setBootModeOptions`

**Interfaces:**
- Consumes: Task 4 server shape (add cases, no new infra).
- Produces: gamepad/pin/profile/key/bootmode parity with Pico contract.

- [ ] **Step 1: Port the six GET/POST pairs + bootmode pair**

Each POST: deserialize 16KB doc, mirror the Pico setter field-for-field (including `hotkey_01..16.{auxMask,buttonsMask,action}` and `usbDesc{Manufacturer,Product,Version}` strings), `Storage::save` semantics identical to Pico (save vs `GPStorageSaveEvent` per Pico handler). `setBootModeOptions`: persist `enabled,webConfigPinMask,usbModePinMask,inputModeMappings[]` (pinMask-based selection works on S3 via existing `getGpioMappedBootAction`).

- [ ] **Step 2: Verify by build**

Run: S3 + Pico builds green. Live curl deferred to Task 7 end-to-end (server starts in Task 6).

- [ ] **Step 3: Commit**

```bash
git add src/webconfig_s3.cpp
git commit -m "feat(s3-webconfig): gamepad/pins/profiles/keys/bootmode endpoints"
```

---

### Task 6: Settings endpoint groups (LED/display/addons/peripherals)

**Files:**
- Modify: `src/webconfig_s3.cpp`
- Reference: Pico `get/set{DisplayOptions,LedOptions,AddonsOptions(all ~150 keys),WiiControls,MacroAddonOptions,PeripheralOptions,I2CPeripheralMap,ExpansionPins,HETrigger*,ReactiveLEDs,AnimationProtoOptions(+test modes),Lights*×4,SplashImage,BoardDefinition,MemoryReport,UsedPins,HeldPins,JoystickCenter×2,PS4Options}`

**Interfaces:**
- Consumes: Task 4–5 server shape.
- Produces: full URI parity (every row of the contract table).

Port notes (S3 adaptations, exact):
- `getMemoryReport`: `totalFlash`/`usedFlash`/`physicalFlash` from `Storage::GetFlashSize`-equivalents + `spi_flash_get_chip_size()`; `totalHeap`/`usedHeap` from `esp_get_free_heap_size()`/`esp_get_minimum_free_heap_size()`; `staticAllocs` = 0 (Pico-specific).
- `getBoardDefinition`: `minPin 0, maxPin 48`, `analogPins` from the `hal_adc_s3.cpp` channel table, `availablePins` = routable set minus 19/20, `usedPins` from pin mappings (mirror Pico shape).
- `getHeldPins`/`abortGetHeldPins`: GPIO scan via `hal::gpioGet` (5 s cap + abort flag, same contract).
- `getJoystickCenter[2]`: live reads via `halAdcRead` (see `hal_esp32s3/hal_adc_s3.cpp`).
- `setHETriggerOptions`/`getHETriggerVoltage`: RAM-only cal globals + ADC init + mux reads via S3 ADC backend (no save, like Pico).
- Animation test modes + `RestartLedSystem()`: S3 AnimationStation exists — call through identically.
- `setPreviewDisplayOptions`: no save (like Pico).
- `setPS4Options`: persist base64 serial/signature fields only (no host needed).
- `setPeripheralOptions`: persist I2C/SPI blocks; skip Pico's USB D± addon-pin reservation on S3 (no USB-PIO init).
- `getSplashImage`/`setSplashImage`: base64 through `DisplayOptions.splashImage` (1024 max, proto already caps).

- [ ] **Step 1: Port all handlers in the list above**

- [ ] **Step 2: Verify by build**

Run: S3 + Pico builds green. Live curl in Task 8.

- [ ] **Step 3: Commit**

```bash
git add src/webconfig_s3.cpp
git commit -m "feat(s3-webconfig): LED/display/addons/peripherals endpoints"
```

---

### Task 7: WiFi AP bring-up + boot actions + React fields

**Files:**
- Modify: `src/webconfig_s3.cpp` (AP lifecycle + server start/stop calls)
- Modify: `src/gp2040.cpp` (S3 L1/L2-hold actions; remove S3 `INPUT_MODE_CONFIG→GENERIC` demotion l.193-200; CONFIG mode starts webconfig, not a gamepad driver)
- Modify: `www/src/Pages/SettingsPage.jsx` (+ `Locales/en/SettingsPage.jsx` key): AP enable switch, SSID/passphrase text fields (maxLength 32/64 mirroring proto), transport pref select; Formik+yup following the `fourWayMode`/`usbDescProduct` patterns
- Modify: `esp32-s3/main/CMakeLists.txt` REQUIRES += `esp_wifi nvs_flash esp_netif`

**Interfaces:**
- Consumes: Tasks 1–6 (settings exist, server exists, endpoints exist).
- Produces: joinable AP serving the full UI; L1/L2 boot behavior; S2 default per pref.

AP bring-up sequence (exact IDF calls):
```cpp
nvs_flash_init(); esp_netif_init(); esp_event_loop_create_default();
esp_netif_create_default_wifi_ap(); wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&cfg);
wifi_config_t ap = {}; strcpy((char*)ap.ap.ssid, ssid); strcpy((char*)ap.ap.password, pass);
ap.ap.authmode = WIFI_AUTH_WPA2_PSK; ap.ap.max_connection = 4;
esp_wifi_set_mode(WIFI_MODE_AP); esp_wifi_set_config(WIFI_IF_AP, &ap); esp_wifi_start();
// default AP netif runs DHCP; verify a client leases 192.168.4.x
```
Boot logic (S3-guarded, in `getButtonMappedBootAction` region + setup): L1-hold with invalid stored `inputModeL1` → WiFi-config boot; L1-hold with valid stored mapping → honor mapping; L2-hold → reserved (normal boot until USB phase); S2-hold → CONFIG with `webconfigTransport` pref (after demotion removal); saved `apEnabled` → AP without any hold. CONFIG mode on S3: start AP-if-requested + server, skip gamepad USB driver setup.

- [ ] **Step 1: AP lifecycle + boot actions + demotion removal**

- [ ] **Step 2: React fields + full web build + end-to-end hardware test**

Rebuild React (`npm run build` in `~/gp2040-wsl/www`), refresh SPIFFS staging, flash S3. Then, on hardware: L1-boot → AP `GP2040-CE` appears → join with `gp2040config` → `http://192.168.4.1/` loads UI → `curl http://192.168.4.1/api/getFirmwareVersion` returns board JSON → change a setting in UI → save → reboot → persists → gamepad inputs live during AP session (XInput panel).

- [ ] **Step 3: Commit (code + bundle staging note, never the bundle)**

```bash
git add src/webconfig_s3.cpp src/gp2040.cpp www/src/Pages/SettingsPage.jsx www/src/Locales/en/SettingsPage.jsx esp32-s3/main/CMakeLists.txt
git commit -m "feat(s3-webconfig): WiFi AP bring-up + boot actions + AP settings UI"
```

---

### Task 8: Guards, docs, final green, push

**Files:**
- Modify: `tests/host/check-s3-guards.sh` (append checks)
- Modify: `docs/s3-port-constraints.md` (webconfig section)
- Modify: `tests/host/Makefile` + `tests/host/test_bootword.cpp` already covered in Task 2

**Interfaces:**
- Consumes: everything above.
- Produces: merge-ready green state on both toolchains + fork push.

New guard checks (concrete):
```bash
# partitions fit 8MB: sum Size column of esp32-s3/partitions.csv ≤ 0x800000
# transport default pinned: grep -q "DEFAULT_WEBCONFIG_TRANSPORT WEBCONFIG_TRANSPORT_USB" src/config_utils.cpp
# no creds in logs: ! grep -q "apPassphrase" esp32-s3/main/CMakeLists.txt src/webconfig_s3.cpp (excluding the documented default define)
```

- [ ] **Step 1: Add guards + docs section, run everything**

Run: `bash tests/host/check-s3-guards.sh` (all PASS incl. new), `make test` in `tests/host` (ble + bootword PASS), S3 `bash ~/s3build.sh build` green, Pico `cmake --build build` green, flash + AP end-to-end recheck.

- [ ] **Step 2: Commit + push**

```bash
git add tests/host/check-s3-guards.sh docs/s3-port-constraints.md
git commit -m "test(s3-webconfig): guards + docs for webconfig bring-up"
git push origin feature/esp32s3-poc
```

---
