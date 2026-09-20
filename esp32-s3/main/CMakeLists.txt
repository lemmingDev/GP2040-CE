# ESP32-S3 app component (Phase 1 Task 4 — LED/audio addons on LEDC + core1 task).
#
# SCOPE: core USB-device loop (HID + XInput) + storage/system managers +
# AddonManager/EventManager/CRC32 (link-required, no HW deps) + S3 HAL +
# Task-2 ConfigUtils stub (fresh defaults; real nanopb port is a later task)
# + Task-3 FlashPROM_esp32 backend (gpconfig partition, layout preserved)
# + Task-4 gp2040aux (FreeRTOS core1 task) with the LED/audio addons
#   (board_led/buzzerspeaker/drv8833_rumble/reactiveleds/playerleds) on the
#   generalized halPwmConfig() LEDC helper + PlayerLEDs animation base.
# Deliberately OUT (Phase 2/3, Tasks 5-7):
#   - host (src/usbhostmanager.cpp, xinput_host.cpp, XInputAuth*),
#     webconfig (src/configmanager.cpp, src/configs/webconfig.cpp,
#     src/config_utils.cpp), display (src/display/*, src/addons/display.cpp,
#     src/addons/neopicoleds.cpp), bluetooth (src/drivers/bluetooth/*),
#     peripherals (src/peripheralmanager.cpp + lib/PicoPeripherals backends
#     — Task 5).

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
        "../../src/gp2040aux.cpp"
        "../../src/addons/board_led.cpp"
        "../../src/addons/buzzerspeaker.cpp"
        "../../src/addons/drv8833_rumble.cpp"
        "../../src/addons/reactiveleds.cpp"
        "../../src/addons/playerleds.cpp"
        "../../lib/PlayerLEDs/src/PlayerLEDs.cpp"
        "../../src/addonmanager.cpp"
        "../../src/eventmanager.cpp"
        "../../lib/CRC32/src/CRC32.cpp"
        "../../hal_esp32s3/hal_gpio_s3.cpp"
        "../../hal_esp32s3/hal_time_s3.cpp"
        "../../hal_esp32s3/hal_adc_s3.cpp"
        "../../hal_esp32s3/hal_pwm_s3.cpp"
        "../../esp32-s3/generated/config.pb.c"
        "../../esp32-s3/generated/enums.pb.c"
        "../../lib/FlashPROM/src/FlashPROM_esp32.cpp"
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
        "../../lib/PlayerLEDs/src"
    REQUIRES tinyusb esp_timer esp_adc esp_driver_ledc esp_driver_gpio esp_partition)

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
