# Task 3 report: RNDIS class driver + netif glue

## Status: DONE (hardware enumerate check deferred to controller per brief)

## What was implemented

1. **`headers/tusb_config.h:181-184`** — flipped `CFG_TUD_ECM_RNDIS` to `1` in the
   `ESP_PLATFORM` branch (Pico branch untouched, still `1`). Comment updated:
   driver compiled in, no descriptor opens it yet.
2. **`src/usbnet_s3.h`** (append-only) — `#if defined(ESP_PLATFORM)` block with:
   `s3_usbnet_frame_t {const uint8_t *data; uint16_t len}` (the `ref` convention
   for `tud_network_xmit`), `s3_usbnet_init(const uint8_t mac[6])`,
   `s3_usbnet_start(esp_netif_t *netif)`, `s3_usbnet_stop()` (exact brief
   signatures). Validation function above the guard untouched.
3. **`src/usbnet_s3.cpp`** (append-only) — `#if defined(ESP_PLATFORM)` block with:
   - `tud_network_mac_address[6] = {0x02,0x02,0x84,0x6A,0x96,0x01}` (last byte
     differs from Pico `lib/rndis` `...:00` so both boards on one PC never clash).
   - `tud_network_recv_cb` → `esp_netif_receive(netif, buf, len, NULL)` when a
     netif is attached, else `false`; null/zero-size frames refused.
   - `tud_network_xmit_cb` → memcpy from `s3_usbnet_frame_t *ref` (null-safe,
     returns 0 on bad input). The driver-facing half (`tud_network_xmit` call
     after `tud_network_can_xmit`) belongs to the bring-up task, which will own
     the `esp_netif` driver transmit registration.
   - `tud_network_init_cb` → no queued state (recv delivers synchronously); calls
     `esp_netif_action_connected()` only when a netif is attached (inert today).
   - `s3_usbnet_init` (MAC override, null keeps current), `s3_usbnet_start/stop`
     (attach/detach `esp_netif_t*`). Callbacks are `extern "C"` matching
     `lib/tinyusb/src/class/net/net_device.h:63-88`.
4. **`esp32-s3/main/CMakeLists.txt`** — added `"../../src/usbnet_s3.cpp"` to SRCS
   after `webconfig_s3.cpp` (explicit list, no glob). No INCLUDE_DIRS change
   needed: quote-include resolves relative to the TU's own `src/` dir (same as
   existing `webconfig_scan.h` precedent).

## Step-1 reuse verdict: DO NOT link `lib/rndis` — wrote new glue

Read `lib/rndis/rndis.h` + `rndis.c` (260 lines). Reuse rejected for three
independent, each-sufficient reasons:

1. **Pico hardware-header dependency** — `rndis.c:260` `sys_now()` calls
   `get_absolute_time()`/`to_ms_since_boot()` (Pico SDK `pico/time.h`),
   violating the brief's reuse precondition (only `tusb.h`/lwIP/`esp_netif`
   allowed).
2. **Wrong network-stack model** — it drives raw lwIP (`netif_add`,
   `ethernet_input`, `pbuf_*`) plus Pico-vendored `dhserver/dnserver/httpd/mdns`
   servers, whereas the plan architecture requires an `esp_netif` handle owned
   by the S3 bring-up task (IDF DHCP server).
3. **Wrong entry-point API + task model** — exposes `rndis_init(hostname)` /
   `rnis_task()` with its own `tud_task()` pump loop (also called from its
   `linkoutput_fn`), incompatible with the IDF TinyUSB task model and with the
   required `s3_usbnet_init/start/stop` interface. It also hard-codes Pico's
   `...:00` MAC.
   Only its *shape* was reused: recv_cb → netif input, xmit via
   `tud_network_xmit`, init_cb state reset — mapped onto `esp_netif_receive` /
   `s3_usbnet_frame_t` / `esp_netif_action_connected`.

## Tests (exact commands + outputs)

- Host tests:
  `wsl bash -c 'cd /mnt/c/Users/PPSHS*/Downloads/GP2040-CE/.worktrees/esp32s3-poc/tests/host && make test'`
  (glob avoids the spaces-in-path quoting breakage of the brief's literal
  command; same directory, same `make test`). Output: all five suites pass,
  including `usbnet validation PASS`. Proves the ESP guard keeps the TU
  host-compilable.
- Sync: all four changed files `Copy-Item`'d to `~/gp2040-s3build/<same-path>`
  and `~/gp2040-wsl/<same-path>`; `diff` worktree↔both clones clean.
- S3: `wsl bash ~/s3build.sh build` → `Project build complete`, exit 0
  (verified `S3_EXIT_0` on incremental re-run). Evidence RNDIS is compiled in:
  `build/esp-idf/espressif__tinyusb/.../src/class/net/ecm_rndis_device.c.obj`
  and `build/esp-idf/main/.../src/usbnet_s3.cpp.obj` both exist.
- Pico: `wsl bash -c 'cmake --build ~/gp2040-wsl/build'` → `Built target
  GP2040-CE`, `PICO_EXIT_0` (only pre-existing warnings).

## Commit

`6c253983` — `s3-usb: RNDIS class driver + netif glue (disabled, no descriptor
change)` (verbatim brief message). Staged exactly the four brief-listed files
(`git show --stat HEAD`: 4 files, 118 insertions, 3 deletions). Local only;
NOT pushed. NOT flashed (no esptool/COM use).

## Self-review findings

- [x] Signatures match brief exactly (`init(const uint8_t mac[6])`,
      `start(esp_netif_t*)`, `stop()`); MAC bytes as specified.
- [x] No descriptor file touched → enumeration byte-identical by construction;
      hardware enumerate check is the controller's per brief.
- [x] Init/start/stop uncalled + no open RNDIS interface ⇒ callbacks inert;
      `init_cb`'s `esp_netif_action_connected` is dead code until bring-up
      attaches a netif (verified `s_netif` starts null, only `start()` sets it).
- [x] Pico unaffected: `usbnet_s3.*` not in Pico SRCS, `tusb_config.h` change
      S3-guarded; Pico symbols (`lib/rndis` MAC/callbacks) unshadowed.
- [x] No stray files staged (test binaries, `.superpowers/`,
      `build-s3-flash/` left untracked).

## Concerns

1. **Minor API-shape note (by design):** the xmit *driver-facing* half
   (`tud_network_xmit(ref,0)` call + `s3_usbnet_frame_t` lifetime) is the
   bring-up task's job; this task delivers the stack-facing half
   (`tud_network_xmit_cb`) plus the documented `ref` convention. Flagged in
   code comments so Task 4+ doesn't guess.
2. **Hardware check outstanding:** gamepad-enumerates-as-before verification
   needs the controller's flash + USB check (explicitly out of scope here).
3. **Tooling note (no code impact):** the brief's literal quoted `wsl bash -c`
   host-test command breaks under PowerShell 5.1 quote-mangling when the path
   contains spaces; the glob-spelling used here runs the identical `make test`.

## Fix round 1/5: xmit-path destination-capacity check

**Finding:** reviewer's Important — `tud_network_xmit_cb` copied `frame->len`
bytes with no `dst`-capacity check; `tud_network_can_xmit(size)` in
`lib/tinyusb/src/class/net/ecm_rndis_device.c:420-425` ignores `size`, so one
oversized frame = overrun of the driver's `transmitted[]` buffer.

**Change** (`src/usbnet_s3.cpp`, S3-guarded, minimal):
- Added `CFG_TUD_NET_MTU` fallback-default comment block (1514, matching
  `net_device.h`) after the `tusb.h`/`esp_netif.h` includes.
- In `tud_network_xmit_cb`, before the copy: `if (frame->len >
  CFG_TUD_NET_MTU) return 0;` — oversized frame dropped safely (no copy, no
  overflow, 0 = nothing transmitted). Rationale: `data` passed to the
  callback sits at `transmitted + prefix`, so MTU is the conservative
  per-frame cap in both ECM and RNDIS modes. Nothing else restructured.

**Covering checks (exact commands + outputs):**
- Host: `wsl bash -c 'cd /mnt/c/Users/PPSHS*/Downloads/GP2040-CE/.worktrees/esp32s3-poc/tests/host && make test'`
  (glob-spelling of the brief's literal command; identical dir/target) →
  all five suites pass, including `usbnet validation PASS` (exit 0).
- Sync: `Copy-Item src\usbnet_s3.cpp` to `~/gp2040-s3build/src/` and
  `~/gp2040-wsl/src/`; worktree↔s3build diff clean.
- S3: `wsl bash ~/s3build.sh build` → `Project build complete`, exit 0
  (`usbnet_s3.cpp.obj` rebuilt + relinked in the log above).

NOT pushed. NOT flashed.
