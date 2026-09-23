/*
 * SPDX-License-Identifier: MIT
 * S3-only CONFIG-mode RNDIS driver (USB-webconfig pivot): standalone RNDIS
 * function (no gamepad interfaces), mirroring Pico NetDriver's proven
 * descriptor bytes. Gamepad modes stay pure RNDIS-free; CONFIG boot serves
 * webconfig over this device when USB networking is enabled.
 */

#ifndef _S3_NET_DRIVER_H_
#define _S3_NET_DRIVER_H_

#include "gpdriver.h"

#if defined(ESP_PLATFORM)

class S3NetDriver : public GPDriver {
public:
    virtual void initialize();
    virtual bool process(Gamepad * gamepad);
    virtual void initializeAux() {}
    virtual void processAux() {}
    virtual uint16_t get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
    virtual void set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
    virtual bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
    virtual const uint16_t * get_descriptor_string_cb(uint8_t index, uint16_t langid);
    virtual const uint8_t * get_descriptor_device_cb();
    virtual const uint8_t * get_hid_descriptor_report_cb(uint8_t itf) ;
    virtual const uint8_t * get_descriptor_configuration_cb(uint8_t index);
    virtual const uint8_t * get_descriptor_device_qualifier_cb();
    virtual uint16_t GetJoystickMidValue();
    virtual USBListener * get_usb_auth_listener() { return nullptr; }
private:
};

#endif // defined(ESP_PLATFORM)

#endif // _S3_NET_DRIVER_H_
