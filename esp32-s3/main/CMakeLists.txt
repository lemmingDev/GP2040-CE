# ESP32-S3 app entry (Phase 0 PoC — USB device: HID + XInput via esp_tinyusb).
#
# Consumes: HID-family drivers, usesUSB() guard (Task 2), S3 HAL (Task 6).
# Produces: enumerating USB gamepad on S3 hardware (toolchain + hardware required).
#
# NOTE on src/main.cpp multicore (documented deferral, per Task 7 brief / YAGNI):
#   src/main.cpp calls Pico-only multicore_launch_core1() and
#   multicore_lockout_victim_init(), and includes "pico/multicore.h".
#   The multicore split is deliberately NOT rewritten in this task. On S3 this
#   file does not compile as-is; the expected Phase-1 fix is an ESP_PLATFORM
#   guard mapping core1() onto a pinned FreeRTOS task (e.g.
#   xTaskCreatePinnedToCore(..., 1)) or a single-core bring-up order.
#   First `idf.py build` will confirm which shape links.
#   See task-7-report.md for the full blocker list.
#
# SCOPE: HID + XInput device paths only. Deliberately OUT (Phase 2/3):
#   - src/usbhostmanager.cpp (USB host; needs pio_usb.h + TUH PIO config)
#   - src/drivers/shared/xinput_host.cpp (host; includes hardware/structs/usb.h)
#   - src/drivers/xinput/XInputAuth*.cpp (auth; Phase 2/3)
#   - src/configs/webconfig.cpp, src/display/*, src/addons/* (out of scope;
#     gp2040.cpp/addonmanager references to them are link blockers, see below)
#   - src/drivers/bluetooth/* (usesUSB()==false seam, Tasks 2/3; not USB-device)

idf_component_register(
    SRCS
        # App entry + core loop (Pico-multicore blocker noted above).
        # TBD-Task7-verify: confirm link once ESP_PLATFORM core1 mapping lands.
        "../../src/main.cpp"
        # TBD-Task7-verify: includes pico/bootrom.h, pico/time.h, hardware/adc.h
        # and calls set_sys_clock_khz() — none exist on S3; needs HAL/porting shim.
        "../../src/gp2040.cpp"
        # TBD-Task7-verify: getMillis()/getMicro() use Pico get_absolute_time();
        # needs hal_time_s3 (Task 6) wiring; confirm no other Pico deps at build.
        "../../src/gamepad.cpp"
        # TBD-Task7-verify: references every driver incl. host/auth/webconfig
        # ones (NetDriver, XBOneDriver, ...); Phase 0 may need trimming to
        # HID/XInput/Bluetooth-only cases to link without the excluded sources.
        "../../src/drivermanager.cpp"
        # USB device drivers (HID + XInput paths only).
        "../../src/drivers/hid/HIDDriver.cpp"
        "../../src/drivers/xinput/XInputDriver.cpp"
        # TBD-Task7-verify: confirm XInputDriver needs xgip_protocol on S3
        # (shared with XBOne); drop from SRCS if the link succeeds without it.
        "../../src/drivers/shared/xgip_protocol.cpp"
        # TinyUSB device callbacks (tud_hid_*, tud_mount_cb, ...).
        "../../src/usbdriver.cpp"
        # TBD-Task7-verify: GamepadState.cpp (under src/gamepad/) link need;
        # confirm symbol references from gamepad.cpp at first build.
        "../../src/gamepad/GamepadState.cpp"
        # TBD-Task7-verify: Storage/System/Peripheral managers are referenced
        # by the core loop but pull Pico headers today (hardware/watchdog.h in
        # storagemanager.cpp; hardware/flash.h + hardware/sync.h +
        # hardware/watchdog.h + pico/multicore.h in system.cpp). Include once
        # their S3 shims exist; otherwise stub for device-only bring-up.
        "../../src/storagemanager.cpp"
        "../../src/system.cpp"
        "../../src/peripheralmanager.cpp"
        # TBD-Task7-verify: GP2040Aux (core1 companion) pulls addon/display
        # managers via gp2040aux.cpp; include only with the core1 mapping fix,
        # else defer with main.cpp.
        "../../src/gp2040aux.cpp"
    INCLUDE_DIRS
        "../../headers"
        # TBD-Task7-verify: brief starting point lists "../../hal"; Task 6 S3
        # backends live in hal_esp32s3/ — confirm whether to list hal_esp32s3
        # here or wire it as its own component under esp32-s3/.
        "../../hal"
    # TBD-Task7-verify: brief starting point lists both `tinyusb` (IDF built-in,
    # enabled via CONFIG_TINYUSB=y in sdkconfig.defaults) and
    # `espressif__tinyusb` (registry component); listing both may duplicate the
    # component. Keep exactly one after the first `idf.py build` resolves it.
    REQUIRES tinyusb espressif__tinyusb)
