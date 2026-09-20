# ESP32-S3 app component (Phase 1 Task 1 — single-core bring-up).
#
# SCOPE: core USB-device loop (HID + XInput) + storage/system/peripheral
# managers + S3 HAL. Deliberately OUT (Phase 2/3, Task 7):
#   - host (src/usbhostmanager.cpp, xinput_host.cpp), auth (XInputAuth*),
#     webconfig (src/configs/webconfig.cpp), display (src/display/*),
#     addons (src/addons/*), bluetooth (src/drivers/bluetooth/*).
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
    REQUIRES esp_tinyusb)
