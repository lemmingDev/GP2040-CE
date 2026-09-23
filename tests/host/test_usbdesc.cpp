// Host byte test for the S3 CONFIG-mode standalone RNDIS device
// (USB-webconfig pivot): the S3NetDriver configuration carries only the
// RNDIS function (no gamepad interfaces), and the device descriptor uses
// the MISC class triple USB-IF mandates for IAD-grouped composites.
//
// Asserts:
//  1. config wTotalLength == 9 + TUD_RNDIS_DESC_LEN (75), bNumInterfaces == 2,
//     array length matches wTotalLength;
//  2. the RNDIS block opens with the IAD class triple at offset 9;
//  3. device descriptor is MISC (EF/02/01) with 1 configuration.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "tusb.h"
#include "device/usbd.h"
#include "drivers/net/S3NetDriverDescriptors.h"

int main() {
    // Config: single RNDIS function, interfaces 0+1.
    uint16_t total = (uint16_t)((uint16_t)s3net_configuration_descriptor[2] |
                                ((uint16_t)s3net_configuration_descriptor[3] << 8));
    assert(sizeof(s3net_configuration_descriptor) == (size_t)(9 + TUD_RNDIS_DESC_LEN));
    assert(total == (uint16_t)(9 + TUD_RNDIS_DESC_LEN));
    assert(s3net_configuration_descriptor[4] == 2);  // bNumInterfaces
    const uint8_t *iad = s3net_configuration_descriptor + 9;
    assert(iad[0] == 8);  // bLength: Interface Association Descriptor
    assert(iad[1] == TUSB_DESC_INTERFACE_ASSOCIATION);
    assert(iad[2] == 0);  // first interface
    assert(iad[3] == 2);  // RNDIS = comm + data interfaces
    assert(iad[4] == TUD_RNDIS_ITF_CLASS);
    assert(iad[5] == TUD_RNDIS_ITF_SUBCLASS);
    assert(iad[6] == TUD_RNDIS_ITF_PROTOCOL);

    // Device: MISC class triple, 1 configuration.
    assert(s3net_device_descriptor[4] == 0xEF);
    assert(s3net_device_descriptor[5] == 0x02);
    assert(s3net_device_descriptor[6] == 0x01);
    assert(s3net_device_descriptor[17] == 0x01);

    std::printf("S3Net standalone RNDIS device+config OK\n");
    return 0;
}
