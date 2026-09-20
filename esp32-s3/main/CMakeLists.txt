# ESP32-S3 app component (Phase 1 Task 2 — core-loop build green).
#
# SCOPE: core USB-device loop (HID + XInput) + storage/system managers +
# AddonManager/EventManager/CRC32 (link-required, no HW deps) + S3 HAL +
# Task-2 RAM stubs (FlashPROM/ConfigUtils; replaced by real backends in
# Task 3). Deliberately OUT (Phase 2/3, Tasks 3-7):
#   - host (src/usbhostmanager.cpp, xinput_host.cpp, XInputAuth*),
#     webconfig (src/configmanager.cpp, src/configs/webconfig.cpp,
#     src/config_utils.cpp), display (src/display/*), addons (src/addons/*),
#     bluetooth (src/drivers/bluetooth/*), peripherals
#     (src/peripheralmanager.cpp + lib/PicoPeripherals backends — Task 5).
# gp2040aux.cpp (core1 companion) is excluded until the Task-4 core1 task.

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
        "../../src/addonmanager.cpp"
        "../../src/eventmanager.cpp"
        "../../lib/CRC32/src/CRC32.cpp"
        "../../hal_esp32s3/hal_gpio_s3.cpp"
        "../../hal_esp32s3/hal_time_s3.cpp"
        "../../hal_esp32s3/hal_adc_s3.cpp"
        "../../hal_esp32s3/hal_pwm_s3.cpp"
        "../../esp32-s3/generated/config.pb.c"
        "../../esp32-s3/generated/enums.pb.c"
        "s3_stubs_task2.cpp"
    INCLUDE_DIRS
        "../../headers"
        "../../headers/gamepad"
        "../../headers/events"
        "../../hal"
        "../../hal_esp32s3"
        "../../esp32-s3/generated"
        "../../configs/ESP32S3DevKitC1"
        "../../lib/nanopb"
        "../../lib/CRC32/src"
        "../../lib/FlashPROM/src"
    REQUIRES tinyusb esp_timer esp_adc esp_driver_ledc esp_driver_gpio)

# Feed OUR tusb_config.h (headers/tusb_config.h, ESP_PLATFORM branch) to the
# raw TinyUSB library build — same injection technique esp_tinyusb uses.
# Without this the lib compiles with no device HID class (no hidd_*).
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tusb_lib_name tinyusb) # Local component
else()
    set(tusb_lib_name espressif__tinyusb) # Managed component
endif()
idf_component_get_property(tusb_lib ${tusb_lib_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../headers")

# Single-pass archive linking (no --start-group on this link line) cannot
# resolve the usbd.c <-> app descriptor callback cycle on its own: seed the
# TinyUSB objects early so their references to OUR callbacks
# (tud_descriptor_*, tud_hid_*) resolve when libmain.a is processed later.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u usbd_edpt_xfer -u hidd_init")
