# ESP32-S3 app component (Phase 1 Task 5 — NeoPixel RMT + display I2C backend).
#
# SCOPE: core USB-device loop (HID + XInput) + storage/system managers +
# AddonManager/EventManager/CRC32 (link-required, no HW deps) + S3 HAL +
# Task-2 ConfigUtils stub (fresh defaults; real nanopb port is a later task)
# + Task-3 FlashPROM_esp32 backend (gpconfig partition, layout preserved)
# + Task-4 gp2040aux (FreeRTOS core1 task) with the LED/audio addons
#   (board_led/buzzerspeaker/drv8833_rumble/reactiveleds/playerleds) on the
#   generalized halPwmConfig() LEDC helper + PlayerLEDs animation base
# + Task-5 NeoPixel (neopicoleds + AnimationStation, RMT via vendored
#   led_strip in esp32-s3/components) + display (display addon + GPGFX +
#   TinySSD1306 over the S3 PeripheralI2C backend; OneBitDisplay/OBD .cpps
#   excluded — GPGFX only instantiates TinySSD1306) + PeripheralManager
#   (initI2C for the OLED probe; SPI/USB are header stubs, Phase 2)
#   + layoutmanager (button-layout/getLayoutA/B for the display screens).
#   XBOneDriver.cpp is deliberately NOT in SRCS (Task 6 owns that driver —
#   it needs to_ms_since_boot/usbd_edpt_xfer porting): ButtonLayoutScreen's
#   getAuthSent() cast is link-satisfied by the s3_stubs_task2.cpp stub.
# Deliberately OUT (Phase 2/3, Task 6+):
#   - host (src/usbhostmanager.cpp, xinput_host.cpp, XInputAuth*),
#     webconfig (src/configmanager.cpp, src/configs/webconfig.cpp,
#     src/config_utils.cpp), bluetooth (src/drivers/bluetooth/*),
#     SPI/USB peripheral backends (headers are S3 stubs for link shape).

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
        "../../src/addons/neopicoleds.cpp"
        "../../src/addons/display.cpp"
        "../../src/display/GPGFX.cpp"
        "../../src/display/GPGFX_UI.cpp"
        "../../src/display/ui/screens/ButtonLayoutScreen.cpp"
        "../../src/display/ui/screens/ConfigScreen.cpp"
        "../../src/display/ui/screens/MainMenuScreen.cpp"
        "../../src/display/ui/screens/PinViewerScreen.cpp"
        "../../src/display/ui/screens/RestartScreen.cpp"
        "../../src/display/ui/screens/StatsScreen.cpp"
        "../../src/display/ui/screens/SplashScreen.cpp"
        "../../src/display/ui/elements/GPWidget.cpp"
        "../../src/display/ui/elements/GPButton.cpp"
        "../../src/display/ui/elements/GPLever.cpp"
        "../../src/display/ui/elements/GPLabel.cpp"
        "../../src/display/ui/elements/GPScreen.cpp"
        "../../src/display/ui/elements/GPShape.cpp"
        "../../src/display/ui/elements/GPSprite.cpp"
        "../../src/interfaces/i2c/i2cdevicebase.cpp"
        "../../src/interfaces/i2c/ssd1306/tiny_ssd1306.cpp"
        "../../src/peripheralmanager.cpp"
        "../../src/layoutmanager.cpp"
        "../../lib/AnimationStation/src/Effects/Chase.cpp"
        "../../lib/AnimationStation/src/Effects/CustomTheme.cpp"
        "../../lib/AnimationStation/src/Effects/CustomThemePressed.cpp"
        "../../lib/AnimationStation/src/Effects/Rainbow.cpp"
        "../../lib/AnimationStation/src/Effects/StaticColor.cpp"
        "../../lib/AnimationStation/src/Effects/StaticTheme.cpp"
        "../../lib/AnimationStation/src/AnimationStation.cpp"
        "../../lib/AnimationStation/src/Animation.cpp"
        "../../lib/PicoPeripherals/peripheral_i2c_s3.cpp"
        "../../hal_esp32s3/hal_ws2812_s3.cpp"
        "../../hal_esp32s3/hal_pico_shim_s3.cpp"
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
        "../../headers/addons"
        "../../headers/gamepad"
        "../../headers/events"
        "../../headers/interfaces"
        "../../headers/interfaces/i2c"
        "../../headers/interfaces/i2c/ssd1306"
        "../../headers/display"
        "../../headers/display/fonts"
        "../../headers/display/ui"
        "../../headers/display/ui/elements"
        "../../headers/display/ui/screens"
        "../../hal"
        "../../hal_esp32s3"
        "../../esp32-s3/generated"
        "../../esp32-s3/shims"
        "../../configs/ESP32S3DevKitC1"
        "../../lib/nanopb"
        "../../lib/CRC32/src"
        "../../lib/FlashPROM/src"
        "../../lib/PlayerLEDs/src"
        "../../lib/AnimationStation/src"
        "../../lib/OneBitDisplay"
        "../../lib/PicoPeripherals"
    REQUIRES tinyusb esp_timer esp_adc esp_driver_ledc esp_driver_gpio esp_partition esp_driver_i2c led_strip mbedtls)
# R5c (Task 5): mbedtls is include-required, not linked, on S3 — the display
# stack (src/display/ui/screens/ButtonLayoutScreen.cpp:4, UNTOUCHED) includes
# headers/drivers/ps4/PS4Driver.h:13 -> PS4Auth.h:5 (#include "mbedtls/rsa.h").
# No S3 TU links mbedtls symbols (PS4Driver.cpp/PS4Auth.cpp are not in SRCS);
# Task 6 needs this REQUIRES for real when the PS4 driver lands.

# Display-stack debt (Task 5): the UNTOUCHED display headers delete through
# base pointers without virtual dtors (GPScreen.h:24 clearElements,
# GPGFX.cpp:66 driver teardown, display.cpp screen switches) — the same
# delete-via-base UB class as Task-2 ruling W1. A header fix would touch
# display sources AND grow Pico vtables, violating two binding constraints,
# so the IDF -Werror=all promotion is absorbed S3-side only. Pico toolchain
# never promoted this; behavior is identical on both paths. Proper owner:
# display-stack virtual-dtor pass (needs Pico+hardware regression).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=delete-non-virtual-dtor)
# -Werror=switch fires on the UNTOUCHED ButtonLayoutScreen.cpp:147 header
# switch, which has no INPUT_MODE_BLUETOOTH case. Bluetooth has no S3 driver
# (Task-2 ruling W3: HID+XInput-only, getInputMode() never returns it), so
# the missing case is unreachable and behavior matches Pico bit-for-bit.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=switch)

# Board-name string for the StatsScreen "Board:" line (StatsScreen.cpp:18),
# mirroring the Pico mechanism (root CMakeLists.txt:297-301 passes
# GP2040_BOARDCONFIG="<board>" as a string compile definition).
target_compile_definitions(${COMPONENT_LIB} PRIVATE GP2040_BOARDCONFIG="ESP32S3DevKitC1")

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
