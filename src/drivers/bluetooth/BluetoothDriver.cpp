#include "drivers/bluetooth/BluetoothDriver.h"
#include "gamepad.h"

void BluetoothDriver::initialize() {
    lastReport = {};
    // Task 8: init NimBLE HOGP service, start advertising, restore NVS bonds.
}

void BluetoothDriver::process(Gamepad * gamepad) {
    BleGamepadReport r = buildBleGamepadReport(
        static_cast<uint16_t>(gamepad->state.buttons & 0x07FFu),
        gamepad->state.lt, gamepad->state.rt,
        gamepad->state.lx, gamepad->state.ly,
        gamepad->state.rx, gamepad->state.ry);
    lastReport = r;
    pushReport(r);
}

uint16_t BluetoothDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                                     uint8_t *buffer, uint16_t reqlen) {
    (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void BluetoothDriver::pushReport(const BleGamepadReport & report) {
    (void)report;
    // Task 8: ble_hid_notify(report bytes) when connected.
}
