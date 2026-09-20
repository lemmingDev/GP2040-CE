// S3 link shims (RAM-only stubs for USB-host-side auth backends).
//
// Task 3 replaced the FlashPROM section below with the real esp_partition
// backend (lib/FlashPROM/src/FlashPROM_esp32.cpp, in SRCS). Task 3c replaced
// the ConfigUtils fresh-defaults stub with the real src/config_utils.cpp +
// src/config_legacy.cpp (guarded XIP-deref/core-id ports, in SRCS): the
// save()->true shim is now DEAD (real ConfigUtils::save links; this TU no
// longer defines any ConfigUtils symbol) and has been removed. Do NOT copy
// these into the Pico build (this file is S3-SRCS-only and is never touched
// by the Pico CMake).

#include "drivers/xbone/XBOneAuth.h"
#include "drivers/ps4/PS4Auth.h"

#if defined(ESP_PLATFORM)

// ---- XBOneAuth device-side link stubs (Task 6: XBOne device parity) ----
// XBOneDriver.cpp (device-side, in S3 SRCS) news XBOneAuth and calls
// available()/initialize()/process(), whose definitions live in
// src/drivers/xbone/XBOneAuth.cpp — a USB-HOST-side TU (host/usbh.h,
// usbhostmanager.h, hid_host) that is parked with USB host/auth (Phase 2)
// and is NOT in the S3 SRCS. These stubs satisfy the link.
// S3 AUTH STAYS NONE UNTIL PHASE 2 USB-HOST LANDS, regardless of the config
// value: even if a persisted config selects a non-NONE xinputAuthType, this
// available()->false keeps initializeAux() from arming xboxOneAuthData, so
// getAuthSent() stays false and processAux()'s available() gate keeps the
// stub process() unreachable — silently no-auth, exactly like Pico with USB
// host disabled (PeripheralManager::isUSBEnabled(0) false). Pico's
// XBOneAuth::available() would ALSO return false here (host never starts on
// S3: gp2040aux host-start is PICO_BOARD-gated), so this is behaviorally
// exact for every S3-reachable state.
// DELETE this section when the real XBOneAuth backend lands (USB-host
// bring-up, Phase 2).
bool XBOneAuth::available() { return false; }
void XBOneAuth::initialize() {}
void XBOneAuth::process() {}

// ---- PS4Auth device-side link stubs (Task 6: PS4 device parity) ----
// PS4Driver.cpp (device-side, in S3 SRCS) calls PS4Auth::initialize(),
// available(), process(), and resetAuth(), whose definitions live in
// src/drivers/ps4/PS4Auth.cpp — a USB-HOST-side TU (host/usbh.h,
// usbhostmanager.h, hid_host) that is parked with USB host/auth (Phase 2)
// and is NOT in the S3 SRCS. These stubs satisfy the link.
// S3 AUTH STAYS NONE UNTIL PHASE 2 USB-HOST LANDS, regardless of the config
// value: even if a persisted config selects a non-NONE ps4AuthType/
// ps5AuthType, this available()->false keeps initializeAux() from arming
// ps4AuthData, so get_report/processAux take the same no-auth path as
// Pico-with-NONE — silently no-auth, exactly like Pico with USB host
// disabled. Non-NONE auth types MUST NOT be honored on S3 before the
// Phase-2 host-auth backend exists.
// DELETE this section when the real PS4Auth backend lands (USB-host
// bring-up, Phase 2).
bool PS4Auth::available() { return false; }
void PS4Auth::initialize() {}
void PS4Auth::process() {}
void PS4Auth::resetAuth() {}

#endif // defined(ESP_PLATFORM)
