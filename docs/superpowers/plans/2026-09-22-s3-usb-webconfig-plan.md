# S3 USB Webconfig (RNDIS) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serve the existing S3 webconfig HTTP backend over USB (RNDIS) as an opt-in third transport alongside WiFi AP+STA, with user-configurable AP/USB subnets.

**Architecture:** One `esp_http_server` (already wildcard-bound, no bind change) gains a third esp_netif driven by TinyUSB's in-tree RNDIS class driver; per-mode USB descriptors append the RNDIS interface only when enabled; settings ride the existing gamepad-options read/write path.

**Tech Stack:** ESP-IDF v5.4 (esp_netif, esp_http_server, DHCP server), TinyUSB device RNDIS (`ecm_rndis_device.c`, `TUD_RNDIS_DESCRIPTOR`), React webconfig, nanopb proto.

**Spec:** `docs/superpowers/specs/2026-09-22-s3-usb-webconfig-design.md`

## Global Constraints

- Fork-only branch `feature/esp32s3-poc`; never open upstream PRs, never push to upstream.
- All firmware changes S3-gated (`#if defined(ESP_PLATFORM)`); Pico builds stay green.
- `CONFIG_FREERTOS_HZ=1000` stays; no blocking loops (use `vTaskDelay`, never spin).
- Commit per task; push branch at task boundaries when hardware-verified.
- USB descriptors byte-identical to today when the feature is Off (default).

---

## File structure

| File | Responsibility |
|---|---|
| `proto/config.proto` | Add `usbNetworkMode=8`, `apSubnet=9`, `usbSubnet=10` to `WebConfigOptions` |
| `esp32-s3/generated/*` | Regenerated nanopb output (via `compile_proto.cmake` recipe) |
| `src/config_utils.cpp` | `DEFAULT_*` macros + `INIT_UNSET_PROPERTY` for the 3 fields |
| `src/usbnet_s3.cpp` / `src/usbnet_s3.h` (new) | RNDIS control plane + TinyUSB↔netif glue; subnet validation pure function |
| `headers/tusb_config.h` | Flip `CFG_TUD_ECM_RNDIS` to 1 under `ESP_PLATFORM` |
| `src/webconfig_s3.cpp` | gamepad-options read/write of 3 fields; `apSubnet`/`usbSubnet` applied via `esp_netif_set_ip_info` + DHCP reconfig; network status gains `usbEnabled`/`usbIP` |
| `src/gp2040.cpp` | Boot resolution: S1+S2 hold override + setting/config-mode gating |
| `headers/drivers/*/*Descriptors.h` | Per-mode config descriptor: appended RNDIS interface when enabled |
| `www/src/Pages/SettingsPage.jsx` | USB + subnet UI in the webconfig tab |
| `www/src/Locales/en/*` | New strings (pattern-match neighboring keys) |
| `tests/host/test_usbnet.{cpp,h}` (new) | Host tests: subnet validation, descriptor byte asserts |
| `tests/host/Makefile` | Build + run the new test |
| `docs/s3-port-constraints.md` | Append USB-networking constraints section |

---

### Task 1: Schema, defaults, generated code

**Files:**
- Modify: `proto/config.proto` (after `staMode = 7;` in `WebConfigOptions`)
- Modify: `src/config_utils.cpp` (defaults block ~189-206, init block ~594-601)
- Modify: `esp32-s3/generated/config.pb.h`, `config.pb.c` (regen output)

**Interfaces:**
- Consumes: nanopb `compile_proto.cmake:21,26-39` regen recipe.
- Produces: `UsbNetworkMode` enum semantics (Off=0, AlwaysOn=1, ConfigModeOnly=2); `DEFAULT_USB_NETWORK_MODE/USB_OFF`, `DEFAULT_AP_SUBNET "192.168.4.0"`, `DEFAULT_USB_SUBNET "192.168.5.0"` macros; `has_*`-guarded fields.

- [ ] **Step 1: Add proto fields**

```proto
enum UsbNetworkMode {
    USB_NETWORK_OFF = 0;
    USB_NETWORK_ALWAYS_ON = 1;
    USB_NETWORK_CONFIG_MODE_ONLY = 2; }
message WebConfigOptions {
    ...
    optional StaMode staMode = 7;
    optional UsbNetworkMode usbNetworkMode = 8;
    optional string apSubnet = 9 [(nanopb).max_length = 16];
    optional string usbSubnet = 10 [(nanopb).max_length = 16]; }
```

- [ ] **Step 2: Regenerate nanopb**

```bash
build/venv/bin/python lib/nanopb/generator/nanopb_generator.py -q -D /tmp/pb -I proto -I lib/nanopb/generator/proto proto/enums.proto proto/config.proto
```

Copy the 4 outputs over `esp32-s3/generated/`. Verify `usbNetworkMode_tag 8`, `apSubnet_tag 9`, `usbSubnet_tag 10` exist in `config.pb.h`.

- [ ] **Step 3: Defaults + INIT_UNSET** (mirror the `apEnabled` lines exactly)

```cpp
#define DEFAULT_USB_NETWORK_MODE USB_NETWORK_OFF
#define DEFAULT_AP_SUBNET "192.168.4.0"
#define DEFAULT_USB_SUBNET "192.168.5.0"
// in initUnsetPropertiesWithDefaults, next to the apEnabled block:
INIT_UNSET_PROPERTY(config.webConfigOptions, usbNetworkMode, DEFAULT_USB_NETWORK_MODE);
INIT_UNSET_PROPERTY_STR(config.webConfigOptions, apSubnet, DEFAULT_AP_SUBNET);
INIT_UNSET_PROPERTY_STR(config.webConfigOptions, usbSubnet, DEFAULT_USB_SUBNET);
```

- [ ] **Step 4: Verify compile of both firmwares (no behavior change yet)**

```bash
wsl bash ~/s3build.sh build
cmake --build ~/gp2040-wsl/build
```

Expected: both exit 0. (Full WSL paths per repo convention.)

- [ ] **Step 5: Commit**

```bash
git add proto/config.proto esp32-s3/generated src/config_utils.cpp
git commit -m "s3-usb: schema + defaults for USB network mode and subnets"
```

---

### Task 2: Subnet validation (pure function + host test)

**Files:**
- Create: `src/usbnet_s3.h`, `src/usbnet_s3.cpp` (validation function only in this task)
- Create: `tests/host/test_usbnet.cpp`
- Modify: `tests/host/Makefile` (add `test_usbnet` to `test:` and a build rule mirroring `test_bootword`)

**Interfaces:**
- Consumes: nothing.
- Produces: `bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet);` — true iff both are valid IPv4 `/24` private-network addresses and differ. (String-only, no IDF headers, so the host test compiles it directly.)

- [ ] **Step 1: Write the failing test** (`tests/host/test_usbnet.cpp`)

```cpp
#include <cassert>
#include <cstdio>
#include "usbnet_s3.h"
int main() {
    assert(s3_validateSubnets("192.168.4.0", "192.168.5.0") == true);   // defaults
    assert(s3_validateSubnets("192.168.4.0", "192.168.4.0") == false);  // must differ
    assert(s3_validateSubnets("192.168.4.0", "10.0.0.0") == true);      // second private range ok
    assert(s3_validateSubnets("8.8.8.0", "192.168.5.0") == false);      // non-private rejected
    assert(s3_validateSubnets("not-an-ip", "192.168.5.0") == false);    // garbage rejected
    assert(s3_validateSubnets("192.168.4.1", "192.168.5.0") == false);  // host bits set rejected
    printf("usbnet validation PASS\n");
    return 0;
}
```

- [ ] **Step 2: Makefile rule + run to verify it fails**

```make
test_usbnet: test_usbnet.cpp ../../src/usbnet_s3.cpp
	$(CXX) $(CXXFLAGS) -I../../src -o $@ $^
```

Run: `make -C tests/host test_usbnet; ./tests/host/test_usbnet` — Expected: compile FAIL (`usbnet_s3.h` missing).

- [ ] **Step 3: Minimal implementation** (`src/usbnet_s3.h` declares the function; `src/usbnet_s3.cpp` parses dotted quads with `sscanf`, requires 4 octets 0-255, last octet 0, first octet 10 / 172.16-31 / 192.168, and `strcmp` inequality; include only `<cstdint>`, `<cstdio>`, `<cstring>` so it stays host-compilable)

- [ ] **Step 4: Run tests**

Run: `make -C tests/host test` — Expected: all PASS including `usbnet validation PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/usbnet_s3.h src/usbnet_s3.cpp tests/host/test_usbnet.cpp tests/host/Makefile
git commit -m "s3-usb: subnet validation + host test"
```

---

### Task 3: RNDIS class driver enable + netif glue

**Files:**
- Modify: `headers/tusb_config.h:181-184` (flip `CFG_TUD_ECM_RNDIS` to 1 under `ESP_PLATFORM`)
- Modify: `src/usbnet_s3.cpp`, `src/usbnet_s3.h` (append glue: MAC, `tud_network_recv_cb` → netif input, `tud_network_xmit` path, init/start/stop entry points)
- Modify: `esp32-s3/main/CMakeLists.txt` (add `../../src/usbnet_s3.cpp` to SRCS if the glob pattern doesn't cover it — check first)

**Interfaces:**
- Consumes: TinyUSB `tud_network_recv_cb`, `tud_network_xmit_cb`, `tud_network_init_cb`, `tud_network_mac_address[6]` (`lib/tinyusb/src/class/net/net_device.h:63-88`); `TUD_RNDIS_DESCRIPTOR` (`lib/tinyusb/src/device/usbd.h:766-786`).
- Produces: `void s3_usbnet_init(const uint8_t mac[6]);`, `void s3_usbnet_start(esp_netif_t *netif);`, `void s3_usbnet_stop();` — called from webconfig bring-up (Task 5).

- [ ] **Step 1: Evaluate `lib/rndis` reuse (read-only)**

Read `lib/rndis/rndis.h` + `rndis.c` (~200 lines: MAC at `:65`, callbacks at `:156,180,205`, task at `:239`). If it includes no Pico hardware headers (only `tusb.h`, lwIP `netif/etharp`, `esp_netif` optional), link it for S3 instead of writing new glue: add its path to the S3 build SRCS and skip to Step 3. Otherwise write the glue in `src/usbnet_s3.cpp` following its shape (recv_cb → `netif->input(p, netif)`; xmit → `tud_network_xmit`; init_cb → netif link up).

- [ ] **Step 2: Enable + wire (whichever glue from Step 1)**

```c
// headers/tusb_config.h, ESP_PLATFORM branch:
#define CFG_TUD_ECM_RNDIS 1
```

MAC: locally-administered `02:02:84:6A:96:01` (Pico's `lib/rndis` uses `...:00`; last byte differs so both boards on one PC never clash).

- [ ] **Step 3: S3 firmware still builds and boots with RNDIS compiled but no descriptor using it**

```bash
wsl bash ~/s3build.sh build
cmake --build ~/gp2040-wsl/build
```

Expected: both exit 0 (the `tusb_config.h` change is S3-guarded; Pico proves it). Flash S3 + boot: gamepad enumerates exactly as before (no descriptor change yet in this task — verify on hardware: USB device visible, inputs work).

- [ ] **Step 4: Commit**

```bash
git add headers/tusb_config.h src/usbnet_s3.cpp src/usbnet_s3.h esp32-s3/main/CMakeLists.txt
git commit -m "s3-usb: RNDIS class driver + netif glue (disabled, no descriptor change)"
```

---

### Task 4: RNDIS USB netif + DHCP + httpd sharing

**Files:**
- Modify: `src/usbnet_s3.cpp` (USB esp_netif create, static IP from `usbSubnet`, DHCP server start, link callbacks)
- Modify: `src/webconfig_s3.cpp` (call into glue from bring-up; extend `s3_getNetworkStatus` with `usbEnabled`/`usbIP`)

**Interfaces:**
- Consumes: `s3_wifi_base_init` pattern (`src/webconfig_s3.cpp:4032-4078`); stored `usbSubnet`; Task 3 glue entry points.
- Produces: USB netif up with `.1` of `usbSubnet`, DHCP serving the host; `GET /api/getNetworkStatus` returns `usbEnabled` + `usbIP` alongside existing keys.

- [ ] **Step 1: USB netif bring-up function**

Mirror the AP-netif sequence (create → set IP info from parsed `usbSubnet` + `.1` → DHCP server start → netif up), reusing `s3_validateSubnets` result: on invalid stored subnets, fall back to compiled defaults (never fail the boot). Parse with the same dotted-quad code as validation (share a `s3_subnetToIp` helper in `usbnet_s3.cpp`; extend the Task 2 host test with 3 asserts for it: `"192.168.5.0"` → `0xC0A80500`-equivalent `esp_ip4_addr_t`, garbage → false).

- [ ] **Step 2: Extend network status (additive keys only)**

```cpp
s3_writeDoc(doc, "usbEnabled", usbNetifUp);
s3_writeDoc(doc, "usbIP", s3_usb_ip_str);  // "192.168.5.1" style, "" when down
```

- [ ] **Step 3: Host tests + S3 build**

Run: `make -C tests/host test` (new `s3_subnetToIp` asserts pass). Then `wsl bash ~/s3build.sh build` (exit 0). NOTE: no descriptor exposes RNDIS yet, so no USB behavior change — hardware check is just clean boot + `getNetworkStatus` shows `usbEnabled:0`.

- [ ] **Step 4: Commit**

```bash
git add src/usbnet_s3.cpp src/usbnet_s3.h src/webconfig_s3.cpp tests/host/test_usbnet.cpp
git commit -m "s3-usb: USB netif + DHCP + status keys (no descriptor exposure yet)"
```

---

### Task 5: Bring-up wiring (settings, boot override, AP subnet apply)

**Files:**
- Modify: `src/webconfig_s3.cpp` (gamepad-options read/write of `usbNetworkMode`/`apSubnet`/`usbSubnet` mirroring the `apEnabled` lines at :485-520/:592-600; apply `apSubnet` via `esp_netif_set_ip_info` + DHCP reconfig in the AP path; call USB bring-up when resolved active)
- Modify: `src/gp2040.cpp` (S1+S2-hold session flag next to the L1 guard at :590-601; extend the :230-242 resolution: hold wins, else setting, else config-mode implies enabled)

**Interfaces:**
- Consumes: Task 4 bring-up; `GAMEPAD_MASK_S1` (read exact value from `headers/gamepad/GamepadState.h` first — S2 is `1U<<9` at line 52).
- Produces: USB networking active iff (S1+S2 held at boot) OR (`usbNetworkMode==AlwaysOn`) OR (`ConfigModeOnly` AND config-mode boot). AP serves the configured `apSubnet`.

- [ ] **Step 1: Settings plumbing (firmware side)**

Add the 3 keys to `s3_setGamepadOptions` (assign-only-when-set, same shape as `apEnabled`) and `s3_getGamepadOptions` (`s3_writeDoc` lines). Validate subnets with `s3_validateSubnets` on SET: reject (leave stored) when invalid.

- [ ] **Step 2: AP subnet apply**

In the AP configure path (`s3_configure_ap`, ~4263-4295): after WiFi config, set the AP netif IP to `.1` of stored `apSubnet` (fallback default on invalid) and reconfigure the DHCP server range accordingly. (Today this is all IDF-default; this is the first explicit IP programming — keep the exact IDF sequence from the AP-netif creation as the template.)

- [ ] **Step 3: Boot resolution**

Add `static bool s3UsbSession` + S1+S2 exact-match guard beside the L1 guard (same `!webConfigLocked` + `inputMode` guard shape; S1+S2 must not fire when S1+S2+Up (ENTER_USB_MODE at :560) — check `Up` bit first). Extend the :230-242 block: `usbActive = s3UsbSession || mode==AlwaysOn || (mode==ConfigOnly && s3ConfigBoot)`; when active, run USB bring-up (Task 4) independent of WiFi state.

- [ ] **Step 4: Host tests + builds (S3 + Pico)**

`make -C tests/host test` PASS; `wsl bash ~/s3build.sh build` exit 0; `cmake --build ~/gp2040-wsl/build` exit 0. Hardware: clean boot, `getNetworkStatus` reflects stored mode; S1+S2 hold enables for one session (status shows `usbEnabled:1`, returns to 0 next boot without hold).

- [ ] **Step 5: Commit**

```bash
git add src/webconfig_s3.cpp src/gp2040.cpp
git commit -m "s3-usb: settings plumbing, AP subnet apply, boot override"
```

---

### Task 6: RNDIS descriptor append (HID-family drivers first)

**Files:**
- Modify: `headers/drivers/hid/HIDDescriptors.h:138-177`, `headers/drivers/switch/SwitchDescriptors.h:107-149`, `headers/drivers/ps3/PS3Descriptors.h:634-720` (both configs), `headers/drivers/ps4/PS4Descriptors.h:716-761`, `headers/drivers/keyboard/KeyboardDescriptors.h:56-64,120-127`
- Create: `tests/host/test_usbdesc.cpp` (+ Makefile rule)

**Interfaces:**
- Consumes: `TUD_RNDIS_DESCRIPTOR` + `TUD_RNDIS_DESC_LEN` (`lib/tinyusb/src/device/usbd.h:762-786`); per-driver EP usage (HID: IN `0x81`; Switch: OUT `0x02`/IN `0x81`; PS3: same; PS4: IN `0x81`/OUT `0x03`; Keyboard: IN `0x81`).
- Produces: RNDIS interface appended iff a shared `S3_USB_NET` condition holds; EP allocation rule (first free: notif intr `0x83`, bulk OUT `0x04`, bulk IN `0x85` unless the driver already uses them — verify per file while editing); `wTotalLength`/`bNumInterfaces` updated (LSB/MSB macro form where the file uses it, literal where it hardcodes `0x29`).

- [ ] **Step 1: Failing descriptor test (HID first)**

New host test includes `headers/drivers/hid/HIDDescriptors.h` and asserts the appended shape: `bNumInterfaces==2`, `wTotalLength == CONFIG1_DESC_SIZE + TUD_RNDIS_DESC_LEN`, RNDIS interface class bytes present. Run to FAIL (no RNDIS yet).

- [ ] **Step 2: HID append (the pattern all others mirror)**

Append after the HID interface: IAD + comm interface + endpoints per the `TUD_RNDIS_DESCRIPTOR` argument order `(itf, str, ep_notif, notif_size, epout, epin, epsize)`, guarded so toggle-Off keeps byte-identical output (separate `_with_net` array selected at return, mirroring XInput's copy-and-patch shape at `src/drivers/xinput/XInputDriver.cpp:565-587` — do NOT mutate the shared static in place).

- [ ] **Step 3: Test passes + mirror to Switch/PS3/PS4/Keyboard**

Extend the host test per driver (same 3 asserts each), implement each append, run green. XInput-style copy-and-patch where the driver mutates.

- [ ] **Step 4: Hardware enumerate check (HID mode, toggle on)**

Flash, enable Always-on, reboot: host OS shows RNDIS adapter + gamepad working; `http://192.168.5.1/` serves webconfig. Toggle off, reboot: descriptors byte-identical (compare `lsusb -v` interface count or re-run enum).

- [ ] **Step 5: Commit**

```bash
git add headers/drivers/hid headers/drivers/switch headers/drivers/ps3 headers/drivers/ps4 headers/drivers/keyboard tests/host/
git commit -m "s3-usb: RNDIS descriptors for HID-family drivers + byte tests"
```

---

### Task 7: RNDIS descriptors for remaining drivers

**Files:**
- Modify: `src/drivers/xinput/XInputDescriptors.h` (4 interfaces already; append as 5th), plus `Astro/PCEngine/P5General/Egret/MDMini/NeoGeo/SwitchPro/SInput/PSClassic/XboxOriginal/XBOne` descriptor headers (list from research §1; each same 3-assert treatment)
- Modify: `tests/host/test_usbdesc.cpp` (one assert block per driver)

**Interfaces:**
- Consumes: Task 6 EP-allocation rule + copy-and-patch pattern.
- Produces: Every S3-served mode exposes RNDIS when enabled, byte-identical when off.

- [ ] **Step 1: Extend descriptor test, watch fail** (one block per driver, same 3 asserts)
- [ ] **Step 2: Implement appends driver by driver** (XInput first — mind its hardcoded `0x99` total length and 4 existing interfaces; then the rest)
- [ ] **Step 3: Host tests green + both firmwares build**
- [ ] **Step 4: Hardware enumerate spot-checks** (XInput + Switch + one more, toggle on/off)
- [ ] **Step 5: Commit**

```bash
git add headers/drivers src/drivers tests/host/
git commit -m "s3-usb: RNDIS descriptors for remaining drivers"
```

---

### Task 8: Web UI (mode toggle, subnets, status)

**Files:**
- Modify: `www/src/Pages/SettingsPage.jsx` (webconfig tab: USB section + subnet fields; schema; fetch coerce; status line)
- Modify: `www/src/Locales/en/` (new keys in the file that already holds `wifi-config-header` — find it with `Select-String www/src/Locales/en -Pattern 'wifi-config-header'`)
- Modify: `www/src/Services/WebApi.js` (only if a new endpoint is needed — none planned; status rides `getNetworkStatus`)

**Interfaces:**
- Consumes: `get/setGamepadOptions` (extended keys), `getNetworkStatus` (`usbEnabled`/`usbIP`).
- Produces: USB mode select (Off/Always-on/Config-mode-only), `apSubnet`/`usbSubnet` text fields with a `yup.string().test('subnet-pair', ...)` validator (IPv4 quad + last-octet-0 + private + pair-inequality — mirror the wifi-pass-length `.test()` shape at `SettingsPage.jsx:479-487`), USB address in the status line.

- [ ] **Step 1: Schema + fields + locale keys**
- [ ] **Step 2: Status line shows USB address when `usbEnabled`**
- [ ] **Step 3: `vite build` green + headless screenshot of the Network tab (dom dump, assert new controls render)**
- [ ] **Step 4: Hardware: set Always-on + custom subnets via UI, reboot, verify `192.168.5.1`-style address serves webconfig; validation rejects equal subnets**
- [ ] **Step 5: Commit**

```bash
git add www/
git commit -m "webconfig: USB network mode + configurable subnets UI"
```

---

### Task 9: Full-matrix verification, docs, guards

**Files:**
- Modify: `docs/s3-port-constraints.md` (append USB-networking section: RNDIS choice, descriptor rule, boot resolution, subnet defaults)
- Modify: `tests/host/check-s3-guards.sh` (add: RNDIS absent from descriptors when disabled — grep the toggle guard; `CFG_TUD_ECM_RNDIS` flip present)

**Interfaces:**
- Consumes: all previous tasks.
- Produces: merged, pushed, hardware-verified feature.

- [ ] **Step 1: Guards addition + `bash tests/host/check-s3-guards.sh` green**
- [ ] **Step 2: `make -C tests/host test` green; S3 + Pico builds green**
- [ ] **Step 3: Hardware matrix** (needs the human): every gamepad mode enumerate+input with toggle off (byte-identical) and on (RNDIS adapter appears, webconfig serves); Windows + Linux RNDIS bring-up; two boards with distinct USB subnets from one PC
- [ ] **Step 4: Merge-commit style summary + push `feature/esp32s3-poc`**

```bash
git add docs/s3-port-constraints.md tests/host/check-s3-guards.sh
git commit -m "s3-usb: constraints docs + guards"
git push origin feature/esp32s3-poc
```
