#ifndef _BLUETOOTH_DRIVER_H_
#define _BLUETOOTH_DRIVER_H_

#include "gpdriver.h"
#include "ble_gamepad_report.h"

class BluetoothDriver : public GPDriver {
public:
    void initialize() override;
    void initializeAux() override {}
    // NOTE (merge): upstream GPDriver::process now returns bool (true =
    // report sent/handled, gates PostprocessAddons); mirror HIDDriver.
    bool process(Gamepad * gamepad) override;
    void processAux() override {}
    uint16_t get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) override;
    void set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) override {}
    bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) override { return false; }
    const uint16_t * get_descriptor_string_cb(uint8_t index, uint16_t langid) override { return nullptr; }
    const uint8_t * get_descriptor_device_cb() override { return nullptr; }
    const uint8_t * get_hid_descriptor_report_cb(uint8_t itf) override { return nullptr; }
    const uint8_t * get_descriptor_configuration_cb(uint8_t index) override { return nullptr; }
    const uint8_t * get_descriptor_device_qualifier_cb() override { return nullptr; }
    uint16_t GetJoystickMidValue() override { return GAMEPAD_JOYSTICK_MID; }
    USBListener * get_usb_auth_listener() override { return nullptr; }
    bool usesUSB() override { return false; }
protected:
    bool pushReport(const BleGamepadReport & report);
    BleGamepadReport lastReport = {};
    bool bleConnected = false;
    BleGamepadReport lastSent = {};
};

#endif
